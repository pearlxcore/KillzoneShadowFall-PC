/* Original, code-drawn monochrome prompt artwork. No game textures/fonts used. */
#ifndef KZSF_PROMPT_ART_H
#define KZSF_PROMPT_ART_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Five-bit rows, seven rows per letter/digit. Small labels are scaled to fit
 * the native icon rectangle rather than extending over the action text. */
static const uint8_t kzsf_prompt_letters[36][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
 {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}
};
static inline unsigned kzsf_prompt_font_row(char c,unsigned row) {
    if(c>='A' && c<='Z') return kzsf_prompt_letters[c-'A'][row];
    if(c>='0' && c<='9') return kzsf_prompt_letters[26+c-'0'][row];
    if(c=='-') return row==3 ? 14u : 0u;
    if(c=='.') return row==6 ? 4u : 0u;
    return 0;
}

static inline int kzsf_prompt_art(uint32_t *pixels,unsigned width,unsigned height,
                                  const int32_t rect[4],int kind,const char *label) {
    if(!pixels || !rect || rect[0]<0 || rect[1]<0 || rect[2]<=rect[0] || rect[3]<=rect[1] ||
       (unsigned)rect[2]>width || (unsigned)rect[3]>height || kind<1 || kind>3) return 0;
    int w=rect[2]-rect[0],h=rect[3]-rect[1];
    if(w<20 || h<20) return 0;
    const int x0=rect[0]+2,x1=rect[2]-3,y0=rect[1]+3,y1=rect[3]-4;
    if(kind==1) {
        size_t len=label ? strlen(label) : 0;
        if(!len || len>8) return 0;
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            int edge=x<x0+2 || x>x1-2 || y<y0+2 || y>y1-2;
            if((x==x0 || x==x1) && (y==y0 || y==y1)) continue;
            pixels[(size_t)y*width+x]=edge ? UINT32_C(0xe0ffffff) : UINT32_C(0x70000000);
        }
        int logical=(int)len*6-1,tw=logical*2,th=14;
        if(tw>w-10) tw=w-10;
        if(th>h-12) th=h-12;
        int left=rect[0]+(w-tw)/2,top=rect[1]+(h-th)/2;
        for(int y=0;y<th;++y) for(int x=0;x<tw;++x) {
            unsigned sx=(unsigned)(x*logical/tw),row=(unsigned)(y*7/th);
            if(sx%6<5 && (kzsf_prompt_font_row(label[sx/6],row) & (1u<<(4-sx%6))))
                pixels[(size_t)(top+y)*width+left+x]=UINT32_C(0xffffffff);
        }
    } else {
        /* Mouse silhouette: a rounded top, outlined body, two buttons and
         * wheel. The selected left/right button is filled bright white. */
        int mw=h*3/5;if(mw>w-8) mw=w-8;
        int mh=h-6,left=rect[0]+(w-mw)/2,top=rect[1]+3;
        int radius=mw/3,mid=left+mw/2,split=top+mh*2/5;
        for(int y=0;y<mh;++y) for(int x=0;x<mw;++x) {
            int inset=y<radius ? (radius-y)/2 : y>=mh-radius ? (y-mh+radius)/2 : 0;
            if(x<inset || x>=mw-inset) continue;
            int edge=x<inset+2 || x>=mw-inset-2 || y<2 || y>=mh-2;
            int selected=top+y<split && (kind==2 ? left+x<mid : left+x>=mid);
            uint32_t colour=edge || selected ? UINT32_C(0xffffffff) : UINT32_C(0x70383838);
            if(!edge && ((top+y==split || top+y==split+1) ||
               (top+y<split && left+x>=mid-1 && left+x<=mid))) colour=UINT32_C(0xff000000);
            pixels[(size_t)(top+y)*width+left+x]=colour;
        }
    }
    return 1;
}
#endif
