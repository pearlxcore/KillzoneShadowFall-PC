#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_camera_observer_win.h"

static unsigned char memory[0x9000];
static uintptr_t memory_base=0x10000000;
static unsigned reads;
static int layer_race;
static int fake_read(void *opaque,uintptr_t p,void *out,size_t n) {
    (void)opaque;++reads;
    if(p<memory_base || p-memory_base>sizeof(memory) || n>sizeof(memory)-(p-memory_base))return 0;
    memcpy(out,memory+(p-memory_base),n);
    if(layer_race && p==memory_base+0x278) {
        int32_t changed=1;memcpy(memory+0x278,&changed,4);
    }
    return 1;
}
static void pointer(size_t p,uintptr_t value){memcpy(memory+p,&value,8);}
static void test_zoom_log(void) {
    memset(memory,0,sizeof(memory));
    pointer(0xa48,memory_base+0x2000);pointer(0x2068,memory_base+0x4048);
    pointer(0x4000,memory_base+0x1000);
    pointer(0x1498,memory_base+0xc24990);pointer(0x14a0,memory_base+0xc249a0);
    pointer(0x14b0,memory_base+0xbfe6c0);
    int32_t one=1,two=2;memcpy(memory+0x4628,&one,4);
    pointer(0x4630,memory_base+0x3000);pointer(0x3000,memory_base+0x5048);
    pointer(0x5048,memory_base+0x6000);pointer(0x6068,memory_base+0x3d8420);
    pointer(0x5060,memory_base+0x7000);pointer(0x7028,memory_base+0xcb54a0);
    pointer(0x7050,memory_base+0x80ad70);pointer(0x5020,memory_base+0x8000);
    memcpy(memory+0x8068,&two,4);memcpy(memory+0x5098,&one,4);
    unsigned char saved[sizeof(memory)];memcpy(saved,memory,sizeof(memory));
    FILE *f=tmpfile();assert(f);char text[1024]={0};
    kzsf_observer_zoom(f,memory_base,memory_base,fake_read,NULL);
    rewind(f);assert(fread(text,1,sizeof(text)-1,f)>0);fclose(f);
    assert(strstr(text,"weapon_zoom status=sampled"));
    assert(strstr(text,"zoom_index=1 zoom_count=2 selected=1 atomic=0"));
    assert(!memcmp(memory,saved,sizeof(memory)));memset(memory,0,sizeof(memory));
}
static void test_layers(void) {
    uintptr_t base=memory_base;int32_t count=1,direction=1,key=7;
    memset(memory,0,sizeof(memory));
    memcpy(memory+0x278,&count,4);pointer(0x280,memory_base+0x1000);
    pointer(0x1000,memory_base+0x1800);pointer(0x1800,base+0x1647070);
    memcpy(memory+0x1828,&direction,4);memcpy(memory+0x182c,&key,4);
    float weight=0.5f;memcpy(memory+0x1848,&weight,4);
    unsigned char saved[sizeof(memory)];memcpy(saved,memory,sizeof(memory));
    FILE *f=tmpfile();assert(f);char text[4096]={0};
    kzsf_observer_layers(f,memory_base,base+0x3c95d88,base,fake_read,NULL);
    rewind(f);assert(fread(text,1,sizeof(text)-1,f)>0);fclose(f);
    assert(strstr(text,"layout=known key=7 direction=1"));
    assert(strstr(text,"weight=0.5 complete=0"));
    assert(strstr(text,"recheck=unchanged atomic=0"));assert(!memcmp(memory,saved,sizeof(memory)));
    /* Completed blend-in is reported as direction0/complete1, never removal. */
    direction=0;memcpy(memory+0x1828,&direction,4);memory[0x184c]=1;
    f=tmpfile();assert(f);memset(text,0,sizeof(text));
    kzsf_observer_layers(f,memory_base,base+0x3c96808,base,fake_read,NULL);
    rewind(f);assert(fread(text,1,sizeof(text)-1,f)>0);fclose(f);
    assert(strstr(text,"direction=0") && strstr(text,"complete=1"));
    pointer(0x1800,0);
    f=tmpfile();assert(f);memset(text,0,sizeof(text));
    kzsf_observer_layers(f,memory_base,base+0x3c95d88,base,fake_read,NULL);
    rewind(f);assert(fread(text,1,sizeof(text)-1,f)>0);assert(strstr(text,"layout=unknown"));fclose(f);
    count=65;memcpy(memory+0x278,&count,4);reads=0;f=tmpfile();assert(f);
    kzsf_observer_layers(f,memory_base,base+0x3c95d88,base,fake_read,NULL);
    assert(reads==1);fclose(f);
    reads=0;f=tmpfile();assert(f);
    kzsf_observer_layers(f,memory_base,base+0x3ca98b0,base,fake_read,NULL);assert(!reads);
    kzsf_observer_layers(f,memory_base,0,UINTPTR_MAX,fake_read,NULL);assert(!reads);fclose(f);
    count=0;memcpy(memory+0x278,&count,4);layer_race=1;
    f=tmpfile();assert(f);memset(text,0,sizeof(text));
    kzsf_observer_layers(f,memory_base,base+0x3c95d88,base,fake_read,NULL);
    rewind(f);assert(fread(text,1,sizeof(text)-1,f)>0);fclose(f);layer_race=0;
    assert(strstr(text,"recheck=changed-or-unreadable"));
    memset(memory,0,sizeof(memory));
}

