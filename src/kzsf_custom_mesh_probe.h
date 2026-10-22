#ifndef KZSF_CUSTOM_MESH_PROBE_H
#define KZSF_CUSTOM_MESH_PROBE_H
#include "kzsf_custom_mesh.h"

/* Direct engine callback only. Native bounds/material references are used for
 * ownership validation, not as approved CJ rendering or animation inputs. */
static int kzsf_custom_mesh_guards(KzsfCustomProbeContext *context) {
    uintptr_t base=context->base;KzsfZoomRead read=context->read;void *opaque=context->opaque;
    const struct {uint32_t rva;unsigned char bytes[16];} guards[]={
        {0x5eb430,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x0f,0x84,0xaa,0x00}},
        {0x5eb500,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x42,0x48,0x89}},
        {0x748920,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x62,0x48,0x89}},
        {0x5e9520,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xfe,0x48,0x8d,0x05,0xaf,0x33,0x04}},
        {0x5eb5a0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec}},
        {0x739090,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8d,0x05,0xc0,0x49,0xf2,0x00}}};
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char code[16];
        if(!kzsf_zoom_read(read,opaque,base,guards[i].rva,code,sizeof(code)) || memcmp(code,guards[i].bytes,sizeof(code))) {
            return 0;
        }
    }
    return 1;
}
static void kzsf_custom_mesh_probe(uintptr_t owner,uintptr_t primitive,uint32_t vertices,
        KzsfCustomProbeContext *context,KzsfCharacterType type) {
    uintptr_t base=context->base;KzsfZoomRead read=context->read;void *opaque=context->opaque;
    if(!kzsf_custom_mesh_guards(context)) {
        fputs("KILLZONE_CUSTOM_MESH_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);return;
    }
    KzsfCharacterInventory inventory;KzsfCharacterPart native;KzsfCharacterMeshTree tree;uintptr_t bounds=0;
    if(!kzsf_character_inventory_read(owner,read,opaque,&inventory) ||
       !kzsf_character_part_read(inventory.variants[inventory.current].part,base,read,opaque,type,&native) ||
       !kzsf_character_mesh_read(native.mesh,base,read,opaque,type,&tree) || !tree.count ||
       tree.leaves[0].binding_count!=98 ||
       !kzsf_zoom_read(read,opaque,tree.leaves[0].mesh,0x78,&bounds,8) ||
       type(bounds,base,read,opaque)!=base+0x3c1e810) {
        fputs("KILLZONE_CUSTOM_MESH_PROBE status=refused reason=native-rig\n",stderr);fflush(stderr);return;
    }
    KzsfMaterialTemplate *source=kzsf_custom_material_owner_snapshot(owner,base,read,opaque,type);if(!source)return;
    KzsfMaterialApi material_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ede0),
        (KzsfCustomRelease)(base+0x330f20),kzsf_material_native_retain_string,kzsf_material_native_retain_resource,base+0x3b64dd8};
    uintptr_t material=kzsf_material_clone(source,material_api);kzsf_material_snapshot_free(source);
    if(!material){fputs("KILLZONE_CUSTOM_MESH_PROBE status=failed reason=material-clone\n",stderr);fflush(stderr);return;}
    KzsfCustomMeshApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x5eb430),
        (KzsfCustomConstruct)(base+0x5eb500),(KzsfCustomConstruct)(base+0x748920),
        kzsf_material_native_retain_resource,(KzsfCustomRelease)(base+0x330f20),base+0x3c1e490,base+0x3c1e8f0,base+0x3c3d7b0};
    KzsfCustomMeshSection section={primitive,material,vertices};
    fputs("KILLZONE_CUSTOM_MESH_PROBE phase=begin attached=0 physics=0\n",stderr);fflush(stderr);
    uintptr_t part=kzsf_custom_model_part_create(tree.leaves[0].skeleton,tree.leaves[0].bindings,bounds,&section,1,api);
    int okay=0;unsigned checks=0,primitive_before=0;
    uintptr_t part_type=part ? type(part,base,read,opaque) : 0;
    if(part_type==base+0x3c3d7b0) {
        KzsfCustomModelPart *p=(void *)part;KzsfCustomSkinnedMesh *mesh=(void *)p->mesh;
        KzsfCustomSkinInfo *skin=(void *)mesh->skin;
        primitive_before=(unsigned)(((KzsfCustomPrimitive *)primitive)->reference&0xffffff);
        if(p->skinned && !p->physics && p->bone_bounds==bounds)checks|=1;
        if(mesh->table==base+0x162c8f0 && mesh->primitives.count==1 && mesh->effects.count==1)checks|=2;
        if(*(uintptr_t *)mesh->primitives.data==primitive && *(uintptr_t *)mesh->effects.data==material)checks|=4;
        if(skin->table==base+0x162cdf0 && skin->primitives.count==1 &&
           ((KzsfCustomPrimitiveSkin *)skin->primitives.data)->vertices==vertices)checks|=8;
        okay=checks==15 && primitive_before==2;
    }
    if(part)api.release(part);
    unsigned primitive_after=(unsigned)(((KzsfCustomPrimitive *)primitive)->reference&0xffffff);
    unsigned material_after=(unsigned)(((KzsfMaterialResource *)material)->references&0xffffff);
    okay=okay && primitive_after==1 && material_after==1;
    api.release(material);
    fprintf(stderr,"KILLZONE_CUSTOM_MESH_PROBE status=%s attached=0 resource_retained=0 checks=%u part_type=%llx primitive_refs=%u->%u material_refs=%u\n",
        okay ? "assembled-released" : "failed",checks,(unsigned long long)(part_type ? part_type-base : 0),
        primitive_before,primitive_after,material_after);fflush(stderr);
}
#endif
