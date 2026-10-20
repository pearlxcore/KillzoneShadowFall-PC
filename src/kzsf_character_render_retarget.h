#ifndef KZSF_CHARACTER_RENDER_RETARGET_H
#define KZSF_CHARACTER_RENDER_RETARGET_H
#include "kzsf_character_retarget.h"
#include "kzsf_character_swap.h"
/* Host identity tokens only; pointers are never dereferenced from this state.
 * Node/model/part/skeleton pointers are reacquired from the native caller. */
static uintptr_t kzsf_character_render_owner,kzsf_character_render_part;
static uintptr_t kzsf_character_render_head_part,kzsf_character_render_added_head;
/* Restoration can leave a103 render pose alive after the Engineer part is
 * removed. Keep draw-only recovery separate from the active F7 selection. */
static uintptr_t kzsf_character_restore_owner,kzsf_character_restore_model,kzsf_character_restore_part;
static int32_t kzsf_character_render_slot=-1;
static unsigned kzsf_character_render_sequence;
static int kzsf_character_render_installed;
static int kzsf_character_model_kind;
static inline void kzsf_character_render_publish(uintptr_t owner,uintptr_t part,int32_t slot,
        uintptr_t restore_owner,uintptr_t restore_model,uintptr_t restore_part) {
    __atomic_add_fetch(&kzsf_character_render_sequence,1,__ATOMIC_ACQ_REL);
    __atomic_store_n(&kzsf_character_render_owner,owner,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_render_part,part,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_render_head_part,0,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_render_added_head,0,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_render_slot,slot,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_model_kind,0,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_restore_owner,restore_owner,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_restore_model,restore_model,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_restore_part,restore_part,__ATOMIC_RELAXED);
    __atomic_add_fetch(&kzsf_character_render_sequence,1,__ATOMIC_RELEASE);
}
static inline void kzsf_character_render_select(uintptr_t owner,uintptr_t part,int32_t slot) {
    kzsf_character_render_publish(owner,part,slot,0,0,0);
}
static inline void kzsf_character_render_restore(uintptr_t owner,uintptr_t model,uintptr_t engineer_part) {
    if(!owner || !model || !engineer_part){kzsf_character_render_select(0,0,-1);return;}
    if(__atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_ACQUIRE)==owner &&
       __atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED)==model) {
        uintptr_t previous=__atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED);
        if(previous)engineer_part=previous;
    }
    kzsf_character_render_publish(0,0,-1,owner,model,engineer_part);
}
static inline void kzsf_character_render_select_compatible(uintptr_t owner,uintptr_t model,uintptr_t part,int slot) {
    uintptr_t recovery=0;
    if(__atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_ACQUIRE)==owner &&
       __atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED)==model)
        recovery=__atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED);
    kzsf_character_render_publish(owner,part,slot,recovery ? owner : 0,recovery ? model : 0,recovery);
}
static inline void kzsf_character_render_head_select(uintptr_t target_pose_head,uintptr_t added_head) {
    __atomic_add_fetch(&kzsf_character_render_sequence,1,__ATOMIC_ACQ_REL);
    __atomic_store_n(&kzsf_character_render_head_part,target_pose_head,__ATOMIC_RELAXED);
    __atomic_store_n(&kzsf_character_render_added_head,added_head,__ATOMIC_RELAXED);
    __atomic_add_fetch(&kzsf_character_render_sequence,1,__ATOMIC_RELEASE);
}
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterPoseAssign)(uintptr_t,uintptr_t);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterPoseApply)(uintptr_t,uintptr_t,const KzsfCharacterNativeMap *);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterPoseCopy)(uintptr_t,uintptr_t,unsigned,unsigned);
typedef struct {uintptr_t model,part,source,current,previous,source_skeleton;} KzsfCharacterRenderContext;
static inline int kzsf_character_render_context(uintptr_t node,uintptr_t selected_owner,uintptr_t selected_part,
        int32_t slot,KzsfZoomRead read,void *opaque,KzsfCharacterRenderContext *out) {
    KzsfCharacterRenderContext r={0};uintptr_t owner=0,owned_model=0,array=0;int32_t count=0,index=0,source_index=0;
    if(!out || !selected_owner || !selected_part || slot<0 || slot>=128 ||
       !kzsf_zoom_read(read,opaque,node,0x18,&r.model,8) ||
       !kzsf_zoom_read(read,opaque,r.model,0x38,&owner,8) || owner!=selected_owner ||
       !kzsf_zoom_read(read,opaque,owner,0x200,&owned_model,8) || owned_model!=r.model ||
       !kzsf_zoom_read(read,opaque,r.model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(read,opaque,r.model,0x60,&array,8) ||
       !kzsf_zoom_read(read,opaque,node,0x270,&index,4) || (unsigned)index>1 ||
       !kzsf_zoom_read(read,opaque,r.model,0x260,&source_index,4) || (unsigned)source_index>1)return 0;
    int matches=0;
    for(int32_t i=0;i<count;i++) {
        uintptr_t part=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0xc8,&part,8))return 0;
        if(part==selected_part){r.part=part;matches++;}
    }
    if(matches<1)return 0;
    r.source=r.model+0x180+(size_t)source_index*0x70;
    r.current=node+0x190+(size_t)index*0x70;r.previous=node+0x190+(size_t)(index^1)*0x70;
    if(!kzsf_zoom_read(read,opaque,r.source,0x20,&r.source_skeleton,8))return 0;
    *out=r;return 1;
}
static inline void kzsf_character_render_pair(const KzsfCharacterRenderContext *context,uintptr_t target,
        const KzsfCharacterNativeMap *map,KzsfCharacterPoseAssign assign,KzsfCharacterPoseApply apply,KzsfCharacterPoseCopy copy) {
    assign(context->current,target);
    apply(context->current,context->source,map);
    assign(context->previous,target);
    copy(context->previous,context->current,7,0);
}
static inline int kzsf_character_render_map_prepare(const KzsfCharacterNativeMap *map,uintptr_t source,
        KzsfZoomRead read,void *opaque,KzsfCharacterJointMap joints[103],int32_t channels[4],KzsfCharacterNativeMap *view) {
    uintptr_t world=0,world_end=0,scalars=0,scalars_end=0;
    if(!map || !view || map->joints.count!=103 || map->joints.capacity<103 || !map->joints.data ||
       map->channels.count!=4 || map->channels.capacity<4 || !map->channels.data ||
       !kzsf_zoom_read(read,opaque,source,0x40,&world,8) || !world ||
       !kzsf_zoom_read(read,opaque,source,0x48,&world_end,8) || world_end<world || world_end-world<98*64 ||
       !kzsf_zoom_read(read,opaque,source,0x50,&scalars,8) ||
       !kzsf_zoom_read(read,opaque,source,0x58,&scalars_end,8) || scalars_end<scalars ||
       (scalars_end-scalars)%4 || scalars_end-scalars>4096*4 ||
       !kzsf_zoom_read(read,opaque,map->joints.data,0,joints,103*8) ||
       !kzsf_zoom_read(read,opaque,map->channels.data,0,channels,4*4))return 0;
    unsigned char seen[98]={0};int matched=0,missing=0;
    for(int i=0;i<103;i++) {
        int32_t index=joints[i].source;
        if(index==-1){joints[i].parent_differs=0;missing++;continue;}
        if(index<0 || index>=98 || seen[index])return 0;
        seen[index]=1;matched++;
        /* Render adapter uses native world->local reconstruction for every
         * matched joint. It therefore needs no source local-pose allocation. */
        joints[i].parent_differs=1;
    }
    if(matched!=98 || missing!=5)return 0;
    for(int i=0;i<4;i++)if(channels[i]<-1 || (channels[i]>=0 &&
            (!scalars || (uintptr_t)channels[i]>=(scalars_end-scalars)/4)))return 0;
    *view=*map;view->joints.data=(uintptr_t)joints;view->channels.data=(uintptr_t)channels;return 1;
}
static inline int kzsf_character_pose_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    static const unsigned char apply[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xd8,0,0,0};
    static const unsigned char assign[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x49,0x89,0xf6,0x49,0x89,0xfc,0x4d,0x85,0xf6};
    static const unsigned char copy[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x41,0x89,0xd7,0x48,0x89,0xf3,0x49,0x89,0xfe};
    unsigned char code[20];
    return kzsf_character_query_guards(base,read,opaque) &&
        kzsf_zoom_read(read,opaque,base,0x5d2ae0,code,sizeof(apply)) && !memcmp(code,apply,sizeof(apply)) &&
        kzsf_zoom_read(read,opaque,base,0x5d1d10,code,sizeof(assign)) && !memcmp(code,assign,sizeof(assign)) &&
        kzsf_zoom_read(read,opaque,base,0x5d2fb0,code,sizeof(copy)) && !memcmp(code,copy,sizeof(copy));
}
/* First adapter scope: the captured adult player -> Engineer, exactly98
 * named player joints and five unmatched scarf/antenna joints. Keep all other
 * rigs gated until the visual adapter is exercised. No shared resource writes. */
