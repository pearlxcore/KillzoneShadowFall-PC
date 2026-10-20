#ifndef KZSF_CHECKPOINT_HANDLE_BRIDGE_H
#define KZSF_CHECKPOINT_HANDLE_BRIDGE_H
#include "kzsf_camera_call_bridge.h"
enum { KZSF_CHECKPOINT_HANDLE_SITE=0x620728,KZSF_CHECKPOINT_HANDLE_BYTES=16,
       KZSF_CHECKPOINT_HANDLE_STUB=69 };
static inline int kzsf_checkpoint_handle_guards(const unsigned char *image,size_t size) {
    static const unsigned char zero[]={0x45,0x31,0xff};
    static const unsigned char unlock[]={0x4c,0x89,0xf7,0xe8,0x54,0xb9,0xb4,0,0x4c,0x89,0xf8};
    static const unsigned char site[]={0x48,0x8b,0x4c,0x11,8,0x48,0x8b,0x49,0x20,
        0xc1,0xe8,8,0x48,0x8b,0x1c,0xc1};
    return image && size>=0x620757 && !memcmp(image+0x62070f,zero,sizeof(zero)) &&
        !memcmp(image+0x62074c,unlock,sizeof(unlock)) && !memcmp(image+KZSF_CHECKPOINT_HANDLE_SITE,site,sizeof(site));
}
/*6206f0 already returns no object for its invalid sentinel and empty object
 * slots. A serialized handle may also name an absent pool. Take the existing
 *62074c unlock/return-zero path; never bypass the container lock cleanup.
 * Pool+18 is the object count (native6202f0..620384), pool+20 its array.
 * Check the serialized index before reading a slot. Reject low values before
 * native62073d increments the object refcount. R10 is volatile scratch;
 * success leaves RAX shifted and RBX resolved, exactly like the displaced code. */
static inline int kzsf_checkpoint_handle_bridge(unsigned char *stub,size_t capacity,uintptr_t address,
        unsigned char patch[16],uintptr_t site,const unsigned char original[16]) {
    static const unsigned char signature[]={0x48,0x8b,0x4c,0x11,0x08,0x48,0x8b,0x49,0x20,
        0xc1,0xe8,8,0x48,0x8b,0x1c,0xc1};
    int32_t entry,missing_pool,missing_index,missing_array,missing_object,back;
    if(!stub || !patch || !original || capacity<KZSF_CHECKPOINT_HANDLE_STUB ||
       site>UINTPTR_MAX-0x24 || address>UINTPTR_MAX-KZSF_CHECKPOINT_HANDLE_STUB ||
       memcmp(original,signature,16) || !kzsf_camera_relative(site+5,address,&entry) ||
       !kzsf_camera_relative(address+14,site+0x24,&missing_pool) ||
       !kzsf_camera_relative(address+31,site+0x24,&missing_index) ||
       !kzsf_camera_relative(address+44,site+0x24,&missing_array) ||
       !kzsf_camera_relative(address+64,site+0x24,&missing_object) ||
       !kzsf_camera_relative(address+69,site+16,&back))return 0;
    unsigned char code[KZSF_CHECKPOINT_HANDLE_STUB]={
        0x48,0x8b,0x4c,0x11,0x08, /*mov rcx,[rcx+rdx+8]*/
        0x48,0x85,0xc9,0x0f,0x84,0,0,0,0, /*test rcx,rcx; jz native cleanup*/
        0x41,0x89,0xc2,0x41,0xc1,0xea,8, /*r10d = packed handle >>8*/
        0x44,0x3b,0x51,0x18,0x0f,0x8d,0,0,0,0, /*index >= signed count*/
        0x48,0x8b,0x49,0x20, /*mov rcx,[rcx+20]*/
        0x48,0x85,0xc9,0x0f,0x84,0,0,0,0,
        0xc1,0xe8,8,0x48,0x8b,0x1c,0xc1, /*original shift and slot load*/
        0x48,0x81,0xfb,0,0,1,0,0x0f,0x82,0,0,0,0, /*object < Windows minimum user address*/
        0xe9,0,0,0,0};
    memcpy(code+10,&missing_pool,4);memcpy(code+27,&missing_index,4);
    memcpy(code+40,&missing_array,4);memcpy(code+60,&missing_object,4);memcpy(code+65,&back,4);
    unsigned char replacement[16];memset(replacement,0x90,16);replacement[0]=0xe9;memcpy(replacement+1,&entry,4);
    memcpy(stub,code,sizeof(code));memcpy(patch,replacement,16);return 1;
}
#endif
