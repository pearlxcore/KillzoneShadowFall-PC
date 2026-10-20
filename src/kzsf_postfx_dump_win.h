/* CUSA00190 01.81: opt-in diagnostic dump of the per-view post-process object.
 *
 * PURPOSE (investigation aid, not a gameplay feature). Chromatic aberration and
 * the cosmetic vignette strength are properties of the lens/ILR settings family
 * whose consumers could not be isolated statically, and the auto-exposure
 * metering pass is still unlocated. This hook prints the runtime values that
 * those consumers read, so they can be identified from a single play session
 * instead of guessed.
 *
 * WHERE. FUN_00577950 is the post-process chain. Just before it calls
 * FUN_00579850 (the bit 2/4 bloom+vignette+ILR-element group) the compiler
 * emits:
 *
 *   0x57808e  mov rax,[rbp-0x1f0]     ; rax = the view's post-process object
 *   0x578095  lea rdx,[rax+0x3d0]     ; rdx = post+0x3d0 (bloom rect + ILR lists)
 *   0x57809f  mov rcx,r15
 *   0x5780a2  call FUN_00579850
 *
 * The ABI above was read from the instructions, not inferred from the
 * decompiler: FUN_00579850's third argument (RDX) is post+0x3d0, so the ILR
 * element lists are the {int count; int pad; void *data} heads at post+0x400
 * (param_3+0x30) and post+0x418 (param_3+0x48), and the group's rect/scale
 * floats are at post+0x3ec..post+0x3f4.
 *
 * WHY THIS SITE. `lea rdx,[rax+0x3d0]` only writes RDX and sets no flags, so the
 * handler can dump and then replay it exactly - no instruction relocation and no
 * stack manipulation. It runs once per view per frame while the mask's bits 2|4
 * are set; the caller bounds the output to the first few hits.
 *
 * FUN_00577270 uploads a 24-float constant block built from post+0x30..0x5f, so
 * that block is dumped too: it is the per-view parameter block most likely to
 * carry vignette/CA/exposure scalars. Read-only, opt-in (KZSF_POSTFX_DUMP=1),
 * and it never writes to the guest. */
#ifndef KZSF_POSTFX_DUMP_WIN_H
#define KZSF_POSTFX_DUMP_WIN_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { KZSF_POSTFX_SITE=0x578095, KZSF_POSTFX_GUARD=0x57808e, KZSF_POSTFX_LEN=7 };
static const unsigned char kzsf_postfx_signature[]={
    0x48,0x8b,0x85,0x10,0xfe,0xff,0xff, /* mov rax,[rbp-0x1f0] */
    0x48,0x8d,0x90,0xd0,0x03,0x00,0x00};/* lea rdx,[rax+0x3d0] */
typedef int (*KzsfPostfxRead)(void *,uintptr_t,void *,size_t);
/* Reads count dwords and prints them; a failed read prints "-" instead.
 * out == NULL suppresses all printing (the handler still replays the instruction). */
