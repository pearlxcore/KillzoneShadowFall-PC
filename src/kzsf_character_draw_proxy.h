#ifndef KZSF_CHARACTER_DRAW_PROXY_H
#define KZSF_CHARACTER_DRAW_PROXY_H
#include "kzsf_character_native_query.h"
#include "kzsf_character_mesh.h"
/* Complete703e10 reads only node00..7f and calls only virtual48/50 on the
 * node. All other calls receive external members/renderer state, not this
 * proxy. Copy that read-only prefix and override those two pose accessors.
 * Never install a vtable on a native object or copy/retain an owning object. */
typedef struct {uintptr_t fields[16],current,previous;} KzsfCharacterDrawProxy;
enum { KZSF_CHARACTER_MAX_DRAW_PARTS=512 };
typedef struct {uintptr_t tagged,resource,mesh;int selected,combined;} KzsfCharacterDrawPart;
/* Every native record can prepare inputs needed by a later render job.
 * Retain ALL records, including original bodies and repeated mesh identities.
 * Hide duplicates through native visibility transactions, never by omitting
 * their native processing here (revision12/14 and generic fall regression). */
static inline int kzsf_character_draw_native_batch(const KzsfCharacterDrawPart *parts,int32_t count,
        uintptr_t original,uintptr_t selected,uintptr_t tags[KZSF_CHARACTER_MAX_DRAW_PARTS],int32_t *visible) {
    if(!parts || !tags || !visible || count<0 || count>KZSF_CHARACTER_MAX_DRAW_PARTS || !original || !selected)return 0;
    int32_t n=0;
    for(int32_t i=0;i<count;i++) {
        tags[n++]=parts[i].tagged;
    }
    *visible=n;return 1;
}
/* 705380 sets drawRecord+00 to its model part (stride f0). The resource
 * belongs to that part at c8. Validate the backlink against the owned model's
 * live array before choosing which pose a record is allowed to consume. */
static inline int kzsf_character_draw_parts(uintptr_t node,uintptr_t model,uintptr_t selected,uintptr_t target_skeleton,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterDrawPart parts[KZSF_CHARACTER_MAX_DRAW_PARTS],int32_t *out_count) {
    uintptr_t records=0,array=0;int32_t count=0,model_count=0;
    if(!parts || !out_count || !selected ||
       !kzsf_zoom_read(read,opaque,node,0x18,&records,8) || records!=model ||
       !kzsf_zoom_read(read,opaque,model,0x58,&model_count,4) || model_count<=0 || model_count>128 ||
       !kzsf_zoom_read(read,opaque,model,0x60,&array,8) || !array ||
       !kzsf_zoom_read(read,opaque,node,0x70,&count,4) || count<0 || count>KZSF_CHARACTER_MAX_DRAW_PARTS ||
       !kzsf_zoom_read(read,opaque,node,0x78,&records,8) || (count && !records))return 0;
    uintptr_t primary=0,secondary=0;int32_t primary_count=0,secondary_count=0;
    if(!kzsf_zoom_read(read,opaque,node,0x40,&primary_count,4) || primary_count<0 || primary_count>KZSF_CHARACTER_MAX_DRAW_PARTS ||
       !kzsf_zoom_read(read,opaque,node,0x48,&primary,8) || (primary_count && !primary) ||
       !kzsf_zoom_read(read,opaque,node,0x58,&secondary_count,4) || secondary_count<0 || secondary_count>KZSF_CHARACTER_MAX_DRAW_PARTS ||
       !kzsf_zoom_read(read,opaque,node,0x60,&secondary,8) || (secondary_count && !secondary))return 0;
    KzsfCharacterDrawPart result[KZSF_CHARACTER_MAX_DRAW_PARTS]={{0}};
    for(int32_t i=0;i<count;i++) {
        uintptr_t entry=0;
        if(!kzsf_zoom_read(read,opaque,records,(size_t)i*8,&result[i].tagged,8))return 0;
        uintptr_t record=result[i].tagged&~(uintptr_t)3;
        int is_primary=record>=primary && primary_count && (record-primary)%0xb0==0 && (record-primary)/0xb0<(uintptr_t)primary_count;
        int is_secondary=record>=secondary && secondary_count && (record-secondary)%0xb0==0 && (record-secondary)/0xb0<(uintptr_t)secondary_count;
        if(!is_primary && !is_secondary)return 0;
        if(!kzsf_zoom_read(read,opaque,record,0x88,&result[i].mesh,8) || !result[i].mesh)return 0;
        if(is_primary) {
            if(!kzsf_zoom_read(read,opaque,record,0,&entry,8) || entry<array || (entry-array)%0xf0 ||
               (entry-array)/0xf0>=(uintptr_t)model_count ||
               !kzsf_zoom_read(read,opaque,entry,0xc8,&result[i].resource,8) || !result[i].resource)return 0;
            result[i].selected=result[i].resource==selected && (result[i].tagged&2)!=0;
        } else {
            /* 705f30 creates combined records with a null model-part backlink.
             * 706c10 appends them to the same draw list. Classify their mesh
             * directly rather than treating every skinned record as Engineer. */
            result[i].combined=1;
            if(result[i].tagged&2) {
                KzsfCharacterMeshTree tree;
                if(kzsf_character_mesh_read(result[i].mesh,base,read,opaque,type,&tree) && tree.count && !tree.unknown_count) {
                    int all_target=1;
                    for(int32_t j=0;j<tree.count;j++)if(tree.leaves[j].skeleton!=target_skeleton)all_target=0;
                    result[i].selected=all_target;
                }
            }
        }
    }
    memcpy(parts,result,(size_t)count*sizeof(*parts));*out_count=count;return 1;
}
static uintptr_t KZSF_CHARACTER_ABI kzsf_character_proxy_current(uintptr_t p) {
    return ((const KzsfCharacterDrawProxy *)p)->current;
}
static uintptr_t KZSF_CHARACTER_ABI kzsf_character_proxy_previous(uintptr_t p) {
    return ((const KzsfCharacterDrawProxy *)p)->previous;
}
static inline int kzsf_character_draw_proxy(uintptr_t node,uintptr_t current,uintptr_t previous,
        KzsfZoomRead read,void *opaque,KzsfCharacterDrawProxy *out,uintptr_t table[11]) {
    KzsfCharacterDrawProxy p;
    if(!out || !table || !current || !previous || !kzsf_zoom_read(read,opaque,node,0,p.fields,128) ||
       !kzsf_zoom_read(read,opaque,p.fields[0],0,table,11*8))return 0;
    table[9]=(uintptr_t)kzsf_character_proxy_current;table[10]=(uintptr_t)kzsf_character_proxy_previous;
    p.fields[0]=(uintptr_t)table;p.current=current;p.previous=previous;*out=p;return 1;
}
#endif
