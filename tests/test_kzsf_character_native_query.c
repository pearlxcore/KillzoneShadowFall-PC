#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_character_native_query.h"
static unsigned char memory[0x20000],query_code[20],free_code[17];
static const uintptr_t origin=0x10000000;
static int queries,releases,bad_guard,unreadable,mode;
static uintptr_t allocation;
static uint32_t expected_type=0x3c3d7b0;
static void ptr(size_t offset,uintptr_t value){memcpy(memory+offset,&value,8);}
static int read_memory(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;
    if(p==origin+0x39b350 && n==20){memcpy(out,query_code,n);if(bad_guard)((unsigned char*)out)[0]^=1;return 1;}
    if(p==origin+0x3295d0 && n==17){memcpy(out,free_code,n);return 1;}
    if(p==origin+0x3b490c0 && n==8){uintptr_t manager=origin+0x1000;memcpy(out,&manager,8);return 1;}
    if(unreadable && p==origin+0x5000)return 0;
    if(p<origin || p-origin>sizeof(memory) || n>sizeof(memory)-(p-origin))return 0;
    memcpy(out,memory+p-origin,n);return 1;
}
static uintptr_t type_of(uintptr_t p,uintptr_t base,KzsfZoomRead read,void *opaque) {
    (void)read;(void)opaque;
    return p==origin+0x19000 || p==origin+0x1a000 || p==origin+0x1b000 ? base+expected_type : 0;
}
static void KZSF_CHARACTER_ABI query(uintptr_t manager,const KzsfCharacterNativeFilter *filter,
        KzsfCharacterNativeArray *array,uint64_t extra) {
    assert(manager==origin+0x1000 && !extra && filter->type==origin+expected_type);
    assert(filter->flags==2 && !filter->name && !filter->padding);
    assert(!array->count && !array->data && !array->capacity);queries++;
    array->count=mode==1 ? -1 : mode==2 ? 922 : mode==3 ? 0 : mode==4 ? 4097 : 4;
    array->capacity=4097;array->data=allocation;
}
static void KZSF_CHARACTER_ABI release(uintptr_t p){assert(p==allocation);releases++;}
static void setup(void) {
    memset(memory,0,sizeof(memory));queries=releases=bad_guard=unreadable=mode=0;
    allocation=origin+0x5000;
    for(unsigned i=0;i<4097;i++)ptr(0x5000+i*8,origin+0x19000);
    ptr(0x5008,origin+0x1a000);ptr(0x5010,origin+0x1c000);
    ptr(0x5000+900*8,origin+0x1b000); // unique resource beyond former512 limit
}
int main(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(!fseek(f,0,SEEK_END));long offset=ftell(f)-(long)size;
    assert(!fseek(f,offset+0x39b350,SEEK_SET));assert(fread(query_code,1,20,f)==20);
    assert(!fseek(f,offset+0x3295d0,SEEK_SET));assert(fread(free_code,1,17,f)==17);fclose(f);
    KzsfCharacterQueryApi api={query,release};KzsfCharacterNativeResources out,before;const char *why=NULL;
    setup();assert(kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(queries==1 && releases==1 && out.count==2 && out.total==4 && out.unknown_types==1 && out.duplicates==1);
    assert(!strcmp(why,"sampled"));
    setup();bad_guard=1;memset(&out,0xa5,sizeof(out));before=out;
    assert(!kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(!queries && !releases && !memcmp(&out,&before,sizeof(out)));
    setup();mode=1;out=before;
    assert(!kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(queries==1 && releases==1 && !strcmp(why,"native-array-shape") && !memcmp(&out,&before,sizeof(out)));
    setup();unreadable=1;
    assert(!kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(queries==1 && releases==1 && !strcmp(why,"native-array-unreadable"));
    setup();mode=2;
    assert(kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(out.total==922 && out.count==3 && queries==1 && releases==1);
    assert(out.resources[2]==origin+0x1b000);
    setup();mode=4;
    assert(kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(out.total==4097 && out.count==3 && queries==1 && releases==1);
    setup();mode=3;
    assert(kzsf_character_native_query(origin,read_memory,NULL,type_of,api,&out,&why));
    assert(!out.count && !out.total && queries==1 && releases==1);
    setup();expected_type=0x3caac10;
    assert(kzsf_character_native_query_type(origin,expected_type,read_memory,NULL,type_of,api,&out,&why));
    assert(out.count==2 && queries==1 && releases==1);
    setup();assert(!kzsf_character_native_query_type(origin,0x123,read_memory,NULL,type_of,api,&out,&why));
    assert(!queries && !releases);
    puts("native character query: real-image guards, SysV layout, type filtering, bounds and exact cleanup pass");
    return 0;
}
