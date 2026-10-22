#!/usr/bin/env python3
"""Offline native vertex-stream payload writer; does not submit resources.

Layout derived from KZSF 01.81's read path 2e7ef0 -> 2e7320, checked against
bounded live captures. Native resource construction/registration is separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def encode_stream(vertices,stride,elements,data,flags=0x31):
    if not 0<vertices<=1000000 or not 12<=stride<=256:
        raise ValueError('Invalid vertex count or stride')
    if not 1<=len(elements)<=15 or len(data)!=vertices*stride:
        raise ValueError('Vertex payload length does not match layout')
    if flags not in (0x31,0xb1):raise ValueError('Unsupported stream flags')
    records=[]
    seen=set()
    for record in elements:
        if len(record)!=4 or record[0]>=stride or record[3] in seen:
            raise ValueError('Invalid or repeated vertex element')
        seen.add(record[3]);records.append(bytes(record))
    layout=struct.pack('<3I',flags,stride,len(records))+b''.join(records)
    # Native dedup keys cover the layout and bytes, never a captured UUID.
    identity=hashlib.sha256(layout+data).digest()[:16]
    return struct.pack('<2I',vertices,1)+layout+identity+data


def decode_stream(payload):
    if len(payload)<40:raise ValueError('Truncated vertex stream')
    vertices,streams,flags,stride,count=struct.unpack_from('<5I',payload)
    if streams!=1 or not 1<=count<=15:raise ValueError('Unsupported vertex stream count')
    end=20+count*4
    elements=[payload[i:i+4] for i in range(20,end,4)]
    if len(payload)<end+16:raise ValueError('Truncated vertex stream identity')
    data=payload[end+16:]
    expected=encode_stream(vertices,stride,elements,data,flags)
    if expected!=payload:raise ValueError('Vertex stream identity mismatch')
    return vertices,stride,elements,data,flags


def encode_indices(vertices,width,data):
    """IndexArrayResource read sequence: 500e90 -> 2e20c0.

    The final header word is consumed but unused by this reader. Write the
    byte count there. Native usage flags are preserved; no UUID is copied.
    """
    if not 0<vertices<=1000000 or width not in (2,4) or not data or len(data)%width:
        raise ValueError('Invalid index layout')
    count=len(data)//width
    if count>3000000 or count%3:raise ValueError('Invalid triangle index count')
    if max(x[0] for x in struct.iter_unpack('<H' if width==2 else '<I',data))>=vertices:
        raise ValueError('Index outside vertex buffer')
    head=struct.pack('<3I',count,0x91,width==4)
    identity=hashlib.sha256(head+data).digest()[:16]
    return head+identity+struct.pack('<I',len(data))+data


def decode_indices(payload,vertices):
    if len(payload)<32:raise ValueError('Truncated index stream')
    count,flags,wide=struct.unpack_from('<3I',payload)
    if flags!=0x91 or wide not in (0,1):raise ValueError('Unsupported index flags')
    width=4 if wide else 2
    if count*width!=len(payload)-32 or encode_indices(vertices,width,payload[32:])!=payload:
        raise ValueError('Index stream length or identity mismatch')
    return count,width,payload[32:]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',required=True,type=Path,help='Native capture.json')
    parser.add_argument('--primitive',required=True,type=int,help='Accepted primitive index')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    report=json.loads(args.capture.read_text())
    primitives=[p for m in report['meshes'] for p in m.get('primitives',[]) if 'layout' in p]
    if not 0<=args.primitive<len(primitives):parser.error('Primitive index outside capture')
    primitive=primitives[args.primitive];layout=primitive['layout']
    source=(args.capture.parent/primitive['vertex_buffer']['file']).resolve()
    if not source.is_relative_to(args.capture.parent.resolve()):parser.error('Buffer path outside capture')
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=primitive['vertex_buffer']['sha256']:
        raise ValueError('Captured buffer checksum mismatch')
    result=encode_stream(layout['vertices'],layout['stride'],
                         [bytes.fromhex(x) for x in layout['element_records_hex']],data)
    decode_stream(result)
    with args.output.open('xb') as stream:stream.write(result)
    print(f'Wrote offline native stream: {args.output} ({len(result)} bytes)')


if __name__=='__main__':main()
