/* Run from the project root; checks the real image and native instruction replay. */
#include <assert.h>
#include <stdlib.h>
#include "../src/kzsf_prompt_trace_win.h"
static unsigned char collection[0x80],records[64*0x30],quads[64*0x40],stack[16];
static unsigned reads;
static int range(uintptr_t a,size_t n,const void *p,size_t size) {
    return a>=(uintptr_t)p && n<=size && a-(uintptr_t)p<=size-n;
}
static int read_guest(void *opaque,uintptr_t a,void *out,size_t n) {
    (void)opaque;++reads;
    if(range(a,n,collection,sizeof(collection)) || range(a,n,records,sizeof(records)) ||
       range(a,n,quads,sizeof(quads)) || range(a,n,stack,sizeof(stack))) {
        memcpy(out,(void *)a,n);return 1;
    }
    return 0;
}
static void put(void *at,uint64_t value,size_t n) {memcpy(at,&value,n);}
int main(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);
    unsigned char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&size,8,1,f)==1 && size<512ull*1024*1024);
    unsigned char *image=calloc(1,(size_t)size);assert(image);
    for(unsigned i=0;i<KZSF_PROMPT_TRACE_SITES;i++) {
        const KzsfPromptTraceSite *s=&kzsf_prompt_sites[i];
        assert(!_fseeki64(f,-(long long)size+s->entry,SEEK_END));
        assert(fread(image+s->entry,1,s->length,f)==s->length);
        assert(kzsf_prompt_trace_signature(image,size,i));
        assert(!kzsf_prompt_trace_signature(image,s->entry+s->length-1,i));
        for(unsigned j=0;j<s->length;j++) {
            image[s->entry+j]^=1;
            assert(!kzsf_prompt_trace_signature(image,size,i));
            image[s->entry+j]^=1;
        }
    }
    fclose(f);free(image);
    const uintptr_t base=0x10000000;
    put(collection+0x18,0xfffffffeu,4);
    for(unsigned i=0;i<KZSF_PROMPT_TRACE_SITES;i++) {
        CONTEXT c,expected;memset(&c,0xa5,sizeof(c));
        c.Rip=base+kzsf_prompt_sites[i].site+1; /* Windows INT3 context */
        c.Rdi=(uintptr_t)collection;c.Rsp=(uintptr_t)stack;expected=c;
        if(i<2) {expected.R10=0xfffffffeu;expected.Rip=base+kzsf_prompt_sites[i].site+4;}
        else {expected.Rbp=c.Rsp;expected.Rip=base+kzsf_prompt_sites[i].site+3;}
        assert(kzsf_prompt_trace_replay(&c,base,i,read_guest,NULL));
        assert(!memcmp(&c,&expected,sizeof(c)));
    }
    CONTEXT c;memset(&c,0,sizeof(c));c.Rdi=UINTPTR_MAX-8;
    CONTEXT before=c;
    assert(!kzsf_prompt_trace_replay(&c,base,0,read_guest,NULL));
    assert(!memcmp(&c,&before,sizeof(c)));
    assert(!kzsf_prompt_trace_replay(&c,base,4,read_guest,NULL));
    assert(!kzsf_prompt_read(read_guest,NULL,UINTPTR_MAX-1,2,&before,1));
    memset(collection,0,sizeof(collection));
    put(collection+0x18,64,4);put(collection+0x20,(uintptr_t)records,8);
    put(collection+0x40,(uintptr_t)quads,8);put(stack+8,base+0xa33b5a,8);
    c.Rdi=(uintptr_t)collection;c.Rsp=(uintptr_t)stack;c.Rsi=2;
    f=tmpfile();assert(f);reads=0;
    kzsf_prompt_trace_dump(f,&c,base,0,1,read_guest,NULL);
    assert(reads<110);assert(ftell(f)<15000);
    rewind(f);char text[16000]={0};fread(text,1,sizeof(text)-1,f);fclose(f);
    assert(strstr(text,"request=2") && strstr(text,"caller=0xa33b5a"));
    assert(strstr(text,"entry=31 ") && !strstr(text,"entry=32 "));
    assert(strstr(text,"32 entries omitted"));
    put(collection+0x18,65,4);f=tmpfile();assert(f);reads=0;
    kzsf_prompt_trace_dump(f,&c,base,0,1,read_guest,NULL);assert(reads<10);fclose(f);
    f=tmpfile();assert(f);reads=0;
    kzsf_prompt_trace_dump(f,&c,base,0,0,read_guest,NULL);assert(reads==1);fclose(f);
    puts("Native prompt trace: real-image guards, register replay and bounded reads pass.");
}
