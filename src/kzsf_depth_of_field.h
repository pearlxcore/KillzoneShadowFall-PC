#ifndef KZSF_DEPTH_OF_FIELD_H
#define KZSF_DEPTH_OF_FIELD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum { KZSF_DOF_GUARD=0x57835d, KZSF_DOF_FLAGS=0x161d504 };
static const unsigned char kzsf_dof_signature[]={
    0x48,0x8d,0x05,0xa0,0x51,0x0a,0x01,0xf6,0x00,0x02,0x74,0x2b,
    0x44,0x0f,0xb6,0x8d,0x4e,0xfe,0xff,0xff};
/* Bit 1 selects FUN_57a600, using postUltraDepthOfFieldShader and its near
 * variant (slots 4bf8 / 4c00). The native OFF branch clears the four affected
 * targets before composition. Preserve motion-blur bit 0 and every other bit. */
static inline int kzsf_disable_depth_of_field(unsigned char *image,size_t size,
                                             uint32_t *before,uint32_t *after) {
    if(!image || size<KZSF_DOF_FLAGS+4 ||
       memcmp(image+KZSF_DOF_GUARD,kzsf_dof_signature,sizeof(kzsf_dof_signature))) return 0;
    uint32_t flags;
    memcpy(&flags,image+KZSF_DOF_FLAGS,4);
    if((flags|3u)!=0xfffu) return 0;
    *before=flags; *after=flags&~2u;
    memcpy(image+KZSF_DOF_FLAGS,after,4);
    return 1;
}
#endif
