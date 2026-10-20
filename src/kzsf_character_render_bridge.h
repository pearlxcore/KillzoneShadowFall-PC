#ifndef KZSF_CHARACTER_RENDER_BRIDGE_H
#define KZSF_CHARACTER_RENDER_BRIDGE_H
#include "kzsf_camera_call_bridge.h"
enum { KZSF_CHARACTER_RENDER_SITE=0x71191a,KZSF_CHARACTER_RENDER_STUB_BYTES=12 };
enum { KZSF_CHARACTER_DRAW_ENTRY=0x703e10 };
enum { KZSF_CHARACTER_LEVEL_EXIT=0xd65530 };
static inline int kzsf_character_level_exit_bridge(unsigned char *trampoline,size_t capacity,uintptr_t address,
        unsigned char patch[20],uintptr_t entry,const unsigned char original[20],uintptr_t callback) {
    static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xd8,0,0,0};
    int32_t back;
    if(!trampoline || !patch || !original || !callback || capacity<25 || entry>UINTPTR_MAX-20 || address>UINTPTR_MAX-25 ||
       memcmp(original,signature,20) || !kzsf_camera_relative(address+25,entry+20,&back))return 0;
    memcpy(trampoline,original,20);trampoline[20]=0xe9;memcpy(trampoline+21,&back,4);
    memset(patch,0x90,20);patch[0]=0x48;patch[1]=0xb8;memcpy(patch+2,&callback,8);patch[10]=0xff;patch[11]=0xe0;return 1;
}
/* Complete20-byte prologue, ending before the first RIP-relative load. */
static inline int kzsf_character_draw_bridge(unsigned char *trampoline,size_t capacity,uintptr_t address,
        unsigned char patch[20],uintptr_t entry,const unsigned char original[20],uintptr_t callback) {
    static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x02,0,0};
    int32_t back;
    if(!trampoline || !patch || !original || !callback || capacity<25 || entry>UINTPTR_MAX-20 || address>UINTPTR_MAX-25 ||
       memcmp(original,signature,20) || !kzsf_camera_relative(address+25,entry+20,&back))return 0;
    memcpy(trampoline,original,20);trampoline[20]=0xe9;memcpy(trampoline+21,&back,4);
    memset(patch,0x90,20);patch[0]=0x48;patch[1]=0xb8;memcpy(patch+2,&callback,8);patch[10]=0xff;patch[11]=0xe0;return 1;
}
/* Replace only CALL711ba0. Stub tail-calls a SysV wrapper, retaining the
 * original call's stack alignment and return address. Wrapper must call the
 * original node update exactly once, including for every non-player node. */
static inline int kzsf_character_render_bridge(unsigned char *stub,size_t capacity,uintptr_t address,
        unsigned char patch[5],uintptr_t site,const unsigned char original[5],uintptr_t callback) {
    static const unsigned char signature[]={0xe8,0x81,0x02,0,0};int32_t relative;
    if(!stub || !patch || !original || !callback || capacity<12 || site>UINTPTR_MAX-5 ||
       memcmp(original,signature,5) || !kzsf_camera_relative(site+5,address,&relative))return 0;
    unsigned char code[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    memcpy(code+2,&callback,8);memcpy(stub,code,12);patch[0]=0xe8;memcpy(patch+1,&relative,4);return 1;
}
#endif