static inline int kzsf_character_engineer_compatible(uintptr_t original,uintptr_t candidate,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,const char **why) {
    KzsfCharacterPart a,b;KzsfCharacterMeshTree x,y;
    if(why)*why="engineer-part-layout";
    if(!kzsf_character_pose_guards(base,read,opaque) ||
       !kzsf_character_part_read(original,base,read,opaque,type,&a) || !a.is_skinned || a.physics ||
       !kzsf_character_part_read(candidate,base,read,opaque,type,&b) || !b.is_skinned || b.physics ||
       !kzsf_character_mesh_read(a.mesh,base,read,opaque,type,&x) || !x.count || x.unknown_count ||
       !kzsf_character_mesh_read(b.mesh,base,read,opaque,type,&y) || !y.count || y.unknown_count)return 0;
    for(int pass=0;pass<2;pass++) {
        KzsfCharacterMeshTree *tree=pass ? &y : &x;int expected=pass ? 103 : 98;
        for(int32_t i=0;i<tree->count;i++) {
            KzsfCharacterMeshLeaf *leaf=&tree->leaves[i];int32_t joints=0;
            if(why)*why="engineer-rig-layout";
            if(leaf->skeleton!=tree->leaves[0].skeleton || leaf->binding_count!=expected ||
               !kzsf_zoom_read(read,opaque,leaf->skeleton,0x30,&joints,4) || joints!=expected)return 0;
            uintptr_t matrices=0;
            if(!kzsf_zoom_read(read,opaque,leaf->bindings,0x30,&matrices,8))return 0;
            for(int32_t j=0;j<expected;j++) {
                float matrix[16];
                if(!kzsf_zoom_read(read,opaque,matrices,(size_t)j*64,matrix,64))return 0;
                for(unsigned k=0;k<16;k++)if(!isfinite(matrix[k]))return 0;
            }
        }
    }
    KzsfCharacterRetargetAudit audit;
    KzsfCharacterMapApi api={(KzsfCharacterBuildMap)(base+0x5d8dd0),(KzsfCharacterQueryFree)(base+0x3295d0)};
    if(!kzsf_character_retarget_audit(x.leaves[0].skeleton,y.leaves[0].skeleton,base,read,opaque,api,&audit,why))return 0;
    if(why)*why="engineer-map-coverage";
    if(audit.source_count!=98 || audit.count!=103 || audit.matched!=98 || audit.missing!=5 || audit.parent_changes)return 0;
    unsigned char seen[98]={0};
    for(int32_t i=0;i<audit.count;i++)if(audit.joints[i].source>=0) {
        int32_t source=audit.joints[i].source;
        if(seen[source])return 0;seen[source]=1;
    }
    if(why)*why="engineer-native-map-experimental";return 1;
}
#endif
