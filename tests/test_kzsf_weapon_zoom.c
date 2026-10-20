#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kzsf_weapon_zoom.h"
static unsigned char mem[0x9000];
static uintptr_t origin=0x10000000;
static int race;
static int read_mem(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;
    if(p<origin || p-origin>sizeof(mem) || n>sizeof(mem)-(p-origin))return 0;
    memcpy(out,mem+p-origin,n);
    if(race && p==origin+0x4918){int32_t changed=1;memcpy(mem+0x4918,&changed,4);}
    return 1;
}
static void ptr(size_t p,uintptr_t v){memcpy(mem+p,&v,8);}
static void integer(size_t p,int32_t v){memcpy(mem+p,&v,4);}
static void setup(void) {
    memset(mem,0,sizeof(mem));race=0;
    ptr(0xa48,origin+0x2000);ptr(0x2068,origin+0x4048);ptr(0x4000,origin+0x1000);
    ptr(0x1498,origin+0xc24990);ptr(0x14a0,origin+0xc249a0);ptr(0x14b0,origin+0xbfe6c0);
    integer(0x4628,2);integer(0x4918,0);ptr(0x4630,origin+0x3000);
    ptr(0x3000,origin+0x5048);ptr(0x5048,origin+0x6000);ptr(0x6068,origin+0x3d8420);
    ptr(0x5060,origin+0x7000);ptr(0x7028,origin+0xcb54a0);ptr(0x7050,origin+0x80ad70);
    ptr(0x5020,origin+0x8000);integer(0x8068,3);integer(0x5098,0);
}
int main(void) {
    KzsfWeaponZoom z,before;unsigned char copy[sizeof(mem)];
    for(int index=0;index<3;index++) {
        setup();integer(0x5098,index);memcpy(copy,mem,sizeof(mem));
        assert(kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));
        assert(z.weapon==origin+0x4000 && z.mode_interface==origin+0x5048);
        assert(z.zoom_interface==origin+0x5060 && z.zoom_index==index && z.zoom_count==3);
        assert(!memcmp(mem,copy,sizeof(mem)));
    }
    setup();ptr(0x3008,origin+0x5048);integer(0x4918,1);integer(0x5098,2);
    assert(kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));
    assert(z.mode_index==1 && z.zoom_index==2);
    const size_t methods[]={0x1498,0x14a0,0x14b0,0x6068,0x7028,0x7050};
    for(unsigned i=0;i<sizeof(methods)/sizeof(*methods);i++) {
        setup();ptr(methods[i],0);memset(&z,0xa5,sizeof(z));before=z;
        assert(!kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));assert(!memcmp(&z,&before,sizeof(z)));
    }
    const struct {size_t p;int32_t v;} invalid[]={{0x4628,65},{0x4918,-1},{0x4918,2},
        {0x8068,0},{0x8068,65},{0x5098,-1},{0x5098,3}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        setup();integer(invalid[i].p,invalid[i].v);
        assert(!kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));
    }
    setup();race=1;assert(!kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));
    setup();ptr(0x3000,UINTPTR_MAX-2);assert(!kzsf_weapon_zoom_read(origin,origin,read_mem,NULL,&z));
    assert(!kzsf_weapon_zoom_read(UINTPTR_MAX,origin,read_mem,NULL,&z));
    assert(!kzsf_weapon_zoom_read(origin,UINTPTR_MAX,read_mem,NULL,&z));
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);
    char magic[8];uint64_t image_size=0;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&image_size,8,1,f)==1);
    const struct {uint64_t a;size_t n;unsigned char b[16];} guards[]={
        {0xc24990,7,{0x8b,0x87,0x28,0x06,0,0,0xc3}},
        {0xc249a0,15,{0x48,0x63,0xc6,0x48,0x8b,0x8f,0x30,0x06,0,0,0x48,0x8b,0x04,0xc1,0xc3}},
        {0xbfe6c0,6,{0x8b,0xb7,0x18,0x09,0,0}},
        {0x3d8420,5,{0x48,0x8d,0x47,0x18,0xc3}},
        {0xcb54a0,8,{0x83,0x7f,0x38,0,0x0f,0x95,0xc0,0xc3}},
        {0x80ad70,4,{0x8b,0x47,0x38,0xc3}},
        {0xcb2df0,8,{0x48,0x8b,0x47,0xc0,0x8b,0x40,0x68,0xc3}}};
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char b[16];assert(guards[i].a<=image_size);
        assert(!_fseeki64(f,-(long long)image_size+guards[i].a,SEEK_END));
        assert(fread(b,1,guards[i].n,f)==guards[i].n && !memcmp(b,guards[i].b,guards[i].n));
    }
    fclose(f);
    puts("Weapon zoom reader: modes, zoom selection, layout refusal, bounds, unchanged output and no writes pass.");
}
