#!/usr/bin/env python3
"""Read-only, bounded capture of an explicit native RenderEffectResource.

Captures texture binding names and resource identities, not texture pixels.
No writes, native calls, thread suspension, or address-space search.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec=importlib.util.spec_from_file_location('material_memory',Path(__file__).with_name('read-character-memory.py'))
memory=importlib.util.module_from_spec(spec);spec.loader.exec_module(memory)


class Capture(memory.Reader):
    def __init__(self,pid):
        super().__init__(pid);self.reads=0;self.bytes=0

    def read(self,address,size):
        self.reads+=1;self.bytes+=size
        if self.reads>3000 or self.bytes>1024*1024:raise ValueError('Material capture budget exceeded')
        return super().read(address,size)

    def exact(self,address,size):
        value=self.read(address,size)
        if value is None:raise ValueError(f'Unreadable material input at {address:#x}')
        return value

    def effect(self,address):
        identity=self.identity(address)
        if identity.get('type_name')!='RenderEffectResource':raise ValueError('Expected RenderEffectResource')
        prefix=self.exact(address,0x58)
        count=struct.unpack_from('<i',prefix,0x20)[0];array=struct.unpack_from('<Q',prefix,0x28)[0]
        if not 1<=count<=8 or not array:raise ValueError('Invalid effect variant count')
        variants=self.exact(array,count*0x98);result={'identity':identity,'variants':[]}
        for vi in range(count):
            start=vi*0x98;variant=variants[start:start+0x98]
            entries=struct.unpack_from('<i',variant,8)[0];entry_array=struct.unpack_from('<Q',variant,0x10)[0]
            if not 0<=entries<=27 or (entries and not entry_array):raise ValueError('Invalid effect entries')
            entry_data=self.exact(entry_array,entries*0x60) if entries else b''
            item={'index':vi,'id':struct.unpack_from('<i',variant)[0],'entries':[]}
            for ei in range(entries):
                entry=entry_data[ei*0x60:(ei+1)*0x60]
                bindings=struct.unpack_from('<i',entry,0x28)[0];binding_array=struct.unpack_from('<Q',entry,0x30)[0]
                if not 0<=bindings<=32 or (bindings and not binding_array):raise ValueError('Invalid texture bindings')
                shader=self.identity(struct.unpack_from('<Q',entry,0x58)[0])
                if shader.get('type_name')!='ShaderResource':raise ValueError('Invalid effect shader')
                data=self.exact(binding_array,bindings*0x30) if bindings else b''
                bound=[]
                for bi in range(bindings):
                    sampler,name,texture=struct.unpack_from('<3Q',data,bi*0x30)
                    sampler=self.engine_string(sampler);name=self.engine_string(name);texture=self.identity(texture)
                    if sampler is None or name is None or texture.get('type_name')!='Texture':
                        raise ValueError('Invalid named texture binding')
                    bound.append({'index':bi,'sampler':sampler,'parameter':name,'texture':texture})
                if bindings and self.exact(binding_array,bindings*0x30)!=data:
                    raise ValueError('Texture bindings changed during capture')
                item['entries'].append({'index':ei,'shader':shader,'textures':bound})
            if entries and self.exact(entry_array,entries*0x60)!=entry_data:
                raise ValueError('Effect entries changed during capture')
            result['variants'].append(item)
        if self.exact(array,count*0x98)!=variants or self.exact(address,0x58)!=prefix:
            raise ValueError('Effect changed during capture')
        return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid',required=True,type=int)
    parser.add_argument('--effect',required=True,type=lambda v:int(v,0))
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    reader=Capture(args.pid)
    try:
        report={'pid':args.pid,'read_only':True,'atomic':False,'replayable':False,'effect':reader.effect(args.effect)}
        report.update(reads=reader.reads,bytes=reader.bytes)
    finally:reader.api.CloseHandle(reader.handle)
    with args.output.open('x',encoding='utf-8') as output:json.dump(report,output,indent=2)
    print(json.dumps({'output':str(args.output),'variants':len(report['effect']['variants']),'reads':reader.reads}))


if __name__=='__main__':main()
