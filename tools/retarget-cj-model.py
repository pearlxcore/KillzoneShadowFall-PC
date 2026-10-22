#!/usr/bin/env python3
"""Offline CJ bind-pose fit to a captured native player rig. No game writes.

Produces a reviewable glTF, not an installable native model. Facial joints are
rigid to the head; simplified finger chains use one native finger chain. These
are explicit initial policies, not verified gameplay retargeting.
"""
import argparse
import json
from pathlib import Path
import numpy as np

MAP={'Normal':'Root','Pelvis':'hips','Spine_1':'spine1','Belly':'spine2',
     'Spine_2':'spine4','Neck':'neckRoot','Head':'neckRoot','HeadNub':'neckRoot'}
# Keep CJ's chest at its anatomical rest height while driving it with the
# native upper chest joint. Using spine4's pivot for both raised the shoulders;
# driving the upper back with spine3 instead misses the upper chest's aim bend.
FIT_ANCHORS={'Spine_2':'spine3'}
for side,word in (('L','left'),('R','right')):
    for source,target in (('Thigh','upperLeg'),('Calf','lowerLeg'),('Foot','foot'),('Toe0','toe'),('Toe0Nub','toe'),
                          ('UpperArm','upperArm'),('ForeArm','lowerArm'),('Hand','wrist'),
                          ('Finger','middleOne'),('Finger01','middleTwo'),('Finger0Nub','middleThree')):
        MAP[f'{side}_{source}']=f'{target}_{word}'
    MAP[f'Bip01_{side}_Clavicle']=f'shoulder_{word}'
    MAP[f'{side}_breast']=f'upperArm_{word}'
    MAP[f'{side}Thumb1']=f'thumbOne_{word}';MAP[f'{side}Thumb2']=f'thumbTwo_{word}'
FACE={'leye','reye','rbrow2','rbrow1','rlid','llid','lbrow2','lbrow1','lcheek','rcheek',
      'rtlip1','rtlip2','rtlip3','ltlip1','ltlip2','ltlip3','lcorner','rcorner','jaw1','jaw22','llip11','jaw2','llip1'}
# Keep the head's rest pivot, but use the lower-neck animation joint for the
# complete rigid head group. The native upper-head aim delta stretches CJ's
# short neck transition; moving only its vertices cannot repair that motion.
MAP.update({name:'neckRoot' for name in FACE})
FIT_ANCHORS.update({name:'neck' for name in FACE|{'Head','HeadNub'}})
# CJ: forward X, up Y, right Z. Native: right X, forward Y, up Z.
C=np.array([[0,0,1],[1,0,0],[0,1,0]],dtype=float)
# glTF: right X, up Y, backward Z.
G=np.array([[1,0,0],[0,0,1],[0,-1,0]],dtype=float)


def align(a,b):
    a=np.asarray(a,dtype=float);b=np.asarray(b,dtype=float)
    if not np.isfinite(a).all() or not np.isfinite(b).all():raise ValueError('Nonfinite bone direction')
    if min(np.linalg.norm(a),np.linalg.norm(b))<1e-8:raise ValueError('Zero bone direction')
    a=a/np.linalg.norm(a);b=b/np.linalg.norm(b);v=np.cross(a,b);dot=float(np.clip(a@b,-1,1))
    if dot>1-1e-10:return np.eye(3)
    if dot<-1+1e-10:
        axis=np.eye(3)[np.argmin(abs(a))];v=np.cross(a,axis);v/=np.linalg.norm(v)
        return 2*np.outer(v,v)-np.eye(3)
    k=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]])
    return np.eye(3)+k+k@k/(1+dot)


