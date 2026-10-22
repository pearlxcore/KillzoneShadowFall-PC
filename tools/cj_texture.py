#!/usr/bin/env python3
"""Decode a top BC mip from locally extracted GTA DE PS5 texture exports.

This is an offline asset-preparation tool, not an in-game model loader.
Embedded Texture2D exports need their absolute serial offset/size from UModel
`-list`. Inputs are read-only; only the requested DDS output is written.
The mode-5 byte address equations were checked against AMD's MIT-licensed
gfx10SwizzlePattern.h (GFX10_SW_4K_S_PATINFO, 8/16 bytes per element):
https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/imported/addrlib/src/gfx10/gfx10SwizzlePattern.h
"""
from pathlib import Path
import argparse
import struct


def linearize_mode5(data, width, height, block_bytes):
    """Convert 4 KiB standard tiles of BC blocks to row-major blocks."""
    if block_bytes not in (8,16) or width<1 or height<1:
        raise ValueError('Unsupported block size or dimensions')
    columns,rows=(width+3)//4,(height+3)//4
    tile_width=32 if block_bytes==8 else 16
    pitch=(columns+tile_width-1)//tile_width
    needed=pitch*((rows+15)//16)*4096
    if len(data)<needed: raise ValueError('Truncated tiled texture')
    result=bytearray(columns*rows*block_bytes)
    for y in range(rows):
        for x in range(columns):
            if block_bytes==8:
                element=((x&1)|((y&1)<<1)|((y&2)<<1)|((x&2)<<2)|
                         ((x&4)<<2)|((y&4)<<3)|((x&8)<<3)|((y&8)<<4)|((x&16)<<4))
            else:
                element=((y&1)|((y&2))|((x&1)<<2)|((x&2)<<2)|
                         ((y&4)<<2)|((x&4)<<3)|((y&8)<<3)|((x&8)<<4))
            source=((y//16)*pitch+x//tile_width)*4096+element*block_bytes
            target=(y*columns+x)*block_bytes
            result[target:target+block_bytes]=data[source:source+block_bytes]
    return bytes(result)


def decode_texture(asset, serial_offset=None, serial_size=None):
    asset=Path(asset)
    export=asset.with_suffix('.uexp').read_bytes()
    if (serial_offset is None)!=(serial_size is None):
        raise ValueError('Supply both serial offset and size')
    if serial_offset is not None:
        start=serial_offset-asset.stat().st_size
        if start<0 or serial_size<1 or start+serial_size>len(export):
            raise ValueError('Invalid Texture2D export range')
        export=export[start:start+serial_size]
    matches=[]
    for name,fourcc,block_bytes in ((b'PF_DXT1\0',b'DXT1',8),(b'PF_DXT5\0',b'DXT5',16),
                                   (b'PF_BC5\0',b'ATI2',16)):
        start=0
        while (position:=export.find(name,start))>=0:
            start=position+len(name)
            if position<16 or start+48>len(export): continue
            width,height,packed,length=struct.unpack_from('<4I',export,position-16)
            if length!=len(name) or not(1<=width<=16384 and 1<=height<=16384): continue
            cursor=start
            if not packed&(1<<30): continue
            mode,tail=struct.unpack_from('<2I',export,cursor);cursor+=8
            first,count,cooked,flags,elements,size,offset,w,h,depth=struct.unpack_from('<6IQ3I',export,cursor)
            if not(0<=first<16 and 1<=count<=16 and cooked==1 and
                   w==max(1,width>>first) and h==max(1,height>>first) and depth==1): continue
            if mode!=5: raise ValueError(f'Unsupported PS5 tile mode {mode}')
            # External payload, offsets already relative to .ubulk. Never guess.
            if flags!=0x10501 or elements!=size:
                raise ValueError(f'Unsupported bulk flags/size: {flags:#x}')
            bulk=asset.with_suffix('.ubulk')
            if offset+size>bulk.stat().st_size: raise ValueError('Invalid bulk payload range')
            with bulk.open('rb') as stream:
                stream.seek(offset);data=stream.read(size)
            linear=linearize_mode5(data,w,h,block_bytes)
            matches.append((w,h,fourcc,linear,tail))
    if len(matches)!=1: raise ValueError(f'Expected one Texture2D payload, found {len(matches)}; select an export range')
    width,height,fourcc,pixels,_=matches[0]
    header=(b'DDS '+struct.pack('<7I',124,0x81007,height,width,len(pixels),0,1)+bytes(44)+
            struct.pack('<II4s5I',32,4,fourcc,0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0))
    return header+pixels


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('asset',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--serial-offset',type=lambda value:int(value,0))
    parser.add_argument('--serial-size',type=lambda value:int(value,0))
    args=parser.parse_args()
    result=decode_texture(args.asset,args.serial_offset,args.serial_size)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(result)
    print(f'Wrote {args.output} ({len(result)} bytes)')


if __name__=='__main__': main()
