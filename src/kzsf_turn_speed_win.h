/* CUSA00190 01.81: engine local-input look-rate scaling.
 *
 * FUN_00a5a7f0 (local input sampling) builds the per-user look rate and stores it
 * after the game's own sensitivity mapping. Two consecutive plain VMOVSS stores
 * write the horizontal (heading, +0x3d4) and vertical (pitch, +0x3d0) rates of the
 * local input object in RBX. Both have no register/flag side effects, so only the
 * destination float is changed here.
 *
 * The piecewise mapping the game applies before these stores is (verified in the
 * decompilation of FUN_00a5a7f0): GetHeadingSpeedFactor/GetPitchSpeedFactor return
 * a factor f in 0..1, mapped to a multiplier that is 0.25 + 0.5*f for f <= 0.5 and
 * f for f > 0.5, i.e. 0.25..1.0 with a knee at 0.5. Because these hooks run after
 * that mapping, multiplying the stored value survives unchanged. */
#ifndef KZSF_TURN_SPEED_WIN_H
#define KZSF_TURN_SPEED_WIN_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
enum { KZSF_TURN_SITE=0xa5ab7a, KZSF_TURN_FIELD=0x3d4,
       KZSF_PITCH_SITE=0xa5ab8a, KZSF_PITCH_FIELD=0x3d0 };
/* Bytes at the heading site -8 .. the pitch store +8 (four instructions). Both
 * stores must match before either is patched. */
static const unsigned char kzsf_turn_signature[]={
    0xc5,0xfa,0x59,0x83,0xd4,0x03,0x00,0x00,
    0xc5,0xfa,0x11,0x83,0xd4,0x03,0x00,0x00,
    0xc5,0xf2,0x59,0x83,0xd0,0x03,0x00,0x00,
    0xc5,0xfa,0x11,0x83,0xd0,0x03,0x00,0x00};
typedef int (*KzsfTurnWrite)(void *,uintptr_t,const void *,size_t);
/* Scales the engine look rate held in XMM0 by percent and stores it to the field of
 * the site the breakpoint came from. Returns 1 when the store was performed and the
 * RIP was advanced past the original instruction. Every GPR, flag and SIMD register
 * is left exactly as the original instruction saw it; the original store is replayed
 * for unsupported local users, non-finite engine values and out-of-range percents.
 * applied (optional) receives the stored value. */
static inline int kzsf_engine_rate_store(CONTEXT *c,uintptr_t base,int pitch,int percent,
                                         KzsfTurnWrite write,void *opaque,float *applied) {
    const uintptr_t site=pitch?KZSF_PITCH_SITE:KZSF_TURN_SITE;
    const uintptr_t field=pitch?KZSF_PITCH_FIELD:KZSF_TURN_FIELD;
    float value;
    memcpy(&value,&c->Xmm0,sizeof(value));
    /* R14 is the local input user index at both verified stores. */
    if(c->R14<4 && percent>=50 && percent<=300 && percent!=100 && isfinite(value))
        value*=((float)percent/100.0f);
    if(applied) *applied=value;
    if(c->Rbx>UINTPTR_MAX-field ||
       !write(opaque,c->Rbx+field,&value,sizeof(value))) return 0;
    c->Rip=base+site+8;
    return 1;
}
/* Heading-only convenience used by the older call sites and tests. */
static inline int kzsf_handle_turn_store(CONTEXT *c,uintptr_t base,int percent,
                                         KzsfTurnWrite write,void *opaque) {
    return kzsf_engine_rate_store(c,base,0,percent,write,opaque,NULL);
}
#endif
