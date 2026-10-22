#!/usr/bin/env python3
"""Prepare detached native CJ vertex/index upload payloads, not a playable model.

Keeps CJ's source pose with a uniform coordinate/scale conversion. Animation
binding corrections are deliberately separate. No game memory access.
"""
import argparse
import importlib.util
import json
import hashlib
import zlib
from pathlib import Path
import struct
import numpy as np
import kzsf_skin_weights as weights
import kzsf_vertex_stream as stream

spec=importlib.util.spec_from_file_location('cj_bind',Path(__file__).with_name('retarget-cj-model.py'))
bind=importlib.util.module_from_spec(spec);spec.loader.exec_module(bind)


def color_payloads(doc,folder,materials):
    """Bounded local base-color exports; shared images get one KZTX payload."""
    from PIL import Image
    payloads=[];by_image={};by_material={};metadata=[]
    for material in sorted(set(materials)):
        source=doc['materials'][material]['pbrMetallicRoughness']['baseColorTexture']['index']
        image=doc['textures'][source]['source']
        if image not in by_image:
            if len(payloads)>=16:raise ValueError('Too many character textures')
            uri=doc['images'][image].get('uri','');path=(folder/uri).resolve()
            if not uri or not path.is_relative_to(folder.resolve()) or path.stat().st_size>64*1024*1024:
                raise ValueError('Texture outside source folder or too large')
            with Image.open(path) as original:
                w,h=original.size
                if original.format!='PNG' or not(64<=w<=4096 and 64<=h<=4096) or (w&(w-1)) or (h&(h-1)):
                    raise ValueError('Unsupported character texture dimensions or format')
                pixels=original.convert('RGBA').tobytes()
            data=b'KZTX'+struct.pack('<III',1,w,h)+pixels
            index=len(payloads);by_image[image]=index;payloads.append(data)
            metadata.append({'file':f'texture-{index}.rgba','width':w,'height':h,'bytes':len(data),
                             'sha256':hashlib.sha256(data).hexdigest(),'source_image':image})
        by_material[material]=by_image[image]
    return payloads,by_material,metadata


def rig_profile(targets):
    crc=0
    for bone in targets:
        name=bone['name'].encode('ascii')
        crc=zlib.crc32(struct.pack('<I',len(name))+name,crc)
        crc=zlib.crc32(struct.pack('<16f',*bone['inverse']),crc)
    return crc


def model_manifest(prepared,textures,primitives,material_textures,profile,bind_fit):
    if not(1<=len(prepared)<=16 and 1<=len(textures)<=16) or len(primitives)!=len(prepared):
        raise ValueError('Unsupported model section or texture count')
    data=bytearray(b'KZMD'+struct.pack('<7I',1,98,len(prepared),len(textures),int(bind_fit),profile,0))
    for (va,ia),section in zip(prepared,primitives):
        data+=struct.pack('<10I',section['vertices'],section['triangles']*3,material_textures[section['material']],
            len(va),len(ia),zlib.crc32(va),zlib.crc32(ia),0,0,0)
    for texture in textures:data+=struct.pack('<3I',len(texture),zlib.crc32(texture),0)
    return bytes(data)


def pack_direction(vector,handedness=0):
    if len(vector)!=3 or not np.isfinite(vector).all():raise ValueError('Invalid direction')
    if not np.isclose(np.linalg.norm(vector),1,atol=.01):raise ValueError('Direction must be normalized')
    bits=sum((int(np.rint(np.clip(float(v),-1,1)*511))&1023)<<(i*10) for i,v in enumerate(vector))
    return bits|((int(handedness)&3)<<30)


def pack_uv(uv):
    if len(uv)!=2 or not np.isfinite(uv).all():
        raise ValueError('Invalid texture coordinate')
    # Captured semantic6 uses zw for base color. Native texture rows have the
    # opposite V convention to the exported glTF (confirmed by the live face).
    u,v=float(uv[0]),1-float(uv[1])
    if max(abs(u),abs(v))>65504:raise ValueError('Invalid texture coordinate')
    return struct.pack('<4e',0,0,u,v)


