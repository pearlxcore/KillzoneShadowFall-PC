"""Resolve catalog shader boundaries using the renderer's explicit footer link.

Unlinked/ambiguous records are left unresolved, never guessed from a nearby
end instruction. Writes metadata only; no game shader bytes are exported.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('inventory', Path(__file__).with_name('inventory-kzsf-shaders.py'))
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)
TOKEN = bytes.fromhex('ff03ebbe')
WINDOW = 128 * 1024


def resolve_window(data, base, record):
    footer = record['footer_offset'] - base
    length = record['code_bytes']
    if not 0 <= footer <= len(data) - 28 or data[footer:footer+7] != b'OrbShdr':
        return None
    if (struct.unpack_from('<I', data, footer+8)[0] >> 8 != length or
            f'{struct.unpack_from("<Q", data, footer+16)[0]:016x}' != record['hash']):
        return None
    starts = []
    cursor = max(0, footer - WINDOW)
    while True:
        cursor = data.find(TOKEN, cursor, footer)
        if cursor < 0:
            break
        start = cursor
        cursor += 1
        if start + 8 > footer or (footer - start) % 4:
            continue
        relative = struct.unpack_from('<I', data, start+4)[0]
        # Identical address calculation to AmdGpu::SearchBinaryInfo.
        if start + (relative + 1) * 8 != footer:
            continue
        if length < 8 or start + length > footer:
            continue
        starts.append(start)
    if len(starts) != 1:
        return None
    start = starts[0]
    return dict(code_offset=base+start, code_bytes=length,
                code_sha256=hashlib.sha256(data[start:start+length]).hexdigest(),
                boundary='s_mov_b32_vcc_hi_footer_link')


def resolve_file(path, records, lz4):
    pending = sorted(records, key=lambda r:r['footer_offset'])
    carry = b''
    index = 0
    end = 0
    for offset, chunk in inventory.blocks(path, lz4):
        data = carry + chunk
        base = offset - len(carry)
        # Keep the boundary footer and its declared code extent available.
        while index < len(pending):
            record = pending[index]
            if record['footer_offset'] + 28 > offset+len(chunk):
                break
            yield record, resolve_window(data, base, record)
            index += 1
        carry = data[-(WINDOW + 28):]
        end = offset + len(chunk)
    base = end - len(carry)
    for record in pending[index:]:
        yield record, resolve_window(carry, base, record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--lz4', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--limit-files', type=int, default=0)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding='utf-8'))
    groups = {}
    for record in catalog['shaders']:
        groups.setdefault(record['file'], []).append(record)
    fingerprints = {r['file']:r for r in catalog['files']}
    lz4 = inventory.Lz4(args.lz4)
    resolved, unresolved = [], []
    for index, (name, records) in enumerate(sorted(groups.items())):
        if args.limit_files and index >= args.limit_files:
            break
        path = args.game / name
        with path.open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual != fingerprints[name]['sha256']:
            raise ValueError(f'Asset does not match supported catalog: {name}')
        for record, location in resolve_file(path, records, lz4):
            if location:
                resolved.append(dict(record, **location))
            else:
                unresolved.append(record['hash'])
        print(f'Sources {index+1}/{len(groups)}: {len(resolved)} linked, {len(unresolved)} unresolved', flush=True)
    result = dict(format=1, catalog_id=catalog['catalog_id'],
                  status='linked source boundaries only; not compiled pipelines',
                  sources=resolved, unresolved=unresolved,
                  complete_scan=not args.limit_files or args.limit_files>=len(groups))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
