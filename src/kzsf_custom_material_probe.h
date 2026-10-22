#ifndef KZSF_CUSTOM_MATERIAL_PROBE_H
#define KZSF_CUSTOM_MATERIAL_PROBE_H
#include "kzsf_custom_material_snapshot.h"
#include <stdio.h>

static void KZSF_CUSTOM_ABI kzsf_material_native_retain_string(uintptr_t string) {
    __atomic_fetch_add((int32_t *)string,1,__ATOMIC_SEQ_CST);
}
static void KZSF_CUSTOM_ABI kzsf_material_native_retain_resource(uintptr_t resource) {
    __atomic_fetch_add((int32_t *)(resource+8),1,__ATOMIC_SEQ_CST);
}
static int kzsf_material_clone_inspect(uintptr_t address,const KzsfMaterialTemplate *source,uintptr_t base) {
    const KzsfMaterialResource *r=(void *)address;
    if(r->table!=base+0x15f3670 || r->owner || r->cache || r->variants.count!=1 || !r->variants.data)return 0;
    const KzsfMaterialVariant *v=(void *)r->variants.data;
    if(v->id || v->entries.count!=source->count || !v->entries.data)return 0;
    const KzsfMaterialEntry *entries=(void *)v->entries.data;
    for(int32_t i=0;i<source->count;i++) {
        const KzsfMaterialTemplateEntry *in=&source->entries[i];const KzsfMaterialEntry *out=&entries[i];
        if(out->shader!=in->shader || !out->parameters || out->parameters==(uintptr_t)in->parameters ||
           memcmp((void *)out->parameters,in->parameters,in->parameter_bytes) ||
           out->textures.count!=in->texture_count || out->buffers.count!=in->variable_count)return 0;
        if(in->variable_count && (!out->buffers.data || out->buffers.data==(uintptr_t)in->variables ||
           memcmp((void *)out->buffers.data,in->variables,(size_t)in->variable_count*sizeof(*in->variables))))return 0;
        if(in->texture_count && (!out->textures.data || out->textures.data==(uintptr_t)in->textures ||
           memcmp((void *)out->textures.data,in->textures,(size_t)in->texture_count*sizeof(*in->textures))))return 0;
    }
    return 1;
}
static int kzsf_custom_material_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    const unsigned char constructor[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x79,0x48,0x89};
    const unsigned char destructor[14]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
    unsigned char code[16];
    if(!kzsf_zoom_read(read,opaque,base,0x52ede0,code,16) || memcmp(code,constructor,16) ||
       !kzsf_zoom_read(read,opaque,base,0x5226a0,code,14) || memcmp(code,destructor,14)) {
        fputs("KILLZONE_CUSTOM_MATERIAL_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);return 0;
    }
    return 1;
}
static KzsfMaterialTemplate *kzsf_custom_material_owner_snapshot(uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    if(!kzsf_custom_material_guards(base,read,opaque))return NULL;
    KzsfCharacterInventory inventory;KzsfCharacterPart part;KzsfCharacterMeshTree tree;uintptr_t effect_array=0,effect=0;int32_t effect_count=0;
    if(!kzsf_character_inventory_read(owner,read,opaque,&inventory) ||
       !kzsf_character_part_read(inventory.variants[inventory.current].part,base,read,opaque,type,&part) ||
       !kzsf_character_mesh_read(part.mesh,base,read,opaque,type,&tree) || !tree.count ||
       !kzsf_zoom_read(read,opaque,tree.leaves[0].mesh,0xe0,&effect_count,4) || effect_count<1 || effect_count>64 ||
       !kzsf_zoom_read(read,opaque,tree.leaves[0].mesh,0xe8,&effect_array,8) ||
       !kzsf_zoom_read(read,opaque,effect_array,0,&effect,8)) {
        fputs("KILLZONE_CUSTOM_MATERIAL_PROBE status=refused reason=source-material\n",stderr);fflush(stderr);return NULL;
    }
    KzsfMaterialTemplate *source=kzsf_material_snapshot(effect,base,read,opaque,type);
    if(!source){fputs("KILLZONE_CUSTOM_MATERIAL_PROBE status=refused reason=source-layout\n",stderr);fflush(stderr);return NULL;}
    return source;
}
/* Diagnostic full-weight material path. Select by shader inputs, never by a
 * captured pointer or leaf ordinal. Auxiliary native material textures remain
 * retained by each clone until their parameter/texture roles are characterized. */
