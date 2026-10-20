#ifndef KZSF_CHARACTER_FACE_H
#define KZSF_CHARACTER_FACE_H
#include "kzsf_character_head.h"
#include "kzsf_camera_call_bridge.h"
#include "kzsf_character_rig_audit.h"

/* b59240 creates/registers a separate FacialExpressionAnimator on SoldierRep.
 * BodyVariant+18 is the resource; +08 is only the body. The live Engineer
 * face uses its own 143-joint rig. Never feed it the 103-joint body pose. */
typedef struct {uintptr_t owner,rep,face,resource;} KzsfCharacterFace;
/* Resolve constructor animation libraries from the selected character while
 * leaving the actual Soldier and every shared resource unchanged. */
static inline uintptr_t kzsf_character_face_animation_source(uintptr_t owner,uintptr_t face,
        uintptr_t selected_owner,uintptr_t selected_part,uintptr_t selected_soldier,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    uintptr_t original=0,rep=0,back=0,resource=0,array=0;int32_t count=0;
    if(!kzsf_zoom_read(read,opaque,owner,0x1d0,&original,8))return 0;
    if(owner!=selected_owner || !selected_part ||
       type(selected_soldier,base,read,opaque)!=base+0x3caac10 ||
       !kzsf_zoom_read(read,opaque,face,0x20,&rep,8) ||
       !kzsf_zoom_read(read,opaque,rep,0x198,&back,8) || back!=owner ||
       !kzsf_zoom_read(read,opaque,face,0x18,&resource,8) ||
       type(resource,base,read,opaque)!=base+0x3cdfb20 ||
       !kzsf_zoom_read(read,opaque,selected_soldier,0x648,&count,4) || count<1 || count>64 ||
       !kzsf_zoom_read(read,opaque,selected_soldier,0x650,&array,8) || !array)return original;
    for(int i=0;i<count;i++) {
        uintptr_t part=0,candidate=0;
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+8,&part,8) ||
           !kzsf_zoom_read(read,opaque,array,(size_t)i*0xf0+0x18,&candidate,8))return original;
        if(part==selected_part && candidate==resource)return selected_soldier;
    }
    return original;
}
/* BodyVariant+60 is the animation skeleton used by the prepared face map,
 * not necessarily a skeleton present in the body's skinned meshes. Preserve
 * all native corrections; only translate named indices into render storage. */
static inline int kzsf_character_face_reindex(uintptr_t variant,uintptr_t actual,
        KzsfZoomRead read,void *opaque,unsigned char prepared[0xf0],int32_t mapped[512]) {
    uintptr_t expected=0,indices=0;int32_t count=0,expected_count=0,actual_count=0;
    if(!prepared || !mapped || !actual || !kzsf_zoom_read(read,opaque,variant,0,prepared,0xf0))return 0;
    memcpy(&expected,prepared+0x60,8);memcpy(&count,prepared+0xb8,4);memcpy(&indices,prepared+0xc0,8);
    if(!expected || !indices || count<1 || count>512 ||
       !kzsf_zoom_read(read,opaque,expected,0x30,&expected_count,4) || expected_count<1 || expected_count>512 ||
       !kzsf_zoom_read(read,opaque,actual,0x30,&actual_count,4) || actual_count<1 || actual_count>512 ||
       !kzsf_zoom_read(read,opaque,indices,0,mapped,(size_t)count*4))return 0;
    for(int i=0;i<count;i++) {
        int source=mapped[i],match=-1;char name[129];
        if(source==-1)continue;
        if(source<0 || source>=expected_count || !kzsf_character_joint_name(expected,source,read,opaque,name))return 0;
        for(int j=0;j<actual_count;j++) {
            char candidate[129];
            if(!kzsf_character_joint_name(actual,j,read,opaque,candidate))return 0;
            if(!strcmp(name,candidate)){if(match>=0)return 0;match=j;}
        }
        if(match<0)return 0;
        mapped[i]=match;
    }
    uintptr_t address=(uintptr_t)mapped;
    memcpy(prepared+0x60,&actual,8);memcpy(prepared+0xc0,&address,8);return 1;
}
static const unsigned char kzsf_character_face_offsets_guard[]={
    0x48,0x8b,0x43,0x20,0x48,0x8b,0x80,0x98,0x01,0,0,
    0x48,0x63,0xb0,0x8c,0x09,0,0,0x48,0x69,0xf6,0xf0,0,0,0,
    0x48,0x8b,0x80,0xd0,0x01,0,0,0x48,0x8b,0x80,0x50,0x06,0,0,
    0x4c,0x8d,0x8c,0x30,0xc0,0,0,0};
