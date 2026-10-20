"""Bounded, read-only comparison of live player/NPC appearance assemblies.

Uses the existing PROCESS_VM_READ reader. No native query calls, writes,
injection, suspension, or full address-space searches. Registry traversal
follows only the known type/pool links, with a shared read budget.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('character_memory', Path(__file__).with_name('read-character-memory.py'))
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


def type_hash(key):
    mask = (1 << 64) - 1
    value = (~(key << 32) + key) & mask
    value ^= value >> 22
    value = (~(value << 13) + value) & mask
    value = ((value ^ (value >> 8)) * 9) & mask
    value ^= value >> 15
    value = (~(value << 27) + value) & mask
    return ((value ^ (value >> 31)) & 0xffffffff) | 0x80000000


class Inspector(memory.Reader):
    def __init__(self, pid, base):
        super().__init__(pid)
        self.base = base
        self.remaining = 30000

    def read(self, address, size):
        self.remaining -= 1
        if self.remaining < 0:
            raise ValueError('Shared read budget exhausted')
        return super().read(address, size)

    def integer(self, address):
        value = self.read(address, 4)
        return struct.unpack('<i', value)[0] if value else None

    def array(self, address, count, stride=8):
        if not isinstance(count, int) or not 0 <= count <= 4096 or (count and not address):
            raise ValueError('Invalid bounded array')
        data = self.read(address, count * stride) if count else b''
        if data is None:
            raise ValueError('Unreadable bounded array')
        return data

    def registry(self, rva, limit, derived=False):
        result = {'type_rva': hex(rva), 'objects': []}
        manager = self.pointer(self.base + 0x3b490c0)
        table = self.pointer(manager + 0x78)
        mask = self.integer(manager + 0x84)
        if not table or mask is None or not 0 <= mask <= 65535 or mask & (mask + 1):
            result['error'] = 'Registry table unavailable'
            return result
        desired = self.base + rva
        desired_header = self.read(desired, 4)
        low, span = struct.unpack('<HH', desired_header) if desired_header else (0, 0)
        def matches(identity):
            actual = int(identity.get('rtti', '0'), 16)
            if actual == desired:
                return True
            if not derived or not actual:
                return False
            header = self.read(actual, 2)
            return bool(header and ((struct.unpack('<H', header)[0] - low) & 65535) <= span)
        hashed = type_hash(desired)
        pool_list = 0
        for probe in range(mask + 1):
            slot = table + ((hashed + probe) & mask) * 0x20
            stored = self.integer(slot)
            if stored is None or stored == 0:
                break
            if (stored & 0xffffffff) == hashed and self.pointer(slot + 8) == desired:
                pool_list = self.pointer(slot + 0x18)
                break
        if not pool_list:
            result['note'] = 'No exact-type pool list; this does not prove no matching instances exist'
            return result
        count = self.integer(pool_list)
        pools = self.pointer(pool_list + 8)
        if count is None or not 0 <= count <= 256:
            raise ValueError('Invalid pool count')
        entries_seen = 0
        seen = set()
        for pool_index in range(count):
            pool = self.pointer(pools + pool_index * 8)
            entries = self.pointer(pool + 0x30)
            entry_count = self.integer(pool + 0x28)
            if entry_count is None or not 0 <= entry_count <= 8192 or entries_seen + entry_count > 8192:
                raise ValueError('Pool entry budget exceeded')
            entries_seen += entry_count
            for index in range(entry_count):
                entry = entries + index * 0x30
                resources = self.pointer(entry + 0x18)
                resource_count = self.integer(entry + 0x10)
                if resource_count is None or not 0 <= resource_count <= 65536:
                    raise ValueError('Invalid resource pointer count')
                if not resource_count:
                    continue
                # Native39b350 tests the first object's RTTI before walking
                # this homogeneous pool entry. Unrelated shader/texture groups
                # can contain thousands of pointers and need not be read.
                first = self.identity(self.pointer(resources))
                if not matches(first):
                    continue
                if resource_count > 4096:
                    raise ValueError('Matching resource pointer budget exceeded')
                for offset in range(resource_count):
                    resource = self.pointer(resources + offset * 8)
                    if resource in seen:
                        continue
                    seen.add(resource)
                    identity = self.identity(resource)
                    if not matches(identity):
                        continue
                    result['objects'].append(identity)
                    if len(result['objects']) == limit:
                        result['truncated'] = True
                        return result
        return result

    def variants(self, soldier):
        result = self.identity(soldier)
        if result.get('type_name') != 'SoldierResource':
            result['error'] = 'Not a SoldierResource'
            return result
        count = self.integer(soldier + 0x648)
        array = self.pointer(soldier + 0x650)
        if count is None or not 0 <= count <= 64:
            result['error'] = 'Invalid variant count'
            return result
        result['variants'] = []
        for index in range(count):
            variant = array + index * 0xf0
            raw = self.array(variant, 0xf0, 1)
            face = self.pointer(variant + 0x18)
            face_rig = self.pointer(face + 0x38) if face else 0
            source_rig = self.pointer(variant + 0x60)
            body = self.pointer(variant + 8)
            record = {'index': index, 'address': hex(variant), 'raw_hex': raw.hex(),
                      'body': self.identity(body), 'body_mesh': self.identity(self.pointer(body + 0x18)),
                      'face': self.identity(face), 'face_rig': self.identity(face_rig),
                      'source_rig': self.identity(source_rig),
                      'face_bones': self.integer(face_rig + 0x30) if face_rig else None,
                      'source_bones': self.integer(source_rig + 0x30) if source_rig else None,
                      'map_count': self.integer(variant + 0xb8),
                      'matrix_count': self.integer(variant + 0xd0)}
            map_count = record['map_count']
            if face and map_count is not None and 0 < map_count <= 512:
                indices = self.array(self.pointer(variant + 0xc0), map_count, 4)
                values = list(struct.unpack('<' + 'i' * map_count, indices))
                record['map_indices'] = values
                record['mapped_count'] = sum(value >= 0 for value in values)
                record['negative_values'] = sorted(set(value for value in values if value < 0))
                matrix_count = record['matrix_count']
                if matrix_count is not None and 0 < matrix_count <= 512:
                    record['correction_matrices_hex'] = self.array(self.pointer(variant + 0xd8), matrix_count, 64).hex()
            result['variants'].append(record)
        return result

    def soldiers(self):
        # 686ad0 traverses ExposedEntityActionsComponent's global list at
        # 3c3b500/508; 6868b0 follows component+38 to its owning entity.
        # This is a partial, typed entity list, not all runtime objects.
        count = self.integer(self.base + 0x3c3b500)
        array = self.pointer(self.base + 0x3c3b508)
        if count is None or not 0 <= count <= 2048 or (count and not array):
            raise ValueError('Invalid exposed-entity component list')
        result = []
        seen = set()
        for index in range(count):
            component = self.pointer(array + index * 8)
            if self.identity(component).get('type_name') != 'ExposedEntityActionsComponent':
                continue
            owner = self.pointer(component + 0x38)
            if owner in seen:
                continue
            seen.add(owner)
            identity = self.identity(owner)
            if identity.get('type_name') == 'Soldier':
                result.append(identity)
                if len(result) == 64:
                    break
        return result

    def actor(self, address):
        result = self.identity(address)
        if result.get('type_name') != 'Soldier':
            result['error'] = 'Not a Soldier'
            return result
        resource = self.pointer(address + 0x1d0)
        rep = self.pointer(address + 0x1d8)
        model = self.pointer(address + 0x200)
        face = self.pointer(rep + 0x600)
        node = self.pointer(model + 0x30)
        result.update(resource=self.identity(resource), rep=self.identity(rep), model=self.identity(model),
                      rep_owner=hex(self.pointer(rep + 0x198)), current_variant=self.integer(address + 0x98c),
                      facial_animator=self.identity(face), node=self.identity(node), parts=[], draws=[])
        count = self.integer(model + 0x58)
        parts = self.pointer(model + 0x60)
        if count is not None and 0 <= count <= 128:
            for index in range(count):
                instance = parts + index * 0xf0
                part = self.pointer(instance + 0xc8)
                result['parts'].append({'instance': hex(instance), 'part': self.identity(part),
                    'mesh': self.identity(self.pointer(part + 0x18)), 'flags': self.integer(instance + 0xe0)})
        pose_index = self.integer(node + 0x270)
        if pose_index in (0, 1):
            rig = self.pointer(node + 0x190 + pose_index * 0x70 + 0x20)
            result['render_rig'] = self.identity(rig)
            result['render_bones'] = self.integer(rig + 0x30)
        count = self.integer(node + 0x70)
        draws = self.pointer(node + 0x78)
        if count is not None and 0 <= count <= 128:
            for index in range(count):
                tagged = self.pointer(draws + index * 8)
                draw = tagged & ~3
                result['draws'].append({'tagged': hex(tagged), 'backlink': hex(self.pointer(draw)),
                    'mesh': self.identity(self.pointer(draw + 0x88)),
                    'skinned_instance': self.identity(self.pointer(draw + 0xa0)),
                    'mesh_handle': self.integer(draw + 0x90), 'active': self.integer(draw + 0x94)})
                # 5e3610 consumes this non-polymorphic instance. Capture its
                # own bounded prefix, not an invented RTTI identity/layout.
                instance = self.pointer(draw + 0xa0)
                data = self.read(instance, 0x80) if instance else None
                if data:
                    result['draws'][-1]['skinned_instance_prefix_hex'] = data.hex()
                    result['draws'][-1]['cached_source_rig'] = self.identity(self.pointer(instance + 0x18))
                    result['draws'][-1]['cached_target_rig'] = self.identity(self.pointer(instance + 0x20))
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--base', type=lambda v: int(v, 0), default=0x800000000)
    parser.add_argument('--player', type=lambda v: int(v, 0))
    parser.add_argument('--soldier-resource', type=lambda v: int(v, 0), action='append', default=[])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    reader = Inspector(args.pid, args.base)
    report = {'pid': args.pid, 'read_only': True, 'atomic': False, 'base': hex(args.base)}
    try:
        report['actor_registry'] = reader.registry(0x3ca98b0, 128)
        report['resource_registry'] = reader.registry(0x3caac10, 128)
        report['actors'] = []
        if args.player:
            report['actors'].append(reader.actor(args.player))
        resources = set(args.soldier_resource)
        report['exposed_soldiers'] = reader.soldiers()
        captured = {actor['address'] for actor in report['actors']}
        for actor in report['exposed_soldiers']:
            address = int(actor['address'], 16)
            if actor['address'] not in captured:
                report['actors'].append(reader.actor(address))
                captured.add(actor['address'])
            resources.add(reader.pointer(address + 0x1d0))
        for resource in report['resource_registry']['objects']:
            if resource.get('resource_name') == 'VSA_Engineer':
                resources.add(int(resource['address'], 16))
        report['resources'] = [reader.variants(address) for address in sorted(resources)]
    except ValueError as error:
        report['error'] = str(error)
    finally:
        report['reads_used'] = 30000 - reader.remaining
        reader.api.CloseHandle(reader.handle)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'reads_used': report['reads_used'],
                      'actors': len(report.get('actors', [])), 'resources': len(report.get('resources', [])),
                      'error': report.get('error')}))


if __name__ == '__main__':
    main()
