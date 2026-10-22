"""Native capture guards, tested with synthetic headers and no running game."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('geometry_capture',Path(__file__).resolve().parents[1]/'tools/capture-model-geometry.py')
capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)


class GeometryCaptureTests(unittest.TestCase):
    def headers(self,stride=36,index_width=2):
        descriptor=bytearray(96);vertex=bytearray(0x108);index=bytearray(0x70)
        struct.pack_into('<I',descriptor,8,100)
        struct.pack_into('<II',descriptor,0x1c,stride,1)
        descriptor[0x24:0x28]=bytes((0,2,3,0))
        struct.pack_into('<I',vertex,0xc,100);struct.pack_into('<I',vertex,0x14,stride)
        struct.pack_into('<QQ',vertex,0x78,100*stride,0x100000)
        struct.pack_into('<I',index,0x14,300)
        struct.pack_into('<QQ',index,0x28,300*index_width,0x200000)
        return descriptor,vertex,index

    def test_observed_strides_and_index_widths(self):
        for stride in (32,36,40,44):
            for width in (2,4):
                layout=capture.geometry_layout(*self.headers(stride,width))
                self.assertEqual(layout['stride'],stride)
                self.assertEqual(layout['index_width'],width)
                self.assertEqual(layout['vertices'],100)

    def test_counts_sizes_addresses_and_attributes_are_guarded(self):
        cases=((0,8,0),(0,0x20,16),(1,0xc,101),(1,0x14,999),
               (1,0x78,0),(1,0x80,0),(2,0x14,0),(2,0x28,301),(2,0x30,0))
        for which,offset,value in cases:
            headers=self.headers()
            struct.pack_into('<I',headers[which],offset,value)
            with self.subTest(which=which,offset=offset):
                with self.assertRaises(ValueError):capture.geometry_layout(*headers)
        headers=self.headers();headers[0][0x24]=36
        with self.assertRaises(ValueError):capture.geometry_layout(*headers)
        with self.assertRaises(ValueError):capture.geometry_layout(b'',b'',b'')

    def test_allocation_flags_are_not_part_of_buffer_length(self):
        headers=self.headers()
        struct.pack_into('<Q',headers[1],0x78,(3<<60)+3600)
        struct.pack_into('<Q',headers[2],0x28,(9<<60)+600)
        layout=capture.geometry_layout(*headers)
        self.assertEqual((layout['vertex_bytes'],layout['index_bytes']),(3600,600))
        struct.pack_into('<Q',headers[1],0x78,(3<<60)+3599)
        with self.assertRaises(ValueError):capture.geometry_layout(*headers)


if __name__=='__main__':unittest.main()
