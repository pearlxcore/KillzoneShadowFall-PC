#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_prompt_pc_win.h"
static unsigned char collection[0xa0],entries[27*0x30],icons[4*0x30],source[0xc0],sprite[0x78],stack[16];
static float quads[27][16];
static uint64_t holder[9];
static unsigned created;
static int inside(uintptr_t a,size_t n,const void *p,size_t bytes) {
    return a>=(uintptr_t)p && n<=bytes && a-(uintptr_t)p<=bytes-n;
}
static int read_memory(void *unused,uintptr_t a,void *out,size_t n) {
    (void)unused;
    if(inside(a,n,collection,sizeof(collection)) || inside(a,n,entries,sizeof(entries)) ||
       inside(a,n,icons,sizeof(icons)) || inside(a,n,source,sizeof(source)) ||
       inside(a,n,sprite,sizeof(sprite)) || inside(a,n,stack,sizeof(stack)) ||
       inside(a,n,holder,sizeof(holder)) || inside(a,n,quads,sizeof(quads))) {
        memcpy(out,(void *)a,n);return 1;
    }
    return 0;
}
static int write_memory(void *unused,uintptr_t a,const void *in,size_t n) {
    (void)unused;if(!inside(a,n,sprite,sizeof(sprite)) && !inside(a,n,stack,sizeof(stack))) return 0;
    memcpy((void *)a,in,n);return 1;
}
static void put(void *p,uint64_t v,size_t n) {memcpy(p,&v,n);}
static int binding(unsigned icon,int mouse,char *label,size_t size) {
    if(icon<2 || icon>5) return 0;
    if(mouse && icon>=4) return icon==4 ? 3 : 2;
    snprintf(label,size,"%s",icon==2 ? "SPC" : icon==3 ? "E" : "F");return 1;
}
static uint64_t create(void *unused,const uint32_t *pixels,size_t bytes) {
    (void)unused;assert(bytes==KZSF_PC_BYTES);
    assert(pixels[15*KZSF_PC_WIDTH+12]!=0); /* keycap border */
    assert(pixels[0]==0 && pixels[KZSF_PC_WIDTH*KZSF_PC_HEIGHT-1]==0);
    ++created;return 0x12340000;
}
static void real_guards(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&size,8,1,f)==1 && size<512ull*1024*1024);
    unsigned char *image=calloc(1,(size_t)size);assert(image);
    assert(!_fseeki64(f,-(long long)size,SEEK_END));assert(fread(image,1,(size_t)size,f)==size);fclose(f);
    assert(kzsf_pc_signature(image,(size_t)size));
    image[0xfc4d92]^=1;assert(!kzsf_pc_signature(image,(size_t)size));image[0xfc4d92]^=1;
    for(unsigned i=0;i<sizeof(kzsf_pc_guards)/sizeof(*kzsf_pc_guards);++i) {
        const KzsfPcGuard *g=&kzsf_pc_guards[i];
        for(unsigned n=0;n<g->length;n++) {
            image[g->address+n]^=1;assert(!kzsf_pc_signature(image,(size_t)size));image[g->address+n]^=1;
        }
    }
    for(unsigned i=0;i<sizeof(kzsf_pc_calls)/sizeof(*kzsf_pc_calls);++i) {
        image[kzsf_pc_calls[i]+1]^=1;assert(!kzsf_pc_signature(image,(size_t)size));
        image[kzsf_pc_calls[i]+1]^=1;
    }
    assert(!kzsf_pc_signature(image,0xfc4d90));free(image);
}
int main(void) {
    real_guards();const uintptr_t base=0x800000000ull;
    put(collection+0x18,27,4);put(collection+0x20,(uintptr_t)entries,8);
    put(collection+0x40,(uintptr_t)quads,8);put(collection+0x50,(uintptr_t)source,8);
    put(source,base+0x14e0420,8);put(source+8,14u<<15,4);
    put(source+0x18,KZSF_PC_WIDTH,4);put(source+0x1c,KZSF_PC_HEIGHT,4);
    for(int n=0;n<27;n++) {put(entries+n*0x30,UINT32_MAX,4);put(entries+n*0x30+4,UINT32_MAX,4);}
    for(int n=0;n<4;n++) {
        put(entries+n*0x30,n+2,4);put(entries+n*0x30+0x28,(uintptr_t)(icons+n*0x30),8);
        int32_t rect[4]={10+n*70,10,62+n*70,52};memcpy(icons+n*0x30+0x20,rect,sizeof(rect));
        const int corner[4][2]={{0,1},{2,1},{2,3},{0,3}};
        for(int k=0;k<4;k++) {
            quads[n][k*4]=(float)rect[corner[k][0]]/KZSF_PC_WIDTH;
            quads[n][k*4+1]=(float)rect[corner[k][1]]/KZSF_PC_HEIGHT;
        }
    }
    KzsfPromptPc pc;memset(&pc,0,sizeof(pc));
    assert(kzsf_pc_prepare(&pc,base,(uintptr_t)collection,1,read_memory,NULL,binding,create,NULL));
    assert(created==1 && pc.supported==0x3c && pc.holder[0]==0x12340000);
    assert(!pc.holder[6] && !pc.holder[7] && !pc.holder[8]);
    unsigned char saved[sizeof(source)];memcpy(saved,source,sizeof(source));
    holder[0]=(uintptr_t)source;put(sprite+0x58,(uintptr_t)holder,8);
    put(stack+8,base+0xe07c9d,8);
    CONTEXT c;memset(&c,0xa5,sizeof(c));c.Rdi=(uintptr_t)sprite;c.Rcx=(uintptr_t)quads;
    c.Rsp=(uintptr_t)stack;c.Rdx=(uint64_t)42<<32|52;CONTEXT before=c;
    assert(kzsf_pc_draw(&pc,&c,base,read_memory,write_memory,NULL)==3);
    assert(!memcmp(&c,&before,sizeof(c)) && !memcmp(source,saved,sizeof(source)));
    uint64_t chosen;memcpy(&chosen,sprite+0x58,8);assert(chosen==(uintptr_t)pc.holder);
    put(sprite+0x58,(uintptr_t)holder,8);put(sprite+0x40,6,4);
    assert(!kzsf_pc_draw(&pc,&c,base,read_memory,write_memory,NULL)); /* shared command */
    put(sprite+0x40,0,4);put(stack+8,base+0x12345,8);
    assert(!kzsf_pc_draw(&pc,&c,base,read_memory,write_memory,NULL)); /* unrelated UI */
    put(stack+8,base+0xfc5157,8);quads[0][0]+=0.001f;
    assert(!kzsf_pc_draw(&pc,&c,base,read_memory,write_memory,NULL)); /* clipped / unknown UV */
    KzsfPromptPc empty;memset(&empty,0,sizeof(empty));
    assert(!kzsf_pc_prepare(&empty,base,(uintptr_t)collection,1,read_memory,NULL,binding,create,NULL));
    assert(!empty.texture && created==1); /* inconsistent UV rejected before construction */
    quads[0][0]-=0.001f;put(collection+0x18,16,4);
    assert(!kzsf_pc_prepare(&empty,base,(uintptr_t)collection,1,read_memory,NULL,binding,create,NULL));
    assert(created==1);
    for(unsigned i=0;i<4;++i) {
        memset(&c,0xa5,sizeof(c));c.Rsp=(uintptr_t)stack+8;before=c;
        assert(kzsf_pc_replay_call(&c,base,i,write_memory,NULL));
        CONTEXT expected=before;expected.Rsp-=8;expected.Rip=base+0xfc4d90;
        assert(!memcmp(&c,&expected,sizeof(c)));
        uint64_t ret;memcpy(&ret,stack,8);assert(ret==base+kzsf_pc_calls[i]+5);
    }
    before=c;
    assert(!kzsf_pc_replay_call(&c,base,4,write_memory,NULL) && !memcmp(&c,&before,sizeof(c)));
    c.Rsp=1;before=c;
    assert(!kzsf_pc_replay_call(&c,base,0,write_memory,NULL) && !memcmp(&c,&before,sizeof(c)));
    puts("Native PC prompts: guards, artwork, separate resource, draw gating and fallback pass.");
}