def tangents(position,normal,uv,indices):
    tangent=np.zeros_like(position,dtype=float);bitangent=np.zeros_like(position,dtype=float);degenerate=0
    for triangle in indices.reshape(-1,3):
        a,b,c=map(int,triangle);ab=position[b]-position[a];ac=position[c]-position[a];du=uv[b]-uv[a];dv=uv[c]-uv[a]
        det=du[0]*dv[1]-du[1]*dv[0]
        if abs(det)<1e-12:degenerate+=1;continue
        t=(ab*dv[1]-ac*du[1])/det;s=(ac*du[0]-ab*dv[0])/det
        for j in (a,b,c):tangent[j]+=t;bitangent[j]+=s
    result=[]
    for n,t,b in zip(normal,tangent,bitangent):
        t=t-n*np.dot(n,t)
        if np.linalg.norm(t)<1e-8:
            axis=np.eye(3)[np.argmin(abs(n))];t=np.cross(axis,n)
        t=t/np.linalg.norm(t);sign=3 if np.dot(np.cross(n,t),b)<0 else 1
        result.append((t,sign))
    return result,degenerate


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--gltf',required=True,type=Path)
    parser.add_argument('--bindings',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--bind-fit',action='store_true',help='Use the offline bind fit; requires visual and gameplay validation')
    parser.add_argument('--deform-helpers',action='store_true',help='Skin limbs to native *_Helper1 joints like the native body (unverified trial)')
    parser.add_argument('--two-influence-poses',nargs='+',type=Path,metavar='POSE_JSON',
                        help='Reduce every vertex to two influences (runtime 4-index streams honour only two), fitted over these captured poses')
    parser.add_argument('--eight-bone-stream',action='store_true',
                        help="Write the native body's 8-index stride-44 stream (all influences honoured; needs the matching probe build)")
    parser.add_argument('--pin-armholes',action='store_true',help='Vest carries no upper-arm weight; arm skin blends from the armhole seam')
    parser.add_argument('--armhole-share',type=float,metavar='K',help='Scale upper-arm weight to K at the armhole seam, back to 1 along the arm (0<K<1)')
    a=parser.parse_args()
    doc=json.loads(a.gltf.read_text());targets=json.loads(a.bindings.read_text());lookup={x['name']:i for i,x in enumerate(targets)}
    if len(targets)!=98 or len(lookup)!=98:raise ValueError('Expected captured default-player binding table')
    palettes=bind.load_pose_palettes(a.two_influence_poses,targets) if a.two_influence_poses else None
    ring=None;pin_nodes=set()
    if a.pin_armholes or a.armhole_share is not None:
        if not a.bind_fit:raise ValueError('armhole options require --bind-fit')
        if a.armhole_share is not None and not 0<a.armhole_share<1:raise ValueError('--armhole-share must be between 0 and 1')
        # Armhole seam = positions shared by the vest and the skin primitive of the same node.
        for node in doc['nodes']:
            if 'mesh' not in node:continue
            prims=doc['meshes'][node['mesh']]['primitives']
            if not any(doc['materials'][p['material']].get('name')=='white vest' for p in prims):continue
            skin=doc['skins'][node['skin']];names=[doc['nodes'][i]['name'] for i in skin['joints']]
            inverse=bind.accessor(doc,a.gltf.parent,skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            _,fit,_,_=bind.fit_skin(names,inverse,targets);fitted={}
            for p in prims:
                at=p['attributes'];acc=lambda k:bind.accessor(doc,a.gltf.parent,at[k])
                fitted[doc['materials'][p['material']].get('name')]=bind.deform(acc('POSITION'),acc('NORMAL'),acc('JOINTS_0'),acc('WEIGHTS_0').astype(float),fit)[0]
            vest=fitted.pop('white vest');other=np.vstack(list(fitted.values()))
            from scipy.spatial import cKDTree
            distance,_=cKDTree(vest).query(other);shared=other[distance<1e-6]
            ring=shared[abs(shared[:,0])>.12];pin_nodes.add(node['name']);seam_all=shared
        if ring is None or len(ring)<8:raise ValueError('No vest armhole seam found')
    report={'status':'detached upload payloads only; animation and materials not installed','bone_count':98,
            'bind_fit_applied':a.bind_fit,'primitives':[]}
    prepared=[];scale=None
    for node in doc['nodes']:
        if 'mesh' not in node:continue
        skin=doc['skins'][node['skin']];names=[doc['nodes'][i]['name'] for i in skin['joints']]
        if set(names)-bind.MAP.keys():raise ValueError('Unmapped source joints')
        mapping=[lookup[bind.MAP[x]] for x in names]
        inverse=bind.accessor(doc,a.gltf.parent,skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
        _,fit,_,skin_scale=bind.fit_skin(names,inverse,targets)
        if scale is None:scale=skin_scale
        hip=np.asarray(targets[lookup['hips']]['world']).reshape(4,4).T[:3,3]
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            attrs=primitive['attributes'];position=bind.accessor(doc,a.gltf.parent,attrs['POSITION'])@bind.C.T*scale+hip
            normal=bind.accessor(doc,a.gltf.parent,attrs['NORMAL'])@bind.C.T;normal/=np.linalg.norm(normal,axis=1)[:,None]
            uv=bind.accessor(doc,a.gltf.parent,attrs['TEXCOORD_0']);joints=bind.accessor(doc,a.gltf.parent,attrs['JOINTS_0'])
            influence=bind.accessor(doc,a.gltf.parent,attrs['WEIGHTS_0']);indices=bind.accessor(doc,a.gltf.parent,primitive['indices']).ravel()
            if joints.shape!=influence.shape or joints.shape[1]!=4 or len(joints)!=len(position) or joints.max()>=len(mapping):
                raise ValueError('Invalid source influence arrays')
            if len(indices)%3 or indices.max()>=len(position):raise ValueError('Invalid triangle indices')
            if a.bind_fit:
                position,normal=bind.deform(bind.accessor(doc,a.gltf.parent,attrs['POSITION']),
                    bind.accessor(doc,a.gltf.parent,attrs['NORMAL']),joints,influence,fit)
            native_joints=np.asarray(mapping)[joints];native_weights=influence
            neck_fit={'adjusted_vertices':0,'maximum_discarded_weight':0}
            chest_fit={'adjusted_vertices':0,'maximum_discarded_weight':0}
            shoulder_fit={'adjusted_vertices':0,'maximum_discarded_weight':0}
            clearance=0.
            if a.bind_fit:
                native_joints,native_weights,chest_fit=bind.fit_spine_chain_weights(position,native_joints,native_weights,targets)
                native_joints,native_weights,shoulder_fit=bind.fit_shoulder_weights(position,native_joints,native_weights,targets)
                native_joints,native_weights,neck_fit=bind.fit_neck_weights(position,native_joints,native_weights,targets)
                # Vest clearance retired: it lifted the vest off its stitched skin seams.
                if node['name'] in pin_nodes and a.armhole_share is not None:
                    native_joints,native_weights,pin=bind.soften_garment_armholes(position,np.asarray(native_joints),native_weights,targets,ring,a.armhole_share)
                    report.setdefault('armhole_soften',[]).append(pin)
                elif node['name'] in pin_nodes:
                    native_joints,native_weights,pin=bind.pin_garment_armholes(position,np.asarray(native_joints),native_weights,targets,ring,
                        doc['materials'][primitive['material']].get('name')=='white vest',shared=seam_all)
                    report.setdefault('armhole_pin',[]).append(pin)
            if a.deform_helpers:
                native_joints,helper_info=bind.remap_deform_helpers(np.asarray(native_joints),targets)
                report.setdefault('deform_helper_remaps',0);report['deform_helper_remaps']+=helper_info['remapped_influences']
            two_fit=None
            if palettes is not None:
                native_joints,native_weights,two_fit=bind.reduce_to_two_influences(position,np.asarray(native_joints),np.asarray(native_weights),palettes)
            native_uv=uv.copy();native_uv[:,1]=1-native_uv[:,1]
            tangent,degenerate=tangents(position,normal,native_uv,indices);data=bytearray()
            for p,n,t,u,j,w in zip(position,normal,tangent,uv,native_joints,native_weights):
                if not np.isfinite(p).all() or not np.isfinite(u).all():raise ValueError('Nonfinite vertex')
                head=struct.pack('<3fII',*p,pack_direction(n),pack_direction(t[0],t[1]))
                if a.eight_bone_stream:
                    bone,weight=weights.encode_eight([int(x) for x in j],[float(x) for x in w],98)
                else:
                    bone,weight=weights.encode_four([int(x) for x in j],[float(x) for x in w],98)
                data+=head+bone+weight+pack_uv(u)
            if a.eight_bone_stream:
                # Native body layout (captured mesh 46): 8 indices, 7 explicit weights, UV at 0x24.
                layout=[bytes.fromhex(x) for x in ('00020300','0c060304','10060401','14070411','18070413','1c040410','20040412','24030406')]
                va=stream.encode_stream(len(position),44,layout,bytes(data))
            else:
                layout=[bytes.fromhex(x) for x in ('00020300','0c060304','10060401','14070411','18040410','1c030406')]
                va=stream.encode_stream(len(position),36,layout,bytes(data))
            stream.decode_stream(va)
            width=2 if len(position)<=65536 else 4;ib=indices.astype('<u2' if width==2 else '<u4').tobytes()
            ia=stream.encode_indices(len(position),width,ib);stream.decode_indices(ia,len(position))
            i=len(prepared);prepared.append((va,ia));report['primitives'].append({'vertex_file':f'primitive-{i}.va','index_file':f'primitive-{i}.ia',
                'vertices':len(position),'triangles':len(indices)//3,'material':primitive['material'],'degenerate_uv_triangles':degenerate,
                'native_bounds':[position.min(axis=0).tolist(),position.max(axis=0).tolist()],'neck_fit':neck_fit,'chest_fit':chest_fit,'shoulder_fit':shoulder_fit,'garment_clearance_metres':clearance,'two_influence_fit':two_fit})
    report['scale']=scale;report['limitations']=[('Offline bind fit used; runtime animation remains unvalidated' if a.bind_fit else
        'Source pose retained; native animation bind correction pending'),
        'Packed normals inferred from captured distributions; shader/render check pending','No drawable attachment or material registration']
    textures,material_textures,metadata=color_payloads(doc,a.gltf.parent,[p['material'] for p in report['primitives']])
    report['textures']=metadata
    for primitive in report['primitives']:
        primitive['texture_file']=metadata[material_textures[primitive['material']]]['file']
    a.output.mkdir(parents=True,exist_ok=False)
    for i,(va,ia) in enumerate(prepared):(a.output/f'primitive-{i}.va').write_bytes(va);(a.output/f'primitive-{i}.ia').write_bytes(ia)
    for i,data in enumerate(textures):(a.output/f'texture-{i}.rgba').write_bytes(data)
    (a.output/'model.kzmd').write_bytes(model_manifest(prepared,textures,report['primitives'],material_textures,rig_profile(targets),a.bind_fit))
    (a.output/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps({'output':str(a.output),'primitives':len(prepared)}))


if __name__=='__main__':main()
