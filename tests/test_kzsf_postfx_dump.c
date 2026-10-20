/* CUSA00190 01.81 post-process diagnostic dump hook: real-image signature, exact
 * LEA replay (RDX only, no flags), read-failure robustness and refusal cases. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/kzsf_postfx_dump_win.h"

/* Synthetic guest memory: one post-process object plus its list data. */
static unsigned char object[0x800];
static unsigned char records[0x40];
static int read_ok=1;
static int fail_at_nonzero=0;
static int reads;
static int in_range(uintptr_t address,size_t size,const unsigned char *buffer,size_t length) {
    uintptr_t base=(uintptr_t)buffer;
    return address>=base && size<=length && address-base<=length-size;
}
static int read_guest(void *unused,uintptr_t address,void *out,size_t size) {
    (void)unused; ++reads;
    if(!read_ok) return 0;
    if(in_range(address,size,object,sizeof(object)) || in_range(address,size,records,sizeof(records))) {
        memcpy(out,(void *)address,size);
        return 1;
    }
    return 0;
}
static void put32(uintptr_t address,uint32_t value) { memcpy((void *)address,&value,4); }
static void put64(uintptr_t address,uint64_t value) { memcpy((void *)address,&value,8); }

static CONTEXT context_at(uintptr_t rip,uintptr_t rax) {
    CONTEXT c; memset(&c,0xa5,sizeof(c));
    c.Rip=rip; c.Rax=rax; return c;
}
int main(void) {
    /* 1. The guard must match the real linked image and the site must be the LEA. */
    FILE *file=fopen("out/boot-linked.bin","rb"); assert(file);
    unsigned char magic[8]; uint64_t image_size;
    assert(fread(magic,1,8,file)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&image_size,8,1,file)==1 && image_size<512*1024*1024);
    unsigned char signature[sizeof(kzsf_postfx_signature)];
    assert(!_fseeki64(file,-(long long)image_size+KZSF_POSTFX_GUARD,SEEK_END));
    assert(fread(signature,1,sizeof(signature),file)==sizeof(signature)); fclose(file);
    assert(!memcmp(signature,kzsf_postfx_signature,sizeof(signature)));
    /* Bytes at the site itself: 48 8d 90 d0 03 00 00 = lea rdx,[rax+0x3d0]. */
    assert(!memcmp(signature+KZSF_POSTFX_SITE-KZSF_POSTFX_GUARD,"\x48\x8d\x90\xd0\x03\x00\x00",7));

    /* 2. A wrong RIP is refused and nothing is written. */
    const uintptr_t base=0x10000000ull, site=base+KZSF_POSTFX_SITE;
    CONTEXT c=context_at(site+1,(uintptr_t)object), original=c;
    assert(!kzsf_postfx_dump(&c,base,read_guest,NULL,NULL));
    assert(!memcmp(&c,&original,sizeof(c)));

    /* 3. Normal replay: RDX = post+0x3d0, RIP advanced, every other byte kept. */
    memset(object,0,sizeof(object)); memset(records,0x11,sizeof(records));
    put32((uintptr_t)object+0x30,0x3f800000u);                 /* 1.0f */
    put32((uintptr_t)object+0x34,0x00000000u);                 /* 0.0f */
    put32((uintptr_t)object+0x400,2);                          /* list A count */
    put64((uintptr_t)object+0x408,(uintptr_t)records);         /* list A data */
    put32((uintptr_t)object+0x418,0);                          /* list B empty */
    c=context_at(site,(uintptr_t)object); original=c;
    assert(kzsf_postfx_dump(&c,base,read_guest,NULL,NULL));     /* quiet replay */
    assert(c.Rdx==(DWORD64)((uintptr_t)object+0x3d0));
    assert(c.Rip==(DWORD64)(site+KZSF_POSTFX_LEN));
    /* Only RDX and RIP may differ: LEA writes RDX and sets no flags. */
    {
        CONTEXT expected=original; expected.Rdx=c.Rdx; expected.Rip=c.Rip;
        assert(!memcmp(&c,&expected,sizeof(c)));
        assert(c.EFlags==original.EFlags);
    }

    /* 4. Printing path: values are read from the object and its list data. */
    reads=0;
    c=context_at(site,(uintptr_t)object);
    FILE *log=fopen("postfx-dump-test.log","wb"); assert(log);
    assert(kzsf_postfx_dump(&c,base,read_guest,NULL,log));
    fclose(log);
    assert(reads>0);
    const int baseline_reads=reads;                 /* a normal dump's read count */
    {
        FILE *in=fopen("postfx-dump-test.log","rb"); assert(in);
        char text[2048]; size_t n=fread(text,1,sizeof(text)-1,in); text[n]=0; fclose(in);
        assert(strstr(text,"KILLZONE_POSTFX post="));
        assert(strstr(text,"3f800000"));            /* the view block is printed */
        assert(strstr(text,"as floats: 1 0"));      /* 1.0f 0.0f  */
        assert(strstr(text,"composite in +0x700..+0x760:")); /* composite inputs dumped */
        assert(strstr(text,"composite floats:"));
        assert(strstr(text,"lens block image+0x3c1a0c0:"));  /* lens cluster dumped */
        assert(strstr(text,"lens floats:"));
        assert(strstr(text,"ILR list A: count=2"));
        assert(strstr(text,"11111111"));            /* records[0] is dumped */
        assert(strstr(text,"ILR list B: count=0"));
        /* Every object-derived line must be fully readable (the lens cluster lives
         * outside this fake image, so only those lines may show '?'). */
        for(const char *line=strtok(text,"\n");line;line=strtok(NULL,"\n"))
            if(!strstr(line,"lens") && !strstr(line,"flags"))
                assert(!strstr(line,"????"));
        remove("postfx-dump-test.log");
    }

    /* 5. Unreadable guest memory must not crash and must still replay. */
    c=context_at(site,0xdeadbeefull); original=c;
    log=fopen("postfx-dump-test.log","wb"); assert(log);
    assert(kzsf_postfx_dump(&c,base,read_guest,NULL,log));      /* replay still happens */
    fclose(log);
    assert(c.Rdx==(DWORD64)(0xdeadbeefull+0x3d0));
    assert(c.Rip==(DWORD64)(site+KZSF_POSTFX_LEN));
    remove("postfx-dump-test.log");

    /* 6. A failing reader with a valid site: replay must still advance the context. */
    read_ok=0;
    c=context_at(site,(uintptr_t)object); original=c;
    assert(kzsf_postfx_dump(&c,base,read_guest,NULL,NULL));
    assert(c.Rdx==(DWORD64)((uintptr_t)object+0x3d0) && c.Rip==(DWORD64)(site+KZSF_POSTFX_LEN));
    read_ok=1;
    /* 7. A null reader is refused rather than dereferenced. */
    assert(!kzsf_postfx_dump(&c,base,NULL,NULL,NULL));
    assert(!kzsf_postfx_dump(NULL,base,read_guest,NULL,NULL));
    /* 8. Out-of-range list counts are reported but never walked. */
    memset(object,0,sizeof(object));
    put32((uintptr_t)object+0x400,4096);                        /* absurd count */
    put64((uintptr_t)object+0x408,0x1000);                      /* bogus pointer */
    reads=0;
    c=context_at(site,(uintptr_t)object);
    log=fopen("postfx-dump-test.log","wb"); assert(log);
    assert(kzsf_postfx_dump(&c,base,read_guest,NULL,log));
    fclose(log);
    /* An absurd count (4096 > 64) must add no further reads at all. */
    assert(reads<=baseline_reads);
    remove("postfx-dump-test.log");
    (void)fail_at_nonzero;
    puts("PASS: post-process dump guard, LEA replay (RDX only, flags intact), "
         "read-failure robustness and refusal cases");
    return 0;
}
