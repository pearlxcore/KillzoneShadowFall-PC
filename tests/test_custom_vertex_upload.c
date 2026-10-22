#undef NDEBUG
#include "../src/kzsf_custom_vertex_upload.h"
#include "../src/kzsf_custom_index_upload.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static int allocated,released,loaded,inspected,fail_allocate,fail_load,fail_inspect;
static size_t expected_size=32;
static uintptr_t KZSF_CUSTOM_ABI allocate_resource(size_t size,size_t alignment) {
    assert(size==expected_size && alignment==8);if(fail_allocate)return 0;
    ++allocated;return (uintptr_t)calloc(1,size);
}
static void KZSF_CUSTOM_ABI construct_resource(uintptr_t type,uintptr_t resource) {
    assert(type==42);((uintptr_t *)resource)[0]=42;
}
static void KZSF_CUSTOM_ABI load_resource(uintptr_t resource,uintptr_t context) {
    assert(((uintptr_t *)resource)[0]==42);++loaded;
    KzsfCustomArchive *archive=(KzsfCustomArchive *)((uintptr_t *)context)[1];
    uintptr_t *table=(uintptr_t *)archive->table;
    int (KZSF_CUSTOM_ABI *read)(KzsfCustomArchive *,void *,size_t)=(void *)table[3];
    int (KZSF_CUSTOM_ABI *skip)(KzsfCustomArchive *,int64_t)=(void *)table[6];
    uint64_t (KZSF_CUSTOM_ABI *tell)(KzsfCustomArchive *)=(void *)table[8];
    unsigned char head[20],payload[32];
    assert(read(archive,head,20));assert(tell(archive)==20);
    if(fail_load){assert(!skip(archive,10000));return;}
    assert(skip(archive,20));assert(read(archive,payload,32));assert(tell(archive)==72);
    assert(skip(archive,-32));assert(read(archive,payload,32));
}
static int inspect_resource(uintptr_t resource,const KzsfCustomVertexLayout *layout,void *opaque) {
    (void)opaque;assert(((uintptr_t *)resource)[0]==42);assert(layout->vertices==1 && layout->stride==32);++inspected;return !fail_inspect;
}
static void KZSF_CUSTOM_ABI release_resource(uintptr_t resource) {
    assert(*(uint32_t *)(resource+8)==1);++released;free((void *)resource);
}
static void KZSF_CUSTOM_ABI load_indices(uintptr_t resource,uintptr_t context) {
    assert(((uintptr_t *)resource)[0]==42);++loaded;
    KzsfCustomArchive *archive=(KzsfCustomArchive *)((uintptr_t *)context)[1];
    unsigned char bytes[38];
    if(fail_load){assert(!kzsf_custom_archive_skip(archive,10000));return;}
    assert(kzsf_custom_archive_read(archive,bytes,sizeof(bytes)));
}
static int inspect_indices(uintptr_t resource,const KzsfCustomIndexLayout *layout,void *opaque) {
    (void)opaque;assert(((uintptr_t *)resource)[0]==42);
    assert(layout->indices==3 && layout->width==2 && layout->vertices==3);++inspected;return !fail_inspect;
}

int main(void) {
    uint64_t native_header[2]={0,UINT64_C(0x40004000000)};
    kzsf_custom_resource_acquire_new((uintptr_t)native_header);
    assert(native_header[1]==UINT64_C(0x40004000001));
    unsigned char payload[72]={0};uint32_t head[5]={1,1,0x31,32,1};memcpy(payload,head,20);
    payload[21]=2;payload[22]=3;
    KzsfCustomVertexLayout layout;assert(kzsf_custom_vertex_layout(payload,sizeof(payload),&layout));
    KzsfCustomVertexApi api={allocate_resource,construct_resource,load_resource,release_resource,inspect_resource,42,NULL};
    assert(kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));
    assert(allocated==1 && released==1 && loaded==1 && inspected==1);
    fail_load=1;assert(!kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));fail_load=0;
    assert(allocated==2 && released==2 && inspected==1);
    fail_inspect=1;assert(!kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));fail_inspect=0;
    assert(allocated==3 && released==3 && inspected==2);
    fail_allocate=1;assert(!kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));fail_allocate=0;
    assert(allocated==3 && released==3);
    payload[21]=99;assert(!kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));payload[21]=2;
    float invalid=NAN;memcpy(payload+40,&invalid,4);assert(!kzsf_custom_vertex_upload_test(payload,sizeof(payload),api));
    assert(allocated==3 && released==3);
    unsigned char bytes[4]={0};KzsfCustomArchive archive={0,0,0,0,0,bytes,4,0};
    assert(!kzsf_custom_archive_skip(&archive,INT64_MIN));assert(archive.error==1);
    unsigned char index_payload[38]={0};uint32_t index_head[3]={3,0x91,0},index_bytes=6;
    uint16_t indices[3]={0,1,2};
    memcpy(index_payload,index_head,12);memcpy(index_payload+28,&index_bytes,4);memcpy(index_payload+32,indices,6);
    expected_size=40;
    KzsfCustomIndexApi ia={allocate_resource,construct_resource,load_indices,release_resource,inspect_indices,42,NULL};
    assert(kzsf_custom_index_upload_test(index_payload,sizeof(index_payload),3,ia));
    assert(allocated==4 && released==4);
    fail_load=1;assert(!kzsf_custom_index_upload_test(index_payload,sizeof(index_payload),3,ia));fail_load=0;
    fail_inspect=1;assert(!kzsf_custom_index_upload_test(index_payload,sizeof(index_payload),3,ia));fail_inspect=0;
    fail_allocate=1;assert(!kzsf_custom_index_upload_test(index_payload,sizeof(index_payload),3,ia));fail_allocate=0;
    assert(allocated==6 && released==6);
    index_payload[36]=3;assert(!kzsf_custom_index_upload_test(index_payload,sizeof(index_payload),3,ia));
    assert(allocated==6 && released==6);
    index_payload[36]=2;
    uintptr_t retained=kzsf_custom_index_create(index_payload,sizeof(index_payload),3,ia);
    assert(retained && allocated==7 && released==6);release_resource(retained);assert(released==7);
    memset(payload+40,0,4);expected_size=32;
    retained=kzsf_custom_vertex_create(payload,sizeof(payload),api,NULL);
    assert(retained && allocated==8 && released==7);release_resource(retained);assert(released==8);
    puts("Custom vertex upload lifecycle tests passed");return 0;
}
