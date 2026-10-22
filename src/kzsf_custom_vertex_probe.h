#ifndef KZSF_CUSTOM_VERTEX_PROBE_H
#define KZSF_CUSTOM_VERTEX_PROBE_H
#include "kzsf_custom_vertex_upload.h"
#include "kzsf_custom_index_upload.h"
#include "kzsf_custom_material_probe.h"
#include "kzsf_character_native_query.h"
#include "kzsf_camera_engine_context.h"
#include <stdio.h>
#include <stdlib.h>
uint32_t runtime_thread_tcb_offset(void);

typedef struct {uintptr_t base;KzsfZoomRead read;void *opaque;} KzsfCustomProbeContext;
#include "kzsf_custom_texture_probe.h"
#include "kzsf_custom_primitive.h"
#include "kzsf_custom_mesh_probe.h"
static int kzsf_custom_index_inspect(uintptr_t resource,const KzsfCustomIndexLayout *layout,void *opaque) {
    KzsfCustomProbeContext *c=opaque;uintptr_t table=0,object=0,data=0;
    uint32_t count=0,wide=0;uint64_t bytes=0;
    return kzsf_zoom_read(c->read,c->opaque,resource,0,&table,8) && table==c->base+0x15f2a30 &&
        kzsf_zoom_read(c->read,c->opaque,resource,0x10,&object,8) && object &&
        kzsf_zoom_read(c->read,c->opaque,object,0x10,&wide,4) && wide==(layout->width==4) &&
        kzsf_zoom_read(c->read,c->opaque,object,0x14,&count,4) && count==layout->indices &&
        kzsf_zoom_read(c->read,c->opaque,object,0x28,&bytes,8) && (bytes&UINT64_C(0x0fffffffffffffff))==(uint64_t)count*layout->width &&
        kzsf_zoom_read(c->read,c->opaque,object,0x30,&data,8) && data;
}
static void kzsf_custom_index_probe(uint32_t vertices,uintptr_t vertex_resource,const float minimum[3],const float maximum[3],KzsfCustomProbeContext *context,uintptr_t owner,KzsfCharacterType type) {
    const char *path=getenv("KZSF_CUSTOM_INDEX_PROBE_FILE");if(!path || !*path)return;
    const unsigned char constructor[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x34,0x48,0x89};
    const unsigned char loader[16]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20,0x49};
    unsigned char code[16];uintptr_t base=context->base;
    if(!kzsf_zoom_read(context->read,context->opaque,base,0x52f140,code,16) || memcmp(code,constructor,16) ||
       !kzsf_zoom_read(context->read,context->opaque,base,0x500e90,code,16) || memcmp(code,loader,16)) {
        fputs("KILLZONE_CUSTOM_INDEX_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);return;
    }
    FILE *input=fopen(path,"rb");long length=-1;
    if(input && !fseek(input,0,SEEK_END))length=ftell(input);
    if(!input || length<32 || length>12*1024*1024+32 || fseek(input,0,SEEK_SET)) {
        if(input)fclose(input);
        fputs("KILLZONE_CUSTOM_INDEX_PROBE status=refused reason=input\n",stderr);fflush(stderr);return;
    }
    unsigned char *data=malloc((size_t)length);
    int loaded=data && fread(data,1,(size_t)length,input)==(size_t)length;fclose(input);
    KzsfCustomIndexLayout layout;
    if(!loaded || !kzsf_custom_index_layout(data,(size_t)length,vertices,&layout)) {
        free(data);fputs("KILLZONE_CUSTOM_INDEX_PROBE status=refused reason=invalid-payload\n",stderr);fflush(stderr);return;
    }
    KzsfCustomIndexApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52f140),
        (KzsfCustomLoad)(base+0x500e90),(KzsfCustomRelease)(base+0x330f20),kzsf_custom_index_inspect,base+0x3b668f8,context};
    fprintf(stderr,"KILLZONE_CUSTOM_INDEX_PROBE phase=begin indices=%u width=%u attached=0\n",layout.indices,layout.width);fflush(stderr);
    uintptr_t index_resource=kzsf_custom_index_create(data,(size_t)length,vertices,api);free(data);
    int okay=index_resource!=0;
    if(index_resource) {
        const unsigned char primitive_constructor[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x66,0x48,0x89};
        const unsigned char primitive_destructor[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8d,0x05,0x10,0x85,0x0e,0x01};
        if(kzsf_zoom_read(context->read,context->opaque,base,0x52ef10,code,16) && !memcmp(code,primitive_constructor,16) &&
           kzsf_zoom_read(context->read,context->opaque,base,0x52efa0,code,16) && !memcmp(code,primitive_destructor,16)) {
            KzsfCustomPrimitiveApi primitive_api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52ef10),
                kzsf_material_native_retain_resource,base+0x3b66c78};
            uintptr_t primitive=kzsf_custom_primitive_create(vertex_resource,index_resource,layout.indices,minimum,maximum,primitive_api);
            int valid=primitive && ((KzsfCustomPrimitive *)primitive)->table==base+0x16174d0;
            if(valid)kzsf_custom_mesh_probe(owner,primitive,vertices,context,type);
            if(primitive)api.release(primitive);
            fprintf(stderr,"KILLZONE_CUSTOM_PRIMITIVE_PROBE status=%s attached=0 buffer_refs=released\n",valid ? "created-released" : "failed");fflush(stderr);
        } else {fputs("KILLZONE_CUSTOM_PRIMITIVE_PROBE status=refused reason=code-signature\n",stderr);fflush(stderr);}
        api.release(index_resource);
    }
    fprintf(stderr,"KILLZONE_CUSTOM_INDEX_PROBE status=%s attached=0 resource_retained=0\n",okay ? "created-released" : "failed");fflush(stderr);
}
static int kzsf_custom_probe_inspect(uintptr_t resource,const KzsfCustomVertexLayout *layout,void *opaque) {
    KzsfCustomProbeContext *context=opaque;uintptr_t table=0,descriptor=0,vertex=0,data=0;
    uint32_t count=0,stride=0;uint64_t bytes=0;
    int okay=kzsf_zoom_read(context->read,context->opaque,resource,0,&table,8) && table==context->base+0x1615f60 &&
        kzsf_zoom_read(context->read,context->opaque,resource,0x10,&descriptor,8) && descriptor &&
        kzsf_zoom_read(context->read,context->opaque,descriptor,8,&count,4) && count==layout->vertices &&
        kzsf_zoom_read(context->read,context->opaque,descriptor,0x1c,&stride,4) && stride==layout->stride &&
        kzsf_zoom_read(context->read,context->opaque,descriptor,0x10,&vertex,8) && vertex &&
        /* Native allocation size shares its high nibble with allocator flags
         * (2d30b0). Compare only the size, as the index inspector does. */
        kzsf_zoom_read(context->read,context->opaque,vertex,0x78,&bytes,8) &&
        (bytes&UINT64_C(0x0fffffffffffffff))==(uint64_t)count*stride &&
        kzsf_zoom_read(context->read,context->opaque,vertex,0x80,&data,8) && data;
    if(!okay) {
        fprintf(stderr,"KILLZONE_CUSTOM_VERTEX_PROBE inspect=failed table=%llx descriptor=%llx vertex=%llx count=%u stride=%u bytes=%llu data=%llx\n",
            (unsigned long long)table,(unsigned long long)descriptor,(unsigned long long)vertex,count,stride,
            (unsigned long long)bytes,(unsigned long long)data);fflush(stderr);
    }
    return okay;
}
#include "kzsf_custom_model_probe.h"
/* Opt-in, one attempt after stable gameplay ownership; direct callback only.
 * It creates and releases a NEW resource without installing anything. */
