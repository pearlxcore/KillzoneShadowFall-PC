/* CUSA00190 01.81: SoldierJoystickController's actual IsSprinting result.
 * b87dd0 calls b363f0 (the registered IsSprinting query), then reduces both
 * look axes by SoldierResource.SprintAimSpeedFactor (+0x3dc). Scale only the
 * heading input when that query returned true, before the native reduction.
 * The separate +0xad0 fallback is deliberately left alone. No resource edits,
 * button-state guesses, movement speed or simulation-clock changes.
 */
#ifndef KZSF_SPRINT_TURN_WIN_H
#define KZSF_SPRINT_TURN_WIN_H
#include "kzsf_player_turn_win.h"
enum { KZSF_SPRINT_SITE=0xb87e1a, KZSF_SPRINT_GUARD=0xb87e12 };
static const unsigned char kzsf_sprint_signature[]={
    0x4c,0x89,0xff,0xe8,0xd6,0xe5,0xfa,0xff, /* IsSprinting(soldier) */
    0x84,0xc0,0x75,0x0e,                     /* test al,al; jne */
    0x41,0x83,0xbf,0xd0,0x0a,0x00,0x00,0x00,
    0x0f,0x8e,0xa9,0x00,0x00,0x00,
    0x49,0x8b,0x87,0xd0,0x01,0x00,0x00,
    0xc5,0xfa,0x10,0x80,0xdc,0x03,0x00,0x00};
/* Carry the sprint result only to the corresponding b8abb0 sensitivity hook.
 * The other caller (b87af0) has no general player-turn hook. Match its caller
 * frame and controller as well as the verified return address; consume once.
 * Storage belongs to the calling host thread, never to a shared player global.
 */
typedef struct { uintptr_t frame,controller; int active; } KzsfSprintScope;
static inline void kzsf_sprint_capture(KzsfSprintScope *scope,const CONTEXT *c,
    uintptr_t base,KzsfPlayerRead read,void *opaque) {
    scope->active=0;
    if(!(unsigned char)c->Rax || c->Rbp>UINTPTR_MAX-8) return;
    uintptr_t ret,frame;
    if(read(opaque,c->Rbp+8,&ret,sizeof(ret)) && ret==base+0xb8aef9 &&
       read(opaque,c->Rbp,&frame,sizeof(frame))) {
        scope->frame=frame;scope->controller=c->R12;scope->active=1;
    }
}
static inline int kzsf_sprint_consume(KzsfSprintScope *scope,const CONTEXT *c) {
    int sprinting=scope->active && scope->frame==c->Rbp && scope->controller==c->R12;
    scope->active=0;
    return sprinting;
}
static inline int kzsf_sprint_turn(CONTEXT *c,uintptr_t base,int percent,
    KzsfPlayerRead read,KzsfTurnWrite write,void *opaque,float *before,float *after) {
    unsigned char state=(unsigned char)c->Rax;
    *before=*after=0;
    if(state && percent>=100 && percent<=500 && percent!=100) {
        if(!read(opaque,c->R14,before,4)) return 0;
        *after=*before;
        if(isfinite(*before)) {
            *after=*before*((float)percent/100.0f);
            if(isfinite(*after) && !write(opaque,c->R14,after,4)) return 0;
        }
    }
    /* Replay TEST AL,AL and its conditional branch. AF is undefined by TEST.
     * All GPRs/SIMD and unrelated flags remain unchanged. */
    unsigned parity=state;parity^=parity>>4;parity^=parity>>2;parity^=parity>>1;
    c->EFlags=(c->EFlags & ~0x8c5u) | (state?0:0x40u) |
        (state&0x80u) | ((parity&1u)?0:4u);
    c->Rip=base+(state?0xb87e2c:0xb87e1e);
    return 1;
}
#endif
