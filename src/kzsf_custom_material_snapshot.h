#ifndef KZSF_CUSTOM_MATERIAL_SNAPSHOT_H
#define KZSF_CUSTOM_MATERIAL_SNAPSHOT_H
#include "kzsf_custom_material.h"
#include "kzsf_character_mesh.h"
#include <stdlib.h>

/* Observed ShaderResource -> shader object -> vertex stage. The stage carries
 * a 27-entry semantic-to-input map at +30. A vertex layout does not select a
 * different shader: the original rigid material has semantic17 but NO 16/18.
 * Validate the observed object tables and the whole bounded map before use. */
static inline int kzsf_material_vertex_inputs(uintptr_t shader,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,uint32_t *mask) {
    uintptr_t object=0,stage=0,table=0;int32_t inputs[27],again[27];uint32_t used=0,result=0;
    if(!mask || !type || type(shader,base,read,opaque)!=base+0x3b77a30 ||
       !kzsf_zoom_read(read,opaque,shader,0x18,&object,8) ||
       !kzsf_zoom_read(read,opaque,object,0,&table,8) || table!=base+0x14e84a0 ||
       !kzsf_zoom_read(read,opaque,object,8,&stage,8) ||
       !kzsf_zoom_read(read,opaque,stage,0,&table,8) || table!=base+0x14e04f0 ||
       !kzsf_zoom_read(read,opaque,stage,0x30,inputs,sizeof(inputs)))return 0;
    for(unsigned i=0;i<27;i++) {
        if(inputs[i]==-1)continue;
        if(inputs[i]<0 || inputs[i]>=32 || (used&(1u<<inputs[i])))return 0;
        used|=1u<<inputs[i];result|=1u<<i;
    }
    if(!(result&1) || !kzsf_zoom_read(read,opaque,stage,0x30,again,sizeof(again)) ||
       memcmp(inputs,again,sizeof(inputs)))return 0;
    *mask=result;return 1;
}

