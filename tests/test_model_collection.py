import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
spec=importlib.util.spec_from_file_location('collection',Path(__file__).resolve().parents[1]/'tools/scan-model-collection.py')
collection=importlib.util.module_from_spec(spec);spec.loader.exec_module(collection)


class ModelCollectionTests(unittest.TestCase):
    def test_header_spanning_blocks_does_not_read_geometry(self):
        name=b'StaticMeshResource'
        data=b'RTTIBin<2.12> \x03\x00'+struct.pack('<IIIII',0,1,1,1,1)
        data+=struct.pack('<I',len(name))+name+bytes(16)
        data+=struct.pack('<III',1,0,1)+bytes(range(16))+struct.pack('<I',4)
        def blocks(path,decoder):
            yield 0,data[:40]
            yield 40,data[40:]
            raise AssertionError('Geometry payload must not be decoded')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'test.core';path.write_bytes(data+b'abcd')
            header=collection.model_header(path,None,blocks)
        self.assertEqual(header['resource_objects'][0]['type'],'StaticMeshResource')
        self.assertEqual(header['resource_objects'][0]['declared_serialized_bytes'],4)

    def test_unknown_header_fails_closed(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'test.core';path.write_bytes(b'X'*64)
            with self.assertRaisesRegex(ValueError,'Unsupported RTTIBin'):
                collection.model_header(path,None,lambda p,d:iter([(0,b'X'*64)]))


if __name__=='__main__':unittest.main()