def accessor(doc,folder,index):
    a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
    if a.get('sparse'):raise ValueError('Unsupported sparse accessor')
    dtype={5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
    width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    path=(folder/doc['buffers'][v['buffer']]['uri']).resolve()
    if not path.is_relative_to(folder.resolve()):raise ValueError('Buffer outside source folder')
    raw=path.read_bytes();item=np.dtype(dtype).itemsize;stride=v.get('byteStride',item*width)
    offset=v.get('byteOffset',0)+a.get('byteOffset',0);count=a['count']
    if not 0<count<=1000000 or stride<item*width or offset+(count-1)*stride+item*width>len(raw):
        raise ValueError('Accessor exceeds buffer')
    result=np.ndarray((count,width),dtype=dtype,buffer=raw,offset=offset,strides=(stride,item)).copy()
    if a.get('normalized'):
        if a['componentType'] not in (5121,5123):raise ValueError('Unsupported normalized accessor')
        result=result.astype(float)/(255 if a['componentType']==5121 else 65535)
    if not np.isfinite(result).all():raise ValueError('Nonfinite accessor')
    return result


def fit_skin(names,inverse,targets):
    if set(names)-MAP.keys():raise ValueError('Unmapped CJ joints')
    lookup={x['name']:i for i,x in enumerate(targets)}
    mapped=np.array([lookup[MAP[n]] for n in names]);source=np.linalg.inv(inverse)
    target=np.array([np.asarray(x['world']).reshape(4,4).T for x in targets])
    if not np.isfinite(target).all():raise ValueError('Nonfinite native bind pose')
    # Uniform scale from the hip-to-ankle distance, shared by both clothing skins.
    hip=names.index('Pelvis');ankle=names.index('L_Foot')
    scale=np.linalg.norm(target[lookup['hips'],:3,3]-target[lookup['foot_left'],:3,3])/np.linalg.norm(source[hip,:3,3]-source[ankle,:3,3])
    if not .5<scale<2:raise ValueError('Implausible bind scale')
    child={}
    for side in ('L','R'):
        for a,b in (('Thigh','Calf'),('Calf','Foot'),('Foot','Toe0'),('UpperArm','ForeArm'),('ForeArm','Hand'),('Hand','Finger'),('Finger','Finger01')):
            child[f'{side}_{a}']=f'{side}_{b}'
        child[f'Bip01_{side}_Clavicle']=f'{side}_UpperArm'
    for side in ('L','R'):child[f'{side}Thumb1']=f'{side}Thumb2'
    inherit={f'{side}_breast':f'{side}_UpperArm' for side in ('L','R')}
    for side in ('L','R'):
        inherit.update({f'{side}Thumb2':f'{side}Thumb1',f'{side}_Finger01':f'{side}_Finger',
                        f'{side}_Finger0Nub':f'{side}_Finger',f'{side}_Toe0Nub':f'{side}_Foot'})
    transforms=[]
    for i,name in enumerate(names):
        if name in inherit:
            transforms.append(transforms[names.index(inherit[name])].copy());continue
        rotation=np.eye(3)
        if name in child:
            j=names.index(child[name]);src=C@(source[j,:3,3]-source[i,:3,3]);dst=target[mapped[j],:3,3]-target[mapped[i],:3,3]
            if np.linalg.norm(src)>1e-8 and np.linalg.norm(dst)>1e-8:rotation=align(src,dst)
        # Facial joints retain their placement relative to CJ's head rather
        # than each being collapsed onto the native neck/head pivot. The
        # camera joint sits above/forward of that pivot and is not a head bone.
        origin=names.index('Head') if name in FACE or name=='HeadNub' else i
        t=np.eye(4);t[:3,:3]=rotation@C*scale
        anchor=lookup[FIT_ANCHORS.get(name,MAP[name])]
        t[:3,3]=target[anchor,:3,3]-t[:3,:3]@source[origin,:3,3]
        transforms.append(t)
    return mapped,np.array(transforms),target,float(scale)


def deform(position,normal,joints,weights,transforms):
    if joints.shape!=weights.shape or position.shape!=normal.shape or len(joints)!=len(position):raise ValueError('Inconsistent vertex arrays')
    if joints.min()<0 or joints.max()>=len(transforms) or (weights<0).any():raise ValueError('Invalid skin influence')
    sums=weights.sum(axis=1)
    if not np.allclose(sums,1,atol=.005):raise ValueError('Unnormalized skin weights')
    weights=weights/sums[:,None];selected=transforms[joints.astype(int)]
    p=np.einsum('nkij,nj->nki',selected[:,:,:3,:3],position)+selected[:,:,:3,3]
    n=np.einsum('nkij,nj->nki',selected[:,:,:3,:3],normal)
    p=(p*weights[:,:,None]).sum(axis=1);n=(n*weights[:,:,None]).sum(axis=1)
    lengths=np.linalg.norm(n,axis=1)
    if (lengths<1e-8).any():raise ValueError('Collapsed normal')
    return p,n/lengths[:,None]


def garment_clearance(position,normal,material_name):
    """Small upper-vest clearance, feathered out above the waist (metres).

    RETIRED from the export pipeline (v19). CJ has no skin under the vest:
    the arm/neck skin is stitched to the vest along shared armhole/neckline
    vertices. Offsetting the vest lifted those seams (exposing culled vest
    backfaces along the straps) and opened 2.5-3.8 mm cracks at UV-split
    vertices whose normals differ by up to 98 degrees. Kept for tests only.
    """
    if material_name!='white vest':return position.copy(),0.
    if position.shape!=normal.shape or not np.isfinite(position).all() or not np.isfinite(normal).all():
        raise ValueError('Invalid garment geometry')
    if not np.allclose(np.linalg.norm(normal,axis=1),1,atol=.01):raise ValueError('Invalid garment normals')
    fade=np.clip((position[:,2]-1.35)/.20,0,1);fade=fade*fade*(3-2*fade)
    offset=.0025*fade
    return position+normal*offset[:,None],float(offset.max(initial=0))


def fit_chest_weights(position,joints,weights,targets):
    """Distribute CJ's single upper-torso influence along native spine/chest."""
    lookup={b['name']:i for i,b in enumerate(targets)}
    chain=[lookup[name] for name in ('spine3','spine4','chest')]
    heights=np.array([targets[i]['world'][14] for i in chain])
    if not np.isfinite(heights).all() or not (np.diff(heights)>0).all():
        raise ValueError('Invalid native chest chain')
    out_j=np.zeros((len(position),4),dtype=np.uint16);out_w=np.zeros((len(position),4));loss=0.;changed=0
    for i,p in enumerate(position):
        merged={};mass=0.
        for joint,weight in zip(joints[i],weights[i]):
            if int(joint)==chain[1]:mass+=float(weight)
            else:merged[int(joint)]=merged.get(int(joint),0)+float(weight)
        segment=int(np.clip(np.searchsorted(heights,p[2])-1,0,1))
        alpha=float(np.clip((p[2]-heights[segment])/(heights[segment+1]-heights[segment]),0,1))
        for joint,weight in ((chain[segment],mass*(1-alpha)),(chain[segment+1],mass*alpha)):
            merged[joint]=merged.get(joint,0)+weight
        changed+=mass>1e-6
        active=sorted(((j,w) for j,w in merged.items() if w>0),key=lambda pair:(-pair[1],pair[0]))
        loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
        if not np.isfinite(total) or total<=0:raise ValueError('Invalid chest weights')
        for k,(joint,weight) in enumerate(active):out_j[i,k]=joint;out_w[i,k]=weight/total
    if loss>.05:raise ValueError('Chest fit would discard too much skin weight')
    return out_j,out_w,{'adjusted_vertices':int(changed),'maximum_discarded_weight':loss}


SPINE_CHAIN=('spine1','spine2','spine3','spine4','chest')


def fit_spine_chain_weights(position,joints,weights,targets):
    """Spread all CJ spine mass linearly by height over the full native chain.

    The older chest fit skipped native spine2 and compressed the spine1 ->
    spine3 -> spine4 -> chest handovers into ~12 cm bands. In captured aim
    poses that concentrated the native spine bend on CJ's under-pectoral vest
    crease. The captured native body itself weights spine2 heavily around the
    belly (native-geometry-capture-28336 mesh 46). Rest geometry is unchanged.
    """
    lookup={b['name']:i for i,b in enumerate(targets)}
    chain=[lookup[name] for name in SPINE_CHAIN]
    heights=np.array([targets[i]['world'][14] for i in chain])
    if not np.isfinite(heights).all() or not (np.diff(heights)>0).all():
        raise ValueError('Invalid native spine chain')
    out_j=np.zeros((len(position),4),dtype=np.uint16);out_w=np.zeros((len(position),4));loss=0.;changed=0
    for i,p in enumerate(position):
        merged={};mass=0.
        for joint,weight in zip(joints[i],weights[i]):
            if int(joint) in chain:mass+=float(weight)
            else:merged[int(joint)]=merged.get(int(joint),0)+float(weight)
        if mass>1e-6:
            changed+=1
            segment=int(np.clip(np.searchsorted(heights,p[2])-1,0,len(chain)-2))
            alpha=float(np.clip((p[2]-heights[segment])/(heights[segment+1]-heights[segment]),0,1))
            for joint,weight in ((chain[segment],mass*(1-alpha)),(chain[segment+1],mass*alpha)):
                merged[joint]=merged.get(joint,0)+weight
        active=sorted(((j,w) for j,w in merged.items() if w>0),key=lambda pair:(-pair[1],pair[0]))
        loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
        if not np.isfinite(total) or total<=0:raise ValueError('Invalid spine weights')
        for k,(joint,weight) in enumerate(active):out_j[i,k]=joint;out_w[i,k]=weight/total
    if loss>.05:raise ValueError('Spine fit would discard too much skin weight')
    return out_j,out_w,{'adjusted_vertices':int(changed),'maximum_discarded_weight':loss}


# The captured native player body (mesh 46) never skins to these raw limb
# joints; it uses the co-located Helper1 deform joints. The two older pose
# captures show them identical, but a live capture (live-pose-5148-*) shows
# Helper1 7 degrees and forearm Roll 11 degrees away from the raw joints, so
# the runtime does drive them separately.
DEFORM_HELPERS={f'{limb}_{side}':f'{limb}_{side}_Helper1'
                for limb in ('upperArm','lowerArm','upperLeg') for side in ('left','right')}


def remap_deform_helpers(joints,targets):
    lookup={b['name']:i for i,b in enumerate(targets)}
    if any(name not in lookup for name in list(DEFORM_HELPERS)+list(DEFORM_HELPERS.values())):
        raise ValueError('Missing native deform helper')
    table=np.arange(len(targets))
    for raw,helper in DEFORM_HELPERS.items():table[lookup[raw]]=lookup[helper]
    out=table[joints.astype(int)].astype(joints.dtype)
    return out,{'remapped_influences':int((out!=joints).sum())}


def load_pose_palettes(paths,targets):
    """Skinning palettes (world @ inverse bind) from captured native pose files.

    Accepts the single-pose format ({'bones':[...]}) and the multi-frame live
    capture format ({'frames':[{'bones':[...]}, ...]}); frames are subsampled.
    """
    inverse=np.array([np.asarray(t['inverse']).reshape(4,4).T for t in targets])
    names=[t['name'] for t in targets];palettes=[]
    for path in paths:
        doc=json.loads(Path(path).read_text())
        frames=[doc] if 'bones' in doc else doc['frames'][::max(1,len(doc['frames'])//3)]
        for frame in frames:
            world={b['name']:np.asarray(b['world']).reshape(4,4).T for b in frame['bones']}
            if set(names)-world.keys():raise ValueError(f'Pose {path} lacks native joints')
            palette=np.array([world[n]@inverse[i] for i,n in enumerate(names)])
            if not np.isfinite(palette).all():raise ValueError(f'Nonfinite pose {path}')
            palettes.append(palette)
    if not palettes:raise ValueError('No fit poses')
    return np.array(palettes)


def reduce_to_two_influences(position,joints,weights,palettes):
    """Best two-influence approximation of each vertex over sample poses.

    Historical two-influence workaround. The old diagnostic did not establish
    a stream limit: later live shader inspection found the cloned rigid
    material has no weight inputs at all. For each vertex with more, choose the bone pair and
    blend factor minimising the posed position error against the full blend.
    Deterministic per (position, weights), so stitched seam vertices stay equal.
    """
    point=np.c_[position,np.ones(len(position))];P=len(palettes)
    out_j=np.zeros((len(position),4),dtype=np.uint16);out_w=np.zeros((len(position),4))
    error=np.zeros(len(position));reduced=0
    for i in range(len(position)):
        active=sorted(((int(j),float(w)) for j,w in zip(joints[i],weights[i]) if w>1e-6),key=lambda q:(-q[1],q[0]))
        if not active:raise ValueError('Vertex without influence')
        if len(active)>2:
            reduced+=1
            moved={j:(palettes[:,j]@point[i])[:,:3] for j,_ in active}
            target=sum(w*moved[j] for j,w in active);best=None
            for x in range(len(active)):
                for y in range(x+1,len(active)):
                    a,b=active[x][0],active[y][0];d=moved[a]-moved[b];den=float((d*d).sum())
                    t=1. if den<1e-12 else float(np.clip(((target-moved[b])*d).sum()/den,0,1))
                    e=float(((t*moved[a]+(1-t)*moved[b]-target)**2).sum())
                    if best is None or e<best[0]-1e-15:best=(e,a,b,t)
            e,a,b,t=best;error[i]=np.sqrt(e/P)
            active=sorted(((a,t),(b,1-t)),key=lambda q:(-q[1],q[0]))
            active=[(j,w) for j,w in active if w>0]
        total=sum(w for _,w in active)
        for k,(j,w) in enumerate(active):out_j[i,k]=j;out_w[i,k]=w/total
    return out_j,out_w,{'reduced_vertices':reduced,'rms_error_mean_m':float(error[error>0].mean()) if reduced else 0.,
                        'rms_error_max_m':float(error.max(initial=0))}


def soften_garment_armholes(position,joints,weights,targets,ring,share=.5,span=.14):
    """Scale upper-arm weight near the armhole by k(p): ``share`` at/medial to the seam,
    rising to 1 over ``span`` metres outward along the upper arm.

    Middle ground between the original blend (vest rim lifted into white flaps)
    and pin_garment_armholes (arm skin took the whole bend and folded into a
    'wing'). k depends only on position, so the same rule on vest and skin keeps
    stitched seam vertices identical.
    """
    lookup={b['name']:i for i,b in enumerate(targets)};out_j=joints.astype(np.int64).copy();out_w=weights.copy();changed=0;loss=0.
    from scipy.spatial import cKDTree
    for side,sign in (('left',-1),('right',1)):
        shoulder,arm=lookup['shoulder_'+side],lookup['upperArm_'+side]
        pivot=np.array(targets[arm]['world']).reshape(4,4).T[:3,3]
        axis=np.array(targets[lookup['lowerArm_'+side]]['world']).reshape(4,4).T[:3,3]-pivot;axis/=np.linalg.norm(axis)
        side_ring=ring[np.sign(ring[:,0])==sign]
        if not len(side_ring):raise ValueError('Missing armhole seam')
        tree=cKDTree(side_ring);ring_axial=(side_ring-pivot)@axis
        for i,p in enumerate(position):
            merged={}
            for joint,weight in zip(out_j[i],out_w[i]):
                if weight>0:merged[int(joint)]=merged.get(int(joint),0)+float(weight)
            if merged.get(arm,0)<1e-6:continue
            _,k=tree.query(p);x=float(np.clip(((p-pivot)@axis-ring_axial[k])/span,0,1))
            scale=share+(1-share)*x*x*(3-2*x)
            if scale>=1-1e-9:continue
            move=merged[arm]*(1-scale);merged[arm]-=move;merged[shoulder]=merged.get(shoulder,0)+move;changed+=1
            active=sorted(((j,w) for j,w in merged.items() if w>1e-9),key=lambda q:(-q[1],q[0]))
            loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
            out_j[i]=0;out_w[i]=0
            for kk,(j,w) in enumerate(active):out_j[i,kk]=j;out_w[i,kk]=w/total
    if loss>.05:raise ValueError('Armhole softening would discard too much skin weight')
    return out_j.astype(joints.dtype),out_w,{'adjusted_vertices':changed,'maximum_discarded_weight':loss}


def pin_garment_armholes(position,joints,weights,targets,ring,garment,blend=.14,shared=None):
    """Keep the sleeveless vest off the upper arm; start the arm-skin blend at the seam.

    In game, upper-arm weight on the vest's armhole rim lifted it into white
    flaps when the arm rose. The vest now carries no upperArm weight (that
    mass moves to the clavicle). Arm skin re-blends clavicle -> upper arm by
    axial distance from the nearest armhole seam point, so stitched seam
    vertices (distance 0) get exactly the vest's weights. ``ring`` holds the
    shared vest/skin armhole positions; ``garment`` selects the vest rule.
    """
    lookup={b['name']:i for i,b in enumerate(targets)};out_j=joints.astype(np.int64).copy();out_w=weights.copy();changed=0;loss=0.
    from scipy.spatial import cKDTree
    # Skin vertices stitched to the vest (any shared position) must follow the vest rule.
    stitched=np.zeros(len(position),bool)
    if shared is not None and len(shared):stitched=cKDTree(shared).query(position)[0]<1e-6
    for side,sign in (('left',-1),('right',1)):
        shoulder,arm=lookup['shoulder_'+side],lookup['upperArm_'+side]
        pivot=np.array(targets[arm]['world']).reshape(4,4).T[:3,3]
        axis=np.array(targets[lookup['lowerArm_'+side]]['world']).reshape(4,4).T[:3,3]-pivot;axis/=np.linalg.norm(axis)
        side_ring=ring[np.sign(ring[:,0])==sign]
        if not len(side_ring):raise ValueError('Missing armhole seam')
        tree=cKDTree(side_ring);ring_axial=(side_ring-pivot)@axis
        for i,p in enumerate(position):
            merged={}
            for joint,weight in zip(out_j[i],out_w[i]):
                if weight>0:merged[int(joint)]=merged.get(int(joint),0)+float(weight)
            mass=merged.get(shoulder,0)+merged.get(arm,0)
            if mass<1e-6:continue
            if garment or stitched[i]:alpha=0.
            else:
                if np.sign(p[0])!=sign:continue
                _,k=tree.query(p);x=float(np.clip(((p-pivot)@axis-ring_axial[k])/blend,0,1));alpha=x*x*(3-2*x)
            merged[shoulder]=mass*(1-alpha);merged[arm]=mass*alpha;changed+=1
            active=sorted(((j,w) for j,w in merged.items() if w>1e-9),key=lambda q:(-q[1],q[0]))
            loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
            out_j[i]=0;out_w[i]=0
            for k,(j,w) in enumerate(active):out_j[i,k]=j;out_w[i,k]=w/total
    if loss>.05:raise ValueError('Armhole pin would discard too much skin weight')
    return out_j.astype(joints.dtype),out_w,{'adjusted_vertices':changed,'maximum_discarded_weight':loss}


def fit_shoulder_weights(position,joints,weights,targets):
    """Replace CJ helper-bone discontinuities with a smooth shoulder blend."""
    lookup={b['name']:i for i,b in enumerate(targets)}
    result_j=joints.copy();result_w=weights.copy();loss=0.;changed=0
    for side in ('left','right'):
        shoulder=lookup['shoulder_'+side];arm=lookup['upperArm_'+side]
        pivot=np.array(targets[arm]['world']).reshape(4,4).T[:3,3]
        elbow=np.array(targets[lookup['lowerArm_'+side]]['world']).reshape(4,4).T[:3,3]
        axis=elbow-pivot;length=np.linalg.norm(axis)
        if not np.isfinite(length) or length<.1:raise ValueError('Invalid shoulder axis')
        axis/=length
        fraction=np.clip(((position-pivot)@axis+.08)/.24,0,1)
        fraction=fraction*fraction*(3-2*fraction)
        for i,alpha in enumerate(fraction):
            merged={};mass=0.
            for joint,weight in zip(result_j[i],result_w[i]):
                if int(joint) in (shoulder,arm):mass+=float(weight)
                else:merged[int(joint)]=merged.get(int(joint),0)+float(weight)
            if mass<1e-6:continue
            changed+=1
            merged[shoulder]=mass*(1-float(alpha));merged[arm]=mass*float(alpha)
            active=sorted(((j,w) for j,w in merged.items() if w>0),key=lambda pair:(-pair[1],pair[0]))
            loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
            result_j[i]=0;result_w[i]=0
            for k,(joint,weight) in enumerate(active):result_j[i,k]=joint;result_w[i,k]=weight/total
    if loss>.05:raise ValueError('Shoulder fit would discard too much skin weight')
    return result_j,result_w,{'adjusted_vertices':changed,'maximum_discarded_weight':loss}


def fit_neck_weights(position,joints,weights,targets):
    """Blend the central lower neck toward neckRoot, keeping head weights.

    CJ's single chest joint cannot reproduce the native multi-joint neck bend.
    Only a bounded anatomical region is adjusted; rest geometry is unchanged.
    """
    if joints.shape!=weights.shape or len(position)!=len(joints) or joints.ndim!=2:
        raise ValueError('Inconsistent neck skin arrays')
    if (not np.isfinite(position).all() or not np.isfinite(weights).all() or
            (weights<0).any() or not np.allclose(weights.sum(axis=1),1,atol=.005) or
            joints.min()<0 or joints.max()>=len(targets)):
        raise ValueError('Invalid neck skin input')
    lookup={b['name']:i for i,b in enumerate(targets)};head=lookup['neck'];neck_root=lookup['neckRoot']
    root=np.array(targets[neck_root]['world']).reshape(4,4).T[:3,3]
    def smooth(value):
        value=np.clip(value,0,1);return value*value*(3-2*value)
    # The rear neck needs a lower transition than the front. Applying that
    # same low boundary to the front pulled the sternum and shirt neckline
    # toward the head. Preserve the upper chest at and below the front base.
    rear=smooth((root[1]-position[:,1])/.06)
    lower=root[2]-.025-.095*rear
    neck_mass=(weights*((joints==head)|(joints==lookup['neckRoot']))).sum(axis=1)
    blend=(smooth((position[:,2]-lower)/(root[2]+.015-lower))*
           (1-smooth((abs(position[:,0]-root[0])-.08)/.10))*
           (1-smooth((abs(position[:,1]-root[1])-.14)/.06)))
    out_j=np.zeros((len(position),4),dtype=np.uint16);out_w=np.zeros((len(position),4));loss=0.0
    for i,alpha in enumerate(blend):
        merged={};head_weight=0.0
        for joint,weight in zip(joints[i],weights[i]):
            joint=int(joint);weight=float(weight)
            if joint==head:head_weight+=weight
            merged[joint]=merged.get(joint,0)+weight*(1 if joint==head else 1-float(alpha))
        merged[neck_root]=merged.get(neck_root,0)+float(alpha)*(1-head_weight)
        active=sorted(((j,w) for j,w in merged.items() if w>0),key=lambda pair:(-pair[1],pair[0]))
        loss=max(loss,sum(w for _,w in active[4:]));active=active[:4];total=sum(w for _,w in active)
        for k,(joint,weight) in enumerate(active):out_j[i,k]=joint;out_w[i,k]=weight/total
    if loss>.05:raise ValueError('Neck fit would discard too much skin weight')
    changed=(blend>1e-6)&(neck_mass<1-1e-6)
    return out_j,out_w,{'adjusted_vertices':int(changed.sum()),'maximum_discarded_weight':loss}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--gltf',required=True,type=Path)
    p.add_argument('--bindings',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--deform-helpers',action='store_true',help='Skin limbs to native *_Helper1 joints like the native body (unverified trial)')
    a=p.parse_args()
    doc=json.loads(a.gltf.read_text());targets=json.loads(a.bindings.read_text())
    if not 78<=len(targets)<=320 or len({x['name'] for x in targets})!=len(targets):raise ValueError('Invalid native bindings')
    out={'asset':{'version':'2.0','generator':'CJ offline bind fit'},'buffers':[], 'bufferViews':[], 'accessors':[],
         'nodes':[],'meshes':[],'skins':[],'scenes':[{'nodes':[]}],'scene':0}
    data=bytearray();report=[]
    def add(array,kind,component):
        while len(data)%4:data.append(0)
        offset=len(data);raw=np.ascontiguousarray(array).tobytes();data.extend(raw)
        view=len(out['bufferViews']);out['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':len(raw)})
        index=len(out['accessors']);out['accessors'].append({'bufferView':view,'componentType':component,'count':len(array),'type':kind});return index
    # Flat joint nodes carry global bind matrices; animation validation is
    # separate. This preview intentionally contains no generated animation.
    target=np.array([np.asarray(x['world']).reshape(4,4).T for x in targets]);g=np.eye(4);g[:3,:3]=G
    target_g=g@target@np.linalg.inv(g)
    for x,m in zip(targets,target_g):out['nodes'].append({'name':x['name'],'matrix':m.T.flatten().tolist()})
    out['scenes'][0]['nodes']=list(range(len(targets)))
    ib=add(np.linalg.inv(target_g).transpose(0,2,1).reshape(-1,16).astype('<f4'),'MAT4',5126)
    out['skins']=[{'joints':list(range(len(targets))),'inverseBindMatrices':ib}]
    for node in doc['nodes']:
        if 'mesh' not in node:continue
        skin=doc['skins'][node['skin']];names=[doc['nodes'][i]['name'] for i in skin['joints']]
        inverse=accessor(doc,a.gltf.parent,skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
        mapped,transforms,_,scale=fit_skin(names,inverse,targets);primitives=[]
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            attrs=primitive['attributes'];pos=accessor(doc,a.gltf.parent,attrs['POSITION']);norm=accessor(doc,a.gltf.parent,attrs['NORMAL'])
            joints=accessor(doc,a.gltf.parent,attrs['JOINTS_0']);weights=accessor(doc,a.gltf.parent,attrs['WEIGHTS_0']).astype(float)
            fitted,normals=deform(pos,norm,joints,weights,transforms)
            native_joints,native_weights,chest_fit=fit_spine_chain_weights(fitted,mapped[joints],weights,targets)
            native_joints,native_weights,shoulder_fit=fit_shoulder_weights(fitted,native_joints,native_weights,targets)
            native_joints,native_weights,neck_fit=fit_neck_weights(fitted,native_joints,native_weights,targets)
            clearance=0.  # vest clearance retired; see garment_clearance
            if a.deform_helpers:native_joints,_=remap_deform_helpers(native_joints,targets)
            uv=accessor(doc,a.gltf.parent,attrs['TEXCOORD_0']);indices=accessor(doc,a.gltf.parent,primitive['indices'])
            if indices.max()>=len(pos):raise ValueError('Invalid mesh index')
            prim={'attributes':{'POSITION':add((fitted@G.T).astype('<f4'),'VEC3',5126),
                'NORMAL':add((normals@G.T).astype('<f4'),'VEC3',5126),'TEXCOORD_0':add(uv.astype('<f4'),'VEC2',5126),
                'JOINTS_0':add(native_joints.astype('<u2'),'VEC4',5123),'WEIGHTS_0':add(native_weights.astype('<f4'),'VEC4',5126)},
                'indices':add(indices.astype('<u4'),'SCALAR',5125),'material':primitive['material']}
            primitives.append(prim)
            report.append({'mesh':node['name'],'vertices':len(pos),'triangles':len(indices)//3,'scale':scale,
                           'native_bounds':[fitted.min(axis=0).tolist(),fitted.max(axis=0).tolist()],'neck_fit':neck_fit,'chest_fit':chest_fit,'shoulder_fit':shoulder_fit,'garment_clearance_metres':clearance})
        mi=len(out['meshes']);out['meshes'].append({'primitives':primitives});ni=len(out['nodes'])
        out['nodes'].append({'name':node['name'],'mesh':mi,'skin':0});out['scenes'][0]['nodes'].append(ni)
    for key in ('materials','textures','samplers','images'):
        if key in doc:out[key]=doc[key]
    # Keep image links local and relative for importers that reject file: URIs.
    import os
    for img in out.get('images',[]):img['uri']=os.path.relpath((a.gltf.parent/img['uri']).resolve(),a.output.resolve()).replace('\\','/')
    a.output.mkdir(parents=True,exist_ok=False);out['buffers']=[{'uri':'CJ-fit.bin','byteLength':len(data)}]
    (a.output/'CJ-fit.bin').write_bytes(data);(a.output/'CJ-fit.gltf').write_text(json.dumps(out,indent=2))
    (a.output/'report.json').write_text(json.dumps({'status':'offline bind-fit preview only; requires visual review; not installed','meshes':report,
        'policies':['Facial joints rigid to head','Simplified fingers use middle-finger chain','No native stream or gameplay validation']},indent=2))
    print(json.dumps({'output':str(a.output),'primitives':len(report),'vertices':sum(x['vertices'] for x in report)}))


if __name__=='__main__':main()
