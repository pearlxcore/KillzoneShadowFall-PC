#ifndef KZSF_CUSTOM_VERTEX_UPLOAD_H
#define KZSF_CUSTOM_VERTEX_UPLOAD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#define KZSF_CUSTOM_ABI __attribute__((sysv_abi))

/* Native 330f00/395780 constructors leave the reference count at zero.
 * Acquire the caller's first reference before loading, transferring or
 * releasing any freshly constructed Resource. Upper flag bits stay intact. */
static inline void kzsf_custom_resource_acquire_new(uintptr_t resource) {
    __atomic_fetch_add((uint32_t *)(resource+8),1,__ATOMIC_SEQ_CST);
}

/* Creates a new detached vertex resource; never installs a part or alters an
 * existing resource. Create returns one caller-owned reference; test wrappers
 * release it before returning. Layout/read sequence: 2e7ef0/2e7320.
 * Caller must validate code signatures and direct engine allocator context. */
typedef struct {
    uintptr_t table;
    uint32_t unused,endian,error,padding;
    const unsigned char *data;
    size_t size,position;
} KzsfCustomArchive;
_Static_assert(offsetof(KzsfCustomArchive,endian)==12,"archive endian offset");
_Static_assert(offsetof(KzsfCustomArchive,error)==16,"archive error offset");

static int KZSF_CUSTOM_ABI kzsf_custom_archive_read(KzsfCustomArchive *archive,void *out,size_t size) {
    if(!archive || !out || archive->position>archive->size || size>archive->size-archive->position) {
        if(archive)archive->error=1;
        return 0;
    }
    memcpy(out,archive->data+archive->position,size);archive->position+=size;return 1;
}
static int KZSF_CUSTOM_ABI kzsf_custom_archive_skip(KzsfCustomArchive *archive,int64_t delta) {
    if(!archive || archive->position>archive->size ||
       (delta<0 ? (uint64_t)(-(delta+1))+1>archive->position : (uint64_t)delta>archive->size-archive->position)) {
        if(archive)archive->error=1;
        return 0;
    }
    if(delta<0)archive->position-=(size_t)((uint64_t)(-(delta+1))+1);
    else archive->position+=(size_t)delta;
    return 1;
}
static uint64_t KZSF_CUSTOM_ABI kzsf_custom_archive_position(KzsfCustomArchive *archive) {return archive->position;}
typedef struct {uint32_t vertices,stride,elements;size_t data_offset;} KzsfCustomVertexLayout;
static inline int kzsf_custom_vertex_layout(const unsigned char *data,size_t size,KzsfCustomVertexLayout *out) {
    uint32_t head[5];
    if(!data || !out || size<40 || size>32u*1024u*1024u)return 0;
    memcpy(head,data,20);
    if(!head[0] || head[0]>1000000 || head[1]!=1 || head[2]!=0x31 ||
       head[3]<12 || head[3]>256 || !head[4] || head[4]>15)return 0;
    size_t offset=36+(size_t)head[4]*4;
    if(offset>size || (size_t)head[0]*head[3]!=size-offset)return 0;
    uint32_t seen=0;int position=0;
    for(uint32_t i=0;i<head[4];i++) {
        const unsigned char *element=data+20+i*4;
        unsigned width=element[1]==2 ? 4u*element[2] : element[1]==3 ? 2u*element[2] :
                       element[1]==4 || element[1]==7 ? element[2] : element[1]==6 ? 4u : 0u;
        if(!width || !element[2] || element[2]>4 || element[0]+width>head[3] ||
           element[3]>=32 || (seen&(1u<<element[3])))return 0;
        seen|=1u<<element[3];
        if(element[3]==0) {
            if(element[0] || element[1]!=2 || element[2]!=3)return 0;
            position=1;
        }
    }
    if(!position)return 0;
    for(uint32_t i=0;i<head[0];i++)for(unsigned j=0;j<3;j++) {
        float value;memcpy(&value,data+offset+(size_t)i*head[3]+j*4,4);
        if(!isfinite(value))return 0;
    }
    *out=(KzsfCustomVertexLayout){head[0],head[3],head[4],offset};return 1;
}
typedef uintptr_t (KZSF_CUSTOM_ABI *KzsfCustomAllocate)(size_t,size_t);
typedef void (KZSF_CUSTOM_ABI *KzsfCustomConstruct)(uintptr_t,uintptr_t);
typedef void (KZSF_CUSTOM_ABI *KzsfCustomLoad)(uintptr_t,uintptr_t);
typedef void (KZSF_CUSTOM_ABI *KzsfCustomRelease)(uintptr_t);
typedef int (*KzsfCustomInspect)(uintptr_t,const KzsfCustomVertexLayout *,void *);
typedef struct {
    KzsfCustomAllocate allocate;
    KzsfCustomConstruct construct;
    KzsfCustomLoad load;
    KzsfCustomRelease release;
    KzsfCustomInspect inspect;
    uintptr_t type;
    void *opaque;
} KzsfCustomVertexApi;
typedef struct {int allocated,archive_error,inspected;size_t consumed;} KzsfCustomUploadResult;
static inline uintptr_t kzsf_custom_vertex_create(const unsigned char *data,size_t size,KzsfCustomVertexApi api,
                                                       KzsfCustomUploadResult *result) {
    if(result)*result=(KzsfCustomUploadResult){0};
    KzsfCustomVertexLayout layout;
    if(!kzsf_custom_vertex_layout(data,size,&layout) || !api.allocate || !api.construct ||
       !api.load || !api.release || !api.inspect || !api.type)return 0;
    uintptr_t table[9]={0};
    table[3]=(uintptr_t)kzsf_custom_archive_read;
    table[6]=(uintptr_t)kzsf_custom_archive_skip;
    table[8]=(uintptr_t)kzsf_custom_archive_position;
    KzsfCustomArchive archive={(uintptr_t)table,0,0,0,0,data,size,0};
    uintptr_t context[2]={0,(uintptr_t)&archive};
    uintptr_t resource=api.allocate(32,8);
    if(!resource)return 0;
    if(result)result->allocated=1;
    api.construct(api.type,resource);
    kzsf_custom_resource_acquire_new(resource);
    api.load(resource,(uintptr_t)context);
    int inspected=!archive.error && archive.position==size && api.inspect(resource,&layout,api.opaque);
    int okay=!archive.error && archive.position==size && inspected;
    if(result){result->archive_error=archive.error;result->consumed=archive.position;result->inspected=inspected;}
    if(!okay){api.release(resource);return 0;}
    return resource;
}
static inline int kzsf_custom_vertex_upload_diagnostic(const unsigned char *data,size_t size,KzsfCustomVertexApi api,
                                                       KzsfCustomUploadResult *result) {
    uintptr_t resource=kzsf_custom_vertex_create(data,size,api,result);
    if(resource)api.release(resource);
    return resource!=0;
}
static inline int kzsf_custom_vertex_upload_test(const unsigned char *data,size_t size,KzsfCustomVertexApi api) {
    return kzsf_custom_vertex_upload_diagnostic(data,size,api,NULL);
}
#endif
