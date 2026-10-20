"""Read-only snapshots of the native lump manager's character bundle leases."""
import argparse
import importlib.machinery
import json
from pathlib import Path
import struct

memory = importlib.machinery.SourceFileLoader('bundle_memory', str(Path(__file__).with_name('read-character-memory.py'))).load_module()

def inspect(reader):
    base = 0x800000000
    manager = reader.pointer(base + 0x3c67760)
    def integer(address):
        data = reader.read(address, 4)
        return struct.unpack('<i', data)[0] if data else None
    count = integer(manager + 8)
    if count is None or not 0 <= count <= 8192:
        raise ValueError('Native manager list is unreadable')
    entries = reader.pointer(manager + 0x10)
    result = {'manager': hex(manager), 'read_only': True, 'atomic': False, 'bundles': []}
    budget = 20000
    for index in range(count):
        entry = reader.pointer(entries + index * 8)
        name = reader.engine_string(reader.pointer(entry + 0x10))
        if not name or 'characters.' not in name:
            continue
        size = integer(entry + 0x18)
        if size is None or not 0 <= size <= 20000 or size > budget:
            raise ValueError('Resource capture bound exceeded')
        budget -= size
        array = reader.pointer(entry + 0x20)
        item = {'name': name, 'entry': hex(entry), 'references': integer(entry),
                'initialized': (reader.read(entry + 0x30, 1) or b'\0')[0],
                'resource_count': size, 'types': {}, 'soldiers': []}
        for resource_index in range(size):
            address = reader.pointer(array + resource_index * 8)
            identity = reader.identity(address)
            kind = identity.get('type_name', 'unknown')
            item['types'][kind] = item['types'].get(kind, 0) + 1
            if kind == 'SoldierResource':
                item['soldiers'].append(identity)
        result['bundles'].append(item)
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reader = memory.Reader(args.pid)
    try:
        report = inspect(reader)
    finally:
        reader.api.CloseHandle(reader.handle)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    for bundle in report['bundles']:
        print(bundle['name'], 'refs=', bundle['references'], 'resources=', bundle['resource_count'],
              'soldiers=', [(entry.get('address'), entry.get('resource_name')) for entry in bundle['soldiers']])
