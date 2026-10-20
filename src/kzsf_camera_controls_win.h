#ifndef KZSF_CAMERA_CONTROLS_WIN_H
#define KZSF_CAMERA_CONTROLS_WIN_H
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* CUSA00190 / 01.81 GameViewGame::camera update (FUN_d92a00).
 * Scale the final degree-valued view FOV, after native zoom/script modifiers.
 * Shake effects still update normally; OFF replaces only this view's evaluated
 * shake transform and FOV multiplier with the engine's no-shake values.
 */
enum { KZSF_FOV_SITE=0xd92f61, KZSF_FOV_GUARD=0xd92f59,
       KZSF_SHAKE_SITE=0xd92b4e, KZSF_SHAKE_GUARD=0xd92b49 };
static const unsigned char kzsf_fov_signature[]={
    0xc5,0xfa,0x59,0x05,0x0f,0x9a,0x4c,0x00,
    0xc5,0xfa,0x11,0x43,0x6c,0x49,0x89,0x8e,0xd4,0x01,0x00,0x00};
static const unsigned char kzsf_shake_signature[]={
    0xe8,0xd2,0x46,0x61,0xff,0xc5,0xfa,0x10,0x85,0xcc,0xfd,0xff,0xff,
    0xc5,0xfa,0x59,0x43,0x6c,0xc5,0xfa,0x11,0x43,0x6c};
typedef int (*KzsfCameraRead)(void*,uintptr_t,void*,size_t);
typedef int (*KzsfCameraWrite)(void*,uintptr_t,const void*,size_t);

static inline int kzsf_camera_fov_store(CONTEXT *c,uintptr_t base,int percent,
                                       KzsfCameraWrite write,void *opaque,float *value) {
    float f;
    memcpy(&f,&c->Xmm0,4);
    if(percent>=75 && percent<=150 && percent!=100 && isfinite(f) && f>1.0f && f<175.0f) {
        f*=percent/100.0f;
        if(f>170.0f) f=170.0f;
    }
    if(c->Rbx>UINTPTR_MAX-0x6c || !write(opaque,c->Rbx+0x6c,&f,4)) return 0;
    if(value) *value=f;
    c->Rip=base+KZSF_FOV_SITE+5;
    return 1;
}

static inline int kzsf_camera_shake_result(CONTEXT *c,uintptr_t base,int enabled,
                         KzsfCameraRead read,KzsfCameraWrite write,void *opaque) {
    if(c->Rbp<0x234) return 0;
    float f=1.0f;
    if(!enabled) {
        /* Exact neutral values used by FUN_d92a00's no-shake branch and
         * FUN_3a7220's empty result (constants 125caf0 / 11ad820). */
        const float neutral[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        if(!write(opaque,c->Rbp-0x170,neutral,sizeof(neutral)) ||
           !write(opaque,c->Rbp-0x234,&f,4)) return 0;
    } else if(!read(opaque,c->Rbp-0x234,&f,4)) return 0;
    memset(&c->Xmm0,0,sizeof(c->Xmm0));
    memcpy(&c->Xmm0,&f,4); /* replay VMOVSS xmm0,[rbp-234] */
    c->Rip=base+KZSF_SHAKE_SITE+8;
    return 1;
}
#endif
