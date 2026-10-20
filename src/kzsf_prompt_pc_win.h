/* Isolated native PC-artwork prototype, CUSA00190 / 01.81 only.
 * Uses the engine's constructor and external-pixel initialization path, as in
 * its generated font texture creator (31a540). No copied texture objects.
 * Lifetime: one atlas retained until process exit; original resources untouched.
 */
#ifndef KZSF_PROMPT_PC_WIN_H
#define KZSF_PROMPT_PC_WIN_H
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kzsf_prompt_art.h"
#include "kzsf_prompt_trace_win.h"

enum {KZSF_PC_WIDTH=2048,KZSF_PC_HEIGHT=1024,
      KZSF_PC_BYTES=KZSF_PC_WIDTH*KZSF_PC_HEIGHT*4};
static const uint32_t kzsf_pc_calls[]={0xfc5152,0xe038dc,0xe06c8c,0xe07c98};
typedef struct {
    uint64_t original_texture,texture;
    uint64_t holder[9];
    float uv[16][16];
    uint32_t supported;
    unsigned draws;
} KzsfPromptPc;
typedef int (*KzsfPromptWrite)(void *,uintptr_t,const void *,size_t);
typedef uint64_t (*KzsfPromptCreate)(void *,const uint32_t *,size_t);
typedef int (*KzsfPromptBinding)(unsigned,int,char *,size_t);

/* Complete draw prologue plus the first complete stack store. */
static const unsigned char kzsf_pc_draw_signature[]={
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
    0x48,0x81,0xec,0x18,0x01,0x00,0x00,0x4c,0x89,0x85,0xd8,0xfe,0xff,0xff};
typedef struct {uint32_t address;unsigned length;unsigned char bytes[24];} KzsfPcGuard;
static const KzsfPcGuard kzsf_pc_guards[]={
    {0x321970,5,{0xe9,0x5b,0x7a,0x00,0x00}}, /* native new thunk -> 3293d0 */
    {0x2cea70,24,{0x48,0x8d,0x05,0x69,0x24,0x21,0x01,0x48,0x83,0xc0,0x10,0x48,
        0x89,0x07,0xb8,0x00,0x00,0x00,0xf8,0x23,0x47,0x08,0x0d,0x00}},
    {0x2d0db0,24,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
        0x53,0x48,0x81,0xec,0xf8,0x00,0x00,0x00,0x44,0x89,0x8d,0x40}},
};

static inline int kzsf_pc_signature(const unsigned char *image,size_t size) {
    if(!image || size<0xfc4d90+sizeof(kzsf_pc_draw_signature) ||
       memcmp(image+0xfc4d90,kzsf_pc_draw_signature,sizeof(kzsf_pc_draw_signature))) return 0;
    for(unsigned i=0;i<sizeof(kzsf_pc_guards)/sizeof(*kzsf_pc_guards);++i) {
        const KzsfPcGuard *g=&kzsf_pc_guards[i];
        if(g->address>size || g->length>size-g->address ||
           memcmp(image+g->address,g->bytes,g->length)) return 0;
    }
    for(unsigned i=0;i<sizeof(kzsf_pc_calls)/sizeof(*kzsf_pc_calls);++i) {
        uint32_t site=kzsf_pc_calls[i];int32_t relative;
        if(site>size || 5>size-site || image[site]!=0xe8) return 0;
        memcpy(&relative,image+site+1,4);
        if((int64_t)site+5+relative!=0xfc4d90) return 0;
    }
    return 1;
}

/* Collect only the gameplay atlas established by both captures. Build the
 * complete bitmap before calling the native constructor; a bad/missing binding
 * retains the original glyph. Texture/program lifetime is not borrowed from
 * the game's holder: explicit program pointer and its slot fields are cleared. */
