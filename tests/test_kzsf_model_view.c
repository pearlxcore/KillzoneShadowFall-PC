#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_model_view_win.h"
static uint64_t resource;
static int read_resource(void *unused,uintptr_t p,void *out,size_t n) {
    (void)unused;
    if(p!=0x100001d0 || n!=8)return 0;
    memcpy(out,&resource,8);return 1;
}
int main(void) {
    CONTEXT c,before,expected;int changed=-1;
    memset(&c,0xa5,sizeof(c));c.Rbx=0x10000000;c.R14&=~UINT64_C(255);before=c;
    resource=0x123456789;
    assert(kzsf_model_fp(&c,0x800000000,0,read_resource,NULL,&changed));
    expected=before;expected.Rdi=resource;expected.Rip=0x800000000+KZSF_MODEL_FP_SITE+7;
    assert(!changed && !memcmp(&c,&expected,sizeof(c)));
    c=before;
    assert(kzsf_model_fp(&c,0x800000000,1,read_resource,NULL,&changed));
    expected=before;expected.Rip=0x800a69d23;
    assert(changed && !memcmp(&c,&expected,sizeof(c))); // no stack/guest writes; flags preserved
    c.Rbx=UINT64_MAX;before=c;
    assert(!kzsf_model_fp(&c,0,0,read_resource,NULL,&changed));
    assert(!memcmp(&c,&before,sizeof(c)));
    for(unsigned active=0;active<2;++active)for(unsigned al=0;al<2;++al) {
        memset(&c,0x5a,sizeof(c));c.Rax=(c.Rax&~UINT64_C(255))|al;before=c;
        kzsf_model_world(&c,0x800000000,active,&changed);
        expected=before;expected.Rcx=(c.Rcx&~UINT64_C(255))|(active ? 1 : al);
        expected.Rip=0x800000000+KZSF_MODEL_WORLD_SITE+2;
        assert(!memcmp(&c,&expected,sizeof(c)));
        assert(changed==(active && !al));
    }
    memset(&c,0x5a,sizeof(c));before=c;
    kzsf_model_world_hidden(&c,0x800000000,&changed);
    expected=before;expected.Rcx&=~UINT64_C(255);expected.Rip=0x800000000+KZSF_MODEL_WORLD_SITE+2;
    assert(changed && !memcmp(&c,&expected,sizeof(c)));
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);
    char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&size,8,1,f)==1);
    const struct {uint64_t address;const unsigned char *bytes;size_t count;} guards[]={
        {0xa69c70,kzsf_model_fp_signature,sizeof(kzsf_model_fp_signature)},
        {0xa69dc0,kzsf_model_world_signature,sizeof(kzsf_model_world_signature)}};
    for(unsigned i=0;i<2;++i) {
        unsigned char bytes[256];assert(guards[i].count<=sizeof(bytes));
        assert(!_fseeki64(f,-(long long)size+guards[i].address,SEEK_END));
        assert(fread(bytes,1,guards[i].count,f)==guards[i].count);
        assert(!memcmp(bytes,guards[i].bytes,guards[i].count));
    }
    fclose(f);
    puts("Model view: original replay, active native branches, full context preservation and whole-function image guards pass.");
}
