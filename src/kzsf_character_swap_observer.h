#ifndef KZSF_CHARACTER_SWAP_OBSERVER_H
#define KZSF_CHARACTER_SWAP_OBSERVER_H
#include "kzsf_character_swap.h"
#include "kzsf_character_selector.h"
#include "kzsf_character_retarget.h"
#include "kzsf_character_render_retarget.h"
#include "kzsf_character_head.h"
#include "kzsf_character_face.h"
#include "kzsf_character_body_visibility.h"
#include "kzsf_character_models.h"
#include "kzsf_character_generic_pose.h"
#include "kzsf_character_bundle_loader.h"
static KzsfCharacterFace kzsf_character_selected_face;
static uintptr_t kzsf_character_face_source_soldier;
static uint64_t kzsf_character_available_models=1;
static uintptr_t kzsf_character_available_owner;
static int kzsf_character_last_model=1;
static uintptr_t kzsf_character_known_parts[KZSF_CHARACTER_MODEL_COUNT];
static KzsfCharacterBundleLease kzsf_character_bundle_leases[KZSF_CHARACTER_MODEL_COUNT];
static int kzsf_character_level_transition;
static unsigned kzsf_character_level_epoch;
static int kzsf_character_body_should_show(void);
static inline void kzsf_observer_body_visibility(FILE *out,uintptr_t owner,uintptr_t base,
        KzsfZoomRead read,void *opaque,uintptr_t tcb) {
    const char *opt=getenv("KZSF_CHARACTER_SINGLE_BODY_TEST");
    if(!opt || strcmp(opt,"1"))return;
    uintptr_t active=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE);
    uintptr_t recovery=__atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_ACQUIRE);
    int show_body=kzsf_character_body_should_show();
    if(active!=owner && recovery!=owner && !show_body)return;
    KzsfCameraEngineContext engine;KzsfCharacterInventory inventory;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&engine) || engine.allocate_method!=base+0x4f67a0 ||
       !kzsf_character_inventory_read(owner,read,opaque,&inventory))return;
    static const unsigned char begin_guard[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xf6,0x48,0x89,0xfb};
    static const unsigned char slot_guard[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x40};
    static const unsigned char end_guard[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58};
    unsigned char code[sizeof(end_guard)];
    if(!kzsf_zoom_read(read,opaque,base,0x7000d0,code,sizeof(begin_guard)) || memcmp(code,begin_guard,sizeof(begin_guard)) ||
       !kzsf_zoom_read(read,opaque,base,0x701940,code,sizeof(slot_guard)) || memcmp(code,slot_guard,sizeof(slot_guard)) ||
       !kzsf_zoom_read(read,opaque,base,0x7001b0,code,sizeof(end_guard)) || memcmp(code,end_guard,sizeof(end_guard)))return;
    uintptr_t original=inventory.variants[inventory.current].part;
    uintptr_t desired=active==owner ? __atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED) : original;
    uintptr_t other=active==owner ? original : recovery==owner ? __atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED) : 0;
    KzsfCharacterBodyVisibility changes[128];int count=0;uintptr_t model=0;
    if(!kzsf_character_body_visibility_plan_ex(owner,desired,other,show_body,read,opaque,&model,changes,&count) || !count)return;
    if(active!=owner && recovery==owner && model!=__atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED))return;
    int ok=kzsf_character_body_visibility_apply(owner,model,changes,count,read,opaque,
        (KzsfCharacterBodyBegin)(base+0x7000d0),(KzsfCharacterBodySlot)(base+0x701940),(KzsfCharacterBodyEnd)(base+0x7001b0));
    int enabled=0;for(int i=0;i<count;i++)enabled+=changes[i].enabled!=0;
    fprintf(out,"KILLZONE_CHARACTER_SINGLE_BODY status=%s desired=%p enabled=%d disabled=%d checkpoint_recovery=%d native_transaction=1\n",
        ok ? "changed" : "refused",(void*)desired,enabled,count-enabled,show_body && enabled>0);fflush(out);
}
static int kzsf_character_face_write(void *opaque,uintptr_t address,const void *data,size_t size) {
    (void)opaque;memcpy((void*)address,data,size);return 1;
}
static inline KzsfCharacterFaceApi kzsf_character_face_api(uintptr_t base) {
    return (KzsfCharacterFaceApi){(KzsfCharacterFaceCreate)(base+0xe49120),
        (KzsfCharacterFacePair)(base+0x655560),(KzsfCharacterFacePair)(base+0x63d560),
        (KzsfCharacterFaceSingle)(base+0xe451c0),(KzsfCharacterFaceSingle)(base+0x568f70),kzsf_character_face_write};
}
/* Read-only appearance assembly snapshot after the native F7 transaction.
 * Do not assume a HeadPart reference is a renderable ModelPartResource: its
 * declared metadata type is distinct and it must be identified first. */
