#ifndef KZSF_CHARACTER_FACE_OWNER_BRIDGE_H
#define KZSF_CHARACTER_FACE_OWNER_BRIDGE_H
#include "kzsf_camera_call_bridge.h"
enum {KZSF_CHARACTER_FACE_OWNER_SITE=0xe45680,KZSF_CHARACTER_FACE_OWNER_CAPACITY=384};
static const unsigned char kzsf_character_face_owner_guard[]={0x48,0x8b,0x80,0xd0,1,0,0};
/* Replace only the constructor's local owner-resource load. Preserve flags,
 * all SysV volatile GPRs except the result RAX, and all16 XMM registers. */
static inline int kzsf_character_face_owner_bridge(unsigned char *stub,size_t capacity,uintptr_t address,
        unsigned char patch[7],uintptr_t site,const unsigned char original[7],uintptr_t callback) {
    if(!stub || !patch || !original || !callback || capacity<KZSF_CHARACTER_FACE_OWNER_CAPACITY ||
       address>UINTPTR_MAX-KZSF_CHARACTER_FACE_OWNER_CAPACITY || site>UINTPTR_MAX-7 ||
       memcmp(original,kzsf_character_face_owner_guard,7))return 0;
    unsigned char code[KZSF_CHARACTER_FACE_OWNER_CAPACITY];size_t n=0;
    const unsigned char save[]={0x51,0x52,0x56,0x57,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
        0x9c,0x48,0x81,0xec,8,1,0,0};
    memcpy(code+n,save,sizeof(save));n+=sizeof(save);
    for(int pass=0;pass<2;pass++) {
        for(int reg=0;reg<16;reg++) {
            code[n++]=0xf3;if(reg>=8)code[n++]=0x44;
            code[n++]=0x0f;code[n++]=pass ? 0x6f : 0x7f;
            code[n++]=(unsigned char)(0x84|((reg&7)<<3));code[n++]=0x24;
            int32_t offset=reg*16;memcpy(code+n,&offset,4);n+=4;
        }
        if(!pass) {
            const unsigned char args[]={0x48,0x89,0xc7,0x4c,0x89,0xe6,0x48,0xb8};
            memcpy(code+n,args,sizeof(args));n+=sizeof(args);memcpy(code+n,&callback,8);n+=8;
            code[n++]=0xff;code[n++]=0xd0;
        }
    }
    const unsigned char restore[]={0x48,0x81,0xc4,8,1,0,0,0x9d,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5f,0x5e,0x5a,0x59};
    memcpy(code+n,restore,sizeof(restore));n+=sizeof(restore);
    int32_t entry=0,back=0;
    if(!kzsf_camera_relative(site+5,address,&entry) || !kzsf_camera_relative(address+n+5,site+7,&back))return 0;
    code[n++]=0xe9;memcpy(code+n,&back,4);n+=4;
    unsigned char replacement[7]={0xe9,0,0,0,0,0x90,0x90};memcpy(replacement+1,&entry,4);
    memcpy(stub,code,n);memcpy(patch,replacement,7);return (int)n;
}
#endif
