#ifndef KZSF_CAMERA_OBSERVER_WIN_H
#define KZSF_CAMERA_OBSERVER_WIN_H
#include "kzsf_free_camera_win.h"
#include "kzsf_weapon_zoom.h"
#include "kzsf_follow_baseline.h"
#include "kzsf_camera_engine_context.h"
#include "kzsf_character_inventory.h"
#include <stdio.h>

/* Observational only. CUSA00190 / 01.81. No guest calls or guest-data writes.
 * The two callback traps replace MOV RBP,RSP after the original PUSH RBP. */
enum { KZSF_CAMERA_SELECT_ENTRY=0xf84ab0, KZSF_CAMERA_BIND_ENTRY=0xf84030,
       KZSF_CAMERA_OBSERVER_LIMIT=64 };
static const unsigned char kzsf_camera_select_guard[]={
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x60,
    0x49,0x89,0xfe};
static const unsigned char kzsf_camera_bind_guard[]={
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x48,
    0x49,0x89,0xd6,0x48,0x89,0xfb};

static inline int kzsf_observer_read(KzsfCameraRead read,void *opaque,uintptr_t p,
                                    size_t offset,void *out,size_t n) {
    return p && offset<=UINTPTR_MAX-p && n<=UINTPTR_MAX-(p+offset) &&
           read(opaque,p+offset,out,n);
}
static inline int kzsf_observer_replay(CONTEXT *c,uintptr_t base,unsigned kind,
                                      KzsfCameraRead read,void *opaque) {
    if(kind<2) {
        c->Rbp=c->Rsp;
        c->Rip=base+(kind ? KZSF_CAMERA_BIND_ENTRY : KZSF_CAMERA_SELECT_ENTRY)+4;
        return 1;
    }
    if(kind!=2)return 0;
    uint64_t vtable=0;
    if(!kzsf_observer_read(read,opaque,c->Rbx,0,&vtable,8))return 0;
    c->Rax=vtable;c->Rip=base+KZSF_FREE_CAMERA_SITE+3;
    return 1;
}

/* Decode only the observed LEA RAX,[RIP+disp32]; RET type accessor. Do not
 * execute a vtable method, infer a type from an arbitrary pointer, or chase a
 * thunk. Unknown accessors are explicitly reported as undecoded. */
