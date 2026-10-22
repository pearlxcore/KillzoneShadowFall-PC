#ifndef KZSF_CUSTOM_MESH_H
#define KZSF_CUSTOM_MESH_H
#include "kzsf_custom_primitive.h"

/* Native layouts: constructors 5eb430/5eb500/748920, destructors
 * 5e9520/5eb5a0/739090. This assembles a NEW detached resource graph.
 * It does not create render instances, physics, or install a player part. */
typedef struct {int32_t count,pad;uintptr_t data;int32_t capacity,pad2;} KzsfCustomMeshArray;
typedef struct {
    uint32_t kind,blend_shape_mask[4],vertices;
    KzsfCustomMeshArray cpu,nbt;
} KzsfCustomPrimitiveSkin;
typedef struct {
    uintptr_t table;uint64_t references;
    KzsfCustomMeshArray primitives,blend_shapes;
} KzsfCustomSkinInfo;
typedef struct {
    uintptr_t table;uint64_t references;uintptr_t name,owner;
    float minimum[4],maximum[4];
    unsigned char state[24];
    uintptr_t skeleton,additional;
    uint32_t flags,pad;
    uintptr_t bindings,bone_bounds;
    KzsfCustomMeshArray auxiliary;
    unsigned char reserved[8];
    float scale[4],offset[4];
    uintptr_t skin;
    KzsfCustomMeshArray primitives,effects;
    unsigned char tail[8];
} KzsfCustomSkinnedMesh;
typedef struct {
    uintptr_t table;uint64_t references;uintptr_t name,mesh,bone_bounds,physics;
    unsigned char skinned,reserved[3];uint32_t flags;uintptr_t secondary_name;
} KzsfCustomModelPart;
_Static_assert(sizeof(KzsfCustomPrimitiveSkin)==0x48,"primitive skin layout");
_Static_assert(sizeof(KzsfCustomSkinInfo)==0x40,"skin resource layout");
_Static_assert(sizeof(KzsfCustomSkinnedMesh)==0x100,"skinned mesh layout");
_Static_assert(offsetof(KzsfCustomSkinnedMesh,skeleton)==0x58,"mesh skeleton offset");
_Static_assert(offsetof(KzsfCustomSkinnedMesh,skin)==0xc0,"mesh skin offset");
_Static_assert(offsetof(KzsfCustomSkinnedMesh,effects)==0xe0,"mesh effects offset");
_Static_assert(sizeof(KzsfCustomModelPart)==0x40,"model part layout");
_Static_assert(offsetof(KzsfCustomModelPart,bone_bounds)==0x20,"model part bone bounds offset");

typedef struct {
    uintptr_t primitive,effect;uint32_t vertices;
} KzsfCustomMeshSection;
typedef struct {
    KzsfCustomAllocate allocate;
    KzsfCustomConstruct mesh_construct,skin_construct,part_construct;
    KzsfCustomRetain retain;KzsfCustomRelease release;
    uintptr_t mesh_type,skin_type,part_type;
} KzsfCustomMeshApi;

static inline int kzsf_custom_mesh_render_config(KzsfCustomSkinnedMesh *mesh,const uint32_t source[6],
        const float lo[3],const float hi[3]) {
    if(!mesh || !source || !lo || !hi || source[4]!=3 || !source[2] || source[2]>16 ||
       source[1]!=0x30+0x20*source[2] || source[3]!=1 || mesh->primitives.count<1 || mesh->primitives.count>16 ||
       mesh->effects.count!=mesh->primitives.count)return 0;
    for(unsigned i=0;i<3;i++)if(!isfinite(lo[i]) || !isfinite(hi[i]) || lo[i]>hi[i])return 0;
    uint32_t state[6];memcpy(state,source,sizeof(state));
    state[1]=0x30+0x20*(uint32_t)mesh->primitives.count;state[2]=(uint32_t)mesh->primitives.count;
    memcpy(mesh->state,state,sizeof(state));mesh->flags|=1;
    for(unsigned i=0;i<3;i++){mesh->minimum[i]=lo[i];mesh->maximum[i]=hi[i];}return 1;
}

