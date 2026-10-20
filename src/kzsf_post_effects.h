#ifndef KZSF_POST_EFFECTS_H
#define KZSF_POST_EFFECTS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* CUSA00190 01.81: two further bits of the native post-process feature mask at
 * 0x161d504, handled exactly like the existing motion-blur (bit 0) and
 * depth-of-field (bit 1) switches: the game's own OFF branch for the pass is
 * selected, so the affected intermediate target is still written with the
 * neutral/default result instead of being skipped.
 *
 * Bit 3 - film grain. FUN_577950 calls FUN_57a4c0 only while
 * (mask & 8) != 0 and *(param_1+0x68) != 0. FUN_57a4c0 binds the shader object
 * in slot param_1+0x4bd0 and makes one full-screen pass into the target at
 * param_1+0x17a0. The shader-object array at param_1+0x4b90 is ordered like the
 * post-shader name pool at 0x11c25a1; two independent anchors confirm the
 * mapping (slot 0x4ba8 = postMotionBlurHiResShader, used by the motion-blur
 * pass; slots 0x4bf8/0x4c00 = the far/near DOF shaders used by FUN_57a600), and
 * 0x4bd0 is index 8 = postGrainWeightShader. The native OFF branch writes the
 * same param_1+0x17a0 target with default parameters.
 *
 * Bit 2 - bloom. FUN_579850 binds slots 0x4bb0/0x4bb8 (index 4/5 =
 * postBloomCaptureShader / postBloomIntegrateShader) and integrates the result
 * into param_1+0x17a0. Its own (mask & 4) == 0 branch draws the neutral
 * composite for that target.
 *
 * Bit 4 - vignette - is deliberately NOT switched. The (mask & 0x10) block in
 * FUN_579850 wraps the vignette pass at param_1+0x35a0 AND the extra
 * post-process effectors at param_3+0x48 / param_3+0x30 applied through
 * FUN_57c3a0. Clearing the bit skips both, so it is not a cosmetic-only switch
 * and the block's output target is never written. See
 * docs/KILLZONE-ENGINE-CONTROLS.md.
 *
 * The existing motion-blur/DOF guards require every other mask bit to be set,
 * so these two run afterwards and accept any combination of bits 0..3 already
 * cleared; bits 4..11 must all still be set or the patch is refused. */
enum { KZSF_FF_FLAGS=0x161d504, KZSF_GRAIN_GUARD=0x57824e, KZSF_BLOOM_GUARD=0x57a15d };
static const unsigned char kzsf_grain_signature[]={
    0x48,0x8d,0x05,0xaf,0x52,0x0a,0x01, /* lea rax,[161d504] */
    0xf6,0x00,0x08,                     /* test byte [rax],8 */
    0x74,0x29,                          /* je native-off */
    0x48,0x83,0x7b,0x68,0x00,           /* cmp qword [rbx+68],0 */
    0x74,0x22};                         /* je native-off */
static const unsigned char kzsf_bloom_signature[]={
    0x48,0x8d,0x05,0xa0,0x33,0x0a,0x01, /* lea rax,[161d504] */
    0xf6,0x00,0x04,                     /* test byte [rax],4 */
    0x48,0x8b,0x9d,0x88,0xfe,0xff,0xff, /* mov rbx,[rbp-178] */
    0x0f,0x85,0x81,0x00,0x00,0x00};    /* jne 57a1f5 (bloom-on) */
/* Bits 4..11 must be set and bits 12..31 clear; only bits 0..3 may differ. */
static inline int kzsf_mask_known(uint32_t flags) { return (flags|0x0fu)==0xfffu; }
static inline int kzsf_clear_mask_bit(unsigned char *image,size_t size,uintptr_t guard,
                                      const unsigned char *signature,size_t length,
                                      uint32_t bit,uint32_t *before,uint32_t *after) {
    if(!image || size<KZSF_FF_FLAGS+4 || guard>size || size-guard<length ||
       memcmp(image+guard,signature,length)) return 0;
    uint32_t flags;
    memcpy(&flags,image+KZSF_FF_FLAGS,4);
    if(!kzsf_mask_known(flags)) return 0;
    *before=flags; *after=flags&~bit;
    memcpy(image+KZSF_FF_FLAGS,after,4);
    return 1;
}
static inline int kzsf_disable_film_grain(unsigned char *image,size_t size,
                                          uint32_t *before,uint32_t *after) {
    return kzsf_clear_mask_bit(image,size,KZSF_GRAIN_GUARD,kzsf_grain_signature,
                               sizeof(kzsf_grain_signature),8u,before,after);
}
static inline int kzsf_disable_bloom(unsigned char *image,size_t size,
                                     uint32_t *before,uint32_t *after) {
    return kzsf_clear_mask_bit(image,size,KZSF_BLOOM_GUARD,kzsf_bloom_signature,
                               sizeof(kzsf_bloom_signature),4u,before,after);
}
#endif
