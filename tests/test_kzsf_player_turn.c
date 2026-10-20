#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/kzsf_player_turn_win.h"
static int read_mem(void *unused,uintptr_t address,void *data,size_t size) {
    (void)unused;memcpy(data,(void*)address,size);return 1;
}
static int write_mem(void *unused,uintptr_t address,const void *data,size_t size) {
    (void)unused;memcpy((void*)address,data,size);return 1;
}
int main(void) {
    unsigned char stack[0x300],object[0x80];unsigned user=0;
    for(unsigned who=0;who<2;++who) for(unsigned axis=0;axis<2;++axis)
    for(int percent=50;percent<=300;percent+=50) {
        memset(stack,0x5a,sizeof(stack));memset(object,0x7b,sizeof(object));
        CONTEXT c,expected;memset(&c,0xa5,sizeof(c));
        c.Rbp=(uintptr_t)stack+0x200;c.R12=(uintptr_t)object;
        user=who?7:0;uintptr_t input=(uintptr_t)&user;
        memcpy((void*)(c.Rbp-0x1c0),&input,sizeof(input));
        float h=.8f,p=.5f;memcpy((void*)(c.Rbp-0xac),&h,4);memcpy((void*)(c.Rbp-0xb0),&p,4);
        unsigned char original[sizeof(stack)];memcpy(original,stack,sizeof(stack));
        float before[2],after[2];unsigned reported;
        expected=c;memcpy(&expected.Rax,object+0x40,8);
        expected.Rip=0x800000000ull+KZSF_PLAYER_TURN_SITE+5;
        assert(kzsf_player_turn(&c,0x800000000ull,axis?100:percent,axis?percent:100,read_mem,write_mem,NULL,before,after,&reported));
        assert(!memcmp(&c,&expected,sizeof(c)) && reported==user);
        float target=who||axis?h:h*((float)percent/100.f);
        float pitch_target=who||!axis?p:p*((float)percent/100.f);
        assert(after[0]==target && after[1]==pitch_target && before[0]==h && before[1]==p);
        memcpy(original+0x200-0xac,&target,4);
        memcpy(original+0x200-0xb0,&pitch_target,4);
        assert(!memcmp(stack,original,sizeof(stack)));
    }
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long length=ftell(f);
    /* The boot container ends in the guest image; locate unique guard bytes. */
    rewind(f);unsigned char *boot=malloc((size_t)length);assert(boot);
    assert(fread(boot,1,(size_t)length,f)==(size_t)length);fclose(f);
    unsigned matches=0;
    for(long i=0;i<=length-(long)sizeof(kzsf_player_turn_signature);++i)
        if(!memcmp(boot+i,kzsf_player_turn_signature,sizeof(kzsf_player_turn_signature)))++matches;
    assert(matches==1);free(boot);
    puts("PASS: player factor scaling, original/invalid-user behavior, exact context/stack preservation, real boot signature");
}
