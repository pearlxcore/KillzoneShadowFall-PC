#!/usr/bin/env python3
"""Bounded read-only native mesh capture for external-model loader research.

Explicit ModelPartResource address only; follows typed mesh/primitive links.
Does not call the game, modify memory, suspend threads, or capture the process.
Captured headers include session pointers and are NOT replayable game objects.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec=importlib.util.spec_from_file_location('geometry_memory',Path(__file__).with_name('read-character-memory.py'))
memory=importlib.util.module_from_spec(spec);spec.loader.exec_module(memory)


def geometry_layout(vertex_descriptor, vertex_object, index_object):
    if len(vertex_descriptor)!=96 or len(vertex_object)!=0x108 or len(index_object)!=0x70:
        raise ValueError('Invalid native header sizes')
    vertices=struct.unpack_from('<I',vertex_descriptor,8)[0]
    stride=struct.unpack_from('<I',vertex_descriptor,0x1c)[0]
    elements=struct.unpack_from('<I',vertex_descriptor,0x20)[0]
    uploaded_vertices=struct.unpack_from('<I',vertex_object,0xc)[0]
    uploaded_stride=struct.unpack_from('<I',vertex_object,0x14)[0]
    vertex_bytes,vertex_address=struct.unpack_from('<QQ',vertex_object,0x78)
    vertex_bytes&=0x0fffffffffffffff  # Native allocation flags occupy the high nibble.
    indices=struct.unpack_from('<I',index_object,0x14)[0]
    index_bytes,index_address=struct.unpack_from('<QQ',index_object,0x28)
    index_bytes&=0x0fffffffffffffff
    if not(0<vertices<=1000000 and 12<=stride<=256 and 0<elements<=15 and
           0x24+elements*4<=96 and vertices==uploaded_vertices and stride==uploaded_stride and
           vertex_bytes==vertices*stride and 0<indices<=3000000 and
           index_bytes in (indices*2,indices*4) and vertex_address>=0x10000 and index_address>=0x10000):
        raise ValueError('Unrecognized vertex/index layout')
    element_data=vertex_descriptor[0x24:0x24+elements*4]
    if any(element_data[i]>=stride for i in range(0,len(element_data),4)):
        raise ValueError('Vertex element offset exceeds stride')
    return {'vertices':vertices,'stride':stride,'indices':indices,
            'index_width':index_bytes//indices,'vertex_bytes':vertex_bytes,'index_bytes':index_bytes,
            'vertex_address':hex(vertex_address),'index_address':hex(index_address),
            'element_records_hex':[element_data[i:i+4].hex() for i in range(0,len(element_data),4)]}


class Capture(memory.Reader):
    def __init__(self,pid,output):
        super().__init__(pid);self.output=output;self.reads=0;self.bytes=0;self.meshes=[];self.seen=set()

    def read(self,address,size):
        self.reads+=1;self.bytes+=size
        if self.reads>20000 or self.bytes>64*1024*1024:raise ValueError('Shared capture budget exceeded')
        return super().read(address,size)

    def exact(self,address,size):
        data=self.read(address,size)
        if data is None:raise ValueError(f'Unreadable native data at {address:#x}')
        return data

    def blob(self,address,size,name):
        if not 0<size<=8*1024*1024:raise ValueError('GPU buffer size exceeds capture limit')
        data=b''.join(self.exact(address+i,min(65536,size-i)) for i in range(0,size,65536))
        path=self.output/name;path.write_bytes(data)
        return {'file':name,'bytes':size,'sha256':hashlib.sha256(data).hexdigest()}

    def primitive(self,address,name):
        identity=self.identity(address)
        if identity.get('type_name')!='RenderingPrimitiveResource':raise ValueError('Not a rendering primitive')
        prefix=self.exact(address,0x60)
        vertex,index=struct.unpack_from('<QQ',prefix,0x10)
        if self.identity(vertex).get('type_name')!='VertexArrayResource' or self.identity(index).get('type_name')!='IndexArrayResource':
            raise ValueError('Invalid primitive arrays')
        descriptor=self.pointer(vertex+0x10);index_object=self.pointer(index+0x10)
        vd=self.exact(descriptor,96);vertex_object=struct.unpack_from('<Q',vd,0x10)[0]
        vo=self.exact(vertex_object,0x108);io=self.exact(index_object,0x70)
        layout=geometry_layout(vd,vo,io)
        vb=self.blob(int(layout['vertex_address'],16),layout['vertex_bytes'],name+'-vertices.bin')
        ib=self.blob(int(layout['index_address'],16),layout['index_bytes'],name+'-indices.bin')
        index_data=(self.output/ib['file']).read_bytes()
        maximum=max(x[0] for x in struct.iter_unpack('<H' if layout['index_width']==2 else '<I',index_data))
        if maximum>=layout['vertices']:raise ValueError('Index outside vertex buffer')
        if (self.exact(address,0x60)!=prefix or self.exact(descriptor,96)!=vd or
            self.exact(vertex_object,0x108)!=vo or self.exact(index_object,0x70)!=io):
            raise ValueError('Geometry headers changed during capture')
        return {'identity':identity,'layout':layout,'vertex_buffer':vb,'index_buffer':ib,
                'primitive_hex':prefix.hex(),'vertex_descriptor_hex':vd.hex(),
                'vertex_object_hex':vo.hex(),'index_object_hex':io.hex()}

    def walk(self,address,depth=0):
        if address in self.seen:return
        if not address or depth>8 or len(self.seen)>=256:raise ValueError('Mesh traversal bound exceeded')
        self.seen.add(address);identity=self.identity(address);kind=identity.get('type_name')
        item={'identity':identity};self.meshes.append(item)
        if kind in ('MultiMeshResource','LodMeshResource','SwitchMeshResource'):
            prefix=self.exact(address,0x70);count=struct.unpack_from('<i',prefix,0x58)[0];array=struct.unpack_from('<Q',prefix,0x60)[0]
            if not 0<=count<=128:raise ValueError('Invalid composite mesh count')
            stride=0x50 if kind=='MultiMeshResource' else 0x10
            data=self.exact(array,count*stride);item.update(prefix_hex=prefix.hex(),children_hex=data.hex())
            for i in range(count):self.walk(struct.unpack_from('<Q',data,i*stride)[0],depth+1)
        elif kind=='RegularSkinnedMeshResource':
            prefix=self.exact(address,0x100);count=struct.unpack_from('<i',prefix,0xc8)[0];array=struct.unpack_from('<Q',prefix,0xd0)[0]
            if not 0<count<=64:raise ValueError('Invalid primitive count')
            data=self.exact(array,count*8);item.update(prefix_hex=prefix.hex(),primitives=[])
            for i in range(count):
                primitive=struct.unpack_from('<Q',data,i*8)[0]
                try:item['primitives'].append(self.primitive(primitive,f'mesh-{len(self.meshes)-1:03d}-{i:02d}'))
                except ValueError as error:item['primitives'].append({'address':hex(primitive),'error':str(error)})
        else:item['unsupported_type']=True


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',required=True,type=int)
    parser.add_argument('--body',required=True,type=lambda x:int(x,0))
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    capture=Capture(args.pid,args.output)
    report={'pid':args.pid,'read_only':True,'atomic':False,'replayable':False,
            'scope':'Known native model hierarchy and bounded GPU vertex/index buffers'}
    try:
        identity=capture.identity(args.body)
        if identity.get('type_name')!='ModelPartResource':raise ValueError('Supply an explicit ModelPartResource body address')
        report['body']=identity;capture.walk(capture.pointer(args.body+0x18))
    finally:
        capture.api.CloseHandle(capture.handle)
        report.update(meshes=capture.meshes,reads=capture.reads,bytes_read=capture.bytes)
        (args.output/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    primitives=[p for mesh in capture.meshes for p in mesh.get('primitives',[])]
    print(f'Captured {len(capture.meshes)} mesh resources, {sum("layout" in p for p in primitives)} validated primitive buffers; {sum("error" in p for p in primitives)} refused')


if __name__=='__main__':main()
