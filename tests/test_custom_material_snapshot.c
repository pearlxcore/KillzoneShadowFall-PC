#undef NDEBUG
#include "../src/kzsf_custom_material_snapshot.h"
#include <assert.h>
#include <stdio.h>

static KzsfMaterialResource effect;
static KzsfMaterialVariant variant;
static KzsfMaterialEntry entry;
static KzsfMaterialTexture texture;
static unsigned char parameters[32],sampler[32],name[64];
static unsigned char shader_resource[32],shader_object[96],vertex_stage[160];
static KzsfMaterialVariable variable;
static int variant_reads,mutate;
static int read_memory(void *opaque,uintptr_t address,void *out,size_t size) {
    (void)opaque;
    const struct {void *p;size_t size;} ranges[]={
        {&effect,sizeof(effect)},{&variant,sizeof(variant)},{&entry,sizeof(entry)},
        {&texture,sizeof(texture)},{parameters,sizeof(parameters)},{sampler,sizeof(sampler)},{name,sizeof(name)},
        {shader_resource,sizeof(shader_resource)},{shader_object,sizeof(shader_object)},{vertex_stage,sizeof(vertex_stage)},
        {&variable,sizeof(variable)}};
    for(unsigned i=0;i<sizeof(ranges)/sizeof(*ranges);i++) {
        uintptr_t start=(uintptr_t)ranges[i].p;
        if(address>=start && address-start<=ranges[i].size && size<=ranges[i].size-(address-start)) {
            memcpy(out,(void *)address,size);
            if(address==(uintptr_t)&variant && ++variant_reads>1 && mutate)((KzsfMaterialVariant *)out)->flags^=1;
            return 1;
        }
    }
    return 0;
}
static uintptr_t type(uintptr_t object,uintptr_t base,KzsfZoomRead read,void *opaque) {
    (void)read;(void)opaque;
    return object==(uintptr_t)&effect ? base+0x3b64dd8 : (object==0x11 || object==(uintptr_t)shader_resource) ? base+0x3b77a30 : object==0x22 ? base+0x3b764a0 : 0;
}
int main(void) {
    const uintptr_t base=0x800000000;
    uintptr_t pointer=(uintptr_t)shader_object;memcpy(shader_resource+0x18,&pointer,8);
    pointer=base+0x14e84a0;memcpy(shader_object,&pointer,8);
    pointer=(uintptr_t)vertex_stage;memcpy(shader_object+8,&pointer,8);
    pointer=base+0x14e04f0;memcpy(vertex_stage,&pointer,8);
    int32_t inputs[27];for(unsigned i=0;i<27;i++)inputs[i]=-1;
    inputs[0]=0;inputs[1]=2;inputs[4]=1;inputs[6]=4;inputs[17]=3;
    memcpy(vertex_stage+0x30,inputs,sizeof(inputs));uint32_t mask=0;
    assert(kzsf_material_vertex_inputs((uintptr_t)shader_resource,base,read_memory,NULL,type,&mask));
    assert(mask==((1u<<0)|(1u<<1)|(1u<<4)|(1u<<6)|(1u<<17)) && !(mask&(1u<<16)));
    inputs[6]=7;inputs[16]=3;inputs[17]=4;inputs[18]=5;inputs[19]=6;
    memcpy(vertex_stage+0x30,inputs,sizeof(inputs));
    assert(kzsf_material_vertex_inputs((uintptr_t)shader_resource,base,read_memory,NULL,type,&mask));
    assert(mask==((1u<<0)|(1u<<1)|(1u<<4)|(1u<<6)|(15u<<16)));
    inputs[19]=5;memcpy(vertex_stage+0x30,inputs,sizeof(inputs));
    assert(!kzsf_material_vertex_inputs((uintptr_t)shader_resource,base,read_memory,NULL,type,&mask));
    inputs[19]=32;memcpy(vertex_stage+0x30,inputs,sizeof(inputs));
    assert(!kzsf_material_vertex_inputs((uintptr_t)shader_resource,base,read_memory,NULL,type,&mask));
    inputs[19]=6;memcpy(vertex_stage+0x30,inputs,sizeof(inputs));vertex_stage[0]^=1;
    assert(!kzsf_material_vertex_inputs((uintptr_t)shader_resource,base,read_memory,NULL,type,&mask));
    vertex_stage[0]^=1;
    effect.variants=(KzsfMaterialArray){1,0,(uintptr_t)&variant,1,0};
    variant.entries=(KzsfMaterialArray){1,0,(uintptr_t)&entry,1,0};
    entry.shader=0x11;entry.parameters=(uintptr_t)parameters;
    uint64_t bytes=32;memcpy(parameters+8,&bytes,8);
    int32_t length=8;memcpy(sampler+8,&length,4);memcpy(sampler+16,"Sampler0",8);
    length=15;memcpy(name+8,&length,4);memcpy(name+16,"inSampler_color",15);
    texture=(KzsfMaterialTexture){(uintptr_t)sampler,(uintptr_t)name,0x22,{0}};
    entry.textures=(KzsfMaterialArray){1,0,(uintptr_t)&texture,1,0};
    KzsfMaterialTemplate *s=kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type);
    assert(s && s->count==1 && s->entries[0].textures!=(void *)&texture && s->entries[0].parameters!=parameters);
    kzsf_material_snapshot_free(s);
    entry.buffers.count=1;assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));entry.buffers.count=0;
    variable.name=(uintptr_t)name;variable.parameter=(uintptr_t)sampler;variable.kind=1;
    entry.buffers=(KzsfMaterialArray){1,0,(uintptr_t)&variable,1,0};
    s=kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type);
    assert(s && s->entries[0].variable_count==1 && s->entries[0].variables!=&variable);
    assert(!memcmp(s->entries[0].variables,&variable,sizeof(variable)));kzsf_material_snapshot_free(s);
    variable.resource=0x22;assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));variable.resource=0;
    entry.buffers.count=33;assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));
    entry.buffers=(KzsfMaterialArray){0};
    effect.variants.count=9;assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));effect.variants.count=1;
    bytes=65552;memcpy(parameters+8,&bytes,8);assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));
    bytes=32;memcpy(parameters+8,&bytes,8);sampler[16]=1;
    assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));sampler[16]='S';
    variant_reads=0;mutate=1;assert(!kzsf_material_snapshot((uintptr_t)&effect,base,read_memory,NULL,type));
    puts("Material snapshot rejection tests passed");return 0;
}
