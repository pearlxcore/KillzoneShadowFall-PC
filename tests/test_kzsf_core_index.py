import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from kzsf_core_index import parse_index


def fixture():
    types=[b'Skeleton',b'ModelPartResource']
    data=bytearray(b'RTTIBin<2.12> \x03\x00'+struct.pack('<IIIII',0,0,0,1,2))
    for name in types:
        data+=struct.pack('<I',len(name))+name+bytes(16)
    data+=struct.pack('<IIII',2,0,1,1) # two object types, one nonempty object
    data+=bytes(range(16))+struct.pack('<I',4)
    data+=bytes(range(16,32))+struct.pack('<I',0)
    return data


class CoreIndexTests(unittest.TestCase):
    def test_empty_core_has_no_type_or_object_table_entries(self):
        data=b'RTTIBin<2.12> \x03\x00'+struct.pack('<IIIIIII',0,1,1,0,0,0,0)
        result=parse_index(data,len(data)+2)
        self.assertEqual(result['object_count'],0)
        self.assertEqual(result['resource_objects'],[])
        self.assertEqual(result['type_count'],0)

    def test_resource_identity_and_implicit_object(self):
        data=fixture();result=parse_index(data,len(data)+4)
        self.assertEqual(result['object_count'],2)
        self.assertEqual(result['asset_count'],1)
        self.assertEqual(result['type_counts'],{'ModelPartResource':1,'Skeleton':1})
        self.assertEqual(result['resource_objects'][0]['uuid_bytes_hex'],bytes(range(16)).hex())
        self.assertFalse(result['payload_references_parsed'])

    def test_truncation_at_every_header_byte(self):
        data=fixture()
        for cut in range(len(data)):
            with self.subTest(cut=cut),self.assertRaises(ValueError):
                parse_index(data[:cut],len(data)+4)

    def test_reject_wrong_version_platform_and_count_bounds(self):
        for offset,value in ((0,b'X'),(14,b'\x01'),(15,b'\x01'),
                             (32,struct.pack('<I',0xffffffff))):
            data=fixture();data[offset:offset+len(value)]=value
            with self.assertRaises(ValueError):parse_index(data,len(data)+4)

    def test_reject_out_of_range_type_and_impossible_payload(self):
        data=fixture()
        object_table=36+sum(20+len(x) for x in (b'Skeleton',b'ModelPartResource'))
        struct.pack_into('<I',data,object_table+4,2)
        with self.assertRaises(ValueError):parse_index(data,len(data)+4)
        with self.assertRaises(ValueError):parse_index(fixture(),len(fixture()))


if __name__=='__main__':unittest.main()