static inline void kzsf_character_assembly_audit(FILE *out,uintptr_t owner,uintptr_t selected,uintptr_t base,
        KzsfZoomRead read,void *opaque,const KzsfCharacterNativeResources *resources) {
    fputs("KILLZONE_CHARACTER_ASSEMBLY phase=begin read_only=1\n",out);
    for(int32_t i=0;i<resources->count;i++) {
        KzsfCharacterInventory inventory;
        if(!kzsf_character_resource_variants_read(resources->resources[i],base,read,opaque,kzsf_observer_baseline_type,&inventory))continue;
        int found=0;
        for(int32_t j=0;j<inventory.count;j++)if(inventory.variants[j].part==selected)found=1;
        if(!found)continue;
        char name[128]={0};kzsf_character_resource_name(inventory.resource,base,read,opaque,name);
        fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY soldier=%p name=%s variants=%d\n",(void*)inventory.resource,name,inventory.count);
        for(unsigned field=0;field<2;field++) {
            uintptr_t model_resource=0,model_parts=0;int32_t model_count=0;size_t offset=field ? 0xc8 : 0x38;
            if(!kzsf_zoom_read(read,opaque,inventory.resource,offset,&model_resource,8))continue;
            uintptr_t type=model_resource ? kzsf_observer_baseline_type(model_resource,base,read,opaque) : 0;
            fprintf(out,"KILLZONE_CHARACTER_HEAD_MODEL_LINK offset=%zx resource=%p type_rva=%llx\n",offset,(void*)model_resource,(unsigned long long)(type>=base ? type-base : 0));
            if(type!=base+0x3c3cfd0 ||
               !kzsf_zoom_read(read,opaque,model_resource,0x58,&model_count,4) || model_count<0 || model_count>128 ||
               !kzsf_zoom_read(read,opaque,model_resource,0x60,&model_parts,8) || (!model_parts && model_count))continue;
            for(int32_t j=0;j<model_count;j++) {
                uintptr_t p=0;KzsfCharacterPart detail;char part_name[128]={0};
                if(!kzsf_zoom_read(read,opaque,model_parts,(size_t)j*8,&p,8) || !p)continue;
                if(!kzsf_character_part_read(p,base,read,opaque,kzsf_observer_baseline_type,&detail))continue;
                kzsf_character_resource_name(p,base,read,opaque,part_name);
                fprintf(out,"KILLZONE_CHARACTER_HEAD_MODEL_PART index=%d resource=%p mesh=%p skinned=%u physics=%p name=%s",j,(void*)p,(void*)detail.mesh,detail.is_skinned,(void*)detail.physics,part_name);
                KzsfCharacterMeshTree mesh;
                if(detail.mesh && detail.is_skinned && kzsf_character_mesh_read(detail.mesh,base,read,opaque,kzsf_observer_baseline_type,&mesh) && mesh.count)
                    fprintf(out," skeleton=%p binding_bones=%d leaves=%d unknown=%d",(void*)mesh.leaves[0].skeleton,mesh.leaves[0].binding_count,mesh.count,mesh.unknown_count);
                fputc('\n',out);
            }
        }
        /* Humanoid resource metadata1736ae0/1736b28: HeadPart+330,
         * HelmetPart+338. They describe part references, not necessarily meshes. */
        for(unsigned k=0;k<2;k++) {
            uintptr_t reference=0;size_t offset=k ? 0x338 : 0x330;
            if(!kzsf_zoom_read(read,opaque,inventory.resource,offset,&reference,8))continue;
            uintptr_t type=reference ? kzsf_observer_baseline_type(reference,base,read,opaque) : 0;
            fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY reference=%s address=%p type_rva=%llx",k ? "helmet" : "head",(void*)reference,(unsigned long long)(type>=base ? type-base : 0));
            if(reference && kzsf_character_resource_name(reference,base,read,opaque,name))fprintf(out," name=%s",name);
            fputc('\n',out);
        }
        for(int32_t j=0;j<inventory.count;j++)if(inventory.variants[j].part==selected) {
            const KzsfCharacterVariant *v=&inventory.variants[j];
            fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY variant=%d animator=%p components=%d\n",j,(void*)v->model_component,v->component_count);
            for(int32_t k=0;k<v->component_count;k++) {
                uintptr_t component=0;
                if(!kzsf_zoom_read(read,opaque,v->components,(size_t)k*8,&component,8) || !component)continue;
                uintptr_t type=kzsf_observer_baseline_type(component,base,read,opaque);
                fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY variant_component=%d resource=%p type_rva=%llx",k,(void*)component,(unsigned long long)(type>=base ? type-base : 0));
                if(kzsf_character_resource_name(component,base,read,opaque,name))fprintf(out," name=%s",name);
                fputc('\n',out);
            }
        }
    }
    uintptr_t model=0,array=0;int32_t count=0;
    if(kzsf_zoom_read(read,opaque,owner,0x200,&model,8) &&
       kzsf_zoom_read(read,opaque,model,0x58,&count,4) && count>=0 && count<=128 &&
       kzsf_zoom_read(read,opaque,model,0x60,&array,8) && (array || !count)) {
        for(int32_t i=0;i<count;i++) {
            uintptr_t part=0;uint32_t flags=0;char name[128]={0};KzsfCharacterPart detail;
            if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8) ||
               !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xe0,&flags,4))break;
            kzsf_character_resource_name(part,base,read,opaque,name);
            fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY slot=%d resource=%p selected=%d flags=%08x name=%s",i,(void*)part,part==selected,flags,name);
            if(kzsf_character_part_read(part,base,read,opaque,kzsf_observer_baseline_type,&detail))
                fprintf(out," mesh=%p skinned=%u physics=%p",(void*)detail.mesh,detail.is_skinned,(void*)detail.physics);
            fputc('\n',out);
        }
    }
    int32_t component_count=0;uintptr_t components=0;
    if(kzsf_zoom_read(read,opaque,owner,0x250,&component_count,4) && component_count>=0 && component_count<=512 &&
       kzsf_zoom_read(read,opaque,owner,0x258,&components,8) && (components || !component_count)) {
        int32_t limit=component_count<64 ? component_count : 64;
        fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY attached_components=%d sampled=%d\n",component_count,limit);
        for(int32_t i=0;i<limit;i++) {
            uintptr_t component=0,resource=0;char name[128]={0};
            if(!kzsf_zoom_read(read,opaque,components,(size_t)i*8,&component,8) || !component)continue;
            uintptr_t type=kzsf_observer_baseline_type(component,base,read,opaque);
            kzsf_zoom_read(read,opaque,component,0x20,&resource,8);
            uintptr_t resource_type=resource ? kzsf_observer_baseline_type(resource,base,read,opaque) : 0;
            if(resource)kzsf_character_resource_name(resource,base,read,opaque,name);
            fprintf(out,"KILLZONE_CHARACTER_ASSEMBLY attached=%d component=%p type_rva=%llx resource=%p resource_type_rva=%llx name=%s\n",
                i,(void*)component,(unsigned long long)(type>=base ? type-base : 0),(void*)resource,
                (unsigned long long)(resource_type>=base ? resource_type-base : 0),name);
        }
    }
    fputs("KILLZONE_CHARACTER_ASSEMBLY phase=end\n",out);fflush(out);
}
static inline void kzsf_character_switch_text(FILE *out,uintptr_t string,KzsfZoomRead read,void *opaque) {
    int32_t length=0;unsigned char bytes[128];
    if(!string || !kzsf_zoom_read(read,opaque,string,8,&length,4) || length<0 || length>128 ||
       !kzsf_zoom_read(read,opaque,string,0x10,bytes,(size_t)length)){fputs("unreadable",out);return;}
    if(!length)fputs("empty",out);
    for(int32_t i=0;i<length;i++)fprintf(out,"%02x",bytes[i]);
}
static inline void kzsf_character_switch_audit(FILE *out,uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque) {
    int32_t count=0;uintptr_t array=0;
    if(!kzsf_zoom_read(read,opaque,owner,0x250,&count,4) || count<0 || count>512 ||
       !kzsf_zoom_read(read,opaque,owner,0x258,&array,8))return;
    int found=0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t component=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*8,&component,8) ||
           kzsf_observer_baseline_type(component,base,read,opaque)!=base+0x3c2e9c8)continue;
        int32_t switches=0;uintptr_t data=0;found++;
        if(!kzsf_zoom_read(read,opaque,component,0x48,&switches,4) || switches<0 || switches>128 ||
           !kzsf_zoom_read(read,opaque,component,0x50,&data,8))continue;
        fprintf(out,"KILLZONE_CHARACTER_SWITCH_COMPONENT address=%p entries=%d\n",(void*)component,switches);
        for(int32_t j=0;j<switches;j++) {
            uintptr_t name=0,value=0;
            if(!kzsf_zoom_read(read,opaque,data,(size_t)j*0x18,&name,8) ||
               !kzsf_zoom_read(read,opaque,data,(size_t)j*0x18+8,&value,8))continue;
            fputs("KILLZONE_CHARACTER_SWITCH name_hex=",out);kzsf_character_switch_text(out,name,read,opaque);
            fputs(" value_hex=",out);kzsf_character_switch_text(out,value,read,opaque);fputc('\n',out);
        }
    }
    fprintf(out,"KILLZONE_CHARACTER_SWITCH_AUDIT components=%d read_only=1\n",found);fflush(out);
}

