#ifndef KZSF_EXECUTION_PARTNER_H
#define KZSF_EXECUTION_PARTNER_H
#include "kzsf_follow_baseline.h"
/* Native f45045 follows CloseCombatComponent+108 (Soldier interface+48)
 * to interface+8b0 (direct Soldier+8f8). Only a currently active, owned
 * execution with the exact retained camera layer can nominate a partner.
 * This permission is recomputed each callback; nothing survives the action. */
static inline uintptr_t kzsf_execution_partner(const KzsfFollowBaseline *b,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfFollowType type) {
    if(!b || !read || !type || !b->combat_layer || !b->combat_component || !b->owner ||
       b->combat_layer>UINTPTR_MAX-0x18 || base>UINTPTR_MAX-0x3ffffff)return 0;
    uintptr_t owner=0,handle=0,partner_handle=0,component=0,partner_owner=0;
    uint8_t active=0;
    if(type(b->combat_component,base,read,opaque)!=base+0x3ced350 ||
       !kzsf_zoom_read(read,opaque,b->combat_component,0x38,&owner,8) || owner!=b->owner ||
       !kzsf_zoom_read(read,opaque,b->combat_component,0x88,&handle,8) || handle!=b->combat_layer+0x18 ||
       !kzsf_zoom_read(read,opaque,b->combat_component,0xdd,&active,1) || active!=1 ||
       !kzsf_zoom_read(read,opaque,b->combat_component,0x108,&partner_handle,8) || partner_handle<=0x48)return 0;
    uintptr_t partner=partner_handle-0x48;
    if(partner==b->owner || type(partner,base,read,opaque)!=base+0x3ca98b0 ||
       !kzsf_zoom_read(read,opaque,partner,0x8f8,&component,8) ||
       type(component,base,read,opaque)!=base+0x3ced350 ||
       !kzsf_zoom_read(read,opaque,component,0x38,&partner_owner,8) || partner_owner!=partner)return 0;
    uintptr_t check=0;
    if(!kzsf_zoom_read(read,opaque,b->combat_component,0x108,&check,8) || check!=partner_handle)return 0;
    return partner;
}
#endif
