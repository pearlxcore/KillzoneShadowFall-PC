#ifndef KZSF_CUSTOM_TEXTURE_H
#define KZSF_CUSTOM_TEXTURE_H
#include "kzsf_custom_vertex_upload.h"

typedef struct {uint32_t width,height;size_t bytes;} KzsfCustomTextureLayout;
static inline int kzsf_custom_texture_layout(const unsigned char *data,size_t size,KzsfCustomTextureLayout *out) {
    uint32_t head[4];if(!data || !out || size<16)return 0;memcpy(head,data,16);
    if(memcmp(data,"KZTX",4) || head[1]!=1 || head[2]<64 || head[2]>4096 || head[3]<64 || head[3]>4096 ||
       (head[2]&(head[2]-1)) || (head[3]&(head[3]-1)))return 0;
    size_t bytes=(size_t)head[2]*head[3]*4;
    if(bytes!=size-16)return 0;
    *out=(KzsfCustomTextureLayout){head[2],head[3],bytes};return 1;
}
typedef void (KZSF_CUSTOM_ABI *KzsfCustomTextureConstruct)(uintptr_t);
typedef uint64_t (KZSF_CUSTOM_ABI *KzsfCustomTextureCreate)(uintptr_t,uint32_t,uint32_t,const uint32_t *,uint32_t,
    uint32_t,uint16_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t,uint32_t,char);
typedef struct {
    KzsfCustomAllocate allocate;KzsfCustomTextureConstruct construct;KzsfCustomTextureCreate create;
    KzsfCustomRelease destroy;void *(*map)(uint64_t);void (*unmap)(void *,uint64_t);
    int (*inspect)(uintptr_t,uintptr_t,const KzsfCustomTextureLayout *,void *);void *opaque;
} KzsfCustomTextureApi;
typedef struct {
    KzsfCustomAllocate allocate;KzsfCustomTextureConstruct construct_image;KzsfCustomTextureCreate create_image;
    KzsfCustomRelease destroy_image;KzsfCustomConstruct construct_resource;KzsfCustomRelease release_resource;
    int (*copy_pixels)(uintptr_t,const unsigned char *,const KzsfCustomTextureLayout *,void *);
    uintptr_t type;void *opaque;
} KzsfCustomOwnedTextureApi;
/* A normal Texture resource owns an internally allocated native image. No
 * external pixel mapping survives this call. Release through the native
 * resource API, which handles queued image destruction. */
static inline uintptr_t kzsf_custom_texture_resource(const unsigned char *data,size_t size,KzsfCustomOwnedTextureApi api) {
    KzsfCustomTextureLayout layout;
    if(!kzsf_custom_texture_layout(data,size,&layout) || !api.allocate || !api.construct_image || !api.create_image ||
       !api.destroy_image || !api.construct_resource || !api.release_resource || !api.copy_pixels || !api.type)return 0;
    uintptr_t image=api.allocate(0xc0,8);if(!image)return 0;
    memset((void *)image,0,0xc0);api.construct_image(image);
    uint32_t description[3]={layout.width,layout.height,15};
    /* param11=0 asks the engine to allocate pixels; memory class2 is released
     * by native image destructor 2ceb90 -> 2fc790. */
    if(!api.create_image(image,0,1,description,0,0,1,0,0,2,0,256,2,1) ||
       !api.copy_pixels(image,data+16,&layout,api.opaque)) {api.destroy_image(image);return 0;}
    uintptr_t resource=api.allocate(40,8);
    if(!resource){api.destroy_image(image);return 0;}
    api.construct_resource(api.type,resource);
    kzsf_custom_resource_acquire_new(resource);
    /* The resource constructor initializes its image link to NULL. Both this
     * object and the image are newly owned; no existing game object is edited. */
    memcpy((void *)(resource+0x18),&image,8);
    return resource;
}
static inline int kzsf_custom_texture_test(const unsigned char *data,size_t size,KzsfCustomTextureApi api) {
    KzsfCustomTextureLayout layout;
    if(!kzsf_custom_texture_layout(data,size,&layout) || !api.allocate || !api.construct || !api.create ||
       !api.destroy || !api.map || !api.unmap || !api.inspect)return 0;
    void *pixels=api.map(layout.bytes);if(!pixels)return 0;
    if(((uintptr_t)pixels&255) || (uintptr_t)pixels>=(UINT64_C(1)<<40)) {api.unmap(pixels,layout.bytes);return 0;}
    memcpy(pixels,data+16,layout.bytes);
    uintptr_t texture=api.allocate(0xc0,8);
    if(!texture){api.unmap(pixels,layout.bytes);return 0;}
    memset((void *)texture,0,0xc0);api.construct(texture);
    uint32_t description[3]={layout.width,layout.height,15};
    int okay=api.create(texture,0,1,description,0,0,1,0,0,2,(uintptr_t)pixels,256,0,0) &&
        api.inspect(texture,(uintptr_t)pixels,&layout,api.opaque);
    /* Detached texture has never been submitted or wrapped in a resource.
     * Native destructor runs before external pixel storage is unmapped. */
    api.destroy(texture);api.unmap(pixels,layout.bytes);return okay;
}
#endif