/* Reacquire named resources on each keypress. Store no resource pointer
 * between frames, load no bundle and never change SoldierResource/variants. */
static inline void kzsf_observer_commando_toggle(FILE *out,uintptr_t owner,uintptr_t base,
        KzsfZoomRead read,void *opaque,uintptr_t tcb) {
    const char *why="engine-context";KzsfCameraEngineContext engine;
    KzsfCharacterInventory player;KzsfCharacterNativeResources resources;
    uintptr_t target=0,model=0,array=0;int32_t count=0;int old_found=0,new_found=0;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&engine) || engine.allocate_method!=base+0x4f67a0)goto refused;
    why="player-inventory";
    if(!kzsf_character_inventory_read(owner,read,opaque,&player))goto refused;
    KzsfCharacterQueryApi api={(KzsfCharacterQuery)(base+0x39b350),(KzsfCharacterQueryFree)(base+0x3295d0)};
    if(!kzsf_character_native_query_type(base,0x3caac10,read,opaque,kzsf_observer_baseline_type,api,&resources,&why))goto refused;
    why="commando-not-loaded";
    for(int32_t i=0;i<resources.count;i++) {
        char name[128];KzsfCharacterInventory candidate;
        if(!kzsf_character_resource_name(resources.resources[i],base,read,opaque,name) || strcmp(name,"HGH_Commando"))continue;
        if(!kzsf_character_resource_variants_read(resources.resources[i],base,read,opaque,kzsf_observer_baseline_type,&candidate) ||
           candidate.count!=1 || !candidate.variants[0].part)continue;
        if(target && target!=candidate.variants[0].part){why="ambiguous-commando";goto refused;}
        target=candidate.variants[0].part;
    }
    if(!target)goto refused;
    uintptr_t original=player.variants[player.current].part;
    KzsfCharacterBindingCheck check={0};
    if(!kzsf_character_swap_compatible_ex(original,target,base,read,opaque,kzsf_observer_baseline_type,&why,&check)) {
        if(check.reason)fprintf(out,"KILLZONE_CHARACTER_BINDING reason=%s reference=%p candidate=%p bone=%d stride=%u byte=%u values=%02x,%02x numeric=%.9g,%.9g delta=%.9g tolerance=%.9g\n",
            check.reason,(void*)check.reference,(void*)check.candidate,check.bone,check.stride,check.byte,
            check.reference_byte,check.candidate_byte,check.reference_value,check.candidate_value,check.delta,check.tolerance);
        goto refused;
    }
    why="active-model-layout";
    if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
       !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8))goto refused;
    for(int32_t i=0;i<count;i++) {
        uintptr_t part=0;if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8))goto refused;
        old_found+=part==original;new_found+=part==target;
    }
    if((old_found==1 && !new_found) || (new_found==1 && !old_found)) {
        uintptr_t from=old_found ? original : target,to=old_found ? target : original;
        fprintf(out,"KILLZONE_CHARACTER_SWAP phase=begin action=%s owner=%p old=%p new=%p\n",
            old_found ? "commando" : "restore",(void*)owner,(void*)from,(void*)to);fflush(out);
        if(!kzsf_character_change_part(owner,from,to,base,read,opaque,
                (KzsfCharacterChangePart)(base+0x667900),&why))goto refused;
        fprintf(out,"KILLZONE_CHARACTER_SWAP status=changed appearance=%s native_action=1 visual_verified=0\n",
            old_found ? "HGH_Commando" : "default");fflush(out);return;
    }
    why="body-slot-ambiguous";