static inline uintptr_t kzsf_observer_type(uintptr_t object,uintptr_t base,
            KzsfCameraRead read,void *opaque,char name[96]) {
    uintptr_t vtable=0,getter=0,type=0,label=0;
    unsigned char code[8];int32_t displacement;
    strcpy(name,"<undecoded>");
    if(!kzsf_observer_read(read,opaque,object,0,&vtable,8) ||
       !kzsf_observer_read(read,opaque,vtable,0,&getter,8) ||
       getter<base || getter-base>0x1190000-8 ||
       !kzsf_observer_read(read,opaque,getter,0,code,8) ||
       code[0]!=0x48 || code[1]!=0x8d || code[2]!=0x05 || code[7]!=0xc3)return 0;
    memcpy(&displacement,code+3,4);
    int64_t relative=(int64_t)(getter-base)+7+displacement;
    if(relative<0 || relative>0x4000000-0x60 || base>UINTPTR_MAX-(uintptr_t)relative)return 0;
    type=base+(uintptr_t)relative;
    if(!kzsf_observer_read(read,opaque,type,0x58,&label,8) ||
       label<base || label-base<0x1190000 || label-base>=0x1300000)return type;
    for(unsigned i=0;i<95;i++) {
        unsigned char ch;
        if(!kzsf_observer_read(read,opaque,label,i,&ch,1) || (ch && (ch<32 || ch>126))) {
            strcpy(name,"<unreadable-name>");return type;
        }
        name[i]=(char)ch;
        if(!ch)return type;
    }
    name[95]=0;return type;
}
static inline uintptr_t kzsf_observer_object(FILE *out,const char *role,uintptr_t p,
                         uintptr_t base,KzsfCameraRead read,void *opaque) {
    char name[96];uintptr_t type=kzsf_observer_type(p,base,read,opaque,name);
    fprintf(out,"  object role=%s address=%p type_rva=0x%llx name=%s\n",role,(void*)p,
        (unsigned long long)(type ? type-base : 0),name);
    return type;
}
static inline uintptr_t kzsf_observer_baseline_type(uintptr_t object,uintptr_t base,
                                                   KzsfZoomRead read,void *opaque) {
    uintptr_t table=0,getter=0;unsigned char code[8];int32_t displacement;
    if(base>UINTPTR_MAX-0x4000000 ||
       !kzsf_observer_read(read,opaque,object,0,&table,8) ||
       !kzsf_observer_read(read,opaque,table,0,&getter,8) ||
       getter<base || getter-base>0x1190000-8 ||
       !kzsf_observer_read(read,opaque,getter,0,code,8) ||
       code[0]!=0x48 || code[1]!=0x8d || code[2]!=0x05 || code[7]!=0xc3)return 0;
    memcpy(&displacement,code+3,4);
    int64_t relative=(int64_t)(getter-base)+7+displacement;
    return relative>=0 && relative<0x4000000 ? base+(uintptr_t)relative : 0;
}
#include "kzsf_character_observer_win.h"
#include "kzsf_character_swap_observer.h"
static inline void kzsf_observer_resource(FILE *out,const char *role,uintptr_t p,
                           uintptr_t base,KzsfCameraRead read,void *opaque) {
    kzsf_observer_object(out,role,p,base,read,opaque);
    if(!p)return;
    unsigned char bytes[96];
    if(!kzsf_observer_read(read,opaque,p,0,bytes,sizeof(bytes))) {
        fputs("    resource_head unreadable\n",out);return;
    }
    fputs("    resource_head=",out);
    for(unsigned i=0;i<sizeof(bytes);i++)fprintf(out,"%02x",bytes[i]);
    fputc('\n',out);
}
static inline void kzsf_observer_camera_fields(FILE *out,uintptr_t component,
                           uintptr_t type,uintptr_t base,KzsfCameraRead read,void *opaque) {
    uintptr_t resource=0;
    kzsf_observer_read(read,opaque,component,0x20,&resource,8);
    kzsf_observer_resource(out,"component-resource",resource,base,read,opaque);
    if(type==base+0x3cf2178) {
        uintptr_t selector=0;
        kzsf_observer_read(read,opaque,resource,0x58,&selector,8);
        kzsf_observer_resource(out,"CameraSelectorProgram",selector,base,read,opaque);
    } else if(type==base+0x3cf2338) {
        uintptr_t selector=0,program=0,instance=0;
        kzsf_observer_read(read,opaque,component,0x48,&selector,8);
        kzsf_observer_read(read,opaque,component,0x50,&instance,8);
        kzsf_observer_read(read,opaque,component,0x60,&program,8);
        kzsf_observer_resource(out,"retained-selector",selector,base,read,opaque);
        kzsf_observer_object(out,"selector-instance",instance,base,read,opaque);
        kzsf_observer_resource(out,"CameraProgramResource",program,base,read,opaque);
    }
}
/* Only exact observed camera types use the verified entity-layer layout.
 * No layer callbacks are invoked. Unknown layer implementations stay unknown. */
