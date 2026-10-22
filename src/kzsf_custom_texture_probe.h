#ifndef KZSF_CUSTOM_TEXTURE_PROBE_H
#define KZSF_CUSTOM_TEXTURE_PROBE_H
#include "kzsf_custom_texture.h"
#include <stdio.h>
#include <stdlib.h>
uint64_t runtime_memory_clamp(uintptr_t,uint64_t);
static int kzsf_custom_texture_inspect(uintptr_t texture,uintptr_t pixels,const KzsfCustomTextureLayout *layout,void *opaque) {
    KzsfCustomProbeContext *c=opaque;unsigned char object[0xc0];uint32_t dims[2],pitch;uint64_t srd[4],address;
    if(!kzsf_zoom_read(c->read,c->opaque,texture,0,object,sizeof(object)))return 0;
    memcpy(dims,object+0x18,8);memcpy(&pitch,object+0x8c,4);memcpy(srd,object+0x90,32);memcpy(&address,object+0x30,8);
    unsigned tile=(unsigned)(srd[1]>>52)&31;
    return dims[0]==layout->width && dims[1]==layout->height && pitch==layout->width*4 && address==pixels &&
        ((srd[0]&UINT64_C(0x3fffffffff))<<8)==pixels && ((srd[0]>>52)&63)==10 && ((srd[0]>>58)&15)==0 &&
        (srd[1]&0x3fff)==layout->width-1 && ((srd[1]>>14)&0x3fff)==layout->height-1 && (tile==8 || tile==31);
}
static int kzsf_custom_owned_texture_copy(uintptr_t image,const unsigned char *data,const KzsfCustomTextureLayout *layout,void *opaque) {
    KzsfCustomProbeContext *context=opaque;uintptr_t pixels=0;uint32_t flags=0;
    if(!kzsf_zoom_read(context->read,context->opaque,image,0x30,&pixels,8) || !pixels ||
       !kzsf_zoom_read(context->read,context->opaque,image,8,&flags,4) || ((flags>>23)&3)!=2 ||
       !kzsf_custom_texture_inspect(image,pixels,layout,context) ||
       runtime_memory_clamp(pixels,layout->bytes)!=layout->bytes)return 0;
    SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),(void *)pixels,data,layout->bytes,&written) && written==layout->bytes;
}
static void kzsf_custom_owned_texture_probe(const unsigned char *data,size_t length,KzsfCustomProbeContext *context,
        uintptr_t owner,KzsfCharacterType type) {
    const char *enabled=getenv("KZSF_CUSTOM_OWNED_TEXTURE_PROBE");if(!enabled || strcmp(enabled,"1"))return;
    const unsigned char guard[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x3c,0x48,0x89};
    unsigned char code[16];uintptr_t base=context->base;
    if(!kzsf_zoom_read(context->read,context->opaque,base,0x598b10,code,16) || memcmp(code,guard,16)) {
        fputs("KILLZONE_CUSTOM_OWNED_TEXTURE_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);return;
    }
    KzsfMaterialTemplate *source=kzsf_custom_material_owner_snapshot(owner,base,context->read,context->opaque,type);
    if(!source)return;
    KzsfCustomOwnedTextureApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomTextureConstruct)(base+0x2cea70),
        (KzsfCustomTextureCreate)(base+0x2d0db0),(KzsfCustomRelease)(base+0x2ceb70),
        (KzsfCustomConstruct)(base+0x598b10),(KzsfCustomRelease)(base+0x330f20),kzsf_custom_owned_texture_copy,base+0x3b764a0,context};
    fputs("KILLZONE_CUSTOM_OWNED_TEXTURE_PROBE phase=begin attached=0 engine_owned_pixels=1\n",stderr);fflush(stderr);
    uintptr_t texture=kzsf_custom_texture_resource(data,length,api);int replacements=0,okay=0;
    if(texture && type(texture,base,context->read,context->opaque)==base+0x3b764a0) {
        for(int32_t i=0;i<source->count;i++) {
            KzsfMaterialTexture *bindings=(void *)source->entries[i].textures;
            for(int32_t j=0;j<source->entries[i].texture_count;j++) {
                int32_t n=0;char name[128]={0};
                if(kzsf_zoom_read(context->read,context->opaque,bindings[j].name,8,&n,4) && n>0 && n<128 &&
                   kzsf_zoom_read(context->read,context->opaque,bindings[j].name,16,name,(size_t)n) &&
                   !strcmp(name,"inSampler_texturesetColorAlpha")) {bindings[j].texture=texture;++replacements;}
            }
        }
        KzsfMaterialApi material={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ede0),
            (KzsfCustomRelease)(base+0x330f20),kzsf_material_native_retain_string,kzsf_material_native_retain_resource,base+0x3b64dd8};
        uintptr_t clone=replacements ? kzsf_material_clone(source,material) : 0;
        okay=clone && kzsf_material_clone_inspect(clone,source,base);
        if(clone)material.release(clone);
    }
    if(texture)api.release_resource(texture);
    kzsf_material_snapshot_free(source);
    fprintf(stderr,"KILLZONE_CUSTOM_OWNED_TEXTURE_PROBE status=%s bindings=%d attached=0 resource_retained=0 native_image_release=engine\n",
        okay ? "bound-released" : "failed",replacements);fflush(stderr);
}
static void kzsf_custom_texture_probe(KzsfCustomProbeContext *context,uintptr_t owner,KzsfCharacterType type) {
    const char *path=getenv("KZSF_CUSTOM_TEXTURE_PROBE_FILE");if(!path || !*path)return;
    const struct {uint32_t rva;unsigned char code[14];unsigned size;} guards[]={
        {0x2cea70,{0x48,0x8d,0x05,0x69,0x24,0x21,0x01,0x48,0x83,0xc0,0x10,0x48,0x89,0x07},14},
        {0x2d0db0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53},13},
        {0x2ceb70,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xe8,0x12,0,0,0},14}};
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char code[14];
        if(!kzsf_zoom_read(context->read,context->opaque,context->base,guards[i].rva,code,guards[i].size) ||
           memcmp(code,guards[i].code,guards[i].size)) {
            fputs("KILLZONE_CUSTOM_TEXTURE_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);return;
        }
    }
    FILE *input=fopen(path,"rb");long length=-1;
    if(input && !fseek(input,0,SEEK_END))length=ftell(input);
    if(!input || length<16 || length>64*1024*1024+16 || fseek(input,0,SEEK_SET)) {
        if(input)fclose(input);
        fputs("KILLZONE_CUSTOM_TEXTURE_PROBE status=refused reason=input\n",stderr);fflush(stderr);return;
    }
    unsigned char *data=malloc((size_t)length);int loaded=data && fread(data,1,(size_t)length,input)==(size_t)length;fclose(input);
    KzsfCustomTextureLayout layout;
    if(!loaded || !kzsf_custom_texture_layout(data,(size_t)length,&layout)) {
        free(data);fputs("KILLZONE_CUSTOM_TEXTURE_PROBE status=refused reason=invalid-payload\n",stderr);fflush(stderr);return;
    }
    uintptr_t base=context->base;
    KzsfCustomTextureApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomTextureConstruct)(base+0x2cea70),
        (KzsfCustomTextureCreate)(base+0x2d0db0),(KzsfCustomRelease)(base+0x2ceb70),
        runtime_memory_map_host_pixels,runtime_memory_unmap_host_pixels,kzsf_custom_texture_inspect,context};
    fprintf(stderr,"KILLZONE_CUSTOM_TEXTURE_PROBE phase=begin width=%u height=%u attached=0\n",layout.width,layout.height);fflush(stderr);
    int okay=kzsf_custom_texture_test(data,(size_t)length,api);
    fprintf(stderr,"KILLZONE_CUSTOM_TEXTURE_PROBE status=%s attached=0 resource_retained=0\n",okay ? "created-released" : "failed");fflush(stderr);
    if(okay)kzsf_custom_owned_texture_probe(data,(size_t)length,context,owner,type);
    free(data);
}
#endif