/* e465cc has live custom-offset count(rcx), local pose(rdx), face resource
 * (r10), plus SIMD inputs xmm3/8/9/10. Preserve all across the SysV callback.
 * Three pushes plus72 bytes of scratch preserve16-byte call alignment. */
static inline int kzsf_character_face_offsets_bridge(unsigned char *stub,size_t capacity,uintptr_t address,
        unsigned char patch[47],uintptr_t base,uintptr_t callback) {
    unsigned char code[]={
        0x51,0x52,0x41,0x52,0x48,0x83,0xec,0x48,
        0xf3,0x0f,0x7f,0x1c,0x24,
        0xf3,0x44,0x0f,0x7f,0x44,0x24,0x10,
        0xf3,0x44,0x0f,0x7f,0x4c,0x24,0x20,
        0xf3,0x44,0x0f,0x7f,0x54,0x24,0x30,
        0x48,0x89,0xdf,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,
        0xf3,0x0f,0x6f,0x1c,0x24,
        0xf3,0x44,0x0f,0x6f,0x44,0x24,0x10,
        0xf3,0x44,0x0f,0x6f,0x4c,0x24,0x20,
        0xf3,0x44,0x0f,0x6f,0x54,0x24,0x30,
        0x48,0x83,0xc4,0x48,0x41,0x5a,0x5a,0x59,
        0x48,0x85,0xc0,0x0f,0x84,0,0,0,0,
        0x4c,0x8d,0x88,0xc0,0,0,0,0xe9,0,0,0,0};
    if(!stub || !patch || !callback || capacity<sizeof(code))return 0;
    int32_t entry=0,skip=0,back=0;
    if(!kzsf_camera_relative(base+0xe465cc+5,address,&entry) ||
       !kzsf_camera_relative(address+92,base+0xe4680c,&skip) ||
       !kzsf_camera_relative(address+sizeof(code),base+0xe465fb,&back))return 0;
    memcpy(code+39,&callback,8);memcpy(code+88,&skip,4);memcpy(code+100,&back,4);
    memcpy(stub,code,sizeof(code));memset(patch,0x90,47);patch[0]=0xe9;memcpy(patch+1,&entry,4);return 1;
}
typedef uintptr_t (KZSF_CHARACTER_ABI *KzsfCharacterFaceCreate)(uintptr_t,uintptr_t);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterFacePair)(uintptr_t,uintptr_t);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterFaceSingle)(uintptr_t);
typedef int (*KzsfCharacterFaceWrite)(void *,uintptr_t,const void *,size_t);
typedef struct {
    KzsfCharacterFaceCreate create;
    KzsfCharacterFacePair attach,detach;
    KzsfCharacterFaceSingle destroy,dirty;
    KzsfCharacterFaceWrite write;
} KzsfCharacterFaceApi;

/* e46830 consumes BodyVariant's prepared face-to-body map at b8/c0 and
 * packed bind corrections at d0/d8. A player variant without a face has
 * neither. Validate the companion variant before creating its animator. */
static inline int kzsf_character_face_mapping(uintptr_t variant,uintptr_t resource,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    uintptr_t face=0,skeleton=0,source=0,indices=0,matrices=0;
    int32_t count=0,bones=0,source_bones=0,matrix_count=0,matched=0;
    if(!kzsf_zoom_read(read,opaque,variant,0x18,&face,8) || face!=resource ||
       !kzsf_zoom_read(read,opaque,resource,0x38,&skeleton,8) ||
       type(skeleton,base,read,opaque)!=base+0x3c1ec70 ||
       !kzsf_zoom_read(read,opaque,skeleton,0x30,&bones,4) || bones<1 || bones>512 ||
       !kzsf_zoom_read(read,opaque,variant,0x60,&source,8) ||
       type(source,base,read,opaque)!=base+0x3c1ec70 ||
       !kzsf_zoom_read(read,opaque,source,0x30,&source_bones,4) || source_bones<1 || source_bones>512 ||
       !kzsf_zoom_read(read,opaque,variant,0xb8,&count,4) || count!=bones ||
       !kzsf_zoom_read(read,opaque,variant,0xc0,&indices,8) || !indices ||
       !kzsf_zoom_read(read,opaque,variant,0xd0,&matrix_count,4) || matrix_count<1 || matrix_count>bones ||
       !kzsf_zoom_read(read,opaque,variant,0xd8,&matrices,8) || !matrices)return 0;
    for(int i=0;i<count;i++) {
        int32_t joint=0;
        if(!kzsf_zoom_read(read,opaque,indices,(size_t)i*4,&joint,4) || joint<-1 || joint>=source_bones)return 0;
        matched+=joint>=0;
    }
    /* e46830 advances its correction cursor once per mapped joint. Live
     * Security Trooper/Sinclair resources contain two identical 3-matrix
     * groups for 3 mapped joints. Extra prepared matrices are valid; require
     * the consumed prefix to exist rather than equal array counts. */
    if(!matched || matched>matrix_count)return 0;
    for(int i=0;i<matched;i++) {
        float matrix[16];
        if(!kzsf_zoom_read(read,opaque,matrices,(size_t)i*64,matrix,64))return 0;
        for(int j=0;j<16;j++)if(!isfinite(matrix[j]))return 0;
    }
    return 1;
}
static const unsigned char kzsf_character_face_mapping_guard[]={
    0x48,0x8b,0x40,0x20,0x48,0x8b,0x88,0x98,0x01,0,0,
    0x48,0x63,0x81,0x8c,0x09,0,0,0x48,0x8b,0x89,0xd0,0x01,0,0,
    0x48,0x8b,0x89,0x50,0x06,0,0,0x48,0x69,0xc0,0xf0,0,0,0,
    0x4c,0x8d,0x84,0x01,0xd8,0,0,0,0x4c,0x8d,0x8c,0x01,0xc0,0,0,0};
