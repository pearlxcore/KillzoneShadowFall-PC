/* SPDX-License-Identifier: GPL-2.0-or-later
 * CUSA00190 01.81. Legacy sites/marker sourced from Celogamez's trainer:
 * https://github.com/TeeKay87/HEN-Cheats-Collection/blob/master/cheats/shn/CUSA00190_01.81_cfeab365.shn
 * The trainer marker failed gameplay verification in this port. Windows v2 uses
 * locally traced engine IsPlayer logic instead. No game files modified.
 */
#ifndef KZSF_CHEATS_H
#define KZSF_CHEATS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
enum { KZSF_HEALTH_SITE=0x6958bc, KZSF_RESERVE_SITE=0xc845a9, KZSF_MAGAZINE_SITE=0xca5afc };
enum { KZSF_DAMAGE_SITE=0x690cf0 };
static const unsigned char kzsf_damage_original[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
    0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec};
typedef int (*KzsfCheatRead)(void *opaque,uintptr_t address,void *out,size_t size);
/* Entity_ExportedIsPlayer (65DAC0) dispatches vtable+140. These implement
 * its verified 01.81 cases, without calling guest code from a VEH callback.
 * Unknown implementations and unreadable pointers never receive protection. */
static inline int kzsf_player_entity(uintptr_t entity,uintptr_t base,KzsfCheatRead read,void *opaque,unsigned depth) {
    uintptr_t vtable,method,controller=0; unsigned char flag=0;
    if(!entity || depth>=4 || !read(opaque,entity,&vtable,8) ||
       !read(opaque,vtable+0x140,&method,8)) return 0;
    if(method==base+0xa7ab20) {
        if(!read(opaque,entity+0x282,&flag,1)) return 0;
        if(flag) return 1;
        if(!read(opaque,vtable+0x138,&method,8) || method!=base+0xb67220) return 0;
        return read(opaque,entity+0x948,&controller,8) && controller!=0;
    }
    size_t user_offset=method==base+0xa74980 ? 0x598 : method==base+0xadb8f0 ? 0x580 : 0;
    if(!user_offset || !read(opaque,entity+user_offset,&controller,8) || controller<0x48) return 0;
    return kzsf_player_entity(controller-0x48,base,read,opaque,depth+1);
}
static const unsigned char kzsf_health_original[]={0xc5,0xea,0x5c,0xc0,0xc5,0xfa,0x11,0x00,
    0xc5,0xf0,0x57,0xc9,0xc5,0xf8,0x2e,0xc1};
static const unsigned char kzsf_reserve_original[]={0x44,0x29,0x70,0x18,0x41,0xc7,0x45,0x70};
static const unsigned char kzsf_magazine_original[]={0x45,0x29,0x75,0x1c,0x85,0xdb};
static inline int kzsf_patch_match(const unsigned char *image,size_t size,size_t at,const unsigned char *original,size_t length) {
    return at<=size && length<=size-at && !memcmp(image+at,original,length);
}
static inline int kzsf_relative(unsigned char *jump,uintptr_t target) {
    int64_t delta=(int64_t)target-(int64_t)(uintptr_t)(jump+5);
    if(delta<INT32_MIN || delta>INT32_MAX) return 0;
    int32_t relative=(int32_t)delta;
    jump[0]=0xe9; memcpy(jump+1,&relative,4); return 1;
}
static inline int kzsf_health_stub(unsigned char *stub,size_t capacity,uintptr_t resume) {
    /* Preserve flags below the guest's 128-byte red zone. For the trainer's
     * player guard (r10d=EEFFA144), retain current health in xmm0 as well as RAM:
     * the following death check must not see a lethal subtraction result. */
    static const unsigned char code[]={
        0x48,0x8d,0xa4,0x24,0x80,0xff,0xff,0xff,0x9c,
        0x41,0x81,0xfa,0x44,0xa1,0xff,0xee,0x75,0x12,
        0x9d,0x48,0x8d,0xa4,0x24,0x80,0x00,0x00,0x00,
        0xc5,0xf8,0x28,0xc2,0xe9,0,0,0,0,
        0x9d,0x48,0x8d,0xa4,0x24,0x80,0x00,0x00,0x00,
        0xc5,0xea,0x5c,0xc0,0xc5,0xfa,0x11,0x00,0xe9,0,0,0,0};
    if(capacity<sizeof(code)) return 0;
    memcpy(stub,code,sizeof(code));
    return kzsf_relative(stub+31,resume) && kzsf_relative(stub+53,resume);
}
#endif
