#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kzsf_turn_speed_win.h"
static unsigned char object[0x400];
static uintptr_t expected_field;
static int write_value(void *unused,uintptr_t address,const void *value,size_t size) {
    (void)unused;
    if(address!=(uintptr_t)object+expected_field || size!=4) return 0;
    memcpy((void *)address,value,size); return 1;
}
static void check(int pitch,float input,int percent,unsigned user,float expected,uintptr_t field) {
    const uintptr_t site=pitch?KZSF_PITCH_SITE:KZSF_TURN_SITE;
    CONTEXT c,original;
    memset(&c,0xa5,sizeof(c));
    memset(object,0x6b,sizeof(object));
    c.Rbx=(uintptr_t)object; c.R14=user;
    memcpy(&c.Xmm0,&input,4); original=c;
    expected_field=field;
    float applied=-1.0f;
    assert(kzsf_engine_rate_store(&c,0x800000000ull,pitch,percent,write_value,NULL,&applied));
    original.Rip=0x800000000ull+site+8;
    assert(!memcmp(&c,&original,sizeof(c))); /* every register, flags and SIMD preserved */
    assert(!memcmp(object+field,&expected,4));
    assert(!memcmp(&applied,&expected,4));
    for(unsigned i=0;i<sizeof(object);++i)
        if(i<field || i>=field+4) assert(object[i]==0x6b);
}
int main(void) {
    /* The pitch store follows the heading store by one instruction; the same 32-byte
     * signature covers both. */
    assert(KZSF_PITCH_SITE==KZSF_TURN_SITE+16);
    assert(sizeof(kzsf_turn_signature)==32);
    for(int pitch=0;pitch<2;++pitch) {
        const uintptr_t field=pitch?KZSF_PITCH_FIELD:KZSF_TURN_FIELD;
        check(pitch,.25f,100,0,.25f,field);         /* 100% is the original store */
        check(pitch,-.25f,200,0,-.5f,field);        /* signed input keeps its sign */
        check(pitch,1.f,150,0,1.5f,field);
        check(pitch,1.f,300,3,3.f,field);           /* user 3 is the last local user */
        check(pitch,1.f,200,4,1.f,field);           /* user 4: original value */
        check(pitch,1.f,0,0,1.f,field);             /* out of range: original value */
        check(pitch,-0.f,100,0,-0.f,field);         /* signed zero exact at 100% */
        check(pitch,0.f,300,0,0.f,field);
        check(pitch,INFINITY,200,0,INFINITY,field); /* non-finite: original value */
        CONTEXT c,original; memset(&c,0,sizeof(c)); c.Rbx=1; original=c;
        expected_field=field;
        assert(!kzsf_engine_rate_store(&c,0,pitch,150,write_value,NULL,NULL));
        assert(!memcmp(&c,&original,sizeof(c)));    /* writer failure leaves the context alone */
    }
    /* The heading-only convenience wrapper still targets the heading field. */
    CONTEXT c; memset(&c,0xa5,sizeof(c)); memset(object,0x6b,sizeof(object));
    c.Rbx=(uintptr_t)object; c.R14=0; float one=1.0f; memcpy(&c.Xmm0,&one,4);
    expected_field=KZSF_TURN_FIELD;
    assert(kzsf_handle_turn_store(&c,0x800000000ull,200,write_value,NULL));
    float two=2.0f; assert(!memcmp(object+KZSF_TURN_FIELD,&two,4));
    assert(object[KZSF_PITCH_FIELD]==0x6b);          /* pitch field untouched */
    FILE *file=fopen("out/boot-linked.bin","rb"); assert(file);
    unsigned char magic[8]; uint64_t image_size;
    assert(fread(magic,1,8,file)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&image_size,8,1,file)==1 && image_size<512*1024*1024);
    unsigned char signature[sizeof(kzsf_turn_signature)];
    assert(!_fseeki64(file,-(long long)image_size+KZSF_TURN_SITE-8,SEEK_END));
    assert(fread(signature,1,sizeof(signature),file)==sizeof(signature)); fclose(file);
    assert(!memcmp(signature,kzsf_turn_signature,sizeof(signature)));
    puts("PASS: actual boot signature covers heading+pitch, per-axis scaling, 100% identity, "
         "local-user guard, failure and full context preservation");
}
