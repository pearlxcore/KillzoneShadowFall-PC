/* Killzone 01.81 SoldierJoystickController sensitivity outputs, not spectator input. */
#ifndef KZSF_PLAYER_TURN_WIN_H
#define KZSF_PLAYER_TURN_WIN_H
#include "kzsf_turn_speed_win.h"
enum { KZSF_PLAYER_TURN_SITE=0xb8af5e, KZSF_PLAYER_TURN_GUARD=0xb8af52 };
static const unsigned char kzsf_player_turn_signature[]={
    0x48,0x8b,0xbd,0x40,0xfe,0xff,0xff, /* mov rdi,[rbp-1c0] */
    0xe8,0x22,0x3e,0xd6,0xff,           /* call 8eed80 */
    0x49,0x8b,0x44,0x24,0x40,           /* mov rax,[r12+40] */
    0x48,0x85,0xc0};
typedef int (*KzsfPlayerRead)(void *,uintptr_t,void *,size_t);
static inline int kzsf_player_turn(CONTEXT *c,uintptr_t base,int heading,int pitch,
    KzsfPlayerRead read,KzsfTurnWrite write,void *opaque,float before[2],float after[2],unsigned *user) {
    if(c->Rbp<0x1c0 || c->R12>UINTPTR_MAX-0x40) return 0;
    uintptr_t input;uint64_t original_rax;unsigned local_user;
    if(!read(opaque,c->Rbp-0x1c0,&input,sizeof(input)) ||
       !read(opaque,input,&local_user,sizeof(local_user)) ||
       !read(opaque,c->R12+0x40,&original_rax,sizeof(original_rax))) return 0;
    const uintptr_t fields[]={c->Rbp-0xac,c->Rbp-0xb0};
    const int percents[]={heading,pitch};
    for(int axis=0;axis<2;++axis) {
        if(!read(opaque,fields[axis],&before[axis],4)) return 0;
        after[axis]=before[axis];
        if(local_user<4 && percents[axis]>=50 && percents[axis]<=300 &&
           percents[axis]!=100 && isfinite(before[axis])) {
            after[axis]*=(float)percents[axis]/100.0f;
            if(!write(opaque,fields[axis],&after[axis],4)) return 0;
        }
    }
    *user=local_user;
    c->Rax=original_rax; /* replay the displaced MOV; flags and all SIMD untouched */
    c->Rip=base+KZSF_PLAYER_TURN_SITE+5;
    return 1;
}
#endif
