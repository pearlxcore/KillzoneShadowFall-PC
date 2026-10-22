#ifndef KZSF_CUSTOM_BOUNDS_H
#define KZSF_CUSTOM_BOUNDS_H
#include "kzsf_custom_mesh.h"
#include <float.h>
typedef struct {float minimum[4],maximum[4];} KzsfCustomBox;
typedef struct {uintptr_t table;uint64_t references;KzsfCustomMeshArray boxes,bones;uint16_t flags;unsigned char pad[6];} KzsfCustomBoneBounds;
_Static_assert(sizeof(KzsfCustomBox)==32,"native bounding box layout");
_Static_assert(sizeof(KzsfCustomBoneBounds)==0x48,"native bone bounds layout");
typedef struct {KzsfCustomAllocate allocate;KzsfCustomConstruct construct;KzsfCustomRelease release;uintptr_t type;} KzsfCustomBoundsApi;
/* Transform the full model's conservative AABB into each bone's local space.
 * Entry zero remains the native empty-box sentinel; bone indices use 1..N.
 * Bounds affect culling only, never the actor's collision/physics. */
static inline uintptr_t kzsf_custom_bounds_create(const float lo[3],const float hi[3],const float *inverse,
        uint32_t count,KzsfCustomBoundsApi api) {
    if(!lo || !hi || !inverse || !count || count>256 || !api.allocate || !api.construct || !api.release || !api.type)return 0;
    for(unsigned j=0;j<3;j++)if(!isfinite(lo[j]) || !isfinite(hi[j]) || lo[j]>hi[j])return 0;
    KzsfCustomBox temporary[257]={0};
    for(unsigned j=0;j<3;j++){temporary[0].minimum[j]=FLT_MAX;temporary[0].maximum[j]=-FLT_MAX;}
    for(uint32_t i=0;i<count;i++) {
        const float *m=inverse+(size_t)i*16;KzsfCustomBox *b=&temporary[i+1];
        for(unsigned j=0;j<16;j++)if(!isfinite(m[j]))return 0;
        if(fabsf(m[3])+fabsf(m[7])+fabsf(m[11])>1e-5f || fabsf(m[15]-1)>1e-5f)return 0;
        for(unsigned j=0;j<3;j++){b->minimum[j]=INFINITY;b->maximum[j]=-INFINITY;}
        for(unsigned corner=0;corner<8;corner++)for(unsigned axis=0;axis<3;axis++) {
            float p=m[12+axis];
            for(unsigned j=0;j<3;j++)p+=m[j*4+axis]*((corner&(1u<<j)) ? hi[j] : lo[j]);
            if(!isfinite(p))return 0;
            if(p<b->minimum[axis])b->minimum[axis]=p;if(p>b->maximum[axis])b->maximum[axis]=p;
        }
    }
    uintptr_t address=api.allocate(sizeof(KzsfCustomBoneBounds),8);if(!address)return 0;
    memset((void *)address,0,sizeof(KzsfCustomBoneBounds));api.construct(api.type,address);kzsf_custom_resource_acquire_new(address);
    KzsfCustomBoneBounds *bounds=(void *)address;
    uintptr_t boxes=api.allocate(((size_t)count+1)*sizeof(KzsfCustomBox),16);if(!boxes)goto failed;
    memcpy((void *)boxes,temporary,((size_t)count+1)*sizeof(KzsfCustomBox));
    bounds->boxes=(KzsfCustomMeshArray){(int32_t)count+1,0,boxes,(int32_t)count+1,0};
    uintptr_t bones=api.allocate((size_t)count*sizeof(uint16_t),8);if(!bones)goto failed;
    for(uint32_t i=0;i<count;i++)((uint16_t *)bones)[i]=(uint16_t)(i+1);
    bounds->bones=(KzsfCustomMeshArray){(int32_t)count,0,bones,(int32_t)count,0};return address;
failed:
    api.release(address);return 0;
}
#endif
