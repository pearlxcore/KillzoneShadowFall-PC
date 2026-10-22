#!/usr/bin/env python3
"""Prepare a textured CJ glTF for offline validation, from local PS5 assets.

Requires extracted Gameface/Content and the GTA Trilogy UModel build. This
does not install CJ in Killzone: native mesh upload/retargeting is still needed.
Game assets and generated files belong outside source control (normally out/).
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
from cj_texture import decode_texture


def run_umodel(executable, content, output, package, command):
    log=subprocess.run([str(executable),command,'-game=ue4.26','-noanim','-notex','-gltf',
                        f'-path={content}',f'-out={output/"exports"}',package],
                       cwd=output,capture_output=True,text=True,errors='replace')
    text=log.stdout+log.stderr
    (output/f'{package}-{command.lstrip("-")}.log').write_text(text,encoding='utf-8')
    if log.returncode: raise RuntimeError(f'UModel failed for {package}; see output logs')
    return text


def material(texture, name):
    return {'name':name,'pbrMetallicRoughness':{'baseColorTexture':{'index':texture},
                                             'metallicFactor':0,'roughnessFactor':.8}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content',required=True,type=Path)
    parser.add_argument('--umodel',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path,help='New output directory')
    args=parser.parse_args()
    content=args.content.resolve();exe=args.umodel.resolve();output=args.output.resolve()
    if not content.is_dir() or not exe.is_file():parser.error('Content folder or UModel executable is missing')
    output.mkdir(parents=True,exist_ok=False)
    player=content/'SanAndreas/Characters/Player'
    for package in ('SK_Player_Combined','SK_vest'):
        run_umodel(exe,content,output,package,'-export')
    paths={}
    for label,package,texture in (
        ('face','T_player_face_BC',None),('torso','T_player_torso_BC',None),
        ('jeans','MI_jeansdenim','T_jeansdenim_legs_BC'),
        ('shoes','MI_sneakerbincblk','T_sneakerbincblk_sneakerbincblk_BC'),
        ('eyes','MI_eyes','T_BmostEye_BC'),('vest','MI_vest','T_vest_vest_BC')):
        asset=player/(package+'.uasset') if texture else content/'SanAndreas/Textures/player'/(package+'.uasset')
        offset=size=None
        if texture:
            listing=run_umodel(exe,content,output,package,'-list')
            match=re.search(r'^\s*\d+\s+([\dA-Fa-f]+)\s+([\dA-Fa-f]+)\s+Texture2D\s+'+re.escape(texture)+r'\s*$',listing,re.M)
            if not match:raise ValueError(f'Missing texture export: {texture}')
            offset,size=(int(value,16) for value in match.groups())
        dds=output/(label+'.dds');dds.write_bytes(decode_texture(asset,offset,size))
        # PNG is a standard glTF image; Pillow is only an offline dependency.
        from PIL import Image
        with Image.open(dds) as image:image.save(output/(label+'.png'))
        paths[label]=str(asset)
    export=output/'exports/SanAndreas/Characters/Player'
    body=json.loads((export/'SK_Player_Combined.gltf').read_text())
    vest=json.loads((export/'SK_vest.gltf').read_text())
    body_names=[body['nodes'][i]['name'] for i in body['skins'][0]['joints']]
    vest_names=[vest['nodes'][i]['name'] for i in vest['skins'][0]['joints']]
    if body_names!=vest_names or len(body_names)!=61:
        raise ValueError('CJ skeleton layout differs from the inspected build')
    for doc,name in ((body,'SK_Player_Combined'),(vest,'SK_vest')):
        for buffer in doc['buffers']:
            buffer['uri']=(Path('exports/SanAndreas/Characters/Player')/buffer['uri']).as_posix()
            if (output/buffer['uri']).stat().st_size!=buffer['byteLength']:
                raise ValueError(f'Truncated mesh buffer: {name}')
    labels=('shoes','jeans','face','torso','eyes','vest')
    body['images']=[{'uri':label+'.png'} for label in labels]
    body['textures']=[{'source':i} for i in range(len(labels))]
    body['materials']=[material(tex,name) for tex,name in
                       ((0,'shoes'),(1,'jeans'),(2,'face'),(3,'hands'),(3,'unused'),(3,'torso'),(4,'eyes'),(5,'white vest'))]
    # Replace the bare torso with the supplied vest + exposed arms.
    # The clear eye shell needs a separate shader; leave the actual eyes visible.
    body['meshes'][0]['primitives']=[p for p in body['meshes'][0]['primitives'] if p['material'] not in (4,5)]
    buffer_base=len(body['buffers']);view_base=len(body['bufferViews']);accessor_base=len(body['accessors'])
    node_base=len(body['nodes']);mesh_base=len(body['meshes']);skin_base=len(body['skins'])
    body['buffers']+=vest['buffers']
    for view in vest['bufferViews']:
        view['buffer']+=buffer_base;body['bufferViews'].append(view)
    for accessor in vest['accessors']:
        accessor['bufferView']+=view_base;body['accessors'].append(accessor)
    for mesh in vest['meshes']:
        for prim in mesh['primitives']:
            prim['attributes']={key:value+accessor_base for key,value in prim['attributes'].items()}
            prim['indices']+=accessor_base
            prim['material']=7 if prim['material']==0 else 3
        body['meshes'].append(mesh)
    # Preserve each mesh's inverse binds: the two rest poses differ slightly.
    for node in vest['nodes']:
        if 'children' in node:node['children']=[i+node_base for i in node['children']]
        if 'mesh' in node:node['mesh']+=mesh_base
        if 'skin' in node:node['skin']+=skin_base
        body['nodes'].append(node)
    for skin in vest['skins']:
        skin['joints']=[i+node_base for i in skin['joints']]
        skin['inverseBindMatrices']+=accessor_base
        if 'skeleton' in skin:skin['skeleton']+=node_base
        body['skins'].append(skin)
    body['scenes'][body.get('scene',0)]['nodes'] += [i+node_base for i in vest['scenes'][vest.get('scene',0)]['nodes']]
    # GTA's per-vertex channels are shader masks, not glTF color multipliers.
    for mesh in body['meshes']:
        for primitive in mesh['primitives']:primitive['attributes'].pop('COLOR_0',None)
    (output/'CJ.gltf').write_text(json.dumps(body,indent=2),encoding='utf-8')
    report={'status':'offline assets prepared; not installed in game','joints_per_skin':61,
            'skins':len(body['skins']),'sources':paths,
            'limitations':['No native Killzone mesh loader or animation mapping yet',
                           'Base-color materials only; clear eye shell omitted',
                           'Normal body shape; clothing morphs and facial animation not converted']}
    (output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'Prepared offline model: {output/"CJ.gltf"}')


if __name__=='__main__':main()
