import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('soldier_names', ROOT / 'tools/inspect-chapter3-soldier-names.py')
scanner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scanner)


def fixture(objects):
    types = [b'OtherResource', b'SoldierResource']
    data = bytearray(b'RTTIBin<2.12> \x03\x00' + struct.pack('<IIIII', 0, 0, 0, 1, 2))
    for name in types:
        data += struct.pack('<I', len(name)) + name + bytes(16)
    data += struct.pack('<I', len(objects))
    data += b''.join(struct.pack('<I', kind) for kind, payload in objects)
    data += struct.pack('<I', sum(bool(payload) for kind, payload in objects))
    for i, (kind, payload) in enumerate(objects):
        data += bytes([i]) * 16 + struct.pack('<I', len(payload))
    return bytes(data) + b''.join(payload for kind, payload in objects)


def payload(name):
    encoded = name.encode('ascii')
    return b'\x01\x01\x01' + struct.pack('<II', len(encoded) + 10, len(encoded)) + encoded + bytes(8)


class SoldierNamesTests(unittest.TestCase):
    def test_names_are_scoped_to_typed_payloads_not_other_strings(self):
        raw = fixture([(0, b'ISA_False_Name'), (1, payload('HGH_Medic')),
                       (1, b''), (1, payload('vsa_Civilians_Female_04'))])
        result = scanner.names(raw)
        self.assertEqual([r['name'] for r in result], ['HGH_Medic', 'vsa_Civilians_Female_04'])
        self.assertEqual([r['object_index'] for r in result], [1, 3])
        for row in result:
            self.assertEqual(raw[row['payload_offset'] + 11:row['payload_offset'] + 11 + len(row['name'])], row['name'].encode())

    def test_invalid_name_bounds_and_encoding_fail(self):
        for body in (bytes(10), bytes(7) + struct.pack('<I', 129) + b'A' * 129,
                     bytes(7) + struct.pack('<I', 4) + b'AB', payload('Good')[:11] + b'\0Bad' + bytes(8)):
            with self.subTest(body=body[:16]), self.assertRaises(ValueError):
                scanner.names(fixture([(1, body)]))


if __name__ == '__main__':
    unittest.main()
