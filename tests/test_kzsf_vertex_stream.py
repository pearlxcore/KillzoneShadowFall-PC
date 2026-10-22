"""Synthetic native stream encoding, bounds and identity tests."""
import importlib.util
from pathlib import Path
import unittest
import struct

spec=importlib.util.spec_from_file_location('vertex_stream',Path(__file__).resolve().parents[1]/'tools/kzsf_vertex_stream.py')
stream=importlib.util.module_from_spec(spec);spec.loader.exec_module(stream)


class VertexStreamTests(unittest.TestCase):
    def test_index_stream_round_trip_and_bounds(self):
        for width,fmt in ((2,'<6H'),(4,'<6I')):
            data=struct.pack(fmt,0,1,2,2,1,3)
            payload=stream.encode_indices(4,width,data)
            self.assertEqual(stream.decode_indices(payload,4),(6,width,data))
            for bad in (payload[:-1],payload+bytes(1),payload[:-1]+b'\xff'):
                with self.assertRaises(ValueError):stream.decode_indices(bad,4)
            with self.assertRaises(ValueError):stream.encode_indices(3,width,data)
        for width,data in ((1,b'123'),(2,b'1'),(2,bytes(4))):
            with self.assertRaises(ValueError):stream.encode_indices(4,width,data)

    def test_round_trip_all_observed_strides(self):
        for stride in (32,36,40,44):
            data=bytes(range(stride))*10;elements=[bytes((0,2,3,0))]
            payload=stream.encode_stream(10,stride,elements,data)
            self.assertEqual(stream.decode_stream(payload),(10,stride,elements,data,0x31))
            self.assertEqual(payload[:8],b'\x0a\0\0\0\x01\0\0\0')

    def test_rejects_bad_lengths_layouts_and_corruption(self):
        for count,stride,elements,data in ((0,32,[bytes(4)],b''),(1,32,[],bytes(32)),
                                          (1,32,[bytes((32,2,3,0))],bytes(32)),
                                          (1,32,[bytes(4),bytes(4)],bytes(32)),
                                          (1,32,[bytes(4)],bytes(31))):
            with self.assertRaises(ValueError):stream.encode_stream(count,stride,elements,data)
        payload=stream.encode_stream(1,32,[bytes((0,2,3,0))],bytes(32))
        for changed in (payload[:-1],payload+bytes(1),payload[:-1]+b'\x01',payload[:10]):
            with self.assertRaises(ValueError):stream.decode_stream(changed)


if __name__=='__main__':unittest.main()
