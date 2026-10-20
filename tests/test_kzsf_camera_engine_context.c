#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kzsf_camera_engine_context.h"
static unsigned char memory[0x2000];static uintptr_t base=0x10000000;
static int64_t displacement=-152;static unsigned reads;static int race;
static void pointer(size_t offset,uintptr_t value){memcpy(memory+offset,&value,8);}
static int read_memory(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;++reads;
    if(p==base+0x185e720 && n==8){memcpy(out,&displacement,8);return 1;}
    if(p<base || p-base>sizeof(memory) || n>sizeof(memory)-(p-base))return 0;
    memcpy(out,memory+p-base,n);
    if(race && p==base+0x168)pointer(0x168,base+0x600);
    return 1;
}
static void setup(void) {
    memset(memory,0,sizeof(memory));displacement=-152;race=0;reads=0;
    pointer(0x200,base+0x200);pointer(0x168,base+0x500);pointer(0x558,base+0x800);
    pointer(0x800,base+0x1000);pointer(0x1010,base+0x54640);
}
int main(void) {
    KzsfCameraEngineContext out,before;unsigned char saved[sizeof(memory)];
    setup();memcpy(saved,memory,sizeof(memory));
    assert(kzsf_camera_engine_context(base+0x200,base,read_memory,NULL,&out));
    assert(out.router==base+0x500 && out.heap==base+0x800 && out.allocate_method==base+0x54640);
    assert(!memcmp(memory,saved,sizeof(memory)));
    const struct {size_t offset;uintptr_t value;} bad[]={
        {0x200,0},{0x168,0},{0x558,UINTPTR_MAX},{0x800,0},{0x1010,base-1},{0x1010,base+0x1190000}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        setup();pointer(bad[i].offset,bad[i].value);memset(&out,0xa5,sizeof(out));before=out;
        assert(!kzsf_camera_engine_context(base+0x200,base,read_memory,NULL,&out));
        assert(!memcmp(&out,&before,sizeof(out)));
    }
    setup();displacement=-160;assert(!kzsf_camera_engine_context(base+0x200,base,read_memory,NULL,&out));
    setup();race=1;assert(!kzsf_camera_engine_context(base+0x200,base,read_memory,NULL,&out));
    reads=0;assert(!kzsf_camera_engine_context(1,base,read_memory,NULL,&out));
    assert(!kzsf_camera_engine_context(base,UINTPTR_MAX,read_memory,NULL,&out));assert(!reads);
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;int64_t real;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(!_fseeki64(f,-(long long)size+0x185e720,SEEK_END));assert(fread(&real,8,1,f)==1);
    assert(real==-152);fclose(f);
    puts("Camera engine context: router chain, overflow/unreadable/layout refusal, rechecks, no writes and real TLS offset pass.");
}
