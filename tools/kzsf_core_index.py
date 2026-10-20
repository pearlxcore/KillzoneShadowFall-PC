"""Bounded RTTIBin 2.12 object-header reader for local character inventories.

Only header identity/type/declared size is decoded. Serialized object payloads,
references, patch application and skeleton compatibility are deliberately unknown.
Format reference: https://github.com/ShadelessFox/decima-workshop/wiki/Corefiles
"""
from collections import Counter
import struct
import uuid

MAX_HEADER = 16 * 1024 * 1024
MAX_TYPES = 16384
MAX_OBJECTS = 500000
RESOURCE_TYPES = frozenset(('SoldierResource', 'HumanoidResource', 'Skeleton',
    'SkinnedModelResource', 'StaticModelResource', 'ModelPartResource',
    'RegularSkinnedMeshResource', 'SkinnedMeshResource', 'StaticMeshResource',
    'MultiMeshResource', 'LodMeshResource', 'SwitchMeshResource',
    'FacialExpressionAnimatorResource', 'SkinnedMeshBoneBindings'))


def parse_index(prefix, decoded_size):
    """Read headers from a bounded prefix; require total decoded length separately."""
    data = memoryview(prefix)
    position = 0

    def take(size):
        nonlocal position
        if size < 0 or position + size > min(len(data), MAX_HEADER):
            raise ValueError('Truncated or oversized RTTIBin header')
        result = data[position:position + size]
        position += size
        return result

    def u32():
        return struct.unpack('<I', take(4))[0]

    if bytes(take(14)) != b'RTTIBin<2.12> ':
        raise ValueError('Unsupported RTTIBin version')
    if bytes(take(2)) != b'\x03\x00':
        raise ValueError('Expected PINK platform, little endian')
    pointer_map_size, strings, wide_strings, asset_count, type_count = (u32() for _ in range(5))
    if asset_count > MAX_OBJECTS or type_count > MAX_TYPES or (asset_count and not type_count):
        raise ValueError('RTTIBin table count exceeds bounds')
    types = []
    for _ in range(type_count):
        length = u32()
        if not 0 < length <= 512:
            raise ValueError('Invalid RTTIBin type name length')
        raw = bytes(take(length))
        if any(c < 33 or c > 126 for c in raw):
            raise ValueError('Invalid RTTIBin type name')
        types.append(dict(name=raw.decode('ascii'), uuid_bytes_hex=bytes(take(16)).hex()))
    count = u32()
    if count > MAX_OBJECTS:
        raise ValueError('RTTIBin object count exceeds bounds')
    indices = [u32() for _ in range(count)]
    if any(index >= type_count for index in indices):
        raise ValueError('RTTIBin object type outside table')
    explicit_count = u32()
    if explicit_count > count:
        raise ValueError('RTTIBin explicit object count exceeds total')
    objects, size_sum, nonempty = [], 0, 0
    counts = Counter()
    for index, type_index in enumerate(indices):
        identity = bytes(take(16))
        size = u32()
        size_sum += size
        nonempty += size != 0
        name = types[type_index]['name']
        counts[name] += 1
        if name in RESOURCE_TYPES:
            objects.append(dict(object_index=index, type=name,
                uuid_bytes_hex=identity.hex(), uuid_le=str(uuid.UUID(bytes_le=identity)),
                declared_serialized_bytes=size))
    if nonempty != explicit_count:
        raise ValueError('RTTIBin explicit/nonempty count mismatch')
    if decoded_size < position or size_sum > decoded_size - position:
        raise ValueError('RTTIBin declared object sizes exceed decoded file')
    return dict(format='RTTIBin 2.12', platform='PINK', header_bytes=position,
        decoded_bytes=decoded_size, pointer_map_size=pointer_map_size,
        string_count=strings, wide_string_count=wide_strings,
        asset_count=asset_count, object_count=count, explicit_object_count=explicit_count,
        type_count=type_count, type_counts=dict(sorted(counts.items())),
        resource_objects=objects, payload_references_parsed=False,
        patch_applied=False)
