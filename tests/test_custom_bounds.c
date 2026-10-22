#undef NDEBUG
#include "../src/kzsf_custom_bounds.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned calls,fail_at,allocations;
static uintptr_t KZSF_CUSTOM_ABI allocate(size_t bytes,size_t align) {
    assert(align==8 || align==16);if(++calls==fail_at)return 0;
    allocations++;return (uintptr_t)calloc(1,bytes);
}
static void KZSF_CUSTOM_ABI construct(uintptr_t type,uintptr_t resource) {assert(type==42);*(uintptr_t *)resource=42;}
static void discard(uintptr_t p){if(p){allocations--;free((void *)p);}}
static void KZSF_CUSTOM_ABI release(uintptr_t address) {
    KzsfCustomBoneBounds *b=(void *)address;assert(b->references==1);
    discard(b->boxes.data);discard(b->bones.data);discard(address);
}
int main(void) {
    KzsfCustomBoundsApi api={allocate,construct,release,42};float lo[3]={-1,-2,0},hi[3]={1,2,2};
    float matrix[32]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1, 1,0,0,0,0,1,0,0,0,0,1,0,0,0,-1,1};
    for(unsigned failure=1;failure<=3;failure++) {
        calls=0;fail_at=failure;assert(!kzsf_custom_bounds_create(lo,hi,matrix,2,api));assert(!allocations);
    }
    calls=fail_at=0;uintptr_t address=kzsf_custom_bounds_create(lo,hi,matrix,2,api);assert(address);
    KzsfCustomBoneBounds *b=(void *)address;KzsfCustomBox *boxes=(void *)b->boxes.data;uint16_t *indices=(void *)b->bones.data;
    assert(b->boxes.count==3 && b->bones.count==2 && indices[0]==1 && indices[1]==2);
    assert(boxes[0].minimum[0]>boxes[0].maximum[0] && boxes[1].maximum[2]==2 && boxes[2].minimum[2]==-1);
    release(address);assert(!allocations);
    calls=0;matrix[3]=1;assert(!kzsf_custom_bounds_create(lo,hi,matrix,2,api) && !calls);matrix[3]=0;
    matrix[0]=NAN;assert(!kzsf_custom_bounds_create(lo,hi,matrix,2,api) && !calls);
    puts("Custom bone bounds tests passed");return 0;
}