static inline void kzsf_material_snapshot_free(KzsfMaterialTemplate *snapshot) {
    if(!snapshot)return;
    for(int i=0;i<KZSF_MATERIAL_MAX_ENTRIES;i++) {
        free((void *)snapshot->entries[i].parameters);free((void *)snapshot->entries[i].textures);
        free((void *)snapshot->entries[i].variables);
    }
    free(snapshot);
}
static inline int kzsf_material_string_valid(uintptr_t address,KzsfZoomRead read,void *opaque) {
    int32_t length=0;char text[129];
    if(!kzsf_zoom_read(read,opaque,address,8,&length,4) || length<1 || length>128 ||
       !kzsf_zoom_read(read,opaque,address,16,text,(size_t)length))return 0;
    for(int32_t i=0;i<length;i++)if((unsigned char)text[i]<32 || (unsigned char)text[i]>126)return 0;
    return 1;
}
static inline KzsfMaterialTemplate *kzsf_material_snapshot(uintptr_t effect,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type) {
    KzsfMaterialResource resource,again;
    if(!type || type(effect,base,read,opaque)!=base+0x3b64dd8 ||
       !kzsf_zoom_read(read,opaque,effect,0,&resource,sizeof(resource)) ||
       resource.variants.count<1 || resource.variants.count>8 || !resource.variants.data)return NULL;
    KzsfMaterialVariant variant={0};uintptr_t variant_address=0;
    for(int32_t i=0;i<resource.variants.count;i++) {
        uintptr_t address=resource.variants.data+(size_t)i*sizeof(variant);
        if(!kzsf_zoom_read(read,opaque,address,0,&variant,sizeof(variant)))return NULL;
        if(!variant.id){variant_address=address;break;}
    }
    if(!variant_address || variant.entries.count<1 || variant.entries.count>KZSF_MATERIAL_MAX_ENTRIES ||
       !variant.entries.data)return NULL;
    KzsfMaterialEntry entries[KZSF_MATERIAL_MAX_ENTRIES],entry_again;
    if(!kzsf_zoom_read(read,opaque,variant.entries.data,0,entries,(size_t)variant.entries.count*sizeof(*entries)))return NULL;
    KzsfMaterialTemplate *result=calloc(1,sizeof(*result));if(!result)return NULL;
    memcpy(result->resource_state,resource.state,sizeof(resource.state));
    memcpy(result->variant_state,variant.state,sizeof(variant.state));
    result->variant_flags=variant.flags;result->count=variant.entries.count;size_t total=0;
    for(int32_t i=0;i<result->count;i++) {
        KzsfMaterialEntry *entry=&entries[i];KzsfMaterialTemplateEntry *out=&result->entries[i];
        uint64_t bytes=0;
        if(entry->buffers.count<0 || entry->buffers.count>32 ||
           (entry->buffers.count && !entry->buffers.data) ||
           type(entry->shader,base,read,opaque)!=base+0x3b77a30 ||
           entry->textures.count<0 || entry->textures.count>KZSF_MATERIAL_MAX_TEXTURES ||
           (entry->textures.count && !entry->textures.data) ||
           !kzsf_zoom_read(read,opaque,entry->parameters,8,&bytes,8) || bytes<32 || bytes>65536 || (bytes&15))goto failed;
        total+=(size_t)bytes+(size_t)entry->textures.count*sizeof(KzsfMaterialTexture)+(size_t)entry->buffers.count*sizeof(KzsfMaterialVariable);
        if(total>KZSF_MATERIAL_MAX_BYTES)goto failed;
        out->parameters=malloc((size_t)bytes);out->parameter_bytes=(size_t)bytes;
        if(!out->parameters || !kzsf_zoom_read(read,opaque,entry->parameters,0,(void *)out->parameters,(size_t)bytes))goto failed;
        memcpy(out->header,entry->header,sizeof(out->header));memcpy(out->state,entry->state,sizeof(out->state));out->shader=entry->shader;
        out->texture_count=entry->textures.count;
        out->variable_count=entry->buffers.count;
        if(out->variable_count) {
            size_t size=(size_t)out->variable_count*sizeof(KzsfMaterialVariable);
            KzsfMaterialVariable *variables=malloc(size);out->variables=variables;
            if(!variables || !kzsf_zoom_read(read,opaque,entry->buffers.data,0,variables,size))goto failed;
            for(int32_t j=0;j<out->variable_count;j++) {
                KzsfMaterialVariable again_variable;
                if(variables[j].resource || !kzsf_material_string_valid(variables[j].name,read,opaque) ||
                   !kzsf_material_string_valid(variables[j].parameter,read,opaque) ||
                   !kzsf_zoom_read(read,opaque,entry->buffers.data,(size_t)j*sizeof(*variables),&again_variable,sizeof(again_variable)) ||
                   memcmp(&variables[j],&again_variable,sizeof(again_variable)))goto failed;
            }
        }
        if(out->texture_count) {
            KzsfMaterialTexture *textures=malloc((size_t)out->texture_count*sizeof(*textures));out->textures=textures;
            if(!textures || !kzsf_zoom_read(read,opaque,entry->textures.data,0,textures,(size_t)out->texture_count*sizeof(*textures)))goto failed;
            for(int32_t j=0;j<out->texture_count;j++)
                if(type(textures[j].texture,base,read,opaque)!=base+0x3b764a0 ||
                   !kzsf_material_string_valid(textures[j].sampler,read,opaque) ||
                   !kzsf_material_string_valid(textures[j].name,read,opaque))goto failed;
        }
        if(!kzsf_zoom_read(read,opaque,variant.entries.data,(size_t)i*sizeof(*entries),&entry_again,sizeof(entry_again)) ||
           memcmp(entry,&entry_again,sizeof(entry_again)))goto failed;
    }
    KzsfMaterialVariant variant_again;
    if(!kzsf_zoom_read(read,opaque,variant_address,0,&variant_again,sizeof(variant_again)) ||
       memcmp(&variant,&variant_again,sizeof(variant)) ||
       !kzsf_zoom_read(read,opaque,effect,0,&again,sizeof(again)) ||
       /* Reference counts can change while the immutable inputs remain valid. */
       memcmp((char *)&resource+16,(char *)&again+16,sizeof(resource)-16) ||
       !kzsf_material_template_valid(result))goto failed;
    return result;
failed:
    kzsf_material_snapshot_free(result);return NULL;
}
#endif