static inline void kzsf_postfx_dwords(uintptr_t address,unsigned count,KzsfPostfxRead read,
                                      void *opaque,FILE *out) {
    if(!out) return;
    for(unsigned i=0;i<count;++i) {
        uint32_t value=0;
        if(read(opaque,address+i*4,&value,4)) fprintf(out," %08x",value);
        else fprintf(out," ????????");
    }
}
static inline void kzsf_postfx_floats(uintptr_t address,unsigned count,KzsfPostfxRead read,
                                      void *opaque,FILE *out) {
    if(!out) return;
    for(unsigned i=0;i<count;++i) {
        float value=0.0f;
        if(read(opaque,address+i*4,&value,4)) fprintf(out," %.5g",(double)value);
        else fprintf(out," ??????");
    }
}
/* One {int count; int pad; void *data} list head at post+offset. */
static inline void kzsf_postfx_list(uintptr_t post,unsigned offset,const char *name,
                                    KzsfPostfxRead read,void *opaque,FILE *out) {
    if(!out) return;
    int32_t count=0; uint64_t data=0;
    if(!read(opaque,post+offset,&count,4)) { fprintf(out,"  %s: unreadable\n",name); return; }
    read(opaque,post+offset+8,&data,8);
    fprintf(out,"  %s: count=%d data=%p\n",name,(int)count,(void *)(uintptr_t)data);
    if(count<=0 || count>64 || !data) return;
    unsigned records=count>4 ? 4u : (unsigned)count;
    for(unsigned r=0;r<records;++r) {
        fprintf(out,"    [%u]",r);
        kzsf_postfx_dwords(data+(uintptr_t)r*0x10,4,read,opaque,out);
        fprintf(out,"\n");
    }
    if((unsigned)count>records) fprintf(out,"    ... %d more\n",(int)count-(int)records);
}
/* Dumps the post-process object, then replays the displaced LEA. Returns 1 when the
 * context was advanced (the caller then continues execution). Read failures are
 * reported, never dereferenced directly. */
static inline int kzsf_postfx_dump(CONTEXT *c,uintptr_t base,KzsfPostfxRead read,void *opaque,
                                   FILE *out) {
    if(!c || !read || c->Rip!=(DWORD64)(base+KZSF_POSTFX_SITE)) return 0;
    const uintptr_t post=(uintptr_t)c->Rax;
    if(out) {
        fprintf(out,"KILLZONE_POSTFX post=%p\n",(void *)post);
        fprintf(out,"  view block +0x30..+0x5f:"); kzsf_postfx_dwords(post+0x30,12,read,opaque,out); fprintf(out,"\n");
        fprintf(out,"  as floats:");              kzsf_postfx_floats(post+0x30,12,read,opaque,out); fprintf(out,"\n");
        fprintf(out,"  rect/scale +0x3ec..+0x3f4:");kzsf_postfx_dwords(post+0x3ec,4,read,opaque,out); fprintf(out,"\n");
        /* FUN_0057b280 (final composite) reads its tone-map/exposure terms from
         * post+0x730, +0x734 and post+0x740 (the last gated by mask bit 6). */
        fprintf(out,"  composite in +0x700..+0x760:"); kzsf_postfx_dwords(post+0x700,24,read,opaque,out); fprintf(out,"\n");
        fprintf(out,"  composite floats:");           kzsf_postfx_floats(post+0x700,24,read,opaque,out); fprintf(out,"\n");
        /* Lens/post settings block uploaded as shader constants by FUN_005791d0 and
         * passed by address into FUN_00598da0. Zero in the image; written at run time. */
        fprintf(out,"  lens block image+0x3c1a0c0:"); kzsf_postfx_dwords(base+0x3c1a0c0,4,read,opaque,out);
        fprintf(out,"  0x3c1a0d0:");                 kzsf_postfx_dwords(base+0x3c1a0d0,4,read,opaque,out);
        fprintf(out,"  0x3c1a0e0:");                 kzsf_postfx_dwords(base+0x3c1a0e0,4,read,opaque,out); fprintf(out,"\n");
        fprintf(out,"  lens floats:");               kzsf_postfx_floats(base+0x3c1a0c0,12,read,opaque,out); fprintf(out,"\n");
        fprintf(out,"  flags +0x7c0..+0x7ff:");    kzsf_postfx_dwords(post+0x7c0,8,read,opaque,out); fprintf(out,"\n");
        kzsf_postfx_list(post,0x400,"ILR list A",read,opaque,out);
        kzsf_postfx_list(post,0x418,"ILR list B",read,opaque,out);
    }
    /* Replay `lea rdx,[rax+0x3d0]`: writes RDX only, no flags. */
    c->Rdx=(DWORD64)(post+0x3d0);
    c->Rip=(DWORD64)(base+KZSF_POSTFX_SITE+KZSF_POSTFX_LEN);
    return 1;
}
#endif
