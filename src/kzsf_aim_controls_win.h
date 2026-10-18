/* CUSA00190 01.81. Overrides only verified ordinary player look paths.
 * Defaults replay displaced instructions. No shared resource data is edited.
 */
#ifndef KZSF_AIM_CONTROLS_WIN_H
#define KZSF_AIM_CONTROLS_WIN_H
#include "kzsf_player_turn_win.h"
enum { KZSF_RESPONSE_SITE=0x8ee291,KZSF_RAMP_SITE=0xe4aa40,KZSF_ASSIST_SITE=0xe4c22d };
static const unsigned char kzsf_response_sig[]={0xc5,0xfa,0x11,0x4d,0xbc,0xc5,0xf8,0x29,0x45,0xc0,0x48,0x8d,0x55,0xc4};
static const unsigned char kzsf_ramp_sig[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x30};
static const unsigned char kzsf_assist_sig[]={0x49,0x89,0xfd,0x4c,0x89,0xfe,0xe8,0x88,0x48,0x19,0x00};
static const unsigned char kzsf_assist_exit_sig[]={0x48,0x8b,0x00,0x48,0x3b,0x45,0xd0,0x75,0x12};
/* Radial inner deadzone followed by a linear or squared magnitude response.
 * This replaces both native shape/deadzone and response curves together.
 * It is not a temporal acceleration filter. Native axis mapping/inversion remain.
 */
static inline int kzsf_look_response(CONTEXT *c,uintptr_t base,int mode,int deadzone,
    KzsfPlayerRead read,KzsfTurnWrite write,void *opaque,int *changed) {
    float magnitude;memcpy(&magnitude,&c->Xmm1,4);*changed=0;
    uintptr_t ret;unsigned user;
    if(c->Rbp<0x44 || c->Rbp>UINTPTR_MAX-8) return 0;
    if(mode>=1 && mode<=2 && deadzone>=0 && deadzone<=30 &&
       read(opaque,c->Rbp+8,&ret,sizeof(ret)) && ret==base+0xb8adda &&
       read(opaque,c->Rbx,&user,sizeof(user)) && user<4) {
        float v[2];memcpy(v,&c->Xmm0,8);
        if(isfinite(v[0]) && isfinite(v[1])) {
            float length=hypotf(v[0],v[1]),d=(float)deadzone/100.f;
            float strength=length>d?fminf(1.f,(length-d)/(1.f-d)):0.f;
            if(mode==2) strength*=strength;
            float scale=length>0?strength/length:0;
            v[0]*=scale;v[1]*=scale;
            if(!write(opaque,c->R15,&v[0],4)||!write(opaque,c->R14,&v[1],4)) return 0;
            c->Rip=base+0x8ee372;*changed=1;return 1;
        }
    }
    if(!write(opaque,c->Rbp-0x44,&magnitude,4)) return 0;
    c->Rip=base+KZSF_RESPONSE_SITE+5;return 1;
}
/* DelayCurve in the joystick assist resource limits how quickly look input
 * rises. Bypass only its four verified calls inside the player look path.
 * Other users of this shared function retain their original PUSH RBP.
 */
static inline int kzsf_look_ramp(CONTEXT *c,uintptr_t base,int enabled,
    KzsfPlayerRead read,KzsfTurnWrite write,void *opaque,int *changed) {
    uintptr_t ret,outer;*changed=0;
    if(c->Rsp<8 || !read(opaque,c->Rsp,&ret,sizeof(ret))) return 0;
    if(!enabled && c->Rbp<=UINTPTR_MAX-8 && read(opaque,c->Rbp+8,&outer,sizeof(outer))) {
        int own=(ret==base+0xe4c1c8 || ret==base+0xe4c1e6)?outer==base+0xb87f42:
            (ret==base+0xb87e98 || ret==base+0xb87eba)?
                (outer==base+0xb8aef9 || outer==base+0xb87c49):0;
        if(own){c->Rsp+=8;c->Rip=ret;*changed=1;return 1;}
    }
    if(!write(opaque,c->Rsp-8,&c->Rbp,8)) return 0;
    c->Rsp-=8;c->Rip=base+KZSF_RAMP_SITE+1;return 1;
}
/* After native input ramp + previous-input updates, before any target query.
 * Skip slowdown/tracking only; preserve the original stack-cookie check and
 * epilogue. Zoom-snap/shoot-assist paths outside e4c160 are not controlled here.
 */
static inline int kzsf_look_assist(CONTEXT *c,uintptr_t base,int enabled,
    KzsfPlayerRead read,void *opaque,int *changed) {
    uintptr_t ret,cookie;*changed=0;
    if(!enabled && c->Rbp<=UINTPTR_MAX-8 && read(opaque,c->Rbp+8,&ret,sizeof(ret)) &&
       ret==base+0xb87f42 && read(opaque,base+0x185e558,&cookie,sizeof(cookie))) {
        c->Rax=cookie;c->Rip=base+0xe4c4c9;*changed=1;return 1;
    }
    c->R13=c->Rdi;c->Rip=base+KZSF_ASSIST_SITE+3;return 1;
}
#endif
