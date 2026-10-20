/* CUSA00190 / 01.81 native button-icon lookup diagnostics.
 * No icon substitution yet: the two native texture paths need runtime proof.
 * The entry instructions below were verified in Ghidra, against eboot.elf SHA256
 * 2d5ddd30d14ac252b2b40e79350b2900250612abba6142c8cee6d207bcf6e3fb.
 * Trace only with KZSF_PROMPT_TRACE=1. No guest data is changed by the handler.
 */
#ifndef KZSF_PROMPT_TRACE_WIN_H
#define KZSF_PROMPT_TRACE_WIN_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*KzsfPromptRead)(void *,uintptr_t,void *,size_t);
typedef struct {
    uint32_t entry,site;
    const char *name;
    unsigned length;
    unsigned char signature[24];
} KzsfPromptTraceSite;
static const KzsfPromptTraceSite kzsf_prompt_sites[]={
    {0xdee8a0,0xdee8a1,"button",21,{0x53,0x44,0x8b,0x57,0x18,0x45,0x31,0xc0,
        0x49,0xc7,0xc1,0xf0,0xff,0xff,0xff,0x41,0xbb,0xfc,0xff,0xff,0xff}},
    {0xdee930,0xdee931,"axis",21,{0x53,0x44,0x8b,0x57,0x18,0x45,0x31,0xc0,
        0x49,0xc7,0xc1,0xf0,0xff,0xff,0xff,0x41,0xbb,0xfc,0xff,0xff,0xff}},
    {0xdee9c0,0xdee9c1,"gamefunction-uv",21,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,
        0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x4d,0x89,0xc6,0x48,0x89,0x4d,0xd0}},
    {0xdeebd0,0xdeebd1,"gamefunction-texture",20,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,
        0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xd6,0x41,0x89,0xf4}},
};
enum { KZSF_PROMPT_TRACE_SITES=4, KZSF_PROMPT_TRACE_RECORD_LIMIT=32 };

static inline int kzsf_prompt_trace_signature(const unsigned char *image,size_t size,unsigned i) {
    if(!image || i>=KZSF_PROMPT_TRACE_SITES) return 0;
    const KzsfPromptTraceSite *s=&kzsf_prompt_sites[i];
    return s->entry<=size && s->length<=size-s->entry &&
        !memcmp(image+s->entry,s->signature,s->length);
}

static inline int kzsf_prompt_read(KzsfPromptRead read,void *opaque,uintptr_t base,
                                  size_t offset,void *out,size_t size) {
    return base && offset<=UINTPTR_MAX-base && size<=UINTPTR_MAX-(base+offset) &&
        read(opaque,base+offset,out,size);
}

/* Match Windows CONTEXT to the guest System V ABI. Only the overwritten MOV's
 * destination and RIP change, including when output is suppressed. Never infer
 * the hook from CONTEXT.Rip: Windows breakpoint RIP can point after INT3. */
static inline int kzsf_prompt_trace_replay(CONTEXT *c,uintptr_t base,unsigned i,
                                         KzsfPromptRead read,void *opaque) {
    if(!c || !read || i>=KZSF_PROMPT_TRACE_SITES) return 0;
    if(i<2) {
        uint32_t count;
        if(!kzsf_prompt_read(read,opaque,c->Rdi,0x18,&count,4)) return 0;
        c->R10=count; /* MOV R10D zero-extends; flags unchanged. */
        c->Rip=base+kzsf_prompt_sites[i].site+4;
    } else {
        c->Rbp=c->Rsp;
        c->Rip=base+kzsf_prompt_sites[i].site+3;
    }
    return 1;
}

static inline void kzsf_prompt_trace_bytes(FILE *out,const char *name,uintptr_t address,
                                          size_t size,KzsfPromptRead read,void *opaque) {
    unsigned char data[128];
    if(size>sizeof(data)) return;
    fprintf(out,"  %s address=%p",name,(void *)address);
    if(!kzsf_prompt_read(read,opaque,address,0,data,size)) {fputs(" unreadable\n",out);return;}
    for(size_t k=0;k<size;k++) fprintf(out,"%s%02x",k%8 ? "" : " ",data[k]);
    fputc('\n',out);
}