refused:
    fprintf(out,"KILLZONE_CHARACTER_SWAP status=refused reason=%s\n",why);fflush(out);
}
static inline void kzsf_observer_character_cycle(FILE *out,uintptr_t owner,uintptr_t base,
        KzsfZoomRead read,void *opaque,uintptr_t tcb,int requested_model) {
    const char *why="engine-context";KzsfCameraEngineContext engine;
    KzsfCharacterInventory player;KzsfCharacterNativeResources resources;
    KzsfCharacterChoice choices[KZSF_CHARACTER_SELECTION_LIMIT];int32_t choice_count=0;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&engine) || engine.allocate_method!=base+0x4f67a0)goto refused;
    why="player-inventory";
    if(!kzsf_character_inventory_read(owner,read,opaque,&player))goto refused;
    uintptr_t original=player.variants[player.current].part;
    /* Restore the selected appearance by its own unique owned part. Native
     * animation transitions may add the default body alongside it or reorder
     * slots; these are not competing restoration targets. No NPC query needed. */
    uintptr_t selected_owner=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE);
    uintptr_t selected_part=__atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED);
    if(requested_model!=-2 && kzsf_character_render_installed && selected_owner==owner && selected_part &&
       (selected_part!=original || requested_model==0)) {
        uintptr_t model=0,array=0;int32_t count=0,selected_count=0,default_count=0;
        why="restore-owned-model";
        if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
           !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>128 ||
           !kzsf_zoom_read(read,opaque,model,0x60,&array,8))goto refused;
        for(int32_t i=0;i<count;i++) {
            uintptr_t part=0;uint32_t flags=0;
            if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8) ||
               !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xe0,&flags,4))goto refused;
            selected_count+=part==selected_part;default_count+=part==original;
            fprintf(out,"KILLZONE_CHARACTER_SLOT phase=restore slot=%d part=%p selected=%d default=%d flags=%08x\n",i,(void*)part,part==selected_part,part==original,flags);
        }
        fprintf(out,"KILLZONE_CHARACTER_RESTORE selected_count=%d default_count=%d parts=%d\n",selected_count,default_count,count);fflush(out);
        if(selected_count>=1) {
            why="restore-facial-animator";
            if(!kzsf_character_face_disable(&kzsf_character_selected_face,owner,base,read,opaque,
                    kzsf_observer_baseline_type,kzsf_character_face_api(base)))goto refused;
            if(selected_part!=original && !kzsf_character_change_body_parts(owner,selected_part,original,base,read,opaque,
                    (KzsfCharacterChangePart)(base+0x667900),&why))goto refused;
            uintptr_t head=__atomic_load_n(&kzsf_character_render_added_head,__ATOMIC_RELAXED);
            why="restore-added-head";
            if(head && !kzsf_character_head_toggle(owner,head,0,base,read,opaque,(KzsfCharacterTogglePart)(base+0x703260),NULL))goto refused;
            kzsf_character_render_restore(owner,model,selected_part);
            kzsf_observer_body_visibility(out,owner,base,read,opaque,tcb);
            fputs("KILLZONE_CHARACTER_SWAP status=changed appearance=default native_action=1 visual_verified=0\n",out);fflush(out);return;
        }
        if(!selected_count && default_count) {
            why="restore-facial-animator";
            if(!kzsf_character_face_disable(&kzsf_character_selected_face,owner,base,read,opaque,
                    kzsf_observer_baseline_type,kzsf_character_face_api(base)))goto refused;
            uintptr_t head=__atomic_load_n(&kzsf_character_render_added_head,__ATOMIC_RELAXED);
            why="restore-added-head";
            if(head && !kzsf_character_head_toggle(owner,head,0,base,read,opaque,(KzsfCharacterTogglePart)(base+0x703260),NULL))goto refused;
            kzsf_character_render_restore(owner,model,selected_part);
            kzsf_observer_body_visibility(out,owner,base,read,opaque,tcb);
            fputs("KILLZONE_CHARACTER_SWAP status=unchanged appearance=default reason=engine-restored-body\n",out);fflush(out);return;
        }
        why="restore-selected-part-not-unique";goto refused;
    }
    KzsfCharacterQueryApi api={(KzsfCharacterQuery)(base+0x39b350),(KzsfCharacterQueryFree)(base+0x3295d0)};
    if(!kzsf_character_native_query_type(base,0x3caac10,read,opaque,kzsf_observer_baseline_type,api,&resources,&why))goto refused;
    const char *bundle_mode=getenv("KZSF_CHARACTER_BUNDLE_TEST");
    if(bundle_mode && !strcmp(bundle_mode,"1")) {
        for(int kind=1;kind<KZSF_CHARACTER_MODEL_COUNT;kind++) {
            if(!kzsf_character_bundle_leases[kind].queued)continue;
            uintptr_t soldier=0;
            int state=kzsf_character_bundle_soldier(kind,base,read,opaque,kzsf_observer_baseline_type,
                &kzsf_character_bundle_leases[kind],&soldier);
            why=state==0 ? "bundle-streamer-busy" : "bundle-resource-identity";
            if(state!=1)goto refused;
            int duplicate=0;
            for(int32_t i=0;i<resources.count;i++)if(resources.resources[i]==soldier){duplicate=1;break;}
            if(duplicate)continue;
            why="bundle-resource-list-limit";
            if(resources.count==KZSF_CHARACTER_NATIVE_LIMIT)goto refused;
            resources.resources[resources.count++]=soldier;resources.total++;
            fprintf(out,"KILLZONE_CHARACTER_BUNDLE_DISCOVERY model=%s soldier=%p source=resident-lump\n",
                kzsf_character_model_name(kind),(void*)soldier);
        }
    }
    uintptr_t fresh_parts[KZSF_CHARACTER_MODEL_COUNT]={0};fresh_parts[0]=original;
    why="selection-query-truncated";
    if(resources.total>KZSF_CHARACTER_NATIVE_LIMIT || resources.unknown_types)goto refused;
    for(int32_t i=0;i<resources.count;i++) {
        char name[128];KzsfCharacterInventory variants;
        why="selection-resource-unreadable";
        if(!kzsf_character_resource_name(resources.resources[i],base,read,opaque,name) ||
           !kzsf_character_resource_variants_read(resources.resources[i],base,read,opaque,kzsf_observer_baseline_type,&variants))goto refused;
        for(int32_t j=0;j<variants.count;j++) {
            int model_index=kzsf_character_model_index(name);
            uintptr_t part=variants.variants[j].part;
            if(!part || (part==original && model_index<=0))continue;
            if(model_index>0 && !fresh_parts[model_index])fresh_parts[model_index]=part;
            why="selection-limit-or-ambiguous-name";
            if(!kzsf_character_choice_add(choices,&choice_count,part,name,j))goto refused;
        }
    }
    for(int i=0;i<KZSF_CHARACTER_MODEL_COUNT;i++)__atomic_store_n(&kzsf_character_known_parts[i],fresh_parts[i],__ATOMIC_RELEASE);
    const char *engineer_mode=getenv("KZSF_CHARACTER_ENGINEER_TEST");
    int engineer_test=engineer_mode && !strcmp(engineer_mode,"1");
    const char *commando_mode=getenv("KZSF_CHARACTER_COMMANDO_TEST");
    int commando_test=commando_mode && !strcmp(commando_mode,"1");
    const char *overlay_mode=getenv("KZSF_CHARACTER_OVERLAY_TEST");
    int overlay_test=overlay_mode && !strcmp(overlay_mode,"1");
    const char *all_mode=getenv("KZSF_CHARACTER_ALL_MODELS_TEST");
    int all_test=all_mode && !strcmp(all_mode,"1");
    uintptr_t model=0,array=0,active=0;int32_t count=0,current=-2,matches=0,body_slot=-1,mixed=0;
    why="active-model-layout";
    if(!kzsf_zoom_read(read,opaque,owner,0x200,&model,8) ||
       !kzsf_zoom_read(read,opaque,model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8))goto refused;
    for(int32_t i=0;i<count;i++) {
        uintptr_t p=0;if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&p,8))goto refused;
        int32_t index=-2;if(p==original)index=-1;
        else for(int32_t j=0;j<choice_count;j++)if(p==choices[j].part){index=j;break;}
        if(index!=-2){if(matches && active!=p)mixed=1;matches++;active=p;current=index;body_slot=i;}
    }
    why="body-slot-ambiguous";if(matches!=1 && !(engineer_test && matches>0 && (!mixed || requested_model==-2)))goto refused;
    unsigned char eligible[KZSF_CHARACTER_SELECTION_LIMIT]={0};int32_t available=0;
    KzsfCharacterPart player_part;KzsfCharacterMeshTree player_tree;
    int player_tree_ok=kzsf_character_part_read(original,base,read,opaque,kzsf_observer_baseline_type,&player_part) &&
        kzsf_character_mesh_read(player_part.mesh,base,read,opaque,kzsf_observer_baseline_type,&player_tree) && player_tree.count;
    for(int32_t i=0;i<choice_count;i++) {
        const char *reason=NULL;
        eligible[i]=(unsigned char)kzsf_character_swap_compatible(original,choices[i].part,base,read,opaque,kzsf_observer_baseline_type,&reason);
        const char *engineer=getenv("KZSF_CHARACTER_ENGINEER_TEST");
        if(engineer && !strcmp(engineer,"1")) {
            if(all_test && overlay_test && kzsf_character_model_index(choices[i].name)>=3) {
                KzsfCharacterPart candidate;KzsfCharacterMeshTree candidate_tree;
                static _Thread_local KzsfCharacterGenericPlan candidate_plan;
                eligible[i]=0;reason="experimental-body-layout";
                if(player_tree_ok && kzsf_character_part_read(choices[i].part,base,read,opaque,kzsf_observer_baseline_type,&candidate) && candidate.is_skinned &&
                   kzsf_character_mesh_read(candidate.mesh,base,read,opaque,kzsf_observer_baseline_type,&candidate_tree) && candidate_tree.count && !candidate_tree.unknown_count &&
                   kzsf_character_generic_plan(&player_tree.leaves[0],&candidate_tree.leaves[0],read,opaque,&candidate_plan)) {
                    int uniform=1;
                    for(int leaf=0;leaf<candidate_tree.count;leaf++)if(candidate_tree.leaves[leaf].skeleton!=candidate_tree.leaves[0].skeleton ||
                        candidate_tree.leaves[leaf].binding_count!=candidate_plan.to_count)uniform=0;
                    eligible[i]=(unsigned char)uniform;reason=uniform ? "experimental-generic-pose" : "experimental-mixed-rigs";
                }
            } else if(overlay_test && !strcmp(choices[i].name,"HGH_Commando")) {
                /* Existing strict same-rig eligibility remains authoritative. */
            } else if(commando_test && !overlay_test) {
                /* Keep the strict same-rig AND binding comparison above.
                 * No joint-prefix-only authorization for this experiment. */
                if(strcmp(choices[i].name,"HGH_Commando")){eligible[i]=0;reason="commando-test-scope";}
            } else {
                eligible[i]=0;reason="engineer-test-scope";
                if(kzsf_character_render_installed && !strcmp(choices[i].name,"VSA_Engineer"))
                    eligible[i]=(unsigned char)kzsf_character_engineer_compatible(original,choices[i].part,base,read,opaque,kzsf_observer_baseline_type,&reason);
            }
        }
        available+=eligible[i];
        fprintf(out,"KILLZONE_CHARACTER_CHOICE name=%s variant=%d eligible=%d reason=%s part=%p\n",
            choices[i].name,choices[i].variant,eligible[i],reason ? reason : "unknown",(void*)choices[i].part);
        KzsfCharacterPart part;KzsfCharacterMeshTree tree;
        if(player_tree_ok && !eligible[i] &&
           kzsf_character_part_read(choices[i].part,base,read,opaque,kzsf_observer_baseline_type,&part) &&
           kzsf_character_mesh_read(part.mesh,base,read,opaque,kzsf_observer_baseline_type,&tree) && tree.count) {
            KzsfCharacterRigAudit audit;
            int prefix=kzsf_character_joint_prefix(player_tree.leaves[0].skeleton,tree.leaves[0].skeleton,read,opaque,&audit);
            fprintf(out,"KILLZONE_CHARACTER_RIG_AUDIT name=%s variant=%d joint_prefix=%d reason=%s reference_joints=%d candidate_joints=%d joint=%d swap_authorized=0\n",
                choices[i].name,choices[i].variant,prefix,audit.reason,audit.reference_count,audit.candidate_count,audit.joint);
            const char *retarget=getenv("KZSF_CHARACTER_RETARGET_AUDIT");
            if(retarget && !strcmp(retarget,"1")) {
                KzsfCharacterRetargetAudit map;const char *map_reason=NULL;
                KzsfCharacterMapApi map_api={(KzsfCharacterBuildMap)(base+0x5d8dd0),(KzsfCharacterQueryFree)(base+0x3295d0)};
                fprintf(out,"KILLZONE_CHARACTER_RETARGET phase=begin name=%s variant=%d first_leaf_only=1 applies_pose=0\n",choices[i].name,choices[i].variant);fflush(out);
                if(kzsf_character_retarget_audit(player_tree.leaves[0].skeleton,tree.leaves[0].skeleton,
                        base,read,opaque,map_api,&map,&map_reason)) {
                    fprintf(out,"KILLZONE_CHARACTER_RETARGET phase=end status=sampled name=%s source_joints=%d destination_joints=%d matched=%d missing=%d parent_changes=%d channels=%d applies_pose=0\n",
                        choices[i].name,map.source_count,map.count,map.matched,map.missing,map.parent_changes,map.channels);
                    for(int32_t joint=0;joint<map.count;joint++) {
                        char joint_name[129];int length=kzsf_character_joint_name(tree.leaves[0].skeleton,joint,read,opaque,joint_name);
                        fprintf(out,"KILLZONE_CHARACTER_RETARGET_JOINT name=%s destination=%d source=%d parent_differs=%u joint_name_hex=",
                            choices[i].name,joint,map.joints[joint].source,map.joints[joint].parent_differs);
                        if(length>0)for(int n=0;n<length;n++)fprintf(out,"%02x",(unsigned char)joint_name[n]);
                        else fputs("unreadable",out);
                        fputc('\n',out);
                    }
                } else fprintf(out,"KILLZONE_CHARACTER_RETARGET phase=end status=refused name=%s reason=%s applies_pose=0\n",choices[i].name,map_reason);
                fflush(out);
            }
        }
    }
    kzsf_character_available_models=1;kzsf_character_available_owner=owner;
    for(int i=0;i<choice_count;i++)if(eligible[i]) {
        int id=kzsf_character_model_index(choices[i].name);
        if(id>0)kzsf_character_available_models|=UINT64_C(1)<<id;
    }
    if(requested_model==-2)return;
    int32_t next=kzsf_character_choice_next(current,choice_count,eligible);
    if(requested_model==-1 && overlay_test && current==-1) {
        int preferred=-1;
        for(int i=0;i<choice_count;i++)if(eligible[i] && kzsf_character_model_index(choices[i].name)==kzsf_character_last_model){preferred=i;break;}
        if(preferred>=0)next=preferred; /* otherwise use the first loaded eligible choice */
    }
    if(requested_model>=0) {
        next=-1;
        if(requested_model) {
            const char *name=kzsf_character_model_name(requested_model);
            for(int i=0;i<choice_count;i++)if(eligible[i] && !strcmp(choices[i].name,name)){next=i;break;}
            if(next<0){why="requested-model-unavailable";goto refused;}
        }
    }
    uintptr_t target=next<0 ? original : choices[next].part;
    fprintf(out,"KILLZONE_CHARACTER_SELECTOR choices=%d compatible=%d next=%s\n",choice_count,available,next<0 ? "default" : choices[next].name);
    if(target==active && next<0){fputs("KILLZONE_CHARACTER_SWAP status=unchanged reason=no-compatible-alternative\n",out);fflush(out);return;}
    fprintf(out,"KILLZONE_CHARACTER_SWAP phase=begin action=selector owner=%p old=%p new=%p\n",(void*)owner,(void*)active,(void*)target);fflush(out);
    int target_engineer=next>=0 && kzsf_character_render_installed && !strcmp(choices[next].name,"VSA_Engineer");
    int target_commando=next>=0 && kzsf_character_render_installed && (commando_test || overlay_test) && !strcmp(choices[next].name,"HGH_Commando");
    int target_kind=next>=0 ? kzsf_character_model_index(choices[next].name) : 0;
    int target_managed=target_engineer || target_commando || (all_test && overlay_test && target_kind>=3);
    uintptr_t prior_recovery_owner=__atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_ACQUIRE);
    uintptr_t prior_recovery_model=__atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED);
    uintptr_t prior_recovery_part=__atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED);
    /* Publish before the transaction: it may synchronously rebuild the render
     * node. The adapter still requires the owned slot to contain target. */
    if(target_managed && !target_engineer)kzsf_character_render_select_compatible(owner,model,target,body_slot);
    else if(target_managed)kzsf_character_render_select(owner,target,body_slot);
    int change_ok=target==active ? 1 : engineer_test ?
        kzsf_character_change_body_parts(owner,active,target,base,read,opaque,(KzsfCharacterChangePart)(base+0x667900),&why) :
        kzsf_character_change_part(owner,active,target,base,read,opaque,(KzsfCharacterChangePart)(base+0x667900),&why);
    if(!change_ok) {
        if(current>=0 && kzsf_character_render_installed && kzsf_character_model_index(choices[current].name)>0) {
            kzsf_character_render_publish(owner,active,body_slot,prior_recovery_owner,prior_recovery_model,prior_recovery_part);
            __atomic_store_n(&kzsf_character_model_kind,kzsf_character_model_index(choices[current].name),__ATOMIC_RELEASE);
        }
        else kzsf_character_render_publish(0,0,-1,prior_recovery_owner,prior_recovery_model,prior_recovery_part);
        goto refused;
    }
    if(!target_managed)kzsf_character_render_select(0,0,-1);
    else {
        __atomic_store_n(&kzsf_character_model_kind,target_kind,__ATOMIC_RELEASE);
        kzsf_character_last_model=target_kind;
        kzsf_character_switch_audit(out,owner,base,read,opaque);
        for(int32_t i=0;i<resources.count;i++) {
            char resource_name[128];
            if(!kzsf_character_resource_name(resources.resources[i],base,read,opaque,resource_name) ||
               strcmp(resource_name,choices[next].name))continue;
            uintptr_t face_resource=0;
            int face_supported=kzsf_character_face_resource_variant(resources.resources[i],target,choices[next].variant,base,read,opaque,
                kzsf_observer_baseline_type,&face_resource);
            __atomic_store_n(&kzsf_character_face_source_soldier,resources.resources[i],__ATOMIC_RELEASE);
            int face_attached=face_supported && kzsf_character_face_enable(&kzsf_character_selected_face,owner,face_resource,
                base,read,opaque,kzsf_observer_baseline_type,kzsf_character_face_api(base));
            fprintf(out,"KILLZONE_CHARACTER_FACE supported=%d attached=%d resource=%p rep=%p animator=%p native_rig=1 variant=%d visual_verified=0\n",
                face_supported,face_attached,(void*)face_resource,(void*)kzsf_character_selected_face.rep,(void*)kzsf_character_selected_face.face,choices[next].variant);
            KzsfCharacterHead head={0};const char *head_reason=NULL;
            int supported=kzsf_character_head_read(resources.resources[i],original,target,base,read,opaque,kzsf_observer_baseline_type,&head,&head_reason);
            fprintf(out,"KILLZONE_CHARACTER_HEAD supported=%d reason=%s reference=%p state=%p part=%p target_pose=%d\n",
                supported,head_reason ? head_reason : "unknown",(void*)head.reference,(void*)head.state,(void*)head.part,head.target_pose);
            if(!supported) {
                supported=kzsf_character_head_from_model(resources.resources[i],original,target,base,read,opaque,kzsf_observer_baseline_type,&head,&head_reason);
                fprintf(out,"KILLZONE_CHARACTER_HEAD_MODEL supported=%d reason=%s model=%p part=%p target_pose=%d\n",supported,
                    head_reason ? head_reason : "unknown",(void*)head.reference,(void*)head.part,head.target_pose);
            }
            if(supported) {
                int before=0;
                /* Keep a recovery token even if the native operation succeeds
                 * but its final validation fails. Restoration can hide it. */
                kzsf_character_render_head_select(head.target_pose ? head.part : 0,head.part);
                int attached=kzsf_character_head_toggle(owner,head.part,1,base,read,opaque,(KzsfCharacterTogglePart)(base+0x703260),&before);
                if(before)kzsf_character_render_head_select(head.target_pose ? head.part : 0,0);
                fprintf(out,"KILLZONE_CHARACTER_HEAD status=%s was_enabled=%d native_action=1\n",attached ? "enabled" : "refused",before);
            }
            break;
        }
        kzsf_character_assembly_audit(out,owner,target,base,read,opaque,&resources);
    }
    kzsf_observer_body_visibility(out,owner,base,read,opaque,tcb);
    fprintf(out,"KILLZONE_CHARACTER_SWAP status=changed appearance=%s variant=%d native_action=1 visual_verified=0\n",
        next<0 ? "default" : choices[next].name,next<0 ? player.current : choices[next].variant);fflush(out);return;
refused:
    fprintf(out,"KILLZONE_CHARACTER_SWAP status=refused reason=%s\n",why);fflush(out);
}
#endif
