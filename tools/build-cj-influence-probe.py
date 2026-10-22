#!/usr/bin/env python3
"""Build a diagnostic vest from an existing stride-44 CJ export, offline only.

The negative-X half uses slots 0/1, the other half slots 0/test_slot.
Both encode exactly the same chest/wrist blend. Correct full-weight skinning
therefore produces one continuous (deliberately displaced) vest. A new split
at the centre indicates slot-dependent behavior, not a retarget-fit difference.
This is a diagnostic asset, never a replacement fit.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

from kzsf_vertex_stream import decode_stream, encode_stream, decode_indices


def probe_stream(payload, chest, wrist, test_slot):
    if not 2 <= test_slot <= 7 or not 0 <= chest < 98 or not 0 <= wrist < 98 or chest == wrist:
        raise ValueError('Invalid diagnostic bones or slot')
    count, stride, elements, original, flags = decode_stream(payload)
    expected = [bytes.fromhex(x) for x in (
        '00020300', '0c060304', '10060401', '14070411',
        '18070413', '1c040410', '20040412', '24030406')]
    if stride != 44 or elements != expected:
        raise ValueError('Diagnostic requires the native eight-index layout')
    data = bytearray(original)
    sides = [0, 0]
    for i in range(count):
        offset = i * stride
        side = int(struct.unpack_from('<f', data, offset)[0] >= 0)
        slot = test_slot if side else 1
        indices = bytearray(8)
        weights = bytearray(8)
        indices[0], indices[slot] = chest, wrist
        weights[slot - 1] = 127  # implicit chest weight = 128
        data[offset + 20:offset + 28] = indices
        data[offset + 28:offset + 36] = weights
        sides[side] += 1
    if not all(sides):
        raise ValueError('Vest must have vertices on both sides of X=0')
    return encode_stream(count, stride, elements, bytes(data), flags), sides


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--textures', type=Path, required=True)
    parser.add_argument('--bindings', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--section', type=int, default=5, help='Vest section in the CJ export')
    parser.add_argument('--test-slot', type=int, default=2, help='Zero-based slot, 2 through 7')
    args = parser.parse_args()
    manifest = bytearray((args.source / 'model.kzmd').read_bytes())
    magic, version, bones, sections, textures, flags, profile, reserved = struct.unpack_from('<4s7I', manifest)
    if (magic, version, bones, reserved) != (b'KZMD', 1, 98, 0) or not 1 <= sections <= 16 or not 1 <= textures <= 16:
        raise ValueError('Unsupported manifest')
    if len(manifest) != 32 + sections * 40 + textures * 12 or not 0 <= args.section < sections:
        raise ValueError('Invalid manifest size or section')
    names = [b['name'] for b in json.loads(args.bindings.read_text())]
    if len(names) != 98:
        raise ValueError('Expected 98 bindings')
    files = {}
    for i in range(sections):
        record = struct.unpack_from('<10I', manifest, 32 + i * 40)
        for suffix, length, crc in (('va', record[3], record[5]), ('ia', record[4], record[6])):
            name = f'primitive-{i}.{suffix}'
            data = (args.source / name).read_bytes()
            if len(data) != length or zlib.crc32(data) != crc:
                raise ValueError(f'Invalid source payload: {name}')
            files[name] = data
        count, *_ = decode_stream(files[f'primitive-{i}.va'])
        indices, *_ = decode_indices(files[f'primitive-{i}.ia'], count)
        if count != record[0] or indices != record[1]:
            raise ValueError('Manifest geometry count mismatch')
    for i in range(textures):
        size, crc, reserved = struct.unpack_from('<3I', manifest, 32 + sections * 40 + i * 12)
        name = f'texture-{i}.rgba'
        data = (args.textures / name).read_bytes()
        if reserved or len(data) != size or zlib.crc32(data) != crc:
            raise ValueError(f'Invalid source texture: {name}')
        files[name] = data
    name = f'primitive-{args.section}.va'
    files[name], sides = probe_stream(files[name], names.index('chest'), names.index('wrist_left'), args.test_slot)
    struct.pack_into('<I', manifest, 32 + args.section * 40 + 20, zlib.crc32(files[name]))
    files['model.kzmd'] = bytes(manifest)
    report = {'diagnostic_only': True, 'source': str(args.source.resolve()),
              'section': args.section, 'control_slot_zero_based': 1,
              'test_slot_zero_based': args.test_slot, 'vertices_per_half': sides,
              'weights': {'chest': 128, 'wrist_left': 127},
              'expected': 'Both halves follow the same blend, with no new centre split.'}
    args.output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (args.output / name).write_bytes(data)
    (args.output / 'diagnostic.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