static inline void kzsf_observer_layers(FILE *out,uintptr_t entity,uintptr_t type,
                          uintptr_t base,KzsfCameraRead read,void *opaque) {
    if(base>UINTPTR_MAX-0x3ffffff ||
       (type!=base+0x3c95d88 && type!=base+0x3c8ebd0 && type!=base+0x3c96808))return;
    int32_t count=-1,check_count=-1;uintptr_t array=0,check_array=0,layers[64]={0};
    if(!kzsf_observer_read(read,opaque,entity,0x278,&count,4) || count<0 || count>64 ||
       !kzsf_observer_read(read,opaque,entity,0x280,&array,8) ||
       (count && !kzsf_observer_read(read,opaque,array,0,layers,(size_t)count*8))) {
        fprintf(out,"  camera_layers status=unknown count=%d\n",count);return;
    }
    fprintf(out,"  camera_layers status=sampled count=%d array=%p\n",count,(void*)array);
    for(int32_t i=0;i<count;i++) {
        uintptr_t table=0;unsigned char fields[0x25];
        if(!kzsf_observer_read(read,opaque,layers[i],0,&table,8) || table!=base+0x1647070 ||
           !kzsf_observer_read(read,opaque,layers[i],0x28,fields,sizeof(fields))) {
            fprintf(out,"    camera_layer index=%d address=%p layout=unknown\n",i,(void*)layers[i]);continue;
        }
        int32_t direction,key,special;float time,weight;
        memcpy(&direction,fields,4);memcpy(&key,fields+4,4);memcpy(&special,fields+0x14,4);
        memcpy(&time,fields+0x1c,4);memcpy(&weight,fields+0x20,4);
        fprintf(out,"    camera_layer index=%d address=%p layout=known key=%d direction=%d special=%d flag40=%u time=%g weight=%g complete=%u\n",
            i,(void*)layers[i],key,direction,special,fields[0x18],(double)time,(double)weight,fields[0x24]);
    }
    int same=kzsf_observer_read(read,opaque,entity,0x278,&check_count,4) && check_count==count &&
        kzsf_observer_read(read,opaque,entity,0x280,&check_array,8) && check_array==array;
    for(int32_t i=0;same && i<count;i++) {
        uintptr_t check=0;
        same=kzsf_observer_read(read,opaque,array,(size_t)i*8,&check,8) && check==layers[i];
    }
    fprintf(out,"  camera_layers recheck=%s atomic=0\n",same ? "unchanged" : "changed-or-unreadable");
}
static inline void kzsf_observer_zoom(FILE *out,uintptr_t soldier,uintptr_t base,
                                      KzsfCameraRead read,void *opaque) {
    KzsfWeaponZoom z;
    if(!kzsf_weapon_zoom_read(soldier,base,read,opaque,&z)) {
        fputs("  weapon_zoom status=unknown\n",out);return;
    }
    fprintf(out,"  weapon_zoom status=sampled weapon=%p mode_interface=%p zoom_interface=%p mode_index=%d mode_count=%d zoom_index=%d zoom_count=%d selected=%d atomic=0\n",
        (void*)z.weapon,(void*)z.mode_interface,(void*)z.zoom_interface,
        z.mode_index,z.mode_count,z.zoom_index,z.zoom_count,z.zoom_index!=0);
}
/* At most four ancestors and 64 components each; cycles and oversized counts
 * terminate the walk. Reads may race lifecycle changes: this is a best-effort
 * snapshot, not a retained object or proof that every printed pointer is live. */
