import importlib.util
from pathlib import Path
import sys
import unittest
import tempfile
import struct
import numpy as np

folder=Path(__file__).resolve().parents[1]/'tools';sys.path.insert(0,str(folder))
spec=importlib.util.spec_from_file_location('native_cj',folder/'build-cj-native-streams.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)


class NativeStreamTests(unittest.TestCase):
    def test_native_uv_uses_base_color_zw_pair(self):
        self.assertEqual(struct.unpack('<4e',c.pack_uv([.125,.75])),(0,0,.125,.25))
        self.assertEqual(struct.unpack('<4e',c.pack_uv([-1,2])),(0,0,-1,-1))
        for uv in ([float('nan'),0],[0,65536],[0,-65504],[0]):
            with self.assertRaises(ValueError):c.pack_uv(uv)

    def test_model_manifest_carries_sections_and_rig_identity(self):
        va=b'v'*168;ia=b'i'*38;texture=b't'*16400
        section={'vertices':3,'triangles':1,'material':4}
        manifest=c.model_manifest([(va,ia)],[texture],[section],{4:0},123,True)
        self.assertEqual(len(manifest),84)
        self.assertEqual(struct.unpack_from('<7I',manifest,4),(1,98,1,1,1,123,0))
        self.assertEqual(struct.unpack_from('<5I',manifest,32),(3,3,0,168,38))
        import zlib
        self.assertEqual(struct.unpack_from('<I',manifest,76)[0],zlib.crc32(texture))
        bones=[{'name':'Root','inverse':np.eye(4).flatten().tolist()}]
        first=c.rig_profile(bones);bones[0]['name']='other'
        self.assertNotEqual(first,c.rig_profile(bones))

    def test_texture_export_deduplicates_and_preserves_rgba(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory);Image.new('RGBA',(64,64),(2,3,4,127)).save(folder/'color.png')
            material={'pbrMetallicRoughness':{'baseColorTexture':{'index':0}}}
            doc={'materials':[material,material],'textures':[{'source':0}],'images':[{'uri':'color.png'}]}
            payloads,mapping,meta=c.color_payloads(doc,folder,[0,1,0])
            self.assertEqual(len(payloads),1);self.assertEqual(mapping,{0:0,1:0})
            self.assertEqual(payloads[0][:16],b'KZTX'+struct.pack('<III',1,64,64))
            self.assertEqual(payloads[0][16:],bytes((2,3,4,127))*4096)
            self.assertEqual(meta[0]['bytes'],16400)
            doc['images'][0]['uri']='../outside.png'
            with self.assertRaises(ValueError):c.color_payloads(doc,folder,[0])
            doc['images'][0]['uri']='color.png';Image.new('RGB',(65,64)).save(folder/'color.png')
            with self.assertRaises(ValueError):c.color_payloads(doc,folder,[0])

    def test_packed_signed_directions(self):
        for vector in ([1,0,0],[-1,0,0],[0,.6,.8]):
            packed=c.pack_direction(vector,3);result=[]
            for i in range(3):
                x=(packed>>(i*10))&1023;result.append((x if x<512 else x-1024)/511)
            np.testing.assert_allclose(result,vector,atol=1/511)
            self.assertEqual(packed>>30,3)
        with self.assertRaises(ValueError):c.pack_direction([2,0,0])
        with self.assertRaises(ValueError):c.pack_direction([float('nan'),0,0])

    def test_tangent_basis_and_degenerate_uv_fallback(self):
        p=np.array([[0,0,0],[1,0,0],[0,1,0]],dtype=float);n=np.array([[0,0,1]]*3,dtype=float)
        for uv,expected in ((np.array([[0,0],[1,0],[0,1]],dtype=float),0),(np.zeros((3,2)),1)):
            result,degenerate=c.tangents(p,n,uv,np.array([0,1,2]))
            self.assertEqual(degenerate,expected)
            for t,sign in result:
                self.assertAlmostEqual(np.linalg.norm(t),1);self.assertAlmostEqual(t@n[0],0);self.assertIn(sign,(1,3))


if __name__=='__main__':unittest.main()
