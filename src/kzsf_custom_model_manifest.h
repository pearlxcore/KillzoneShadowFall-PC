#ifndef KZSF_CUSTOM_MODEL_MANIFEST_H
#define KZSF_CUSTOM_MODEL_MANIFEST_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum {KZSF_CUSTOM_MODEL_LIMIT=16};
typedef struct {uint32_t vertices,indices,texture,vertex_bytes,index_bytes,vertex_crc,index_crc,reserved[3];} KzsfCustomModelSectionRecord;
typedef struct {uint32_t bytes,crc,reserved;} KzsfCustomModelTextureRecord;
typedef struct {
    uint32_t flags,profile,sections,textures;
    KzsfCustomModelSectionRecord section[KZSF_CUSTOM_MODEL_LIMIT];
    KzsfCustomModelTextureRecord texture[KZSF_CUSTOM_MODEL_LIMIT];
} KzsfCustomModelManifest;
_Static_assert(sizeof(KzsfCustomModelSectionRecord)==40,"model section record");
static inline uint32_t kzsf_custom_crc32(uint32_t previous,const void *data,size_t bytes) {
    const unsigned char *p=data;uint32_t crc=~previous;
    for(size_t i=0;i<bytes;i++) {
        crc^=p[i];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static inline int kzsf_custom_model_manifest(const void *data,size_t bytes,KzsfCustomModelManifest *out) {
    uint32_t h[8];if(!data || !out || bytes<32 || bytes>1024)return 0;memcpy(h,data,32);
    if(memcmp(data,"KZMD",4) || h[1]!=1 || h[2]!=98 || !h[3] || h[3]>16 || !h[4] || h[4]>16 || h[5]>1 || h[7] ||
       bytes!=32+(size_t)h[3]*40+(size_t)h[4]*12)return 0;
    KzsfCustomModelManifest result={.flags=h[5],.profile=h[6],.sections=h[3],.textures=h[4]};
    memcpy(result.section,(const unsigned char *)data+32,(size_t)h[3]*40);
    memcpy(result.texture,(const unsigned char *)data+32+(size_t)h[3]*40,(size_t)h[4]*12);
    uint64_t total=0;
    for(uint32_t i=0;i<result.sections;i++) {
        const KzsfCustomModelSectionRecord *s=&result.section[i];
        if(!s->vertices || s->vertices>1000000 || !s->indices || s->indices>3000000 || s->indices%3 ||
           s->texture>=result.textures ||
           /* Accepted native layouts. Layout validation does not establish how
              many influences the selected material's GPU shader consumes. */
           (s->vertex_bytes!=60+(uint64_t)s->vertices*36 && s->vertex_bytes!=68+(uint64_t)s->vertices*44) ||
           (s->index_bytes!=32+(uint64_t)s->indices*2 && s->index_bytes!=32+(uint64_t)s->indices*4) ||
           s->reserved[0] || s->reserved[1] || s->reserved[2])return 0;
        total+=(uint64_t)s->vertex_bytes+s->index_bytes;
    }
    for(uint32_t i=0;i<result.textures;i++) {
        const KzsfCustomModelTextureRecord *t=&result.texture[i];
        if(t->bytes<16+64*64*4 || t->bytes>16+4096*4096*4 || t->reserved)return 0;
        total+=t->bytes;
    }
    if(total>256u*1024u*1024u)return 0;
    *out=result;return 1;
}
#endif