/* Inputs retain their caller references. The result owns separate references
 * to each section and native rig resource. Bounds must be appropriate to the
 * geometry before the caller considers installing it as a drawable. */
static inline uintptr_t kzsf_custom_model_part_create(uintptr_t skeleton,uintptr_t bindings,uintptr_t bone_bounds,
        const KzsfCustomMeshSection *sections,uint32_t count,KzsfCustomMeshApi api) {
    if(!skeleton || !bindings || !bone_bounds || !sections || !count || count>16 ||
       !api.allocate || !api.mesh_construct || !api.skin_construct || !api.part_construct ||
       !api.retain || !api.release || !api.mesh_type || !api.skin_type || !api.part_type)return 0;
    for(uint32_t i=0;i<count;i++)
        if(!sections[i].primitive || !sections[i].effect || !sections[i].vertices || sections[i].vertices>1000000)return 0;
    uintptr_t address=api.allocate(sizeof(KzsfCustomSkinnedMesh),16);if(!address)return 0;
    memset((void *)address,0,sizeof(KzsfCustomSkinnedMesh));api.mesh_construct(api.mesh_type,address);
    kzsf_custom_resource_acquire_new(address);
    KzsfCustomSkinnedMesh *mesh=(void *)address;
    api.retain(skeleton);mesh->skeleton=skeleton;
    api.retain(bindings);mesh->bindings=bindings;
    api.retain(bone_bounds);mesh->bone_bounds=bone_bounds;
    mesh->scale[0]=mesh->scale[1]=mesh->scale[2]=1.0f;
    uintptr_t skin_address=api.allocate(sizeof(KzsfCustomSkinInfo),8);if(!skin_address)goto failed;
    memset((void *)skin_address,0,sizeof(KzsfCustomSkinInfo));api.skin_construct(api.skin_type,skin_address);
    kzsf_custom_resource_acquire_new(skin_address);
    mesh->skin=skin_address; /* Transfer the new resource's initial reference. */
    KzsfCustomSkinInfo *skin=(void *)skin_address;
    KzsfCustomPrimitiveSkin *entries=(void *)api.allocate((size_t)count*sizeof(*entries),8);
    if(!entries)goto failed;
    memset(entries,0,(size_t)count*sizeof(*entries));
    skin->primitives=(KzsfCustomMeshArray){(int32_t)count,0,(uintptr_t)entries,(int32_t)count,0};
    for(uint32_t i=0;i<count;i++){entries[i].kind=3;entries[i].vertices=sections[i].vertices;}
    uintptr_t *primitives=(void *)api.allocate((size_t)count*sizeof(*primitives),8);if(!primitives)goto failed;
    memset(primitives,0,(size_t)count*sizeof(*primitives));
    mesh->primitives=(KzsfCustomMeshArray){0,0,(uintptr_t)primitives,(int32_t)count,0};
    for(uint32_t i=0;i<count;i++){api.retain(sections[i].primitive);primitives[i]=sections[i].primitive;++mesh->primitives.count;}
    uintptr_t *effects=(void *)api.allocate((size_t)count*sizeof(*effects),8);if(!effects)goto failed;
    memset(effects,0,(size_t)count*sizeof(*effects));
    mesh->effects=(KzsfCustomMeshArray){0,0,(uintptr_t)effects,(int32_t)count,0};
    for(uint32_t i=0;i<count;i++){api.retain(sections[i].effect);effects[i]=sections[i].effect;++mesh->effects.count;}
    uintptr_t part_address=api.allocate(sizeof(KzsfCustomModelPart),8);if(!part_address)goto failed;
    memset((void *)part_address,0,sizeof(KzsfCustomModelPart));api.part_construct(api.part_type,part_address);
    kzsf_custom_resource_acquire_new(part_address);
    KzsfCustomModelPart *part=(void *)part_address;part->mesh=address;
    /* 70b3e0 merges each skinned ModelPart's +20 bone bounds while committing
     * a native part transaction. The part and mesh each own a reference. */
    api.retain(bone_bounds);part->bone_bounds=bone_bounds;part->skinned=1;
    return part_address;
failed:
    api.release(address);return 0;
}
#endif
