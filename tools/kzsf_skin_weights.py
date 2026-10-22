"""Bounded offline skin-weight codec for the observed four-influence layout.

Native CPU skinning at 5e5730 explicitly reconstructs the first weight as
255 minus seven stored bytes. Captured four-index GPU streams store three
nonzero weight channels plus zero padding. GPU equivalence still needs a
shader-side/render check; this codec alone does not establish it.
"""
import math


def encode_four(joints,weights,bone_count):
    if len(joints)!=len(weights) or not 1<=len(joints)<=8 or not 1<=bone_count<=256:
        raise ValueError('Invalid influence layout')
    merged={}
    for joint,weight in zip(joints,weights):
        if not isinstance(joint,int) or not 0<=joint<bone_count or not math.isfinite(weight) or weight<0:
            raise ValueError('Invalid influence')
        merged[joint]=merged.get(joint,0)+weight
    total=sum(merged.values())
    if abs(total-1)>.005:raise ValueError('Weights must sum to one')
    active=sorted(((joint,value/total) for joint,value in merged.items() if value>0),key=lambda x:(-x[1],x[0]))
    if len(active)>4:raise ValueError('Four-index stream cannot encode more than four influences')
    values=[weight*255 for _,weight in active];quantized=[math.floor(x) for x in values]
    missing=255-sum(quantized)
    order=sorted(range(len(values)),key=lambda i:(-(values[i]-quantized[i]),i))
    for i in order[:missing]:quantized[i]+=1
    packed=sorted(zip((x[0] for x in active),quantized),key=lambda x:(-x[1],x[0]))
    packed+=[(packed[0][0],0)]*(4-len(packed))
    indices=bytes(x[0] for x in packed);explicit=bytes([packed[1][1],packed[2][1],packed[3][1],0])
    return indices,explicit


def encode_eight(joints,weights,bone_count):
    """Native body 8-index layout (captured mesh 46): 8 index bytes, then 7 explicit
    weight bytes in descending order plus one zero pad byte; slot 0 carries the
    implicit 255-minus-sum weight; unused slots use index 0 with weight 0."""
    if len(joints)!=len(weights) or not 1<=len(joints)<=8 or not 1<=bone_count<=256:
        raise ValueError('Invalid influence layout')
    merged={}
    for joint,weight in zip(joints,weights):
        if not isinstance(joint,int) or not 0<=joint<bone_count or not math.isfinite(weight) or weight<0:
            raise ValueError('Invalid influence')
        merged[joint]=merged.get(joint,0)+weight
    total=sum(merged.values())
    if abs(total-1)>.005:raise ValueError('Weights must sum to one')
    active=sorted(((joint,value/total) for joint,value in merged.items() if value>0),key=lambda x:(-x[1],x[0]))
    values=[weight*255 for _,weight in active];quantized=[math.floor(x) for x in values]
    order=sorted(range(len(values)),key=lambda i:(-(values[i]-quantized[i]),i))
    for i in order[:255-sum(quantized)]:quantized[i]+=1
    packed=[p for p in sorted(zip((x[0] for x in active),quantized),key=lambda x:(-x[1],x[0])) if p[1]>0]
    packed+=[(0,0)]*(8-len(packed))
    return bytes(x[0] for x in packed),bytes([x[1] for x in packed[1:]]+[0])


def decode_eight(indices,explicit,bone_count):
    if len(indices)!=8 or len(explicit)!=8 or explicit[7] or sum(explicit)>255 or any(i>=bone_count for i in indices):
        raise ValueError('Invalid eight-index stream')
    return list(indices),[(255-sum(explicit))/255,*(x/255 for x in explicit[:7])]


def decode_four(indices,explicit,bone_count):
    if len(indices)!=4 or len(explicit)!=4 or explicit[3] or sum(explicit)>255 or any(i>=bone_count for i in indices):
        raise ValueError('Invalid four-index stream')
    weights=[255-sum(explicit),*explicit[:3]]
    return list(indices),[x/255 for x in weights]
