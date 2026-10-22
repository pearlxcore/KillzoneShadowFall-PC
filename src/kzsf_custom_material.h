#ifndef KZSF_CUSTOM_MATERIAL_H
#define KZSF_CUSTOM_MATERIAL_H
#include "kzsf_custom_vertex_upload.h"

/* Inspected 01.81 layouts: 501b90, 515530, 523670, 503db0, 5226a0.
 * Only the base (id=0) variant is supported here. The entry+40 array holds
 * 64-byte named material variables (not GPU buffers). 503db0 releases strings
 * at +10/+28 and an optional resource at +20. Only resource-free values are
 * supported; the observed weighted body has nine such scalar variables.
 * Source inputs are host snapshots, validated by the caller. New owning
 * arrays/parameter blocks are independent; shader/texture/string references
 * are explicitly retained. The returned resource owns one reference. */
typedef struct {int32_t count,pad;uintptr_t data;int32_t capacity,pad2;} KzsfMaterialArray;
typedef struct {uintptr_t sampler,name,texture;unsigned char state[24];} KzsfMaterialTexture;
typedef struct {
    unsigned char value[16];uintptr_t name;uint64_t kind;
    uintptr_t resource,parameter;unsigned char state[16];
} KzsfMaterialVariable;
_Static_assert(sizeof(KzsfMaterialVariable)==64,"material variable layout");
_Static_assert(offsetof(KzsfMaterialVariable,resource)==32,"material variable resource offset");
typedef struct {
    unsigned char header[16];uintptr_t parameters;unsigned char state[16];
    KzsfMaterialArray textures,buffers;uintptr_t shader;
} KzsfMaterialEntry;
typedef struct {
    int32_t id;uint32_t flags;KzsfMaterialArray entries;unsigned char state[48];
    KzsfMaterialArray caches[3];
} KzsfMaterialVariant;
typedef struct {
    uintptr_t table;uint64_t references;uintptr_t name,owner;
    KzsfMaterialArray variants;unsigned char state[24];uintptr_t cache;
} KzsfMaterialResource;
_Static_assert(sizeof(KzsfMaterialTexture)==0x30,"texture binding layout");
_Static_assert(sizeof(KzsfMaterialEntry)==0x60,"material entry layout");
_Static_assert(sizeof(KzsfMaterialVariant)==0x98,"effect variant layout");
_Static_assert(sizeof(KzsfMaterialResource)==0x58,"effect resource layout");
_Static_assert(offsetof(KzsfMaterialEntry,textures)==0x28,"texture array offset");

enum {KZSF_MATERIAL_MAX_ENTRIES=27,KZSF_MATERIAL_MAX_TEXTURES=32,KZSF_MATERIAL_MAX_BYTES=1048576};
typedef struct {
    unsigned char header[16],state[16];uintptr_t shader;
    const unsigned char *parameters;size_t parameter_bytes;
    const KzsfMaterialTexture *textures;int32_t texture_count;
    const KzsfMaterialVariable *variables;int32_t variable_count;
} KzsfMaterialTemplateEntry;
typedef struct {
    unsigned char resource_state[24],variant_state[48];uint32_t variant_flags;
    int32_t count;KzsfMaterialTemplateEntry entries[KZSF_MATERIAL_MAX_ENTRIES];
} KzsfMaterialTemplate;
typedef void (KZSF_CUSTOM_ABI *KzsfMaterialRetain)(uintptr_t);
typedef struct {
    KzsfCustomAllocate allocate;KzsfCustomConstruct construct;KzsfCustomRelease release;
    KzsfMaterialRetain retain_string,retain_resource;uintptr_t type;
} KzsfMaterialApi;

