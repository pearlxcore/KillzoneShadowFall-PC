#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_follow_baseline.h"
static unsigned char memory[0x17000];
static uintptr_t base=0x10000000;
static unsigned reads,mode_reads;static int race;
static void pointer(size_t offset,uintptr_t value){memcpy(memory+offset,&value,8);}
static void integer(size_t offset,int32_t value){memcpy(memory+offset,&value,4);}
static int read_memory(void *opaque,uintptr_t address,void *out,size_t n) {
    (void)opaque;++reads;
    if(address<base || address-base>sizeof(memory) || n>sizeof(memory)-(address-base))return 0;
    memcpy(out,memory+address-base,n);
    if(race && address==base+0xb100 && ++mode_reads==2)pointer(0x30d0,base+0x3120);
    return 1;
}
static uintptr_t type(uintptr_t object,uintptr_t image,KzsfZoomRead read,void *opaque) {
    uintptr_t result=0;(void)image;
    if(!read(opaque,object,&result,8))return 0;
    return result==base+0x12500 ? base+0x3c99960 : result;
}
static void setup(void) {
    memset(memory,0,sizeof(memory));reads=mode_reads=0;race=0;
    pointer(0x1000,base+0x3c8ebd0);pointer(0x2000,base+0x3ca98b0);
    pointer(0x3000,base+0x3c73920);pointer(0x4000,base+0x3caa190);
    pointer(0x5000,base+0x3cad168);pointer(0x6000,base+0x3c26d60);
    pointer(0x7000,base+0x3c95d88);pointer(0x8000,base+0x3cce7f8);
    pointer(0x8010,base+0x3000);pointer(0x8018,base+0x2000);pointer(0x8020,base+0x6000);
    pointer(0x8038,base+0x1048);float one=1;memcpy(memory+0x80f0,&one,4);
    pointer(0x30c0,base+0x8000);integer(0x30c8,1);pointer(0x30d0,base+0x3100);
    pointer(0x3100,base+0x1048);pointer(0x3120,base+0x1048);
    pointer(0x11e0,base+0x2000);pointer(0x11d8,base+0x7000);
    pointer(0x2290,base+0x4000);pointer(0x2900,base+0x5000);
    pointer(0x2a48,base+0xb000);pointer(0xb068,base+0xa048);pointer(0xa000,base+0x9000);
    pointer(0x9498,base+0xc24990);pointer(0x94a0,base+0xc249a0);pointer(0x94b0,base+0xbfe6c0);
    integer(0xa628,1);pointer(0xa630,base+0xb100);pointer(0xb100,base+0xc048);
    pointer(0xc048,base+0xd000);pointer(0xd068,base+0x3d8420);
    pointer(0xc060,base+0xe000);pointer(0xe028,base+0xcb54a0);pointer(0xe050,base+0x80ad70);
    pointer(0xc020,base+0xf000);integer(0xf068,2);
}
static int sample(KzsfFollowBaseline *out,const char **why) {
    return kzsf_follow_baseline_read(base+0x8000,base+0x6000,base,read_memory,NULL,type,out,why);
}
static void setup_climb(void) {
    setup();pointer(0x5000,base+0x3cada28);pointer(0x5028,base+0x2000);
    pointer(0x5030,base+0xf200);pointer(0xf278,base+0xf300);pointer(0x5138,base+0x10018);
    integer(0x1278,1);pointer(0x1280,base+0x1600);pointer(0x1600,base+0x10000);
    pointer(0x10000,base+0x1647070);integer(0x1002c,1);
    pointer(0x10070,base+0xf300);pointer(0x10050,base+0x10500);pointer(0x10500,base+0x1653360);
}
static void setup_combat(int transition) {
    setup_climb();pointer(0x5000,base+0x3cad168);integer(0x1002c,6);
    pointer(0x28f8,base+0x5200);pointer(0x5200,base+0x3ced350);
    pointer(0x5238,base+0x2000);pointer(0x5288,base+0x10018);
    pointer(0x5220,base+0xf200);memory[0x52dd]=1;
    if(transition) {
        pointer(0xf280,base+0xf400);pointer(0x10500,base+0x16534c0);
        pointer(0x10518,base+0xf400);
    }
}
static void setup_ladder(void) {
    setup();pointer(0x8018,base+0x11000);pointer(0x11000,base+0x3c9a390);
    pointer(0x11598,base+0x2048);pointer(0x11290,base+0x12000);
    pointer(0x12000,base+0x3c9a800);pointer(0x12040,base+0x11000);
    pointer(0x12028,base+0x13048);pointer(0x8038,base+0x13048);
    pointer(0x13000,base+0x3c8ebd0);pointer(0x131e0,base+0x11000);
    pointer(0x131d8,base+0x14000);pointer(0x14000,base+0x3c95d88);
    integer(0x30c8,2);pointer(0x3118,base+0x13048);
    pointer(0x28b8,base+0x15000);pointer(0x15000,base+0x3c74dc0);
    pointer(0x15038,base+0x11000);
}
int main(void) {
    KzsfFollowBaseline out,before;const char *why=NULL;unsigned char saved[sizeof(memory)];
    setup();memcpy(saved,memory,sizeof(memory));assert(sample(&out,&why));
    assert(!strcmp(why,"matches-baseline") && out.owner==base+0x2000);
    assert(!memcmp(saved,memory,sizeof(memory)));
    const uintptr_t traversal[]={0x3ca91b0,0x3caa970,0x3cada28,0x3cabd20,0x3cad4e8,0x3ca8d50,0x3ca9290,0x3cad788};
    for(unsigned i=0;i<sizeof(traversal)/sizeof(*traversal);i++) {
        setup();pointer(0x5000,base+traversal[i]);assert(sample(&out,&why));
        pointer(0x28b8,base+1);assert(!sample(&out,&why) && !strcmp(why,"player-override"));
    }
    const uintptr_t unsupported[]={0x3cabee0,0x3ca91b1,0x3cce7f8,0};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(*unsupported);i++) {
        setup();pointer(0x5000,unsupported[i] ? base+unsupported[i] : 0);
        assert(!sample(&out,&why) && !strcmp(why,"player-state"));
    }
    assert(!kzsf_follow_supported_state(base-1,base));
    /* Selector readiness and visibility use the same policy: floating is
     * immediately eligible, without waiting for a transition to walking. */
    setup();pointer(0x5000,base+0x3cad788);
    assert(sample(&out,&why) && kzsf_follow_model_state(type(out.state,base,read_memory,NULL),base));
    pointer(0x5000,base+0x3cad168);
    assert(sample(&out,&why) && kzsf_follow_model_state(type(out.state,base,read_memory,NULL),base));
    pointer(0x5000,base+0x3ca91b0);
    assert(sample(&out,&why) && !kzsf_follow_model_state(type(out.state,base,read_memory,NULL),base));
    assert(!kzsf_follow_model_state(base+0x3cad789,base));
    assert(!kzsf_follow_model_state(base-1,base));
    /* ZeroGravity is the exact observed owned state, not a bypass for
     * foreign cameras, control overrides, unknown layers or ADS. */
    setup();pointer(0x5000,base+0x3cad788);integer(0xc098,1);
    assert(!sample(&out,&why) && !strcmp(why,"zoom-selected"));
    setup();pointer(0x5000,base+0x3cad788);integer(0x1278,2);
    assert(!sample(&out,&why) && !strcmp(why,"camera-layers-present-or-invalid"));
    setup();pointer(0x5000,base+0x3cad788);pointer(0x11e0,base+1);
    assert(!sample(&out,&why) && !strcmp(why,"source-parent-or-rep"));
    setup();pointer(0x5000,base+0x3cad788);pointer(0x4000,base+0x3cad788);
    assert(!sample(&out,&why) && !strcmp(why,"controller-type"));
    setup();pointer(0xa000,base+0x3cb0c40);memcpy(saved,memory,sizeof(memory));
    assert(sample(&out,&why) && out.throwable==base+0xa000 && out.throwable_inventory==base+0xb000);
    assert(!out.zoom.weapon && !out.zoom.zoom_index && !memcmp(saved,memory,sizeof(memory)));
    integer(0x1278,1);assert(!sample(&out,&why)); /* Grenade does not bypass layers. */
    setup();pointer(0xa000,base+0x3cb0c41);assert(!sample(&out,&why) && !strcmp(why,"zoom-unknown"));
    setup();pointer(0xa000,base+0x3cb15e0);memcpy(saved,memory,sizeof(memory));
    assert(sample(&out,&why) && out.unarmed==base+0xa000 && out.unarmed_inventory==base+0xb000);
    assert(!out.throwable && !out.zoom.weapon && !memcmp(saved,memory,sizeof(memory)));
    integer(0x1278,1);assert(!sample(&out,&why)); /* Unarmed never bypasses unknown camera layers. */
    setup();pointer(0xa000,base+0x3cb15e1);assert(!sample(&out,&why));
    setup();pointer(0xa000,base+0x3cafa90);assert(sample(&out,&why));
    assert(out.knife==base+0xa000 && !out.throwable && !out.unarmed);
    setup_combat(1);pointer(0xa000,base+0x3cafa90);assert(sample(&out,&why));
    setup();pointer(0xb068,0);assert(!sample(&out,&why)); /* Null item is not InventoryNothing. */
    setup_climb();memcpy(saved,memory,sizeof(memory));assert(sample(&out,&why));
    assert(out.climb_layer==base+0x10000 && !memcmp(saved,memory,sizeof(memory)));
    integer(0x10028,1);assert(sample(&out,&why)); /* Native reverse/fade direction. */
    const struct {size_t p;uintptr_t v;} bad_climb[]={
        {0x5138,base+0x10020},{0x5028,base+1},{0x10000,base+1},
        {0x10070,base+1},{0x10500,base+1},{0x10050,0},{0xf278,0}};
    for(unsigned i=0;i<sizeof(bad_climb)/sizeof(*bad_climb);i++) {
        setup_climb();pointer(bad_climb[i].p,bad_climb[i].v);assert(!sample(&out,&why));
    }
    const struct {size_t p;int32_t v;} bad_climb_int[]={
        {0x1002c,4},{0x10028,2},{0x1003c,1},{0x1278,2},{0xc098,1}};
    for(unsigned i=0;i<sizeof(bad_climb_int)/sizeof(*bad_climb_int);i++) {
        setup_climb();integer(bad_climb_int[i].p,bad_climb_int[i].v);assert(!sample(&out,&why));
    }
    setup_climb();integer(0xc098,1);assert(!sample(&out,&why) && !strcmp(why,"zoom-selected"));
    setup_climb();pointer(0x5000,base+0x3cad168);assert(!sample(&out,&why));
    for(int transition=0;transition<2;transition++) {
        setup_combat(transition);memcpy(saved,memory,sizeof(memory));assert(sample(&out,&why));
        assert(out.combat_component==base+0x5200 && out.combat_layer==base+0x10000);
        assert(!memcmp(saved,memory,sizeof(memory)));
        integer(0x10028,1);assert(sample(&out,&why));
        const struct {size_t p;uintptr_t v;} bad_combat[]={
            {0x28f8,0},{0x5200,base+1},{0x5238,base+1},{0x5288,base+0x10020},
            {0x5220,0},{0xf278,0},{0x10000,base+1},{0x10070,base+1},
            {0x10050,0},{0x10500,base+1}};
        for(unsigned i=0;i<sizeof(bad_combat)/sizeof(*bad_combat);i++) {
            setup_combat(transition);pointer(bad_combat[i].p,bad_combat[i].v);
            memset(&out,0xa5,sizeof(out));before=out;assert(!sample(&out,&why));
            assert(!memcmp(&out,&before,sizeof(out)));
        }
        const struct {size_t p;int32_t v;} bad_combat_int[]={
            {0x1002c,5},{0x10028,2},{0x1003c,1},{0x1278,2},{0xc098,1}};
        for(unsigned i=0;i<sizeof(bad_combat_int)/sizeof(*bad_combat_int);i++) {
            setup_combat(transition);integer(bad_combat_int[i].p,bad_combat_int[i].v);
            assert(!sample(&out,&why));
        }
        setup_combat(transition);memory[0x52dd]=0;assert(!sample(&out,&why));
        integer(0x10028,1);memcpy(saved,memory,sizeof(memory));assert(sample(&out,&why));
        assert(!memcmp(saved,memory,sizeof(memory))); /* Owned post-execution fade. */
        pointer(0x5288,0);assert(!sample(&out,&why)); /* Fade still needs retained ownership. */
        setup_combat(transition);integer(0x10028,1);memory[0x52dd]=2;
        assert(!sample(&out,&why));
    }
    setup_combat(1);pointer(0x10518,base+1);assert(!sample(&out,&why));
    setup_ladder();memcpy(saved,memory,sizeof(memory));assert(sample(&out,&why));
    assert(out.owner==base+0x2000 && out.camera_owner==base+0x11000);
    assert(out.ladder_controller==base+0x12000 && out.retained_source==base+0x1048);
    assert(!memcmp(saved,memory,sizeof(memory)));
    const struct {size_t p;uintptr_t v;} bad_ladder[]={
        {0x11598,0},{0x11598,base+0x3048},{0x12000,base+0x3caa190},
        {0x12040,base+0x2000},{0x12028,base+0x1048},{0x3118,base+0x1048},
        {0x3100,base+0x13048},{0x11e0,base+1},{0x7000,base+1},
        {0x131e0,base+0x2000},{0x14000,base+1},{0x28b8,0},
        {0x15000,base+1},{0x15038,base+0x2000},{0x8050,base+0x7048}};
    for(unsigned i=0;i<sizeof(bad_ladder)/sizeof(*bad_ladder);i++) {
        setup_ladder();pointer(bad_ladder[i].p,bad_ladder[i].v);
        memset(&out,0xa5,sizeof(out));before=out;assert(!sample(&out,&why));
        assert(!memcmp(&out,&before,sizeof(out)));
    }
    const struct {size_t p;int32_t v;} bad_ladder_int[]={
        {0x30c8,1},{0x30c8,3},{0x13278,1},{0xc098,1}};
    for(unsigned i=0;i<sizeof(bad_ladder_int)/sizeof(*bad_ladder_int);i++) {
        setup_ladder();integer(bad_ladder_int[i].p,bad_ladder_int[i].v);assert(!sample(&out,&why));
    }
    const uintptr_t interact_types[]={0x3c99180,0x3c9e7c0,0x3c9c150};
    for(unsigned i=0;i<3;i++) {
        setup_ladder();pointer(0x11000,base+interact_types[i]);
        pointer(0x12000,base+0x12500);
        pointer(0x12608,base+0xa6c710);pointer(0x12610,base+0xa6c8f0);
        if(i==2) {pointer(0x13000,base+0x3c288a8);pointer(0x14000,base+0x3c28988);}
        assert(sample(&out,&why));
        /* Actual active interaction: native use flag clears, pose owns object. */
        pointer(0x5000,base+0x3cac7a8);pointer(0x5028,base+0x2000);
        pointer(0x5040,base+0x11000);pointer(0x28b8,0);
        assert(sample(&out,&why) && out.pose_owner==base+0x11000);
        /* Animation-owned pose remains eligible without the walking input controller. */
        pointer(0x2290,0);assert(sample(&out,&why));
        pointer(0x2290,base+0x12000);assert(sample(&out,&why));
        pointer(0x5040,base+1);assert(!sample(&out,&why));
        pointer(0x5040,base+0x11000);pointer(0x2290,base+0x4000);
        pointer(0x5040,base+1);assert(!sample(&out,&why));
        pointer(0x5040,base+0x11000);pointer(0x5028,base+1);assert(!sample(&out,&why));
        pointer(0x5028,base+0x2000);
        pointer(0x8050,base+0x1048);float half=.5f;memcpy(memory+0x80f0,&half,4);
        assert(sample(&out,&why)); /* Native incoming blend, both owners proven. */
        pointer(0x8050,base+0x7048);assert(!sample(&out,&why));
        pointer(0x8050,base+0x1048);pointer(0x12608,base+0xab4330);assert(!sample(&out,&why));
        pointer(0x12608,base+0xa6c710);
        /* Prelude and return handoff keep the Soldier camera as the source. */
        pointer(0x8018,base+0x2000);pointer(0x8038,base+0x1048);integer(0x30c8,1);
        pointer(0x5000,base+0x3cad168);pointer(0x28b8,base+0x15000);
        pointer(0x8050,0);float one=1;memcpy(memory+0x80f0,&one,4);
        assert(sample(&out,&why));
        pointer(0x11598,0);assert(sample(&out,&why)); /* Requested use before actor attaches. */
        pointer(0x11598,base+0x2048);
        pointer(0x8050,base+0x13048);memcpy(memory+0x80f0,&half,4);
        assert(sample(&out,&why));
        pointer(0x28b8,0);assert(sample(&out,&why));
        pointer(0x5000,base+0x3cac7a8);assert(sample(&out,&why));
        pointer(0x5040,base+1);assert(!sample(&out,&why));
        pointer(0x5040,base+0x11000);pointer(0x5000,base+0x3cad168);
        pointer(0x11598,0);assert(sample(&out,&why)); /* Outgoing view retains camera after actor detaches. */
        pointer(0x11598,base+0x3048);assert(!sample(&out,&why));
    }
    const struct {size_t offset;uintptr_t value;} bad_ptr[]={
        {0x8000,0},{0x6000,0},{0x8020,base+1},{0x8050,base+0x1048},
        {0x3000,0},{0x2000,0},{0x30c0,base+1},{0x3100,base+0x1148},
        {0x1000,0},{0x11e0,base+1},{0x7000,0},{0x4000,0},{0x5000,0},
        {0x28b8,base+1},{0x2a48,UINTPTR_MAX},{0xe028,0},{0x30d0,UINTPTR_MAX}};
    for(unsigned i=0;i<sizeof(bad_ptr)/sizeof(*bad_ptr);i++) {
        setup();pointer(bad_ptr[i].offset,bad_ptr[i].value);
        memset(&out,0xa5,sizeof(out));before=out;
        assert(!sample(&out,&why));assert(!memcmp(&out,&before,sizeof(out)));
    }
    const struct {size_t offset;int32_t value;} bad_int[]={
        {0x30c8,0},{0x30c8,2},{0x30c8,65},{0x1278,1},{0x1278,-1},{0xc098,1}};
    for(unsigned i=0;i<sizeof(bad_int)/sizeof(*bad_int);i++) {
        setup();integer(bad_int[i].offset,bad_int[i].value);assert(!sample(&out,&why));
    }
    setup();float nan=0;uint32_t bits=0x7fc00000;memcpy(&nan,&bits,4);
    memcpy(memory+0x80f0,&nan,4);assert(!sample(&out,&why));
    setup();race=1;memset(&out,0xa5,sizeof(out));before=out;
    assert(!sample(&out,&why) && !strcmp(why,"changed-during-sample"));
    assert(!memcmp(&out,&before,sizeof(out)));
    reads=0;assert(!kzsf_follow_baseline_read(base,base,UINTPTR_MAX,read_memory,NULL,type,&out,&why));
    assert(!reads);assert(!kzsf_follow_baseline_read(base,base,base,NULL,NULL,type,&out,&why));
    assert(!kzsf_follow_baseline_read(base,base,base,read_memory,NULL,type,NULL,&why));
    /* Guard supported native traversal metadata against the prepared image. */
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t image_size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&image_size,8,1,f)==1);
    const uint32_t getters[]={0xbd17f0,0xbd2890,0xbd2400,0xbd22d0,0xbd4db0,0xbd13c0,0xbd13f0};
    for(unsigned i=0;i<sizeof(getters)/sizeof(*getters);i++) {
        unsigned char code[8];int32_t relative;
        assert(!_fseeki64(f,-(long long)image_size+getters[i],SEEK_END));assert(fread(code,1,8,f)==8);
        assert(code[0]==0x48 && code[1]==0x8d && code[2]==0x05 && code[7]==0xc3);
        memcpy(&relative,code+3,4);assert((int64_t)getters[i]+7+relative==(int64_t)traversal[i]);
    }
    unsigned char grenade_getter[8];int32_t grenade_relative;
    assert(!_fseeki64(f,-(long long)image_size+0xc27e60,SEEK_END));assert(fread(grenade_getter,1,8,f)==8);
    assert(!memcmp(grenade_getter,"\x48\x8d\x05",3) && grenade_getter[7]==0xc3);
    memcpy(&grenade_relative,grenade_getter+3,4);assert(0xc27e67+grenade_relative==0x3cb0c40);
    assert(!_fseeki64(f,-(long long)image_size+0xf62ec0,SEEK_END));assert(fread(grenade_getter,1,8,f)==8);
    assert(!memcmp(grenade_getter,"\x48\x8d\x05",3) && grenade_getter[7]==0xc3);
    memcpy(&grenade_relative,grenade_getter+3,4);assert(0xf62ec7+grenade_relative==0x3ced350);
    const uint32_t ladder_getters[]={0xac7490,0xac8790,0x96df50,0xc29710,0xc25860,
        0xa80910,0xaf45b0,0xac8210,0x6abd00,0x6aabc0,0xa80aa0,0xbd32c0};
    const uint32_t ladder_types[]={0x3c9a390,0x3c9a800,0x3c74dc0,0x3cb15e0,0x3cafa90,
        0x3c99180,0x3c9e7c0,0x3c9c150,0x3c288a8,0x3c28988,0x3c99960,0x3cac7a8};
    for(unsigned i=0;i<sizeof(ladder_getters)/sizeof(*ladder_getters);i++) {
        assert(!_fseeki64(f,-(long long)image_size+ladder_getters[i],SEEK_END));
        assert(fread(grenade_getter,1,8,f)==8);
        assert(!memcmp(grenade_getter,"\x48\x8d\x05",3) && grenade_getter[7]==0xc3);
        memcpy(&grenade_relative,grenade_getter+3,4);
        assert(ladder_getters[i]+7+grenade_relative==ladder_types[i]);
    }
    fclose(f);
    puts("Follow baseline: traversal allowlist/image guards, ownership, ADS/layers/unknown refusal, races and no writes pass.");
}
