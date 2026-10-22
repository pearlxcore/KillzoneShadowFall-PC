#ifndef KZSF_CHARACTER_BODY_VISIBILITY_H
#define KZSF_CHARACTER_BODY_VISIBILITY_H
#include "kzsf_character_native_query.h"
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterBodyBegin)(uintptr_t,unsigned);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterBodySlot)(uintptr_t,int,int);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterBodyEnd)(uintptr_t);
typedef struct {int32_t index;uintptr_t part;int enabled;} KzsfCharacterBodyVisibility;
/* Only exact full-body resources for the owned player's chosen appearance.
 * Preserve unrelated attachments and respect a native all-hidden body state. */
static inline int kzsf_character_body_visibility_plan_follow(uintptr_t owner,uintptr_t desired,uintptr_t other,int show_body,int follow_other,
        KzsfZoomRead read,void *opaque,uintptr_t *model,KzsfCharacterBodyVisibility changes[128],int *change_count) {
    uintptr_t m=0,parent=0,array=0;int32_t count=0;int keeper=-1,first=-1,n=0;
    uintptr_t parts[128];uint32_t flags[128];
    if(!owner || !desired || desired==other || !model || !changes || !change_count ||
       !kzsf_zoom_read(read,opaque,owner,0x200,&m,8) || !m ||
       !kzsf_zoom_read(read,opaque,m,0x38,&parent,8) || parent!=owner ||
       !kzsf_zoom_read(read,opaque,m,0x58,&count,4) || count<1 || count>128 ||
       !kzsf_zoom_read(read,opaque,m,0x60,&array,8) || !array)return 0;
    for(int i=0;i<count;i++) {
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&parts[i],8) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xe0,&flags[i],4))return 0;
        if(keeper<0 && parts[i]==desired && (flags[i]&0x100))keeper=i;
        if(first<0 && parts[i]==desired)first=i;
        /* Traversal may enable the native body after disabling the selected
         * one. Transfer that visibility only for callers opting into it. */
        if(follow_other && other && parts[i]==other && (flags[i]&0x100))show_body=1;
    }
    /* Checkpoint restore can leave every full-body slot disabled. A caller
     * that has established normal third-person ownership may recover exactly
     * one chosen body through the native visibility transaction. */
    if(keeper<0 && show_body && first>=0) {
        keeper=first;changes[n++]=(KzsfCharacterBodyVisibility){keeper,desired,1};
    }
    if(keeper>=0)for(int i=0;i<count;i++)if((parts[i]==desired || (other && parts[i]==other)) &&
            i!=keeper && (flags[i]&0x100))changes[n++]=(KzsfCharacterBodyVisibility){i,parts[i],0};
    *model=m;*change_count=n;return 1;
}
static inline int kzsf_character_body_visibility_plan_ex(uintptr_t owner,uintptr_t desired,uintptr_t other,int show_body,
        KzsfZoomRead read,void *opaque,uintptr_t *model,KzsfCharacterBodyVisibility changes[128],int *change_count) {
    return kzsf_character_body_visibility_plan_follow(owner,desired,other,show_body,0,read,opaque,model,changes,change_count);
}
static inline int kzsf_character_body_visibility_plan(uintptr_t owner,uintptr_t desired,uintptr_t other,
        KzsfZoomRead read,void *opaque,uintptr_t *model,KzsfCharacterBodyVisibility changes[128],int *change_count) {
    return kzsf_character_body_visibility_plan_ex(owner,desired,other,0,read,opaque,model,changes,change_count);
}
static inline int kzsf_character_body_visibility_apply(uintptr_t owner,uintptr_t model,
        const KzsfCharacterBodyVisibility changes[128],int count,KzsfZoomRead read,void *opaque,
        KzsfCharacterBodyBegin begin,KzsfCharacterBodySlot slot,KzsfCharacterBodyEnd end) {
    if(count<0 || count>128 || !begin || !slot || !end)return 0;
    if(!count)return 1;
    int ok=1;begin(model,0x20000);
    for(int i=0;i<count;i++) {
        uintptr_t owned=0,parent=0,array=0,part=0;int32_t size=0;
        if(!kzsf_zoom_read(read,opaque,owner,0x200,&owned,8) || owned!=model ||
           !kzsf_zoom_read(read,opaque,model,0x38,&parent,8) || parent!=owner ||
           !kzsf_zoom_read(read,opaque,model,0x58,&size,4) || size<1 || size>128 ||
           changes[i].index<0 || changes[i].index>=size ||
           !kzsf_zoom_read(read,opaque,model,0x60,&array,8) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)changes[i].index*0xf0+0xc8,&part,8) || part!=changes[i].part) {ok=0;break;}
        slot(model,changes[i].index,changes[i].enabled);
    }
    end(model);return ok;
}
#endif