static inline int kzsf_material_template_valid(const KzsfMaterialTemplate *source) {
    if(!source || source->count<1 || source->count>KZSF_MATERIAL_MAX_ENTRIES)return 0;
    size_t total=0;
    for(int32_t i=0;i<source->count;i++) {
        const KzsfMaterialTemplateEntry *e=&source->entries[i];uint64_t stored_bytes=0;
        if(!e->shader || e->texture_count<0 || e->texture_count>KZSF_MATERIAL_MAX_TEXTURES ||
           (e->texture_count && !e->textures) || !e->parameters || e->parameter_bytes<32 ||
           e->parameter_bytes>65536 || (e->parameter_bytes&15))return 0;
        memcpy(&stored_bytes,e->parameters+8,8);
        if(stored_bytes!=e->parameter_bytes)return 0;
        if(e->variable_count<0 || e->variable_count>32 || (e->variable_count && !e->variables))return 0;
        total+=e->parameter_bytes+(size_t)e->texture_count*sizeof(KzsfMaterialTexture)+(size_t)e->variable_count*sizeof(KzsfMaterialVariable);
        if(total>KZSF_MATERIAL_MAX_BYTES)return 0;
        for(int32_t j=0;j<e->texture_count;j++)
            if(!e->textures[j].sampler || !e->textures[j].name || !e->textures[j].texture)return 0;
        for(int32_t j=0;j<e->variable_count;j++)
            if(!e->variables[j].name || !e->variables[j].parameter || e->variables[j].resource)return 0;
    }
    return 1;
}
static inline uintptr_t kzsf_material_clone(const KzsfMaterialTemplate *source,KzsfMaterialApi api) {
    if(!kzsf_material_template_valid(source) || !api.allocate || !api.construct || !api.release ||
       !api.retain_string || !api.retain_resource || !api.type)return 0;
    uintptr_t address=api.allocate(sizeof(KzsfMaterialResource),8);if(!address)return 0;
    api.construct(api.type,address);
    kzsf_custom_resource_acquire_new(address);
    KzsfMaterialResource *resource=(void *)address;
    KzsfMaterialVariant *variant=(void *)api.allocate(sizeof(*variant),8);
    if(!variant)goto failed;
    memset(variant,0,sizeof(*variant));resource->variants=(KzsfMaterialArray){1,0,(uintptr_t)variant,1,0};
    memcpy(resource->state,source->resource_state,sizeof(resource->state));
    variant->flags=source->variant_flags;memcpy(variant->state,source->variant_state,sizeof(variant->state));
    KzsfMaterialEntry *entries=(void *)api.allocate((size_t)source->count*sizeof(*entries),8);
    if(!entries)goto failed;
    memset(entries,0,(size_t)source->count*sizeof(*entries));
    variant->entries=(KzsfMaterialArray){source->count,0,(uintptr_t)entries,source->count,0};
    for(int32_t i=0;i<source->count;i++) {
        const KzsfMaterialTemplateEntry *in=&source->entries[i];KzsfMaterialEntry *out=&entries[i];
        memcpy(out->header,in->header,sizeof(out->header));memcpy(out->state,in->state,sizeof(out->state));
        out->parameters=api.allocate(in->parameter_bytes,16);if(!out->parameters)goto failed;
        memcpy((void *)out->parameters,in->parameters,in->parameter_bytes);
        api.retain_resource(in->shader);out->shader=in->shader;
        if(in->variable_count) {
            KzsfMaterialVariable *variables=(void *)api.allocate((size_t)in->variable_count*sizeof(*variables),8);
            if(!variables)goto failed;
            memset(variables,0,(size_t)in->variable_count*sizeof(*variables));
            out->buffers=(KzsfMaterialArray){0,0,(uintptr_t)variables,in->variable_count,0};
            for(int32_t j=0;j<in->variable_count;j++) {
                const KzsfMaterialVariable *v=&in->variables[j];
                api.retain_string(v->name);api.retain_string(v->parameter);
                variables[j]=*v;++out->buffers.count;
            }
        }
        if(in->texture_count) {
            KzsfMaterialTexture *textures=(void *)api.allocate((size_t)in->texture_count*sizeof(*textures),8);
            if(!textures)goto failed;
            memset(textures,0,(size_t)in->texture_count*sizeof(*textures));
            out->textures=(KzsfMaterialArray){0,0,(uintptr_t)textures,in->texture_count,0};
            for(int32_t j=0;j<in->texture_count;j++) {
                const KzsfMaterialTexture *t=&in->textures[j];
                api.retain_string(t->sampler);api.retain_string(t->name);api.retain_resource(t->texture);
                textures[j]=*t;++out->textures.count;
            }
        }
    }
    return address;
failed:
    api.release(address);return 0;
}
#endif
