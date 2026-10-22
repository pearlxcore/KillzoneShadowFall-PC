import importlib.util
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from kzsf_vertex_stream import encode_stream, decode_stream
from kzsf_skin_weights import decode_eight

spec = importlib.util.spec_from_file_location('probe', Path(__file__).resolve().parents[1] / 'tools/build-cj-influence-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class InfluenceProbeTests(unittest.TestCase):
    def test_all_test_slots_preserve_identical_bone_blends(self):
        elements = [bytes.fromhex(x) for x in ('00020300', '0c060304', '10060401', '14070411', '18070413', '1c040410', '20040412', '24030406')]
        data = bytearray(88)
        struct.pack_into('<f', data, 0, -0.2)
        struct.pack_into('<f', data, 44, 0.2)
        source = encode_stream(2, 44, elements, data)
        for slot in range(2, 8):
            result, sides = probe.probe_stream(source, 10, 55, slot)
            self.assertEqual(sides, [1, 1])
            _, _, _, output, _ = decode_stream(result)
            for offset in (0, 44):
                self.assertEqual(output[offset:offset+20], data[offset:offset+20])
                self.assertEqual(output[offset+36:offset+44], data[offset+36:offset+44])
                joints, weights = decode_eight(output[offset+20:offset+28], output[offset+28:offset+36], 98)
                blend = {j: w for j, w in zip(joints, weights) if w}
                self.assertEqual(blend, {10: 128/255, 55: 127/255})
            # A two-slot shader would distinguish the halves, unlike full skinning.
            self.assertEqual(output[28], 127)
            self.assertEqual(output[44+28], 0)


if __name__ == '__main__':
    unittest.main()
