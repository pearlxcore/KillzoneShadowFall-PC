#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kzsf_character_inventory.h"
static unsigned char memory[0x5000];
static const uintptr_t origin=0x10000000;
static int mutate,reads;
static int wrong_type;
static uintptr_t part_type(uintptr_t p,uintptr_t base,KzsfZoomRead read,void *opaque) {
    (void)read;(void)opaque;
    return !wrong_type && p==origin+0x4000 ? base+0x3c3d7b0 : 0;
}
static uintptr_t resource_type(uintptr_t p,uintptr_t base,KzsfZoomRead read,void *opaque) {
    (void)read;(void)opaque;
    if(p==origin+0x1000)return wrong_type ? 0 : base+0x3caac10;
    return p==origin+0x4000 || p==origin+0x4400 ? base+0x3c3d7b0 : 0;
}
static void ptr(size_t offset,uintptr_t value){memcpy(memory+offset,&value,8);}
static void integer(size_t offset,int32_t value){memcpy(memory+offset,&value,4);}
static int read_memory(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;
    if(p<origin || p-origin>sizeof(memory) || n>sizeof(memory)-(p-origin))return 0;
    memcpy(out,memory+p-origin,n);
    if(mutate && p==origin+0x3008 && ++reads==1)ptr(0x3008,origin+0x4100);
    return 1;
}
static void setup(void) {
    memset(memory,0,sizeof(memory));mutate=reads=wrong_type=0;
    ptr(0x1d0,origin+0x1000);integer(0x98c,1);
    integer(0x1648,2);ptr(0x1650,origin+0x3000);
    ptr(0x3008,origin+0x4000);ptr(0x3020,origin+0x4200);
    integer(0x3028,1);ptr(0x3030,origin+0x4300);
    ptr(0x30f8,origin+0x4400);
    ptr(0x4018,origin+0x4500);ptr(0x4028,origin+0x4600);memory[0x4030]=1;
}
int main(void) {
    KzsfCharacterInventory out,before;unsigned char copy[sizeof(memory)];
    setup();memcpy(copy,memory,sizeof(copy));
    assert(kzsf_character_inventory_read(origin,read_memory,NULL,&out));
    assert(out.count==2 && out.current==1 && out.variants[0].component_count==1);
    assert(out.variants[1].part==origin+0x4400);
    assert(!memcmp(copy,memory,sizeof(copy)));
    const struct {size_t p;int32_t n;} invalid[]={
        {0x1648,-1},{0x1648,0},{0x1648,65},{0x98c,-1},{0x98c,2},
        {0x3028,-1},{0x3028,257}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        setup();integer(invalid[i].p,invalid[i].n);memset(&out,0xa5,sizeof(out));before=out;
        assert(!kzsf_character_inventory_read(origin,read_memory,NULL,&out));
        assert(!memcmp(&out,&before,sizeof(out)));
    }
    setup();mutate=1;assert(!kzsf_character_inventory_read(origin,read_memory,NULL,&out));
    setup();ptr(0x1650,UINTPTR_MAX-8);assert(!kzsf_character_inventory_read(origin,read_memory,NULL,&out));
    setup();ptr(0x3030,UINTPTR_MAX-2);assert(!kzsf_character_inventory_read(origin,read_memory,NULL,&out));
    setup();ptr(0x1650,origin+sizeof(memory)-8);assert(!kzsf_character_inventory_read(origin,read_memory,NULL,&out));
    assert(!kzsf_character_inventory_read(UINTPTR_MAX,read_memory,NULL,&out));
    setup();memcpy(copy,memory,sizeof(copy));
    assert(kzsf_character_resource_variants_read(origin+0x1000,origin,read_memory,NULL,resource_type,&out));
    assert(out.count==2 && out.current==-1 && !out.soldier && out.variants[1].part==origin+0x4400);
    assert(!memcmp(copy,memory,sizeof(copy)));
    before=out;wrong_type=1;
    assert(!kzsf_character_resource_variants_read(origin+0x1000,origin,read_memory,NULL,resource_type,&out));
    assert(!memcmp(&out,&before,sizeof(out)));
    setup();ptr(0x30f8,origin+0x4999);
    assert(!kzsf_character_resource_variants_read(origin+0x1000,origin,read_memory,NULL,resource_type,&out));
    setup();mutate=1;
    assert(!kzsf_character_resource_variants_read(origin+0x1000,origin,read_memory,NULL,resource_type,&out));
    setup();KzsfCharacterPart part,original;memcpy(copy,memory,sizeof(copy));
    assert(kzsf_character_part_read(origin+0x4000,origin,read_memory,NULL,part_type,&part));
    assert(part.mesh==origin+0x4500 && part.physics==origin+0x4600 && part.is_skinned==1);
    assert(!memcmp(copy,memory,sizeof(copy)));
    original=part;wrong_type=1;
    assert(!kzsf_character_part_read(origin+0x4000,origin,read_memory,NULL,part_type,&part));
    assert(!memcmp(&part,&original,sizeof(part)));
    setup();memory[0x4030]=2;
    assert(!kzsf_character_part_read(origin+0x4000,origin,read_memory,NULL,part_type,&part));
    setup();ptr(0x4000,origin+0x2000);ptr(0x2018,origin+0x33b790);
    ptr(0x4010,origin+0x4500);integer(0x4508,4);memcpy(memory+0x4510,"Body",4);
    char name[128];memset(name,0xa5,sizeof(name));
    assert(kzsf_character_resource_name(origin+0x4000,origin,read_memory,NULL,name) && !strcmp(name,"Body"));
    integer(0x4508,4097);assert(!kzsf_character_resource_name(origin+0x4000,origin,read_memory,NULL,name));
    integer(0x4508,4);ptr(0x2018,origin+123);
    assert(!kzsf_character_resource_name(origin+0x4000,origin,read_memory,NULL,name));
    puts("character inventory bounds, ownership snapshot stability and read-only checks passed");
}
