import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('face_watch', Path(__file__).resolve().parents[1] / 'tools/watch-character-face.py')
watch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watch)


class Reader:
    def __init__(self):
        header = bytearray(0x70)
        struct.pack_into('<Q', header, 0x20, 0x20000)
        struct.pack_into('<QQ', header, 0x40, 0x30000, 0x30000 + 128)
        self.blocks = {0x10000: header, 0x20030: struct.pack('<i', 2),
                       0x30000: struct.pack('<32f', *([1.0] * 31 + [float('nan')]))}

    def read(self, address, size):
        for start, data in self.blocks.items():
            if start <= address and address + size <= start + len(data):
                return bytes(data[address-start:address-start+size])
        return None

    def identity(self, address):
        return {'address': hex(address), 'type_name': 'Skeleton'}


class FaceWatchTests(unittest.TestCase):
    def test_invalid_world_is_captured_without_mutation(self):
        reader = Reader()
        before = bytes(reader.blocks[0x10000])
        result = watch.pose_snapshot(reader, 0x10000)
        self.assertEqual(result['world']['nonfinite_count'], 1)
        self.assertEqual(result['world']['nonfinite_indices'], [31])
        self.assertFalse(result['header_changed_during_read'])
        self.assertEqual(reader.blocks[0x10000], before)

    def test_oversized_pose_array_is_refused(self):
        reader = Reader()
        struct.pack_into('<Q', reader.blocks[0x10000], 0x48, 0x30000 + 192)
        self.assertTrue(watch.pose_snapshot(reader, 0x10000)['world']['invalid_bounds'])

    def test_nonfinite_scalar_summary(self):
        result = watch.numeric_summary(struct.pack('<4f', 1, float('inf'), -4, float('nan')))
        self.assertEqual(result['nonfinite_count'], 2)
        self.assertEqual(result['max_abs_finite'], 4)


if __name__ == '__main__':
    unittest.main()