/* Bounded read-only snapshot. These are raw records, not a claimed decoded GPU
 * texture descriptor. A corrupt count/pointer cannot cause unbounded reads. */
static inline void kzsf_prompt_trace_dump(FILE *out,const CONTEXT *c,uintptr_t base,
                                         unsigned i,int include_collection,
                                         KzsfPromptRead read,void *opaque) {
    if(!out || !c || !read || i>=KZSF_PROMPT_TRACE_SITES) return;
    uint64_t caller=0;
    kzsf_prompt_read(read,opaque,c->Rsp,8,&caller,8); /* PUSH RBX/RBP has executed. */
    fprintf(out,"KILLZONE_PROMPT path=%s request=%d collection=%p caller=0x%llx\n",
        kzsf_prompt_sites[i].name,(int)(uint32_t)c->Rsi,(void *)(uintptr_t)c->Rdi,
        (unsigned long long)(caller>=base ? caller-base : caller));
    if(!include_collection) return;
    int32_t count=0;
    uint64_t records=0,uv=0;
    if(!kzsf_prompt_read(read,opaque,c->Rdi,0x18,&count,4) ||
       !kzsf_prompt_read(read,opaque,c->Rdi,0x20,&records,8) ||
       !kzsf_prompt_read(read,opaque,c->Rdi,0x40,&uv,8)) {
        fputs("  collection unreadable\n",out);return;
    }
    fprintf(out,"  count=%d entries=%p uv=%p\n",count,(void *)(uintptr_t)records,(void *)(uintptr_t)uv);
    kzsf_prompt_trace_bytes(out,"collection",c->Rdi,0x80,read,opaque);
    if(c->Rdi<=UINTPTR_MAX-0x50)
        kzsf_prompt_trace_bytes(out,"texture-reference",c->Rdi+0x50,0x20,read,opaque);
    uint64_t texture=0;
    if(kzsf_prompt_read(read,opaque,c->Rdi,0x50,&texture,8) && texture)
        kzsf_prompt_trace_bytes(out,"texture-reference-target",texture,0x80,read,opaque);
    if(count<0 || count>64 || (count && (!records || !uv))) {
        fputs("  rejected collection bounds\n",out);return;
    }
    int limit=count>KZSF_PROMPT_TRACE_RECORD_LIMIT ? KZSF_PROMPT_TRACE_RECORD_LIMIT : count;
    for(int n=0;n<limit;n++) {
        unsigned char record[0x30]; uint64_t icon=0; int32_t button=0,axis=0;
        if(!kzsf_prompt_read(read,opaque,records,(size_t)n*0x30,record,sizeof(record))) break;
        memcpy(&button,record,4);memcpy(&axis,record+4,4);memcpy(&icon,record+0x28,8);
        fprintf(out,"  entry=%d button=%d axis=%d icon=%p\n",n,button,axis,(void *)(uintptr_t)icon);
        kzsf_prompt_trace_bytes(out,"entry-bytes",records+(uintptr_t)n*0x30,0x30,read,opaque);
        if(uv<=UINTPTR_MAX-(uintptr_t)n*0x40)
            kzsf_prompt_trace_bytes(out,"uv-quad",uv+(uintptr_t)n*0x40,0x40,read,opaque);
        if(icon) {
            kzsf_prompt_trace_bytes(out,"icon-record",icon,0x30,read,opaque);
            texture=0;
            if(kzsf_prompt_read(read,opaque,icon,0x18,&texture,8) && texture)
                kzsf_prompt_trace_bytes(out,"icon-texture-target",texture,0x80,read,opaque);
        }
    }
    if(count>limit) fprintf(out,"  %d entries omitted\n",count-limit);
}
#endif
