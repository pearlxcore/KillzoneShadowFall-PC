"""Map captured Killzone frame evidence without assuming GOW3's camera or post shaders.

Usage: python tools/kzsf_frame_map.py <frame.txt> [...]
Prints JSON; addresses are capture-local evidence, never runtime patch constants.
"""
import json
import math
from pathlib import Path
import re
import sys

from gow3_frame_map import buffers, depth, passes, targets


def multiply(a,b):
    # Captured 4x4 matrices are column-major.
    return [sum(a[k*4+r]*b[c*4+k] for k in range(4))
            for c in range(4) for r in range(4)]


def camera_candidate(values,float_digits=6):
    if len(values)<160 or not all(math.isfinite(x) for x in values[:48]):
        return None
    view,projection,inverse=values[:16],values[16:32],values[32:48]
    if any(abs(view[i]-expected)>1e-5 for i,expected in ((3,0),(7,0),(11,0),(15,1))):
        return None
    residual=max(abs(v-(1.0 if i%5==0 else 0.0))
                 for i,v in enumerate(multiply(view,inverse)))
    if residual>0.02: return None
    # The observed perspective has w=-view_z; verify rather than reusing GOW3's +z.
    if (projection[0]<=0 or projection[5]<=0 or abs(projection[11]+1)>1e-5
            or abs(projection[15])>1e-5 or projection[14]>=0):
        return None
    width,height=values[114:116]
    if width<64 or height<64 or abs(projection[0]/projection[5]-height/width)>1e-4:
        return None
    depth_scale=-projection[10]
    near=-projection[14]/depth_scale
    far=None
    if float_digits>=9 and depth_scale>1:
        far=-projection[14]/(depth_scale-1)
    return {'view_float_offset':0,'projection_float_offset':16,'inverse_view_float_offset':32,
            'viewport_float_offset':114,'viewport':[width,height],
            'view_inverse_max_residual':residual,'clip_w':'-view_z',
            'projection_xy':[projection[0],projection[5]],'depth_scale':depth_scale,
            'depth_offset':projection[14],'near_estimate':near,
            'far_estimate':far,
            'far_status':('estimate from float projection' if far else
                          'unresolved finite/infinite or insufficient precision'),
            'vertical_fov_degrees':math.degrees(2*math.atan(1/projection[5])),
            'camera_world_position':inverse[12:15],
            'view':view,'inverse_view':inverse,'projection':projection}


def analyze(path):
    text=Path(path).read_text(encoding='utf-8',errors='replace')
    precision=re.search(r'^capture schema \d+ float_digits (\d+)$',text,re.M)
    float_digits=int(precision[1]) if precision else 6
    ps=passes(text)
    gbuffer=[p for p in ps if p[1]=='PASS' and len(targets(p[3],'color'))>=5 and depth(p[3])]
    if not gbuffer: raise ValueError('No multi-target scene pass found; capture gameplay.')
    main=max(gbuffer,key=lambda p:p[2])
    scene_depth=depth(main[3])
    cameras=[]
    for pass_index,_,_,body in ps:
      # The first four captured G-buffer draws can all be weapon draws. Include
      # scene-depth and lighting consumers instead of silently losing the world camera.
      if depth(body)!=scene_depth and not any(t[0]==scene_depth[0] for t in targets(body,'samples')):
        continue
      for stage,slot,size,floats in buffers(body):
        if size==640 and (candidate:=camera_candidate(floats,float_digits)):
            # Keep distinct stage/slot bindings; common contents corroborate global camera data.
            match=next((c for c in cameras if c['shader']==stage and c['slot']==slot
                        and c['projection']==candidate['projection']),None)
            if match is None:
                cameras.append(dict(shader=stage,slot=slot,bytes=size,passes=[pass_index],**candidate))
            elif pass_index not in match['passes']:
                match['passes'].append(pass_index)
    hdr_targets=[t for t in targets(main[3],'color') if t[1]=='R16G16B16A16Sfloat']
    transitions=[]
    for index,kind,draws,body in ps:
        sampled=targets(body,'samples'); colors=targets(body,'color')
        if (index>main[0] and kind=='PASS' and draws==1 and not depth(body)
                and any(t[0]==h[0] for t in sampled for h in hdr_targets)
                and any(t[1]=='B10G11R11UfloatPack32' and t[2:]==scene_depth[2:] for t in colors)):
            transitions.append({'pass':index,'header':body.splitlines()[0],
                                'samples':sampled,'outputs':colors,
                                'role':'HDR format-transition candidate; not proven final tone-map/UI boundary'})
    projection_groups=[]
    for camera in cameras:
        # Distinct projections can share the same physical camera. Do not select the
        # first camera buffer and silently treat weapon depth as world depth.
        signature=(camera['projection_xy'],camera['depth_scale'],camera['depth_offset'])
        group=next((g for g in projection_groups if g['signature']==signature),None)
        if group is None:
            group={'signature':signature,'bindings':[],'camera':camera}
            projection_groups.append(group)
        group['bindings'].append({'shader':camera['shader'],'slot':camera['slot']})
    return {'capture':str(Path(path).resolve()),'float_digits':float_digits,'passes':len(ps),
            'gbuffer_pass':main[0],'gbuffer_draws':main[2],'scene_depth':scene_depth,
            'scene_depth_other_passes':[p[0] for p in ps if depth(p[3])==scene_depth],
            'camera_candidates':cameras,'projection_groups':projection_groups,
            'hdr_transition_candidates':transitions,
            'gow3_compute_trigger_present':any(p[1]=='COMPUTE' and
                '000000009a9cf8a9' in p[3].splitlines()[0] for p in ps),
            'limitations':['Capture-local addresses must not become hardcoded pointers.',
                'Text matrices need full-precision validation and camera-motion tests.',
                'FSR needs validated depth/motion, scene insertion and consistent jitter; no runtime changes here.']}


if __name__=='__main__':
    print(json.dumps([analyze(p) for p in sys.argv[1:]],indent=2,allow_nan=False))