static inline int kzsf_character_face_mapping_patch(unsigned char out[55],uintptr_t callback) {
    if(!out || !callback)return 0;
    /* SysV stack was aligned here. Save the live r11 body-world pointer and
     * preserve alignment across the callback. All other live values reside
     * in callee-saved registers/stack. The following loop reloads its SIMDs. */
    unsigned char code[55]={0x41,0x53,0x48,0x83,0xec,8,0x48,0x89,0xc7,
        0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,8,0x41,0x5b,
        0x48,0x85,0xc0,0x0f,0x84,0,0,0,0,
        0x4c,0x8d,0x80,0xd8,0,0,0,0x4c,0x8d,0x88,0xc0,0,0,0,
        0x90,0x90,0x90,0x90,0x90};
    int32_t skip=0xe46d7c-(0xe46a4d+36);
    memcpy(code+11,&callback,8);memcpy(code+32,&skip,4);memcpy(out,code,sizeof(code));return 1;
}

static const unsigned char kzsf_character_face_lookup_guard[]={
    0x48,0x8b,0x83,0x98,0x01,0,0,0x48,0x63,0x88,0x8c,0x09,0,0,
    0x48,0x69,0xc9,0xf0,0,0,0,0x48,0x8b,0x80,0xd0,0x01,0,0,
    0x48,0x8b,0x80,0x50,0x06,0,0,0x4c,0x8b,0x7c,0x08,0x18};