static inline void kzsf_observer_entity(FILE *out,uintptr_t entity,uintptr_t base,
                                        KzsfCameraRead read,void *opaque) {
    uintptr_t seen[4]={0};
    for(unsigned depth=0;entity && depth<4;depth++) {
        for(unsigned j=0;j<depth;j++)if(seen[j]==entity){fputs("  hierarchy cycle\n",out);return;}
        seen[depth]=entity;
        fprintf(out,"  hierarchy depth=%u\n",depth);
        uintptr_t entity_type=kzsf_observer_object(out,"camera-source-entity",entity,base,read,opaque);
        kzsf_observer_layers(out,entity,entity_type,base,read,opaque);
        uintptr_t controller=0,array=0,parent=0;int32_t count=-1;
        /* +290 is a verified controller field on Soldier; on a camera entity
         * the same offset contains unrelated data (observed 0x200000000/1). */
        if(entity_type==base+0x3ca98b0) {
            kzsf_observer_zoom(out,entity,base,read,opaque);
            kzsf_observer_read(read,opaque,entity,0x290,&controller,8);
            uintptr_t controller_type=kzsf_observer_object(out,"entity-controller",controller,base,read,opaque);
            if(controller_type==base+0x3caa190) {
                int32_t aiming=-1;uint32_t input_flags=0;uintptr_t override=0,state=0,inventory=0;
                int ok=kzsf_observer_read(read,opaque,controller,0x26c,&aiming,4);
                kzsf_observer_read(read,opaque,controller,0x48,&input_flags,4);
                kzsf_observer_read(read,opaque,entity,0x8b8,&override,8);
                kzsf_observer_read(read,opaque,entity,0x900,&state,8);
                kzsf_observer_read(read,opaque,entity,0xa48,&inventory,8);
                fprintf(out,"  campaign_state aiming_readable=%d IsAiming=%d aiming_enum=%d input_flags=0x%x override_8b8=%p\n",
                    ok,ok ? aiming==2 : -1,aiming,input_flags,(void*)override);
                kzsf_observer_object(out,"soldier-state",state,base,read,opaque);
                kzsf_observer_object(out,"inventory-interface",inventory,base,read,opaque);
            }
        }
        if(!kzsf_observer_read(read,opaque,entity,0x250,&count,4) || count<0 || count>256 ||
           !kzsf_observer_read(read,opaque,entity,0x258,&array,8) || (count && !array)) {
            fprintf(out,"  components unreadable-or-rejected count=%d\n",count);return;
        }
        fprintf(out,"  component_count=%d\n",count);
        unsigned limit=count>64 ? 64 : (unsigned)count;
        for(unsigned j=0;j<limit;j++) {
            uintptr_t component=0;
            if(!kzsf_observer_read(read,opaque,array,j*8,&component,8))break;
            if(!component)continue;
            uintptr_t type=kzsf_observer_object(out,"component",component,base,read,opaque);
            if(type==base+0x3cf2178 || type==base+0x3cf2338)
                kzsf_observer_camera_fields(out,component,type,base,read,opaque);
        }
        if(count>64)fputs("  component list truncated at 64\n",out);
        if(!kzsf_observer_read(read,opaque,entity,0x1e0,&parent,8))return;
        entity=parent;
        if(entity && depth==3)fputs("  hierarchy truncated at four ancestors\n",out);
    }
}
static inline void kzsf_observer_snapshot(FILE *out,const CONTEXT *c,uintptr_t base,
                                        KzsfCameraRead read,void *opaque) {
    kzsf_observer_object(out,"GameViewGame",c->R14,base,read,opaque);
    kzsf_observer_object(out,"render-view",c->Rbx,base,read,opaque);
    float blend=0,fov=0,pose[16]={0};
    int blend_ok=kzsf_observer_read(read,opaque,c->R14,0xf0,&blend,4);
    int fov_ok=kzsf_observer_read(read,opaque,c->Rbx,0x6c,&fov,4);
    int pose_ok=kzsf_observer_read(read,opaque,c->Rbx,0x10,pose,sizeof(pose));
    fprintf(out,"  view blend_ok=%d blend=%g fov_ok=%d fov=%g pose_ok=%d eye=%g,%g,%g\n",
        blend_ok,(double)blend,fov_ok,(double)fov,pose_ok,(double)pose[12],(double)pose[13],(double)pose[14]);
    uintptr_t sources[2]={0};int source_ok[2]={0};
    for(unsigned i=0;i<2;i++) {
        uintptr_t handle=0,camera=0;
        int ok=kzsf_observer_read(read,opaque,c->R14,i ? 0x50 : 0x38,&handle,8);
        sources[i]=handle;source_ok[i]=ok;
        fprintf(out,"  source slot=0x%x readable=%d interface=%p\n",i ? 0x50 : 0x38,ok,(void*)handle);
        if(!ok || handle<0x48)continue;
        kzsf_observer_read(read,opaque,handle,0x190,&camera,8);
        kzsf_observer_object(out,"source-camera",camera,base,read,opaque);
        kzsf_observer_entity(out,handle-0x48,base,read,opaque);
    }
    uintptr_t check_current=0,check_outgoing=0;float check_blend=0;
    int unchanged=source_ok[0] && source_ok[1] && blend_ok &&
        kzsf_observer_read(read,opaque,c->R14,0x38,&check_current,8) && check_current==sources[0] &&
        kzsf_observer_read(read,opaque,c->R14,0x50,&check_outgoing,8) && check_outgoing==sources[1] &&
        kzsf_observer_read(read,opaque,c->R14,0xf0,&check_blend,4) && !memcmp(&blend,&check_blend,4);
    fprintf(out,"  view_sources recheck=%s outgoing_present=%d atomic=0\n",
        unchanged ? "unchanged" : "changed-or-unreadable",source_ok[1] ? sources[1]!=0 : -1);
    /* d92a00 reads its user index through view+10 -> +b0. 648040/647d00
     * resolve the controlled entity from the last entry in that user's stack.
     * No native lock is taken: label this a candidate and bound both reads. */
    uintptr_t user=0,stack=0,controlled=0;int32_t count=0;
    int user_ok=kzsf_observer_read(read,opaque,c->R14,0x10,&user,8);
    kzsf_observer_object(out,"view-user-candidate",user,base,read,opaque);
    if(user_ok && kzsf_observer_read(read,opaque,user,0xc8,&count,4) && count>0 && count<=64 &&
       kzsf_observer_read(read,opaque,user,0xd0,&stack,8) &&
       kzsf_observer_read(read,opaque,stack,(size_t)(count-1)*0x18,&controlled,8) && controlled>=0x48) {
        uintptr_t controller=0;
        fprintf(out,"  user_control_stack_count=%d candidate_interface=%p\n",count,(void*)controlled);
        kzsf_observer_object(out,"controlled-entity-candidate",controlled-0x48,base,read,opaque);
        char name[96];
        uintptr_t entity_type=kzsf_observer_type(controlled-0x48,base,read,opaque,name);
        if(entity_type==base+0x3ca98b0) {
            kzsf_observer_read(read,opaque,controlled-0x48,0x290,&controller,8);
            kzsf_observer_object(out,"controlled-controller-candidate",controller,base,read,opaque);
        }
    } else fprintf(out,"  user_control_stack unresolved-or-empty count=%d\n",count);
    KzsfFollowBaseline baseline;const char *why="unknown";
    int matches=kzsf_follow_baseline_read(c->R14,c->Rbx,base,read,opaque,
                                         kzsf_observer_baseline_type,&baseline,&why);
    fprintf(out,"  follow_baseline status=%s reason=%s atomic=0 reader_writes=0\n",
        matches ? "matches-baseline" : "not-established",why);
    if(matches) {
        KzsfCharacterInventory inventory;
        if(kzsf_character_inventory_read(baseline.owner,read,opaque,&inventory)) {
            fprintf(out,"  character_inventory soldier=%p resource=%p count=%d current=%d read_only=1 atomic=0\n",
                (void*)inventory.soldier,(void*)inventory.resource,inventory.count,inventory.current);
            for(int32_t i=0;i<inventory.count;i++) {
                KzsfCharacterVariant *v=&inventory.variants[i];
                fprintf(out,"    body_variant index=%d selected=%d attached_components=%d\n",
                    i,i==inventory.current,v->component_count);
                kzsf_observer_object(out,"body-variant-part",v->part,base,read,opaque);
                kzsf_observer_object(out,"body-variant-model-component",v->model_component,base,read,opaque);
                KzsfCharacterPart part;
                if(kzsf_character_part_read(v->part,base,read,opaque,kzsf_observer_baseline_type,&part)) {
                    fprintf(out,"    body_part is_skinned=%u rig_compatibility=unknown read_only=1\n",part.is_skinned);
                    kzsf_observer_object(out,"body-part-mesh",part.mesh,base,read,opaque);
                    kzsf_observer_object(out,"body-part-physics",part.physics,base,read,opaque);
                }
            }
        } else fputs("  character_inventory status=unavailable read_only=1\n",out);
    }
}
#endif
