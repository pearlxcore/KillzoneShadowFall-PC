#undef NDEBUG
#include "../src/kzsf_custom_material.h"
#include "../src/kzsf_custom_material_maps.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned calls,fail_at,outstanding,refs[8];
static uintptr_t KZSF_CUSTOM_ABI allocate(size_t bytes,size_t alignment) {
    assert(alignment==8 || alignment==16);
    if(++calls==fail_at)return 0;
    void *p=calloc(1,bytes);assert(p);++outstanding;return (uintptr_t)p;
}
static void discard(uintptr_t p){if(p){assert(outstanding);--outstanding;free((void *)p);}}
static void KZSF_CUSTOM_ABI retain(uintptr_t p){assert(p>0 && p<8);++refs[p];}
static void unref(uintptr_t p){if(p){assert(p<8 && refs[p]>1);--refs[p];}}
static void KZSF_CUSTOM_ABI construct(uintptr_t type,uintptr_t address) {
    assert(type==42);KzsfMaterialResource *r=(void *)address;memset(r,0,sizeof(*r));
}
static void KZSF_CUSTOM_ABI release(uintptr_t address) {
    KzsfMaterialResource *r=(void *)address;
    assert(r->references==1);
    assert(!r->owner && !r->cache);
    KzsfMaterialVariant *variants=(void *)r->variants.data;
    for(int i=0;i<r->variants.count;i++) {
        KzsfMaterialEntry *entries=(void *)variants[i].entries.data;
        for(int j=0;j<variants[i].entries.count;j++) {
            KzsfMaterialEntry *e=&entries[j];
            KzsfMaterialVariable *variables=(void *)e->buffers.data;
            for(int k=0;k<e->buffers.count;k++) {
                assert(!variables[k].resource);unref(variables[k].name);unref(variables[k].parameter);
            }
            discard(e->buffers.data);
            discard(e->parameters);unref(e->shader);
            KzsfMaterialTexture *textures=(void *)e->textures.data;
            for(int k=0;k<e->textures.count;k++) {
                unref(textures[k].sampler);unref(textures[k].name);unref(textures[k].texture);
            }
            discard(e->textures.data);
        }
        discard(variants[i].entries.data);
    }
    discard(r->variants.data);discard(address);
}
static void balanced(void){assert(!outstanding);for(unsigned i=1;i<8;i++)assert(refs[i]==1);}

int main(void) {
    assert(kzsf_custom_material_map_role("inSampler_texturesetColor")==KZSF_MAP_COLOR);
    assert(kzsf_custom_material_map_role("inSampler_detailset_RNormal")==KZSF_MAP_NORMAL);
    assert(kzsf_custom_material_map_role("inSampler_texturesetMaskReflectance")==KZSF_MAP_MASK);
    assert(kzsf_custom_material_map_role("inSampler_texturesetRoughnessMask_AlphaAO")==KZSF_MAP_ROUGHNESS);
    assert(kzsf_custom_material_map_role("inSampler_dust_set1Color")==KZSF_MAP_WHITE);
    assert(kzsf_custom_material_map_role("inSampler_lit_sphere")==KZSF_MAP_LIGHTING);
    assert(kzsf_custom_material_map_role("inSampler_unknown")==KZSF_MAP_UNKNOWN);
    assert(kzsf_custom_material_neutral_rgba[KZSF_MAP_ROUGHNESS-KZSF_MAP_NORMAL][3]==255);
    assert(kzsf_custom_material_neutral_rgba[KZSF_MAP_ROUGHNESS-KZSF_MAP_NORMAL][1]==0);
    assert(kzsf_custom_material_neutral_rgba[KZSF_MAP_MASK-KZSF_MAP_NORMAL][3]==0);
    assert(kzsf_custom_material_neutral_rgba[KZSF_MAP_ROUGHNESS-KZSF_MAP_NORMAL][2]==255);
    for(unsigned i=1;i<8;i++)refs[i]=1;
    unsigned char params[32]={0};uint64_t size=sizeof(params);memcpy(params+8,&size,8);
    KzsfMaterialTexture textures[2]={{4,5,2,{0}},{4,6,3,{0}}};
    KzsfMaterialTemplate source={0};source.count=2;
    for(int i=0;i<2;i++)source.entries[i]=(KzsfMaterialTemplateEntry){.shader=1,.parameters=params,
        .parameter_bytes=sizeof(params),.textures=textures,.texture_count=2};
    KzsfMaterialApi api={allocate,construct,release,retain,retain,42};
    for(unsigned failure=1;failure<=7;failure++) {
        calls=0;fail_at=failure;assert(!kzsf_material_clone(&source,api));balanced();
    }
    calls=fail_at=0;uintptr_t clone=kzsf_material_clone(&source,api);assert(clone && calls==7);
    KzsfMaterialResource *r=(void *)clone;KzsfMaterialVariant *v=(void *)r->variants.data;
    KzsfMaterialEntry *e=(void *)v->entries.data;
    assert(e[0].parameters!=e[1].parameters && e[0].parameters!=(uintptr_t)params);
    assert(e[0].textures.data!=e[1].textures.data && e[0].textures.data!=(uintptr_t)textures);
    ((unsigned char *)e[0].parameters)[0]=99;assert(params[0]==0);
    ((KzsfMaterialTexture *)e[0].textures.data)[0].state[0]=99;assert(textures[0].state[0]==0);
    release(clone);balanced();
    source.entries[1].texture_count=33;calls=0;assert(!kzsf_material_clone(&source,api) && !calls);
    source.entries[1].texture_count=2;source.entries[1].parameter_bytes=31;
    assert(!kzsf_material_clone(&source,api) && !calls);balanced();
    source.entries[1].parameter_bytes=32;
    KzsfMaterialVariable variable={.name=4,.kind=1,.parameter=5};
    source.entries[0].variables=&variable;source.entries[0].variable_count=1;
    for(unsigned failure=1;failure<=8;failure++) {
        calls=0;fail_at=failure;assert(!kzsf_material_clone(&source,api));balanced();
    }
    calls=fail_at=0;clone=kzsf_material_clone(&source,api);assert(clone && calls==8);
    r=(void *)clone;v=(void *)r->variants.data;e=(void *)v->entries.data;
    assert(e[0].buffers.count==1 && e[0].buffers.data!=(uintptr_t)&variable);
    ((KzsfMaterialVariable *)e[0].buffers.data)->value[0]=42;assert(variable.value[0]==0);
    release(clone);balanced();
    variable.resource=2;calls=0;assert(!kzsf_material_clone(&source,api) && !calls);balanced();
    puts("Independent material ownership tests passed");return 0;
}
