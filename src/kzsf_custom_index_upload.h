#ifndef KZSF_CUSTOM_INDEX_UPLOAD_H
#define KZSF_CUSTOM_INDEX_UPLOAD_H
#include "kzsf_custom_vertex_upload.h"

typedef struct {uint32_t indices,width,vertices;} KzsfCustomIndexLayout;
static inline int kzsf_custom_index_layout(const unsigned char *data,size_t size,uint32_t vertices,
                                           KzsfCustomIndexLayout *out) {
    uint32_t head[3],bytes;
    if(!data || !out || size<32 || size>12u*1024u*1024u+32 || !vertices || vertices>1000000)return 0;
    memcpy(head,data,12);memcpy(&bytes,data+28,4);
    if(!head[0] || head[0]>3000000 || head[0]%3 || head[1]!=0x91 || head[2]>1)return 0;
    uint32_t width=head[2] ? 4 : 2;
    if((size_t)head[0]*width!=size-32 || bytes!=size-32)return 0;
    for(uint32_t i=0;i<head[0];i++) {
        uint32_t value=0;memcpy(&value,data+32+(size_t)i*width,width);
        if(value>=vertices)return 0;
    }
    *out=(KzsfCustomIndexLayout){head[0],width,vertices};return 1;
}
typedef int (*KzsfCustomIndexInspect)(uintptr_t,const KzsfCustomIndexLayout *,void *);
typedef struct {
    KzsfCustomAllocate allocate;KzsfCustomConstruct construct;KzsfCustomLoad load;
    KzsfCustomRelease release;KzsfCustomIndexInspect inspect;uintptr_t type;void *opaque;
} KzsfCustomIndexApi;
static inline uintptr_t kzsf_custom_index_create(const unsigned char *data,size_t size,uint32_t vertices,
                                                KzsfCustomIndexApi api) {
    KzsfCustomIndexLayout layout;
    if(!kzsf_custom_index_layout(data,size,vertices,&layout) || !api.allocate || !api.construct ||
       !api.load || !api.release || !api.inspect || !api.type)return 0;
    uintptr_t table[9]={0};
    table[3]=(uintptr_t)kzsf_custom_archive_read;
    table[6]=(uintptr_t)kzsf_custom_archive_skip;
    table[8]=(uintptr_t)kzsf_custom_archive_position;
    KzsfCustomArchive archive={(uintptr_t)table,0,0,0,0,data,size,0};
    uintptr_t context[2]={0,(uintptr_t)&archive};
    uintptr_t resource=api.allocate(40,8);
    if(!resource)return 0;
    api.construct(api.type,resource);kzsf_custom_resource_acquire_new(resource);api.load(resource,(uintptr_t)context);
    int okay=!archive.error && archive.position==size && api.inspect(resource,&layout,api.opaque);
    if(!okay){api.release(resource);return 0;}return resource;
}
static inline int kzsf_custom_index_upload_test(const unsigned char *data,size_t size,uint32_t vertices,KzsfCustomIndexApi api) {
    uintptr_t resource=kzsf_custom_index_create(data,size,vertices,api);
    if(resource)api.release(resource);return resource!=0;
}
#endif