int main(void) {
    test_layers();
    test_zoom_log();
    CONTEXT c,before,expected;
    for(unsigned kind=0;kind<3;kind++) {
        memset(&c,0xa5,sizeof(c));c.Rsp=memory_base+0x800;c.Rbx=memory_base;
        pointer(0,0x12345678);before=c;expected=c;
        if(kind<2) {
            expected.Rbp=c.Rsp;
            expected.Rip=0x800000000+(kind ? KZSF_CAMERA_BIND_ENTRY : KZSF_CAMERA_SELECT_ENTRY)+4;
        } else {expected.Rax=0x12345678;expected.Rip=0x800000000+KZSF_FREE_CAMERA_SITE+3;}
        assert(kzsf_observer_replay(&c,0x800000000,kind,fake_read,NULL));
        assert(!memcmp(&c,&expected,sizeof(c)));
    }
    c=before;c.Rbx=UINTPTR_MAX;before=c;
    assert(!kzsf_observer_replay(&c,0,2,fake_read,NULL));assert(!memcmp(&c,&before,sizeof(c)));
    assert(!kzsf_observer_replay(&c,0,3,fake_read,NULL));assert(!memcmp(&c,&before,sizeof(c)));
    char output[16];reads=0;
    assert(!kzsf_observer_read(fake_read,NULL,UINTPTR_MAX-2,4,output,1));
    assert(!kzsf_observer_read(fake_read,NULL,0,0,output,1));assert(reads==0);

    /* Type decoding must not call a target function or follow arbitrary code. */
    uintptr_t base=memory_base-0x118f000;
    pointer(0,memory_base+0x100);pointer(0x100,memory_base+0x200);
    unsigned char accessor[]={0x48,0x8d,0x05,0,0,0,0,0xc3};
    int32_t displacement=0x300-0x207;
    memcpy(accessor+3,&displacement,4);memcpy(memory+0x200,accessor,8);
    pointer(0x358,memory_base+0x1500);strcpy((char*)memory+0x1500,"TestCameraResource");
    char name[96];
    assert(kzsf_observer_type(memory_base,base,fake_read,NULL,name)==memory_base+0x300);
    assert(kzsf_observer_baseline_type(memory_base,base,fake_read,NULL)==memory_base+0x300);
    assert(!strcmp(name,"TestCameraResource"));
    memory[0x200]=0xe9;
    assert(!kzsf_observer_type(memory_base,base,fake_read,NULL,name));assert(!strcmp(name,"<undecoded>"));
    assert(!kzsf_observer_baseline_type(memory_base,base,fake_read,NULL));

    /* Cyclic entity hierarchies and corrupt/large component counts are bounded. */
    memset(memory,0,sizeof(memory));pointer(0x1e0,memory_base);
    FILE *f=tmpfile();assert(f);reads=0;
    kzsf_observer_entity(f,memory_base,base,fake_read,NULL);assert(reads<30);
    int32_t count=257;memcpy(memory+0x250,&count,4);reads=0;
    kzsf_observer_entity(f,memory_base,base,fake_read,NULL);assert(reads<20);
    count=256;memcpy(memory+0x250,&count,4);pointer(0x258,memory_base+0x1000);
    reads=0;kzsf_observer_entity(f,memory_base,base,fake_read,NULL);assert(reads<100);
    memset(&c,0xa5,sizeof(c));c.R14=UINTPTR_MAX;c.Rbx=0;before=c;
    reads=0;kzsf_observer_snapshot(f,&c,base,fake_read,NULL);assert(reads<30);
    assert(!memcmp(&c,&before,sizeof(c)));fclose(f);
    memset(memory,0,sizeof(memory));
    c.R14=memory_base;c.Rbx=memory_base+0x1800;before=c;
    pointer(0x10,memory_base+0x800);count=1;memcpy(memory+0x8c8,&count,4);
    pointer(0x8d0,memory_base+0x1000);pointer(0x1000,memory_base+0x1648);
    pointer(0x1890,memory_base+0x1900);
    unsigned char saved[sizeof(memory)];memcpy(saved,memory,sizeof(memory));
    f=tmpfile();assert(f);kzsf_observer_snapshot(f,&c,base,fake_read,NULL);
    assert(!memcmp(&c,&before,sizeof(c)) && !memcmp(saved,memory,sizeof(memory)));
    rewind(f);char text[4096]={0};assert(fread(text,1,sizeof(text)-1,f)>0);
    assert(strstr(text,"user_control_stack_count=1"));
    assert(strstr(text,"view_sources recheck=unchanged outgoing_present=0 atomic=0"));
    assert(!strstr(text,"role=controlled-controller-candidate"));fclose(f);

    /* A verified Soldier/controller type permits the native IsAiming read;
     * unrelated camera objects must never be interpreted through that layout. */
    memset(memory,0,sizeof(memory));pointer(0,memory_base+0x100);
    pointer(0x100,memory_base+0x200);
    displacement=(int32_t)(0x3ca98b0-(memory_base+0x207-base));
    memcpy(accessor+3,&displacement,4);memcpy(memory+0x200,accessor,8);
    pointer(0x290,memory_base+0x800);pointer(0x800,memory_base+0x900);
    pointer(0x900,memory_base+0xa00);
    displacement=(int32_t)(0x3caa190-(memory_base+0xa07-base));
    memcpy(accessor+3,&displacement,4);memcpy(memory+0xa00,accessor,8);
    count=2;memcpy(memory+0xa6c,&count,4);memcpy(saved,memory,sizeof(memory));
    f=tmpfile();assert(f);kzsf_observer_entity(f,memory_base,base,fake_read,NULL);
    rewind(f);memset(text,0,sizeof(text));assert(fread(text,1,sizeof(text)-1,f)>0);
    assert(strstr(text,"IsAiming=1 aiming_enum=2"));
    assert(strstr(text,"weapon_zoom status=unknown"));
    assert(!memcmp(saved,memory,sizeof(memory)));fclose(f);

    f=fopen("out/boot-linked.bin","rb");assert(f);
    char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    const struct {uint64_t a;const unsigned char *b;size_t n;} guards[]={
        {KZSF_CAMERA_SELECT_ENTRY,kzsf_camera_select_guard,sizeof(kzsf_camera_select_guard)},
        {KZSF_CAMERA_BIND_ENTRY,kzsf_camera_bind_guard,sizeof(kzsf_camera_bind_guard)},
        {KZSF_FREE_CAMERA_GUARD,kzsf_free_camera_signature,sizeof(kzsf_free_camera_signature)}};
    for(unsigned i=0;i<3;i++) {
        unsigned char bytes[64];assert(guards[i].n<=sizeof(bytes));
        assert(!_fseeki64(f,-(long long)size+guards[i].a,SEEK_END));
        assert(fread(bytes,1,guards[i].n,f)==guards[i].n);assert(!memcmp(bytes,guards[i].b,guards[i].n));
    }
    fclose(f);puts("Camera observer: context replay, layers/races, source rechecks, zoom integration, bounded reads and real-image guards pass.");
}
