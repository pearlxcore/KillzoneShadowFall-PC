"""Bounded read-only face snapshots for one diagnostic session.

Follows only the PID and attached face addresses emitted by that session.
PROCESS_VM_READ only; no writes, native calls, injection or suspension.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import struct
import time

spec = importlib.util.spec_from_file_location('face_memory', Path(__file__).with_name('read-character-memory.py'))
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


def numeric_summary(data):
    values = struct.unpack('<' + 'f' * (len(data) // 4), data)
    invalid = [i for i, value in enumerate(values) if not math.isfinite(value)]
    finite = [abs(value) for value in values if math.isfinite(value)]
    return {'float_count': len(values), 'nonfinite_count': len(invalid),
            'nonfinite_indices': invalid[:64], 'max_abs_finite': max(finite, default=0)}


def pose_snapshot(reader, address):
    header = reader.read(address, 0x70)
    if header is None:
        return {'address': hex(address), 'unreadable': True}
    skeleton = struct.unpack_from('<Q', header, 0x20)[0]
    identity = reader.identity(skeleton)
    result = {'address': hex(address), 'skeleton': identity, 'header_hex': header.hex()}
    if identity.get('type_name') != 'Skeleton':
        result['unsupported_skeleton'] = True
        return result
    count_data = reader.read(skeleton + 0x30, 4)
    count = struct.unpack('<i', count_data)[0] if count_data else -1
    result['bones'] = count
    if not 1 <= count <= 512:
        result['invalid_count'] = True
        return result
    for name, offset, stride, maximum in [('local', 0x30, 48, count),
                                           ('world', 0x40, 64, count),
                                           ('scalar', 0x50, 4, 4096)]:
        start, end = struct.unpack_from('<QQ', header, offset)
        length = end - start
        if not start and not end:
            continue
        if not start or length < 0 or length % stride or length > maximum * stride:
            result[name] = {'invalid_bounds': True, 'start': hex(start), 'end': hex(end)}
            continue
        data = reader.read(start, length)
        result[name] = {'unreadable': True} if data is None else {
            **numeric_summary(data), 'raw_hex': data.hex(), 'address': hex(start)}
    result['header_changed_during_read'] = reader.read(address, 0x70) != header
    return result


def snapshot(reader, rep, face):
    # Revalidate attachment before and after reading potentially transient data.
    if reader.pointer(rep + 0x600) != face or reader.identity(face).get('type_name') != 'FacialExpressionAnimator':
        return None
    header = reader.read(face, 0x3f0)
    if header is None or reader.pointer(face + 0x20) != rep:
        return None
    resource = reader.pointer(face + 0x18)
    result = {'read_only': True, 'atomic': False, 'face': reader.identity(face),
              'rep': hex(rep), 'owner': hex(reader.pointer(rep + 0x198)),
              'resource': reader.identity(resource), 'face_header_hex': header.hex(),
              'resource_header_hex': (reader.read(resource, 0x1d0) or b'').hex(),
              'poses': [pose_snapshot(reader, face + offset)
                        for offset in (0x40, 0xb0, 0x120, 0x190, 0x200, 0x270, 0x2e0)]}
    if reader.pointer(rep + 0x600) != face or reader.pointer(face + 0x18) != resource:
        return None
    result['nonfinite_count'] = sum(p.get(kind, {}).get('nonfinite_count', 0)
                                   for p in result['poses'] for kind in ('local', 'world', 'scalar'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    args = parser.parse_args()
    folder = args.session
    pattern = re.compile(r'KILLZONE_CHARACTER_FACE supported=1 attached=1 resource=[0-9a-f]+ rep=([0-9a-f]+) animator=([0-9a-f]+)', re.I)
    reader = None
    rep = face = 0
    offset = 0
    carry = ''
    saved = anomalies = samples = 0
    last_saved = 0
    last_bad = False
    started = time.monotonic()
    destination = folder / 'face-capture'
    destination.mkdir(exist_ok=True)
    try:
        while time.monotonic() - started < 1800:
            try:
                status = json.loads((folder / 'status.json').read_text())
            except (OSError, ValueError):
                time.sleep(0.5)
                continue
            if 'exit_code' in status:
                break
            if reader is None and status.get('pid'):
                reader = memory.Reader(status['pid'])
            if reader is None:
                time.sleep(0.5)
                continue
            with (folder / 'stderr.log').open('r', errors='replace') as log:
                log.seek(offset)
                chunk = log.read(65536)
                offset = log.tell()
            lines = (carry + chunk).split('\n')
            carry = lines.pop()[-2048:]
            for line in lines:
                match = pattern.search(line)
                if match:
                    rep, face = (int(value, 16) for value in match.groups())
            now = time.monotonic()
            capture = snapshot(reader, rep, face) if rep and face else None
            if capture:
                samples += 1
                bad = capture['nonfinite_count'] > 0
                if (bad and not last_bad and anomalies < 20) or (now - last_saved >= 5 and saved < 120):
                    capture.update(elapsed_seconds=round(now - started, 3), sample=samples,
                                   automatic_numeric_anomaly=bad)
                    (destination / f'face-{saved:03d}.json').write_text(json.dumps(capture, indent=2))
                    saved += 1
                    anomalies += bad
                    last_saved = now
                last_bad = bad
            time.sleep(0.5)
    except (OSError, ValueError) as exc:
        (destination / 'error.txt').write_text(str(exc))
    finally:
        if reader:
            reader.api.CloseHandle(reader.handle)
        (destination / 'summary.json').write_text(json.dumps({
            'read_only': True, 'samples': samples, 'snapshots': saved,
            'numeric_anomaly_snapshots': anomalies,
            'note': 'Periodic samples can miss short glitches; pink/material correctness is not determined by finite pose values.'}, indent=2))


if __name__ == '__main__':
    main()
