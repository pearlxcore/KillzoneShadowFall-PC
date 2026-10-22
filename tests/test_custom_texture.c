#undef NDEBUG
#include "../src/kzsf_custom_texture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
static int mapped,constructed,destroyed,unmapped,fail_allocate,fail_create,fail_inspect;
static void *map_pixels(uint64_t size) {
    void *p=VirtualAlloc((void *)UINT64_C(0x500000000),(SIZE_T)size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(p);++mapped;return p;
}
static void unmap_pixels(void *p,uint64_t size) {(void)size;assert(destroyed==constructed);assert(VirtualFree(p,0,MEM_RELEASE));++unmapped;}
static uintptr_t KZSF_CUSTOM_ABI allocate_texture(size_t size,size_t alignment) {
    assert(size==0xc0 && alignment==8);return fail_allocate ? 0 : (uintptr_t)calloc(1,size);
}
static void KZSF_CUSTOM_ABI construct_texture(uintptr_t p) {assert(p);++constructed;}
static void KZSF_CUSTOM_ABI destroy_texture(uintptr_t p){++destroyed;assert(mapped>unmapped);free((void *)p);}
static uint64_t KZSF_CUSTOM_ABI create_texture(uintptr_t p,uint32_t a,uint32_t b,const uint32_t *dims,uint32_t c,
        uint32_t d,uint16_t e,uint32_t f,uint32_t g,uint32_t h,uint64_t pixels,uint64_t alignment,uint32_t j,char k) {
    assert(p && !a && b==1 && !c && !d && e==1 && !f && !g && h==2 && alignment==256 && !j && !k);
    assert(dims[0]==64 && dims[1]==64 && dims[2]==15 && *(unsigned char *)(uintptr_t)pixels==123);
    return !fail_create;
}
static int inspect_texture(uintptr_t p,uintptr_t pixels,const KzsfCustomTextureLayout *layout,void *opaque) {
    (void)opaque;assert(p && pixels && layout->bytes==16384);return !fail_inspect;
}
static int owned_calls,owned_fail_at,owned_images,owned_pixels,owned_resources,owned_fail_copy;
static uintptr_t KZSF_CUSTOM_ABI owned_allocate(size_t size,size_t alignment) {
    assert((size==0xc0 || size==40) && alignment==8);
    if(++owned_calls==owned_fail_at)return 0;
    void *p=calloc(1,size);assert(p);
    if(size==0xc0)++owned_images;else ++owned_resources;return (uintptr_t)p;
}
static void KZSF_CUSTOM_ABI owned_construct(uintptr_t p){assert(p);}
static uint64_t KZSF_CUSTOM_ABI owned_create(uintptr_t p,uint32_t a,uint32_t b,const uint32_t *dims,uint32_t c,
        uint32_t d,uint16_t e,uint32_t f,uint32_t g,uint32_t h,uint64_t pixels,uint64_t alignment,uint32_t j,char k) {
    assert(p && !a && b==1 && !c && !d && e==1 && !f && !g && h==2 && !pixels && alignment==256 && j==2 && k==1);
    if(fail_create)return 0;
    void *data=calloc(1,(size_t)dims[0]*dims[1]*4);assert(data);++owned_pixels;
    memcpy((void *)(p+0x30),&data,8);return 1;
}
static void KZSF_CUSTOM_ABI owned_destroy(uintptr_t p) {
    void *pixels=NULL;memcpy(&pixels,(void *)(p+0x30),8);
    if(pixels){free(pixels);--owned_pixels;}free((void *)p);--owned_images;
}
static void KZSF_CUSTOM_ABI owned_resource_construct(uintptr_t type,uintptr_t p){assert(type==42);memset((void *)p,0,40);}
static void KZSF_CUSTOM_ABI owned_release(uintptr_t p) {
    assert(*(uint32_t *)(p+8)==1);
    uintptr_t image=0;memcpy(&image,(void *)(p+0x18),8);assert(image);
    owned_destroy(image);free((void *)p);--owned_resources;
}
static int owned_copy(uintptr_t p,const unsigned char *data,const KzsfCustomTextureLayout *layout,void *opaque) {
    (void)opaque;if(owned_fail_copy)return 0;
    void *pixels=NULL;memcpy(&pixels,(void *)(p+0x30),8);assert(pixels);memcpy(pixels,data,layout->bytes);return 1;
}
int main(void) {
    unsigned char *data=calloc(1,16+16384);assert(data);memcpy(data,"KZTX",4);
    uint32_t head[3]={1,64,64};memcpy(data+4,head,12);data[16]=123;
    KzsfCustomTextureApi api={allocate_texture,construct_texture,create_texture,destroy_texture,map_pixels,unmap_pixels,inspect_texture,NULL};
    assert(kzsf_custom_texture_test(data,16400,api));
    fail_create=1;assert(!kzsf_custom_texture_test(data,16400,api));fail_create=0;
    fail_inspect=1;assert(!kzsf_custom_texture_test(data,16400,api));fail_inspect=0;
    fail_allocate=1;assert(!kzsf_custom_texture_test(data,16400,api));fail_allocate=0;
    assert(constructed==3 && destroyed==3 && mapped==4 && unmapped==4);
    data[8]=65;assert(!kzsf_custom_texture_test(data,16400,api));assert(mapped==4);
    data[8]=64;
    KzsfCustomOwnedTextureApi owned={owned_allocate,owned_construct,owned_create,owned_destroy,
        owned_resource_construct,owned_release,owned_copy,42,NULL};
    for(int failure=1;failure<=2;failure++) {
        owned_calls=0;owned_fail_at=failure;assert(!kzsf_custom_texture_resource(data,16400,owned));
        assert(!owned_images && !owned_pixels && !owned_resources);
    }
    owned_calls=owned_fail_at=0;fail_create=1;assert(!kzsf_custom_texture_resource(data,16400,owned));fail_create=0;
    owned_fail_copy=1;assert(!kzsf_custom_texture_resource(data,16400,owned));owned_fail_copy=0;
    assert(!owned_images && !owned_pixels && !owned_resources);
    uintptr_t resource=kzsf_custom_texture_resource(data,16400,owned);assert(resource);
    assert(owned_images==1 && owned_pixels==1 && owned_resources==1);owned_release(resource);
    assert(!owned_images && !owned_pixels && !owned_resources);
    free(data);puts("Detached texture lifecycle tests passed");return 0;
}