static inline int kzsf_pc_prepare(KzsfPromptPc *pc,uintptr_t base,uintptr_t collection,
                                  int mouse,KzsfPromptRead read,void *opaque,
                                  KzsfPromptBinding binding,KzsfPromptCreate create,void *creator) {
    int32_t count=0;uint64_t entries=0,quads=0,source=0,vtable=0;uint32_t dims[2],flags=0;
    if(!pc || !read || !binding || !create ||
       !kzsf_prompt_read(read,opaque,collection,0x18,&count,4) || count!=27 ||
       !kzsf_prompt_read(read,opaque,collection,0x20,&entries,8) ||
       !kzsf_prompt_read(read,opaque,collection,0x40,&quads,8) ||
       !kzsf_prompt_read(read,opaque,collection,0x50,&source,8) ||
       !kzsf_prompt_read(read,opaque,source,0,&vtable,8) || vtable!=base+0x14e0420 ||
       !kzsf_prompt_read(read,opaque,source,8,&flags,4) || ((flags>>15)&255)!=14 ||
       !kzsf_prompt_read(read,opaque,source,0x18,dims,sizeof(dims)) ||
       dims[0]!=KZSF_PC_WIDTH || dims[1]!=KZSF_PC_HEIGHT) return 0;
    KzsfPromptPc candidate;memset(&candidate,0,sizeof(candidate));
    if(!kzsf_prompt_read(read,opaque,collection,0x50,candidate.holder,sizeof(candidate.holder))) return 0;
    uint32_t *pixels=calloc(1,KZSF_PC_BYTES);if(!pixels) return 0;
    for(int n=0;n<count;++n) {
        unsigned char item[0x30];int32_t button,axis,rect[4];uint64_t icon;
        if(!kzsf_prompt_read(read,opaque,entries,(size_t)n*0x30,item,sizeof(item))) goto invalid;
        memcpy(&button,item,4);memcpy(&axis,item+4,4);memcpy(&icon,item+0x28,8);
        if(axis!=-1 || button<0 || button>=16) continue;
        char label[16];int kind=binding((unsigned)button,mouse,label,sizeof(label));
        if(!kind) continue;
        memcpy(rect,item+0x10,sizeof(rect));
        if(icon && !kzsf_prompt_read(read,opaque,icon,0x20,rect,sizeof(rect))) goto invalid;
        float *uv=candidate.uv[button];
        if(!kzsf_prompt_read(read,opaque,quads,(size_t)n*0x40,uv,64)) goto invalid;
        const float expected[8]={(float)rect[0]/KZSF_PC_WIDTH,(float)rect[1]/KZSF_PC_HEIGHT,
            (float)rect[2]/KZSF_PC_WIDTH,(float)rect[1]/KZSF_PC_HEIGHT,
            (float)rect[2]/KZSF_PC_WIDTH,(float)rect[3]/KZSF_PC_HEIGHT,
            (float)rect[0]/KZSF_PC_WIDTH,(float)rect[3]/KZSF_PC_HEIGHT};
        for(unsigned v=0;v<4;++v) for(unsigned a=0;a<2;++a)
            if(!isfinite(uv[v*4+a]) || fabsf(uv[v*4+a]-expected[v*2+a])>0.000001f) goto invalid;
        if(!kzsf_prompt_art(pixels,KZSF_PC_WIDTH,KZSF_PC_HEIGHT,rect,kind,label)) goto invalid;
        candidate.supported|=1u<<button;
    }
    if(!candidate.supported) goto invalid;
    candidate.texture=create(creator,pixels,KZSF_PC_BYTES);
    free(pixels);
    if(!candidate.texture) return 0;
    candidate.original_texture=source;
    candidate.holder[0]=candidate.texture;
    memset(&candidate.holder[6],0,3*sizeof(uint64_t));
    *pc=candidate;
    return 1;
invalid:
    free(pixels);return 0;
}

/* Gate on the native CALL sites that create a fresh command for each glyph,
 * original atlas, fresh vertex list,
 * exact UV quad and bounded dimensions. Unsupported art uses the game texture.
 * Clipped tutorial quads deliberately fall back instead of guessing UVs. */
static inline int kzsf_pc_draw_caller(KzsfPromptPc *pc,CONTEXT *c,uintptr_t ret,
                                      KzsfPromptRead read,KzsfPromptWrite write,void *opaque) {
    uint64_t holder=0,texture=0;int32_t vertices=0;float uv[16];
    if(!pc || !c || !pc->texture) return 0;
    if(ret!=0xfc5157 && ret!=0xe038e1 && ret!=0xe06c91 && ret!=0xe07c9d) return 0;
    int32_t width=(int32_t)c->Rdx,height=(int32_t)(c->Rdx>>32);
    if(width<=0 || width>512 || height<=0 || height>256 ||
       !kzsf_prompt_read(read,opaque,c->Rdi,0x40,&vertices,4) || vertices!=0 ||
       !kzsf_prompt_read(read,opaque,c->Rdi,0x58,&holder,8) ||
       !kzsf_prompt_read(read,opaque,holder,0,&texture,8) || texture!=pc->original_texture ||
       !kzsf_prompt_read(read,opaque,c->Rcx,0,uv,sizeof(uv))) return 0;
    for(unsigned i=0;i<16;++i) if((pc->supported&(1u<<i)) && !memcmp(uv,pc->uv[i],sizeof(uv))) {
        uint64_t replacement=(uintptr_t)pc->holder;
        if(c->Rdi>UINTPTR_MAX-0x58 || !write(opaque,c->Rdi+0x58,&replacement,8)) return 0;
        __atomic_add_fetch(&pc->draws,1,__ATOMIC_RELAXED);return (int)i+1;
    }
    return 0;
}
static inline int kzsf_pc_draw(KzsfPromptPc *pc,CONTEXT *c,uintptr_t base,
                               KzsfPromptRead read,KzsfPromptWrite write,void *opaque) {
    uint64_t caller=0;
    if(!c || !kzsf_prompt_read(read,opaque,c->Rsp,8,&caller,8)) return 0;
    return kzsf_pc_draw_caller(pc,c,caller>=base ? caller-base : 0,read,write,opaque);
}
/* Replay the guarded native CALL, without a handler on the common UI renderer.
 * Only the four gameplay call sites trap; ordinary menus/sprites pay no cost. */
static inline int kzsf_pc_replay_call(CONTEXT *c,uintptr_t base,unsigned index,
                                      KzsfPromptWrite write,void *opaque) {
    if(!c || !write || index>=sizeof(kzsf_pc_calls)/sizeof(*kzsf_pc_calls) || c->Rsp<8) return 0;
    uint64_t ret=base+kzsf_pc_calls[index]+5;
    if(!write(opaque,c->Rsp-8,&ret,8)) return 0;
    c->Rsp-=8;c->Rip=base+0xfc4d90;
    return 1;
}
#endif
