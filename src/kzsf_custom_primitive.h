#ifndef KZSF_CUSTOM_PRIMITIVE_H
#define KZSF_CUSTOM_PRIMITIVE_H
#include "kzsf_custom_vertex_upload.h"

/* RenderingPrimitiveResource (52ef10/52efa0). The constructor owns its
 * resource header; only geometry fields are filled here. Input buffers remain
 * caller-owned, with one extra reference held by the returned primitive. */
typedef struct {
    uintptr_t table;
    uint64_t reference;
    uintptr_t vertices,indices;
    float minimum[4],maximum[4];
    uint32_t flags,padding;
    uintptr_t auxiliary;
    uint32_t first_index,index_count;
    uintptr_t extra;
} KzsfCustomPrimitive;
_Static_assert(sizeof(KzsfCustomPrimitive)==0x60,"primitive size");
_Static_assert(offsetof(KzsfCustomPrimitive,index_count)==0x54,"primitive index count");
typedef void (KZSF_CUSTOM_ABI *KzsfCustomRetain)(uintptr_t);
typedef struct {
    KzsfCustomAllocate allocate;
    KzsfCustomConstruct construct;
    KzsfCustomRetain retain;
    uintptr_t type;
} KzsfCustomPrimitiveApi;

static inline uintptr_t kzsf_custom_primitive_create(uintptr_t vertices,uintptr_t indices,
        uint32_t index_count,const float minimum[3],const float maximum[3],KzsfCustomPrimitiveApi api) {
    if(!vertices || !indices || !index_count || index_count>3000000 || index_count%3 ||
       !minimum || !maximum || !api.allocate || !api.construct || !api.retain || !api.type)return 0;
    for(unsigned i=0;i<3;i++)
        if(!isfinite(minimum[i]) || !isfinite(maximum[i]) || minimum[i]>maximum[i])return 0;
    uintptr_t address=api.allocate(sizeof(KzsfCustomPrimitive),16);
    if(!address)return 0;
    memset((void *)address,0,sizeof(KzsfCustomPrimitive));api.construct(api.type,address);
    kzsf_custom_resource_acquire_new(address);
    KzsfCustomPrimitive *primitive=(void *)address;
    /* 52ef10 defaults to topology 4 (strip). Our payloads are triangle lists:
     * 5010b0 decodes (+0xc >> 8)&15 as 3 for count/3, 4/5 for count-2.
     * This nibble shares the header with resource flags; preserve all other
     * flags and the acquired reference count. */
    primitive->reference=(primitive->reference&~UINT64_C(0xf0000000000))|UINT64_C(0x30000000000);
    api.retain(vertices);primitive->vertices=vertices;
    api.retain(indices);primitive->indices=indices;
    for(unsigned i=0;i<3;i++){primitive->minimum[i]=minimum[i];primitive->maximum[i]=maximum[i];}
    primitive->index_count=index_count;
    return address;
}
#endif
