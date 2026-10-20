"""Capture exact bounded immutable rig inputs for offline selector regression.
Read-only process access; explicit PID/body addresses, no game calls or writes.
"""
import argparse,importlib.machinery,json,struct
from pathlib import Path
memory=importlib.machinery.SourceFileLoader('rig_memory',str(Path(__file__).with_name('read-character-memory.py'))).load_module()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',required=True,type=int);p.add_argument('--body',action='append',required=True);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 if not 2<=len(a.body)<=64:p.error('Use2..64 explicit named body addresses')
 r=memory.Reader(a.pid);blocks={};leaves=[];report={}
 def capture(address,size):
  if not 0<size<=65536:raise ValueError('Invalid read size')
  data=r.read(address,size)
  if data is None:raise ValueError('Unreadable rig input')
  blocks[(address,size)]=data;return data
 def text(address):
  head=capture(address,16);n=struct.unpack_from('<i',head,8)[0]
  if not 0<n<=128:raise ValueError('Invalid rig name')
  capture(address,16+n)
 def first(mesh,depth=0):
  if depth>=9:raise ValueError('Mesh depth bound')
  kind=r.identity(mesh).get('type_name')
  if kind in ('RegularSkinnedMeshResource','SkinnedMeshResource'):return mesh
  stride={'MultiMeshResource':80,'LodMeshResource':16,'SwitchMeshResource':16}.get(kind)
  if not stride:raise ValueError('Unknown mesh')
  header=r.read(mesh+0x58,16)
  if not header:raise ValueError('Unreadable mesh')
  count,_,array=struct.unpack('<iiQ',header)
  if not 0<count<=128:raise ValueError('Mesh children bound')
  return first(r.pointer(array),depth+1)
 try:
  for item in a.body:
   name,body=item.split('=',1);body=int(body,0);mesh=first(r.pointer(body+0x18));sk=r.pointer(mesh+0x58);bd=r.pointer(mesh+0x70)
   sh=capture(sk,0x40);bh=capture(bd,0x38);n=struct.unpack_from('<i',sh,0x30)[0];bn=struct.unpack_from('<i',bh,0x10)[0]
   if n!=bn or not 78<=n<=320:raise ValueError('Rig size mismatch')
   joints=struct.unpack_from('<Q',sh,0x38)[0];jointdata=capture(joints,n*24)
   names=struct.unpack_from('<Q',bh,0x18)[0];nd=capture(names,n*8);inverse=struct.unpack_from('<Q',bh,0x30)[0];capture(inverse,n*64)
   for i in range(n):text(struct.unpack_from('<Q',jointdata,i*24)[0]);text(struct.unpack_from('<Q',nd,i*8)[0])
   if r.read(sk,0x40)!=sh or r.read(bd,0x38)!=bh:raise ValueError('Rig header changed during capture')
   leaves.append((sk,bd,n));report[name]={'body':hex(body),'skeleton':hex(sk),'bindings':hex(bd),'bones':n}
 finally:r.api.CloseHandle(r.handle)
 with a.output.open('wb') as f:
  f.write(struct.pack('<I',len(leaves)))
  for leaf in leaves:f.write(struct.pack('<QQi',*leaf))
  f.write(struct.pack('<I',len(blocks)))
  for (address,size),data in blocks.items():f.write(struct.pack('<QI',address,size));f.write(data)
 a.output.with_suffix('.json').write_text(json.dumps({'read_only':True,'atomic':False,'pid':a.pid,'models':report,'blocks':len(blocks)},indent=2))
 print(f'Captured{len(leaves)} rigs in{len(blocks)} bounded blocks')
if __name__=='__main__':main()