static void kzsf_custom_vertex_probe(uintptr_t owner,uintptr_t base,KzsfZoomRead read,void *opaque,uintptr_t tcb,KzsfCharacterType type) {
    static int checked,done;static const char *path;static uintptr_t previous_owner;static unsigned stable;
    if(!checked){path=getenv("KZSF_CUSTOM_VERTEX_PROBE_FILE");checked=1;}
    if(!path || !*path || done || !owner)return;
    if(owner!=previous_owner){previous_owner=owner;stable=0;}
    if(++stable<60)return;
    KzsfCameraEngineContext engine;
    if(!kzsf_camera_engine_context(tcb,base,read,opaque,&engine) || engine.allocate_method!=base+0x4f67a0)return;
    done=1;
    struct {uint32_t rva;unsigned char bytes[19];unsigned size;} guards[]={
        /* Validate the complete rewritten instruction against this process's
         * actual TEB slot; its displacement is not fixed across launches. */
        {0x329400,{0x41,0x56,0x53,0x50,0x49,0x89,0xf6,0x48,0x89,0xfb,0x65,0x48,0x8b,0x04,0x25},19},
        {0x52f1d0,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xf3,0x48,0x85,0xdb,0x74,0x1e,0x48,0x89},16},
        {0x500d20,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x49,0x89,0xf6,0x49,0x89},16},
        {0x330f20,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0xb8,0xff,0xff,0xff,0xff,0xf0},16}};
    uint32_t tls_offset=runtime_thread_tcb_offset();
    memcpy(guards[0].bytes+15,&tls_offset,4);
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char bytes[19];
        if(!kzsf_zoom_read(read,opaque,base,guards[i].rva,bytes,guards[i].size) || memcmp(bytes,guards[i].bytes,guards[i].size)) {
            fprintf(stderr,"KILLZONE_CUSTOM_VERTEX_PROBE status=refused reason=code-signature rva=0x%x\n",guards[i].rva);fflush(stderr);return;
        }
    }
    FILE *input=fopen(path,"rb");
    if(!input){fputs("KILLZONE_CUSTOM_VERTEX_PROBE status=refused reason=input-open\n",stderr);fflush(stderr);return;}
    long length=-1;
    if(!fseek(input,0,SEEK_END))length=ftell(input);
    if(length<40 || length>32*1024*1024 || fseek(input,0,SEEK_SET)) {
        fclose(input);fputs("KILLZONE_CUSTOM_VERTEX_PROBE status=refused reason=input-size\n",stderr);fflush(stderr);return;
    }
    unsigned char *data=malloc((size_t)length);
    int loaded=data && fread(data,1,(size_t)length,input)==(size_t)length;fclose(input);
    KzsfCustomVertexLayout layout;
    if(!loaded || !kzsf_custom_vertex_layout(data,(size_t)length,&layout)) {
        free(data);fputs("KILLZONE_CUSTOM_VERTEX_PROBE status=refused reason=invalid-payload\n",stderr);fflush(stderr);return;
    }
    KzsfCustomProbeContext context={base,read,opaque};
    KzsfCustomVertexApi api={(KzsfCustomAllocate)(base+0x329400),(KzsfCustomConstruct)(base+0x52f1d0),
        (KzsfCustomLoad)(base+0x500d20),(KzsfCustomRelease)(base+0x330f20),kzsf_custom_probe_inspect,base+0x3b66818,&context};
    fprintf(stderr,"KILLZONE_CUSTOM_VERTEX_PROBE phase=begin vertices=%u stride=%u attached=0\n",layout.vertices,layout.stride);fflush(stderr);
    KzsfCustomUploadResult result;
    uintptr_t resource=kzsf_custom_vertex_create(data,(size_t)length,api,&result);
    int okay=resource!=0;
    float minimum[3]={INFINITY,INFINITY,INFINITY},maximum[3]={-INFINITY,-INFINITY,-INFINITY};
    for(uint32_t i=0;i<layout.vertices;i++)for(unsigned j=0;j<3;j++) {
        float value;memcpy(&value,data+layout.data_offset+(size_t)i*layout.stride+j*4,4);
        if(value<minimum[j])minimum[j]=value;
        if(value>maximum[j])maximum[j]=value;
    }
    free(data);
    if(okay) {
        kzsf_custom_index_probe(layout.vertices,resource,minimum,maximum,&context,owner,type);
        api.release(resource);
    }
    fprintf(stderr,"KILLZONE_CUSTOM_VERTEX_PROBE status=%s attached=0 resource_retained=0 allocated=%d archive_error=%d consumed=%zu expected=%ld inspected=%d\n",
        okay ? "created-released" : "failed",result.allocated,result.archive_error,result.consumed,length,result.inspected);fflush(stderr);
    /* Independent diagnostic; a failed vertex inspection has already released
     * its resource and does not make this material reference the vertex. */
    kzsf_custom_material_probe(owner,base,read,opaque,type);
    kzsf_custom_texture_probe(&context,owner,type);
    kzsf_custom_model_probe(owner,&context,type);
}
#endif
