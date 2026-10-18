#ifndef KZSF_MOTION_BLUR_H
#define KZSF_MOTION_BLUR_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* CUSA00190 01.81: native post-process feature mask, not an arbitrary draw skip.
 * FUN_577950 tests bit 0 before calling 5787f0, which uses the registered
 * postMotionBlurShader / postMotionBlurHiResShader. Its off branch clears the
 * same intermediate target before the later composite reads it. Other bits
 * control other effects and must remain unchanged. Original dump stays intact.
 */
enum { KZSF_BLUR_GUARD=0x577c1a, KZSF_BLUR_FLAGS=0x161d504 };
static const unsigned char kzsf_blur_signature[]={
    0x48,0x8d,0x05,0xe3,0x58,0x0a,0x01, /* lea rax,[161d504] */
    0xf6,0x00,0x01,0x74,0x10,           /* test byte [rax],1; je native-off */
    0x48,0x89,0xdf,0x4c,0x89,0xfe,
    0xe8,0xbf,0x0b,0x00,0x00,           /* call 5787f0 */
    0xe9,0xdf,0x00,0x00,0x00};
static inline int kzsf_disable_motion_blur(unsigned char* image,size_t size,
                                         uint32_t* before,uint32_t* after) {
    if (!image || size<KZSF_BLUR_FLAGS+4 ||
        memcmp(image+KZSF_BLUR_GUARD,kzsf_blur_signature,sizeof(kzsf_blur_signature))) return 0;
    uint32_t flags;
    memcpy(&flags,image+KZSF_BLUR_FLAGS,4);
    if ((flags|1u)!=0xfffu) return 0; // refuse an unknown feature-mask layout
    *before=flags; *after=flags&~1u;
    memcpy(image+KZSF_BLUR_FLAGS,after,4);
    return 1;
}
#endif
