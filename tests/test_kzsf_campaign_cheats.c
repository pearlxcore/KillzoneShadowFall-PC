#include "../src/kzsf_campaign_cheats_win.h"
#include <assert.h>
#include <stdio.h>
typedef struct { unsigned char actor[4096], table[512], ability[1024]; unsigned writes; } Fixture;
static int read_memory(void *p,uintptr_t at,void *out,size_t n) {
    uintptr_t first=(uintptr_t)p;
    if(at<first || n>sizeof(Fixture) || at-first>sizeof(Fixture)-n)return 0;
    memcpy(out,(void *)at,n);return 1;
}
static int write_memory(void *p,uintptr_t at,const void *in,size_t n) {
    unsigned char scratch[16];
    if(n>sizeof(scratch) || !read_memory(p,at,scratch,n))return 0;
    memcpy((void *)at,in,n);++((Fixture *)p)->writes;return 1;
}
static void pointer(void *p,uintptr_t v){memcpy(p,&v,8);}
static void integer(void *p,int v){memcpy(p,&v,4);}
static int getint(void *p){int v;memcpy(&v,p,4);return v;}
static void scalar(void *p,float v){memcpy(p,&v,4);}
static float getfloat(void *p){float v;memcpy(&v,p,4);return v;}
int main(void) {
    Fixture f={0}; uintptr_t base=0x800000000; CONTEXT c={0},before; int changed;
    pointer(f.actor,(uintptr_t)f.table);pointer(f.table+0x140,base+0xa7ab20);
    f.actor[0x282]=1;scalar(f.actor+0xa80,100);
    c.R13=(uintptr_t)f.actor;c.Rip=base+KZSF_STAMINA_SITE_A+1;c.EFlags=0x246;
    memset(&c.Xmm0,0x5a,sizeof(c.Xmm0));scalar(&c.Xmm0,12.5f);before=c;
    assert(kzsf_stamina_store(&c,base,read_memory,write_memory,&f,&changed));
    assert(changed && getfloat(f.actor+0xa7c)==100);
    assert(!memcmp(&c,&before,sizeof(c))); // store must not alter registers/flags/SIMD
    f.actor[0x282]=0;
    assert(kzsf_stamina_store(&c,base,read_memory,write_memory,&f,&changed));
    assert(!changed && getfloat(f.actor+0xa7c)==12.5f); // NPC original store
    f.actor[0x282]=1;scalar(f.actor+0xa80,NAN);
    assert(kzsf_stamina_store(&c,base,read_memory,write_memory,&f,&changed));
    assert(!changed && getfloat(f.actor+0xa7c)==12.5f);
    c.R13=UINTPTR_MAX;
    assert(!kzsf_stamina_store(&c,base,read_memory,write_memory,&f,&changed));
    pointer(f.ability+0x1e0,(uintptr_t)f.actor);
    for(int state=0;state<=7;++state) {
        integer(f.ability+0x2b8,state);integer(f.ability+0x2c0,1);f.ability[0x2c6]=0;
        unsigned writes=f.writes;
        int active=state==2 || state==3;
        assert(kzsf_continue_scan((uintptr_t)f.ability,base,read_memory,write_memory,&f)==active);
        assert(getint(f.ability+0x2c0)==(active?2:1));
        assert(f.ability[0x2c6]==active);
        if(!active)assert(f.writes==writes); // idle/cooldown/cancellation never forced active
    }
    integer(f.ability+0x2b8,2);integer(f.ability+0x2c0,5);
    assert(kzsf_continue_scan((uintptr_t)f.ability,base,read_memory,write_memory,&f));
    assert(getint(f.ability+0x2c0)==5);
    f.actor[0x282]=0;unsigned writes=f.writes;
    assert(!kzsf_continue_scan((uintptr_t)f.ability,base,read_memory,write_memory,&f));
    assert(f.writes==writes); // non-player scan state unchanged
    assert(!kzsf_continue_scan(UINTPTR_MAX,base,read_memory,write_memory,&f));
    puts("Campaign cheats: player/NPC isolation, invalid pointers, state transitions and SIMD preservation passed");
}
