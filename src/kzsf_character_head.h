#ifndef KZSF_CHARACTER_HEAD_H
#define KZSF_CHARACTER_HEAD_H
#include "kzsf_character_swap.h"
typedef struct {uintptr_t reference,state,part;int target_pose;} KzsfCharacterHead;
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterTogglePart)(uintptr_t,uintptr_t,int);
static inline int kzsf_character_head_guard(uintptr_t base,KzsfZoomRead read,void *opaque) {
    static const unsigned char signature[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xf8,0,0,0};
    unsigned char bytes[sizeof(signature)];
    return kzsf_zoom_read(read,opaque,base,0x703260,bytes,sizeof(bytes)) && !memcmp(bytes,signature,sizeof(bytes));
}
/* HumanoidResource HeadPart+330 -> DestructibilityPart InitialState+98 ->
 * DestructibilityPartStateResource ModelPartResource+20. Metadata164f568 and
 * 164e9d0 establish these offsets. Only a separate render-only head with the
 * exact binding layout of one of the already-supported rigs can be attached. */
static inline int kzsf_character_head_part_compatible(KzsfCharacterHead *h,uintptr_t original,uintptr_t body,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,const char **why) {
    KzsfCharacterPart part,reference_part;KzsfCharacterMeshTree head;
    if(why)*why="head-not-separate-render-part";
    if(!h || !h->part || h->part==original || h->part==body ||
       !kzsf_character_part_read(h->part,base,read,opaque,type,&part) || !part.mesh || !part.is_skinned || part.physics ||
       !kzsf_character_mesh_read(part.mesh,base,read,opaque,type,&head) || !head.count || head.unknown_count)return 0;
    for(int mode=0;mode<2;mode++) {
        uintptr_t reference=mode ? body : original;KzsfCharacterMeshTree rig;
        if(!kzsf_character_part_read(reference,base,read,opaque,type,&reference_part) ||
           !kzsf_character_mesh_read(reference_part.mesh,base,read,opaque,type,&rig) || !rig.count || rig.unknown_count)continue;
        int valid=1;
        for(int32_t i=0;i<head.count;i++) {
            if(head.leaves[i].skeleton!=rig.leaves[0].skeleton ||
               !kzsf_character_binding_compare(rig.leaves[0].bindings,head.leaves[i].bindings,base,read,opaque,NULL)){valid=0;break;}
        }
        if(valid){h->target_pose=mode;if(why)*why="head-supported-rig";return 1;}
    }
    if(why)*why="head-rig-needs-adapter";return 0;
}
static inline int kzsf_character_head_read(uintptr_t soldier,uintptr_t original,uintptr_t body,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterHead *out,const char **why) {
    KzsfCharacterHead h={0};
    if(why)*why="head-state-layout";
    if(!out || type(soldier,base,read,opaque)!=base+0x3caac10 ||
       !kzsf_zoom_read(read,opaque,soldier,0x330,&h.reference,8) ||
       type(h.reference,base,read,opaque)!=base+0x3c30948 ||
       !kzsf_zoom_read(read,opaque,h.reference,0x98,&h.state,8) ||
       type(h.state,base,read,opaque)!=base+0x3c30b08 ||
       !kzsf_zoom_read(read,opaque,h.state,0x20,&h.part,8))return 0;
    *out=h;
    if(!kzsf_character_head_part_compatible(&h,original,body,base,read,opaque,type,why))return 0;
    *out=h;return 1;
}
/* Entity ModelResource attributes163fe10/167b540 describe model links at38/c8.
 * Exact SkinnedModelResource typing is required, so unrelated inherited fields
 * are never interpreted as models. ModelPartResources at58/60 is a pointer
 * array (metadata165c258), unlike the live model's f0-stride instance array. */
static inline int kzsf_character_head_from_model(uintptr_t soldier,uintptr_t original,uintptr_t body,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterHead *out,const char **why) {
    if(!out || type(soldier,base,read,opaque)!=base+0x3caac10)return 0;
    *out=(KzsfCharacterHead){0};
    KzsfCharacterHead selected={0};int matches=0;
    for(unsigned field=0;field<2;field++) {
        uintptr_t model=0,array=0;int32_t count=0;
        if(!kzsf_zoom_read(read,opaque,soldier,field ? 0xc8 : 0x38,&model,8) ||
           type(model,base,read,opaque)!=base+0x3c3cfd0)continue;
        if(!kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<0 || count>128 ||
           !kzsf_zoom_read(read,opaque,model,0x60,&array,8) || (count && !array)) {
            if(why)*why="head-model-part-array";return 0;
        }
        for(int32_t i=0;i<count;i++) {
            KzsfCharacterHead candidate={.reference=model};
            if(!kzsf_zoom_read(read,opaque,array,(size_t)i*8,&candidate.part,8))return 0;
            if(!candidate.part || candidate.part==original || candidate.part==body)continue;
            const char *reason=NULL;
            if(!kzsf_character_head_part_compatible(&candidate,original,body,base,read,opaque,type,&reason))continue;
            if(matches && selected.part==candidate.part)continue;
            selected=candidate;if(++matches>1){if(why)*why="head-model-parts-ambiguous";return 0;}
        }
    }
    if(matches==1){*out=selected;if(why)*why="head-model-part-supported";return 1;}
    if(why)*why="head-model-no-compatible-extra-part";return 0;
}
/* This function follows the game's native EnableParts path (703260). Caller
 * has established local ownership and engine allocator context. */
static inline int kzsf_character_head_toggle(uintptr_t owner,uintptr_t part,int enabled,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterTogglePart toggle,int *was_enabled) {
    uintptr_t model=0,model_owner=0,array=0;int32_t count=0;int before=0,found=0;
    if(!part || !toggle || !kzsf_character_head_guard(base,read,opaque) ||
       !kzsf_zoom_read(read,opaque,owner,0x200,&model,8) || !model ||
       !kzsf_zoom_read(read,opaque,model,0x38,&model_owner,8) || model_owner!=owner ||
       !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>=128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8))return 0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t p=0;uint32_t flags=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&p,8) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xe0,&flags,4))return 0;
        if(p==part){found++;before=(flags&0x100)!=0;}
    }
    if(found>1)return 0;
    if(was_enabled)*was_enabled=before;
    if(before==enabled)return 1;
    toggle(model,part,enabled);
    if(!kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8))return 0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t p=0;uint32_t flags=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&p,8) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xe0,&flags,4))return 0;
        if(p==part)return ((flags&0x100)!=0)==enabled;
    }
    return !enabled;
}
#endif
