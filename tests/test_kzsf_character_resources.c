#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_character_registry.h"
#include "../src/kzsf_character_mesh.h"
static unsigned char memory[0x30000];
static uintptr_t origin=0x10000000,manager;
static int race,reads;
static void ptr(size_t p,uintptr_t v){memcpy(memory+p,&v,8);}
static void integer(size_t p,int32_t v){memcpy(memory+p,&v,4);}
static int read_memory(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;
    if(p==origin+0x3b490c0 && n==8){memcpy(out,&manager,8);return 1;}
    if(p<origin || p-origin>sizeof(memory) || n>sizeof(memory)-(p-origin))return 0;
    memcpy(out,memory+p-origin,n);
    if(race==1 && p==origin+0x5800 && ++reads==1)ptr(0x5800,origin+0x7000);
    if(race==2 && p==origin+0xc0cc && ++reads==1)integer(0xc0cc,1234);
    return 1;
}
static uintptr_t type_of(uintptr_t p,uintptr_t base,KzsfZoomRead read,void *opaque) {
    uintptr_t t=0;(void)base;
    return read(opaque,p,&t,8) ? t : 0;
}
static void leaf(size_t mesh,size_t binding) {
    ptr(mesh,origin+0x3c1e490);ptr(mesh+0x58,origin+0xc000);ptr(mesh+0x70,origin+binding);
    ptr(binding,origin+0x3c1e730);integer(binding+0x10,32);ptr(binding+0x18,origin+0xe000);
    integer(binding+0x28,32);ptr(binding+0x30,origin+0xf000);
}
static void setup(void) {
    memset(memory,0,sizeof(memory));race=reads=0;manager=origin+0x1000;
    ptr(0x1078,origin+0x2000);integer(0x1084,7);
    uint32_t hash=kzsf_character_type_hash(origin+0x3c3d7b0);
    size_t slot=0x2000+(hash&7)*0x20;
    integer(slot,(int32_t)hash);ptr(slot+8,origin+0x3c3d7b0);ptr(slot+0x18,origin+0x3000);
    integer(0x3000,1);ptr(0x3008,origin+0x3100);ptr(0x3100,origin+0x4000);
    integer(0x4028,2);ptr(0x4030,origin+0x5000);
    memset(memory+0x5000,0x11,16);integer(0x5010,1);ptr(0x5018,origin+0x5800);
    memset(memory+0x5030,0x22,16);integer(0x5040,1);ptr(0x5048,origin+0x5810);
    ptr(0x5800,origin+0x6000);ptr(0x5810,origin+0x7000);
    ptr(0x6000,origin+0x3c3d7b0);ptr(0x7000,origin+0x3c3d7b0);
    ptr(0x8000,origin+0x3b50078);integer(0x8058,2);ptr(0x8060,origin+0xa000);
    ptr(0xa000,origin+0xa100);ptr(0xa010,origin+0x9000);
    ptr(0xa100,origin+0x3b4feb8);integer(0xa158,2);ptr(0xa160,origin+0xa200);
    ptr(0xa200,origin+0x9000);ptr(0xa250,origin+0x9200);
    ptr(0xc000,origin+0x3c1ec70);integer(0xc0c8,0x1234);integer(0xc0cc,0x5678);
    leaf(0x9000,0xb000);leaf(0x9200,0xb100);
}
int main(void) {
    KzsfCharacterRegistry registry,before;KzsfCharacterMeshTree tree,original;
    unsigned char copy[sizeof(memory)];
    setup();memcpy(copy,memory,sizeof(copy));
    assert(kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry));
    assert(registry.count==2 && registry.parts[0].resource==origin+0x6000);
    assert(registry.parts[1].entry_key[0]==0x22 && !memcmp(copy,memory,sizeof(copy)));
    setup();ptr(0x5810,origin+0x6000);
    assert(kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry) && registry.count==1);
    const struct {size_t p;int32_t v;} invalid[]={{0x1084,6},{0x1084,131071},
        {0x3000,-1},{0x3000,1025},{0x4028,65537},{0x5010,65}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        setup();integer(invalid[i].p,invalid[i].v);memset(&registry,0xa5,sizeof(registry));before=registry;
        assert(!kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry));
        assert(!memcmp(&registry,&before,sizeof(registry)));
    }
    setup();race=1;assert(!kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry));
    setup();ptr(0x1078,UINTPTR_MAX-8);assert(!kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry));
    setup();integer(0x3000,0);
    assert(kzsf_character_registry_read(origin,read_memory,NULL,type_of,&registry) && !registry.count);
    setup();memcpy(copy,memory,sizeof(copy));
    assert(kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    assert(tree.count==2 && !tree.unknown_count && tree.leaves[0].binding_count==32);
    assert(tree.leaves[0].layout_hash==0x5678 && !memcmp(copy,memory,sizeof(copy)));
    original=tree;ptr(0xa200,origin+0x8000); // cycle
    assert(!kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    assert(!memcmp(&tree,&original,sizeof(tree)));
    setup();integer(0x8058,129);assert(!kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    setup();integer(0xb028,31);assert(!kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    setup();ptr(0xc000,origin+123);assert(!kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    setup();race=2;assert(!kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    setup();ptr(0x9000,origin+123);
    assert(kzsf_character_mesh_read(origin+0x8000,origin,read_memory,NULL,type_of,&tree));
    assert(tree.count==1 && tree.unknown_count==2); // same unknown mesh visited twice
    puts("character registry and mesh-tree bounds, cycles, shared leaves, stability and read-only checks passed");
    return 0;
}
