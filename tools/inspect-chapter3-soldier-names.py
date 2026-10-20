"""Read-only base SoldierResource names anchored to RTTIBin object payloads.

This does not decode body/face variant relationships, patch merging or rig
compatibility. Chapter membership comes from the separate scoped reference scan.
"""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import uuid
from kzsf_core_index import parse_index

ROOT = Path(__file__).resolve().parents[1]


def names(raw):
    header = parse_index(raw, len(raw))
    pos = 16 + 5 * 4
    types = []
    for _ in range(header['type_count']):
        size = struct.unpack_from('<I', raw, pos)[0]
        pos += 4
        types.append(raw[pos:pos + size].decode('ascii'))
        pos += size + 16
    count = struct.unpack_from('<I', raw, pos)[0]
    pos += 4
    indices = struct.unpack_from('<' + 'I' * count, raw, pos)
    pos += count * 4 + 4  # explicit-object count was validated by parse_index
    payload = header['header_bytes']
    result = []
    for index, type_index in enumerate(indices):
        identity = str(uuid.UUID(bytes_le=raw[pos:pos + 16]))
        size = struct.unpack_from('<I', raw, pos + 16)[0]
        pos += 20
        if types[type_index] == 'SoldierResource' and size:
            # Observed serialized SoldierResource prefix: three flag bytes,
            # record field, then a length-prefixed narrow name at+7/+11.
            if size < 11:
                raise ValueError('Truncated SoldierResource name header')
            length = struct.unpack_from('<I', raw, payload + 7)[0]
            if not 0 < length <= 128 or length > size - 11:
                raise ValueError('Invalid SoldierResource name bounds')
            value = raw[payload + 11:payload + 11 + length]
            if any(c < 33 or c > 126 for c in value):
                raise ValueError('Invalid SoldierResource name encoding')
            result.append({'object_index': index, 'uuid_le': identity,
                           'payload_offset': payload, 'payload_bytes': size,
                           'name': value.decode('ascii')})
        payload += size
    return result


def main():
    sys.path.insert(0, str(ROOT / 'launcher'))
    import kzsf_local
    game = Path(kzsf_local.load_preferences()['game_dir'])
    membership = json.loads((ROOT / 'out/chapter3-bundle-membership.json').read_text())
    spec = importlib.util.spec_from_file_location('asset_decoder', ROOT / 'tools/inventory-kzsf-shaders.py')
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    lz4 = decoder.Lz4(Path('C:/msys64/clang64/bin/liblz4.dll'))
    report = {'read_only': True, 'scope': 'Installed Cassandra-referenced character bundle base SoldierResource names',
              'limits': ['Base files only; patches not merged', 'Reference strings do not prove runtime placement',
                         'Body/face variants and rig compatibility not decoded'], 'bundles': [], 'missing': []}
    for bundle in membership['character_refs']:
        path = game / 'localcachepink/lumps' / (bundle + '.core')
        if not path.is_file():
            report['missing'].append(bundle)
            continue
        raw = b''.join(block for _, block in decoder.blocks(path, lz4))
        soldiers = names(raw)
        report['bundles'].append({'bundle': bundle, 'file': path.relative_to(game).as_posix(),
                                  'soldiers': soldiers})
        print(bundle, [soldier['name'] for soldier in soldiers], flush=True)
    (ROOT / 'out/chapter3-soldier-names.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