static KzsfMaterialTemplate *kzsf_custom_material_weighted_snapshot(const KzsfCharacterMeshTree *tree,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    if(!tree || tree->count<1 || tree->count>KZSF_CHARACTER_MAX_MESHES || !kzsf_custom_material_guards(base,read,opaque))return NULL;
    const uint32_t required=(1u<<0)|(1u<<1)|(1u<<4)|(1u<<6)|(15u<<16);
    uintptr_t seen[256];unsigned seen_count=0;
    for(int32_t i=0;i<tree->count;i++) {
        const KzsfCharacterMeshLeaf *leaf=&tree->leaves[i];
        if(leaf->skeleton!=tree->leaves[0].skeleton || leaf->bindings!=tree->leaves[0].bindings)continue;
        int32_t count=0;uintptr_t array=0;
        if(!kzsf_zoom_read(read,opaque,leaf->mesh,0xe0,&count,4) || count<1 || count>64 ||
           !kzsf_zoom_read(read,opaque,leaf->mesh,0xe8,&array,8))return NULL;
        for(int32_t j=0;j<count;j++) {
            uintptr_t effect=0;if(!kzsf_zoom_read(read,opaque,array,(size_t)j*8,&effect,8))return NULL;
            unsigned k=0;for(;k<seen_count;k++)if(seen[k]==effect)break;
            if(k<seen_count)continue;
            if(seen_count==256)return NULL;seen[seen_count++]=effect;
            KzsfMaterialTemplate *source=kzsf_material_snapshot(effect,base,read,opaque,type);
            if(!source)continue;
            uint32_t mask=0;
            if(kzsf_material_vertex_inputs(source->entries[0].shader,base,read,opaque,type,&mask) && mask==required) {
                int color=0;
                for(int32_t e=0;e<source->count;e++)for(int32_t t=0;t<source->entries[e].texture_count;t++) {
                    uintptr_t name=source->entries[e].textures[t].name;int32_t length=0;char text[129]={0};
                    if(kzsf_zoom_read(read,opaque,name,8,&length,4) && length>0 && length<=128 &&
                       kzsf_zoom_read(read,opaque,name,16,text,(size_t)length) &&
                       (!strcmp(text,"inSampler_texturesetColor") || !strcmp(text,"inSampler_texturesetColorAlpha")))color=1;
                }
                if(color) {
                    fprintf(stderr,"KILLZONE_CUSTOM_MATERIAL_PROBE status=weighted-source mesh=%llx effect=%llx vertex_inputs=%08x auxiliary_textures=native\n",
                        (unsigned long long)leaf->mesh,(unsigned long long)effect,mask);fflush(stderr);return source;
                }
            }
            kzsf_material_snapshot_free(source);
        }
    }
    return NULL;
}
static void kzsf_custom_material_probe(uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    const char *enabled=getenv("KZSF_CUSTOM_MATERIAL_PROBE");if(!enabled || strcmp(enabled,"1"))return;
    KzsfMaterialTemplate *source=kzsf_custom_material_owner_snapshot(owner,base,read,opaque,type);if(!source)return;
    KzsfMaterialApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ede0),
        (KzsfCustomRelease)(base+0x330f20),kzsf_material_native_retain_string,kzsf_material_native_retain_resource,base+0x3b64dd8};
    fprintf(stderr,"KILLZONE_CUSTOM_MATERIAL_PROBE phase=begin entries=%d attached=0\n",source->count);fflush(stderr);
    uintptr_t clone=kzsf_material_clone(source,api);
    int okay=clone && kzsf_material_clone_inspect(clone,source,base);
    if(clone)api.release(clone);
    kzsf_material_snapshot_free(source);
    fprintf(stderr,"KILLZONE_CUSTOM_MATERIAL_PROBE status=%s attached=0 resource_retained=0\n",okay ? "created-released" : "failed");fflush(stderr);
}
#endif
