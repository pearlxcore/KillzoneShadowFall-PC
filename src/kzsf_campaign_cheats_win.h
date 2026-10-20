/* SPDX-License-Identifier: GPL-2.0-or-later
 * CUSA00190 01.81, locally traced Soldier stamina and SpotAndMark update.
 * Only local-player state is changed. No shared resources or NPC state writes.
 */
#ifndef KZSF_CAMPAIGN_CHEATS_WIN_H
#define KZSF_CAMPAIGN_CHEATS_WIN_H
#include "kzsf_cheats.h"
#include <windows.h>
#include <math.h>
enum { KZSF_STAMINA_SITE_A=0xb394e0, KZSF_STAMINA_SITE_B=0xb3963a,
       KZSF_CONTINUOUS_SCAN_SITE=0xc1a860 };
static const unsigned char kzsf_stamina_original[]={
    0xc4,0xc1,0x7a,0x11,0x85,0x7c,0x0a,0,0};
static const unsigned char kzsf_scan_original[]={
    0x49,0x89,0xfe,0x48,0x8b,0x05,0xee,0x3c,0xc4,0};
typedef int (*KzsfCampaignWrite)(void *,uintptr_t,const void *,size_t);
/* Replay the complete scalar store, including its SIMD register semantics.
 * The original VMOVSS store changes neither flags nor SIMD registers. */
static inline int kzsf_stamina_store(CONTEXT *c,uintptr_t base,KzsfCheatRead read,
                                    KzsfCampaignWrite write,void *opaque,int *changed) {
    float value,maximum;
    *changed=0;
    if(c->R13>UINTPTR_MAX-0xa84) return 0;
    memcpy(&value,&c->Xmm0,4);
    if(kzsf_player_entity(c->R13,base,read,opaque,0) &&
       read(opaque,c->R13+0xa80,&maximum,4) && isfinite(maximum) && maximum>0 && maximum<1e6f) {
        value=maximum; *changed=1;
    }
    if(!write(opaque,c->R13+0xa7c,&value,4)) return 0;
    return 1;
}
/* State 2 is an expanding native scan; state 3 waits between scans. Keeping
 * at least two pulses queued avoids the native expiration path. Holding only
 * the scan's own input flag prevents button-release cancellation. Idle,
 * cooldown, scripted cancellation and ability ownership remain native.
 * The player must activate Tactical Echo normally once after loading. */
static inline int kzsf_continue_scan(uintptr_t ability,uintptr_t base,KzsfCheatRead read,
                                    KzsfCampaignWrite write,void *opaque) {
    uintptr_t owner=0; int state=0,count=0; unsigned char held=1;
    if(!ability || ability>UINTPTR_MAX-0x2c7 ||
       !read(opaque,ability+0x1e0,&owner,8) ||
       !kzsf_player_entity(owner,base,read,opaque,0) ||
       !read(opaque,ability+0x2b8,&state,4) || (state!=2 && state!=3) ||
       !read(opaque,ability+0x2c0,&count,4) || count<0 || count>10000) return 0;
    if(count<2) {
        count=2;
        if(!write(opaque,ability+0x2c0,&count,4))return 0;
    }
    return write(opaque,ability+0x2c6,&held,1);
}
#endif
