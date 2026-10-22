"""Synthetic texture fixtures only; no game assets are required."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('cj_texture',Path(__file__).resolve().parents[1]/'tools/cj_texture.py')
decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(decoder)


class CjTextureTests(unittest.TestCase):
    def test_known_bc1_tile_addresses_and_next_tile(self):
        data=b''.join(struct.pack('<Q',i) for i in range(1024))
        linear=decoder.linearize_mode5(data,256,64,8)
        def block(x,y): return struct.unpack_from('<Q',linear,(y*64+x)*8)[0]
        self.assertEqual(block(1,0),1)
        self.assertEqual(block(0,1),2)
        self.assertEqual(block(0,2),4)
        self.assertEqual(block(2,0),8)
        self.assertEqual(block(16,0),256)
        self.assertEqual(block(32,0),512)
        self.assertEqual(len(set(struct.unpack('<1024Q',linear))),1024)

    def test_bc5_addresses_and_invalid_buffers(self):
        data=b''.join(struct.pack('<QQ',i,i) for i in range(256))
        linear=decoder.linearize_mode5(data,64,64,16)
        self.assertEqual(struct.unpack_from('<Q',linear,16)[0],4)
        self.assertEqual(struct.unpack_from('<Q',linear,16*16)[0],1)
        with self.assertRaises(ValueError):decoder.linearize_mode5(data[:-1],64,64,16)
        with self.assertRaises(ValueError):decoder.linearize_mode5(data,64,64,4)

    def test_texture_export_bounds_flags_and_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            asset=Path(folder)/'texture.uasset';asset.write_bytes(bytes(64))
            bulk=asset.with_suffix('.ubulk');bulk.write_bytes(bytes(4096))
            def fixture(mode=5,flags=0x10501,offset=0,first=0):
                return (struct.pack('<4I',128<<first,64<<first,0x40000001,8)+b'PF_DXT1\0'+
                        struct.pack('<2I',mode,1)+struct.pack('<6IQ3I',first,1,1,flags,4096,4096,offset,128,64,1))
            export=fixture();asset.with_suffix('.uexp').write_bytes(export)
            dds=decoder.decode_texture(asset,64,len(export))
            self.assertEqual(dds[:4],b'DDS ')
            self.assertEqual(len(dds),128+4096)
            asset.with_suffix('.uexp').write_bytes(fixture(first=1))
            self.assertEqual(decoder.decode_texture(asset),dds)
            for blob in (fixture(mode=9),fixture(flags=0),fixture(offset=100)):
                asset.with_suffix('.uexp').write_bytes(blob)
                with self.assertRaises(ValueError):decoder.decode_texture(asset)
            with self.assertRaises(ValueError):decoder.decode_texture(asset,0,12)


if __name__=='__main__':unittest.main()
