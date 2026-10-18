/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KZSF_CHEATS_WIN_H
#define KZSF_CHEATS_WIN_H
#include <windows.h>
#include "kzsf_cheats.h"
/* 2 = player event skipped, 1 = original PUSH RBP replayed, 0 = unreadable
 * player return address. All other guest register/flag/SIMD state is unchanged. */
static inline int kzsf_handle_player_damage(CONTEXT *c,uintptr_t base,KzsfCheatRead read,void *opaque) {
    uintptr_t entity=0;
    int player=read(opaque,c->Rdi+0x38,&entity,8) && kzsf_player_entity(entity,base,read,opaque,0);
    if(player) {
        uintptr_t resume;
        if(!read(opaque,c->Rsp,&resume,8)) return 0;
        c->Rip=resume; c->Rsp+=8;
        return 2;
    }
    uintptr_t saved=c->Rbp;
    memcpy((void *)(c->Rsp-8),&saved,8);
    c->Rsp-=8; c->Rip=base+KZSF_DAMAGE_SITE+1;
    return 1;
}
#endif
