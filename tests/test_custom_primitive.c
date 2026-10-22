#include "../src/kzsf_custom_primitive.h"
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static int fail,allocated,vertex_refs,index_refs;
static uintptr_t KZSF_CUSTOM_ABI allocate(size_t size,size_t alignment) {
    assert(size==0x60 && alignment==16);if(fail)return 0;
    allocated++;return (uintptr_t)malloc(size);
}
static void KZSF_CUSTOM_ABI construct(uintptr_t type,uintptr_t address) {
    assert(type==123);KzsfCustomPrimitive *p=(void *)address;
    p->table=456;p->reference=UINT64_C(0x1000040000000000);
}
static void KZSF_CUSTOM_ABI retain(uintptr_t resource) {
    if(resource==11)vertex_refs++;else {assert(resource==22);index_refs++;}
}
static void release(uintptr_t address) {
    KzsfCustomPrimitive *p=(void *)address;
    assert(p->reference==UINT64_C(0x1000030000000001));
    assert(p->vertices==11 && p->indices==22);vertex_refs--;index_refs--;
    free(p);allocated--;
}
int main(void) {
    KzsfCustomPrimitiveApi api={allocate,construct,retain,123};
    float lo[3]={-1,-2,-3},hi[3]={1,2,3};
    vertex_refs=index_refs=1;
    uintptr_t address=kzsf_custom_primitive_create(11,22,31368,lo,hi,api);
    assert(address && vertex_refs==2 && index_refs==2);
    KzsfCustomPrimitive *p=(void *)address;
    assert(p->table==456 && p->reference==UINT64_C(0x1000030000000001));
    assert(p->index_count==31368 && !p->first_index && !p->auxiliary && !p->extra);
    assert(p->minimum[1]==-2 && p->maximum[2]==3 && !p->minimum[3] && !p->maximum[3]);
    release(address);assert(!allocated && vertex_refs==1 && index_refs==1);
    fail=1;assert(!kzsf_custom_primitive_create(11,22,3,lo,hi,api));fail=0;
    assert(!kzsf_custom_primitive_create(11,22,4,lo,hi,api));
    assert(!kzsf_custom_primitive_create(0,22,3,lo,hi,api));
    hi[0]=-2;assert(!kzsf_custom_primitive_create(11,22,3,lo,hi,api));
    hi[0]=NAN;assert(!kzsf_custom_primitive_create(11,22,3,lo,hi,api));
    assert(!allocated && vertex_refs==1 && index_refs==1);
    puts("custom primitive ownership tests passed");return 0;
}
