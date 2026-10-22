#undef NDEBUG
#include "../src/kzsf_custom_mesh.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static unsigned calls,fail_at,allocations,refs[8];
static uintptr_t KZSF_CUSTOM_ABI allocate(size_t bytes,size_t alignment) {
    assert(alignment==8 || alignment==16);if(++calls==fail_at)return 0;
    void *p=calloc(1,bytes);assert(p);allocations++;return (uintptr_t)p;
}
static void discard(uintptr_t p) {if(p){assert(allocations);allocations--;free((void *)p);}}
static void KZSF_CUSTOM_ABI retain(uintptr_t p) {assert(p>0 && p<8);refs[p]++;}
static void KZSF_CUSTOM_ABI construct(uintptr_t type,uintptr_t p) {
    assert(type>=101 && type<=103);*(uintptr_t *)p=type;*(uint64_t *)(p+8)=0;
    if(type==103)((KzsfCustomModelPart *)p)->flags=1;
}
static void KZSF_CUSTOM_ABI release(uintptr_t address) {
    if(!address)return;
    if(address<8){assert(refs[address]>1);refs[address]--;return;}
    assert(*(uint64_t *)(address+8)==1);*(uint64_t *)(address+8)=0;
    uintptr_t kind=*(uintptr_t *)address;
    if(kind==103) {
        KzsfCustomModelPart *part=(void *)address;
        assert(!part->physics && part->bone_bounds==3 && part->flags==1);
        release(part->bone_bounds);release(part->mesh);
    } else if(kind==101) {
        KzsfCustomSkinnedMesh *mesh=(void *)address;
        uintptr_t *p=(void *)mesh->primitives.data,*e=(void *)mesh->effects.data;
        for(int i=0;i<mesh->primitives.count;i++)release(p[i]);
        for(int i=0;i<mesh->effects.count;i++)release(e[i]);
        discard(mesh->primitives.data);discard(mesh->effects.data);release(mesh->skin);
        release(mesh->skeleton);release(mesh->bindings);release(mesh->bone_bounds);
    } else {
        assert(kind==102);KzsfCustomSkinInfo *skin=(void *)address;
        KzsfCustomPrimitiveSkin *entries=(void *)skin->primitives.data;
        assert(!skin->blend_shapes.count && !skin->blend_shapes.data);
        for(int i=0;i<skin->primitives.count;i++) {
            assert(entries[i].kind==3 && !entries[i].cpu.count && !entries[i].cpu.data && !entries[i].nbt.data);
        }
        discard(skin->primitives.data);
    }
    discard(address);
}
static void balanced(void) {assert(!allocations);for(unsigned i=1;i<8;i++)assert(refs[i]==1);}
int main(void) {
    for(unsigned i=1;i<8;i++)refs[i]=1;
    KzsfCustomMeshApi api={allocate,construct,construct,construct,retain,release,101,102,103};
    KzsfCustomMeshSection sections[2]={{4,6,6406},{5,7,424}};
    for(unsigned failure=1;failure<=6;failure++) {
        calls=0;fail_at=failure;
        assert(!kzsf_custom_model_part_create(1,2,3,sections,2,api));balanced();
    }
    calls=fail_at=0;uintptr_t address=kzsf_custom_model_part_create(1,2,3,sections,2,api);
    assert(address && calls==6);KzsfCustomModelPart *part=(void *)address;
    assert(part->skinned==1 && part->references==1);
    KzsfCustomSkinnedMesh *mesh=(void *)part->mesh;
    assert(mesh->primitives.count==2 && mesh->effects.count==2 && mesh->scale[2]==1);
    KzsfCustomSkinInfo *skin=(void *)mesh->skin;
    KzsfCustomPrimitiveSkin *entries=(void *)skin->primitives.data;
    assert(entries[0].vertices==6406 && entries[1].vertices==424);
    uint32_t native_state[6]={0x20,0x50,1,1,3,0},configured[6];float lo[3]={-1,-2,0},hi[3]={1,2,2};
    assert(kzsf_custom_mesh_render_config(mesh,native_state,lo,hi));memcpy(configured,mesh->state,sizeof(configured));
    assert(configured[0]==0x20 && configured[1]==0x70 && configured[2]==2 && mesh->maximum[2]==2);
    native_state[1]=0x40;assert(!kzsf_custom_mesh_render_config(mesh,native_state,lo,hi));
    for(unsigned i=1;i<8;i++)assert(refs[i]==(i==3 ? 3u : 2u));
    release(address);balanced();
    calls=0;sections[1].vertices=0;assert(!kzsf_custom_model_part_create(1,2,3,sections,2,api) && !calls);
    sections[1].vertices=424;assert(!kzsf_custom_model_part_create(1,2,3,sections,17,api) && !calls);
    assert(!kzsf_custom_model_part_create(1,2,0,sections,2,api) && !calls);balanced();
    puts("Custom skinned mesh ownership tests passed");return 0;
}
