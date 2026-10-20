#ifndef KZSF_CAMERA_CALL_BRIDGE_H
#define KZSF_CAMERA_CALL_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

/* Experimental direct-call replacement for the exact d92e8e MOV/MOV/CALL
 * window. Installed only by the opt-in direct-call diagnostic. Runs on the original guest
 * stack, outside VEH. Callback must use sysv_abi and call the supplied original
 * refresh exactly once. It may not assume a validated physics context.
 * Additional RSI/RDX inputs are SysV caller-clobbered by the replaced call.
 * The host compiler must preserve all SysV nonvolatile registers in callback. */
enum { KZSF_CAMERA_CALL_SITE=0xd92e8e,KZSF_CAMERA_CALL_BYTES=12,
       KZSF_CAMERA_BRIDGE_BYTES=33 };
static const unsigned char kzsf_camera_call_signature[]={
    0x48,0x8b,0x03,0x48,0x89,0xdf,0xff,0x90,0x90,0,0,0};
static inline int kzsf_camera_relative(uintptr_t from,uintptr_t to,int32_t *out) {
    if(to>=from) {if(to-from>INT32_MAX)return 0;*out=(int32_t)(to-from);}
    else {uintptr_t delta=from-to;if(delta>(uintptr_t)INT32_MAX+1)return 0;
        *out=delta==(uintptr_t)INT32_MAX+1 ? INT32_MIN : -(int32_t)delta;}
    return 1;
}
static inline int kzsf_camera_call_bridge(unsigned char *stub,size_t capacity,uintptr_t stub_address,
        unsigned char patch[KZSF_CAMERA_CALL_BYTES],uintptr_t site,const unsigned char original[12],
        uintptr_t callback) {
    if(!stub || !patch || !original || !callback || capacity<KZSF_CAMERA_BRIDGE_BYTES ||
       site>UINTPTR_MAX-12 || stub_address>UINTPTR_MAX-KZSF_CAMERA_BRIDGE_BYTES ||
       memcmp(original,kzsf_camera_call_signature,12))return 0;
    int32_t entry,back;
    if(!kzsf_camera_relative(site+5,stub_address,&entry) ||
       !kzsf_camera_relative(stub_address+KZSF_CAMERA_BRIDGE_BYTES,site+12,&back))return 0;
    unsigned char code[KZSF_CAMERA_BRIDGE_BYTES]={
        0x48,0x8b,0x03,                 /* mov rax,[rbx] */
        0x48,0x8b,0x90,0x90,0,0,0,    /* mov rdx,[rax+90]: original refresh */
        0x48,0x89,0xdf,                 /* mov rdi,rbx: render camera */
        0x4c,0x89,0xf6,                 /* mov rsi,r14: GameViewGame */
        0x48,0xb8,0,0,0,0,0,0,0,0,    /* movabs rax,callback */
        0xff,0xd0,                      /* call rax */
        0xe9,0,0,0,0};                 /* jmp original continuation */
    unsigned char replacement[12];memset(replacement,0x90,sizeof(replacement));
    replacement[0]=0xe9;memcpy(replacement+1,&entry,4);
    memcpy(code+18,&callback,8);memcpy(code+29,&back,4);
    memcpy(stub,code,sizeof(code));memcpy(patch,replacement,sizeof(replacement));return 1;
}
#endif