static inline int kzsf_character_face_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    static const unsigned char create[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xf6,0x49,0x89,0xff};
    static const unsigned char pair[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
    static const unsigned char destroy[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xe8,0x42,0,0,0};
    static const unsigned char dirty[]={0x48,0x89,0xf8,0x48,0x8d,0x0d,0x46,0x01,0x5e,0x03};
    const struct {uintptr_t at;const unsigned char *bytes;size_t count;} guards[]={
        {0xe49120,create,sizeof(create)},{0x655560,pair,sizeof(pair)},
        {0x63d560,pair,sizeof(pair)},{0xe451c0,destroy,sizeof(destroy)},
        {0x568f70,dirty,sizeof(dirty)}};
    unsigned char bytes[16];
    for(unsigned i=0;i<sizeof(guards)/sizeof(guards[0]);i++)
        if(!kzsf_zoom_read(read,opaque,base,guards[i].at,bytes,guards[i].count) ||
           memcmp(bytes,guards[i].bytes,guards[i].count))return 0;
    return 1;
}
static inline int kzsf_character_face_rep(uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque,
        KzsfCharacterType type,uintptr_t *rep) {
    uintptr_t back=0;
    return type(owner,base,read,opaque)==base+0x3ca98b0 &&
        kzsf_zoom_read(read,opaque,owner,0x1d8,rep,8) &&
        type(*rep,base,read,opaque)==base+0x3caab30 &&
        kzsf_zoom_read(read,opaque,*rep,0x198,&back,8) && back==owner;
}
static inline int kzsf_character_face_resource_variant(uintptr_t soldier,uintptr_t body,int variant_index,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,uintptr_t *resource) {
    KzsfCharacterInventory inventory;uintptr_t selected=0;
    if(!kzsf_character_resource_variants_read(soldier,base,read,opaque,type,&inventory))return 0;
    if(variant_index < -1 || variant_index>=inventory.count)return 0;
    for(int i=0;i<inventory.count;i++)if((variant_index<0 || i==variant_index) && inventory.variants[i].part==body) {
        uintptr_t face=0;
        if(!kzsf_zoom_read(read,opaque,inventory.array,(size_t)i*0xf0+0x18,&face,8) ||
           type(face,base,read,opaque)!=base+0x3cdfb20 || (selected && selected!=face) ||
           !kzsf_character_face_mapping(inventory.array+(size_t)i*0xf0,face,base,read,opaque,type))return 0;
        selected=face;
    }
    uintptr_t mesh=0,skeleton=0;KzsfCharacterMeshTree tree;
    if(!selected || !kzsf_zoom_read(read,opaque,selected,0x30,&mesh,8) ||
       !kzsf_zoom_read(read,opaque,selected,0x38,&skeleton,8) ||
       type(skeleton,base,read,opaque)!=base+0x3c1ec70 ||
       !kzsf_character_mesh_read(mesh,base,read,opaque,type,&tree) || !tree.count || tree.unknown_count)return 0;
    for(int i=0;i<tree.count;i++)if(tree.leaves[i].skeleton!=skeleton)return 0;
    *resource=selected;return 1;
}
static inline int kzsf_character_face_resource(uintptr_t soldier,uintptr_t body,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,uintptr_t *resource) {
    return kzsf_character_face_resource_variant(soldier,body,-1,base,read,opaque,type,resource);
}
static inline int kzsf_character_face_dirty(uintptr_t rep,KzsfZoomRead read,void *opaque,KzsfCharacterFaceApi api) {
    const size_t offsets[]={0x190,0x1e1};const unsigned char masks[]={0x10,8};
    for(unsigned i=0;i<2;i++) {
        unsigned char flags=0;
        if(!kzsf_zoom_read(read,opaque,rep,offsets[i],&flags,1))return 0;
        flags|=masks[i];if(!api.write(opaque,rep+offsets[i],&flags,1))return 0;
    }
    api.dirty(rep+0x40);return 1;
}
static inline int kzsf_character_face_enable(KzsfCharacterFace *state,uintptr_t owner,uintptr_t resource,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterFaceApi api) {
    uintptr_t rep=0,current=0,player_resource=0,fallback=0;
    if(!state || !api.create || !api.attach || !api.detach || !api.destroy || !api.dirty || !api.write ||
       !kzsf_character_face_rep(owner,base,read,opaque,type,&rep) ||
       type(resource,base,read,opaque)!=base+0x3cdfb20 ||
       !kzsf_zoom_read(read,opaque,rep,0x600,&current,8))return 0;
    if(current)return current==state->face && state->rep==rep && state->resource==resource;
    /* Constructor can fall back to the player's facial animation library. */
    if(!kzsf_zoom_read(read,opaque,owner,0x1d0,&player_resource,8) ||
       type(player_resource,base,read,opaque)!=base+0x3caac10 ||
       !kzsf_zoom_read(read,opaque,player_resource,0x6f0,&fallback,8) ||
       type(fallback,base,read,opaque)!=base+0x3cdfa40)return 0;
    uintptr_t face=api.create(resource,rep);
    if(!face)return 0;
    if(type(face,base,read,opaque)!=base+0x3cdf670) {api.destroy(face);return 0;}
    if(!api.write(opaque,rep+0x600,&face,8)){api.destroy(face);return 0;}
    *state=(KzsfCharacterFace){owner,rep,face,resource};
    api.attach(rep+8,face);
    return kzsf_character_face_dirty(rep,read,opaque,api);
}
static inline int kzsf_character_face_disable(KzsfCharacterFace *state,uintptr_t owner,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterFaceApi api) {
    if(!state || !state->face)return 1;
    uintptr_t rep=0,current=0;
    if(!kzsf_character_face_rep(owner,base,read,opaque,type,&rep))return 0;
    if(state->owner!=owner || state->rep!=rep){*state=(KzsfCharacterFace){0};return 1;}
    if(!kzsf_zoom_read(read,opaque,rep,0x600,&current,8))return 0;
    /* Chapter/body rebuilds may already have destroyed our animator. */
    if(current!=state->face){*state=(KzsfCharacterFace){0};return 1;}
    uintptr_t resource=0;
    if(type(current,base,read,opaque)!=base+0x3cdf670 ||
       !kzsf_zoom_read(read,opaque,current,0x18,&resource,8) || resource!=state->resource)return 0;
    api.detach(rep+8,current);
    uintptr_t zero=0;
    if(!api.write(opaque,rep+0x600,&zero,8))return 0;
    api.destroy(current);*state=(KzsfCharacterFace){0};
    return kzsf_character_face_dirty(rep,read,opaque,api);
}
#endif
