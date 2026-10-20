#ifndef KZSF_CHARACTER_REGISTRY_H
#define KZSF_CHARACTER_REGISTRY_H
#include "kzsf_character_inventory.h"

/* Read-only traversal of the resource manager path used by 39b350.
 * Global manager+78/+84 is the type -> pool-list hash table. A pool has
 * +28/+30 entries of stride30; entry+10/+18 is its resource-pointer array.
 * Entry bytes00..0f are reported as an opaque key until matched to disk IDs.
 * Does not load, retain or grant ownership of any resource. */
enum { KZSF_CHARACTER_MAX_LOADED_PARTS=512 };
typedef struct {
    uintptr_t resource,pool;
    unsigned char entry_key[16];
} KzsfCharacterLoadedPart;
typedef struct {
    uintptr_t manager,table,pool_list;
    uint32_t mask;
    int32_t count;
    KzsfCharacterLoadedPart parts[KZSF_CHARACTER_MAX_LOADED_PARTS];
} KzsfCharacterRegistry;
static inline uint32_t kzsf_character_type_hash(uint64_t key) {
    uint64_t x=~(key<<32)+key;
    x=(x>>22)^x;x=~(x<<13)+x;x=((x>>8)^x)*9;
    x=(x>>15)^x;x=~(x<<27)+x;
    return (uint32_t)((x>>31)^x)|UINT32_C(0x80000000);
}
static inline int kzsf_character_registry_collect(uintptr_t base,KzsfZoomRead read,
        void *opaque,KzsfCharacterType type,KzsfCharacterRegistry *out) {
    KzsfCharacterRegistry s;memset(&s,0,sizeof(s));
    if(!read || !type || !out || base>UINTPTR_MAX-0x3c3d7b0 ||
       !kzsf_zoom_read(read,opaque,base,0x3b490c0,&s.manager,8) ||
       !kzsf_zoom_read(read,opaque,s.manager,0x78,&s.table,8) || !s.table ||
       !kzsf_zoom_read(read,opaque,s.manager,0x84,&s.mask,4) ||
       s.mask>65535 || (s.mask&(s.mask+1)))return 0;
    uintptr_t desired=base+0x3c3d7b0;
    uint32_t hash=kzsf_character_type_hash(desired),start=hash&s.mask;
    for(uint32_t n=0;n<=s.mask;n++) {
        size_t offset=(size_t)((start+n)&s.mask)*0x20;
        uint32_t stored=0;uintptr_t key=0;
        if(!kzsf_zoom_read(read,opaque,s.table,offset,&stored,4))return 0;
        if(!stored)break;
        if(stored==hash) {
            if(!kzsf_zoom_read(read,opaque,s.table,offset+8,&key,8))return 0;
            if(key==desired) {
                if(!kzsf_zoom_read(read,opaque,s.table,offset+0x18,&s.pool_list,8))return 0;
                break;
            }
        }
    }
    if(!s.pool_list){*out=s;return 1;}
    int32_t pool_count=0;uintptr_t pools=0;
    if(!kzsf_zoom_read(read,opaque,s.pool_list,0,&pool_count,4) || pool_count<0 || pool_count>1024 ||
       !kzsf_zoom_read(read,opaque,s.pool_list,8,&pools,8) || (pool_count && !pools))return 0;
    unsigned visited=0;
    for(int32_t i=0;i<pool_count;i++) {
        uintptr_t pool=0,entries=0;int32_t count=0;
        if(!kzsf_zoom_read(read,opaque,pools,(size_t)i*8,&pool,8) ||
           !kzsf_zoom_read(read,opaque,pool,0x28,&count,4) || count<0 || count>65536 ||
           !kzsf_zoom_read(read,opaque,pool,0x30,&entries,8) || (count && !entries) ||
           (unsigned)count>65536-visited)return 0;
        visited+=(unsigned)count;
        for(int32_t j=0;j<count;j++) {
            uintptr_t resources=0;int32_t resource_count=0;size_t offset=(size_t)j*0x30;
            unsigned char key[16];
            if(!kzsf_zoom_read(read,opaque,entries,offset,key,16) ||
               !kzsf_zoom_read(read,opaque,entries,offset+0x10,&resource_count,4) ||
               resource_count<0 || resource_count>64 ||
               !kzsf_zoom_read(read,opaque,entries,offset+0x18,&resources,8) ||
               (resource_count && !resources))return 0;
            for(int32_t k=0;k<resource_count;k++) {
                uintptr_t resource=0;
                if(!kzsf_zoom_read(read,opaque,resources,(size_t)k*8,&resource,8))return 0;
                uintptr_t actual=type(resource,base,read,opaque);
                if(!actual)return 0;
                if(actual!=desired)continue;
                int duplicate=0;
                for(int32_t n=0;n<s.count;n++)if(s.parts[n].resource==resource){duplicate=1;break;}
                if(duplicate)continue;
                if(s.count==KZSF_CHARACTER_MAX_LOADED_PARTS)return 0;
                KzsfCharacterLoadedPart *p=&s.parts[s.count++];p->resource=resource;p->pool=pool;
                memcpy(p->entry_key,key,16);
            }
        }
    }
    *out=s;return 1;
}
static inline int kzsf_character_registry_read(uintptr_t base,KzsfZoomRead read,
        void *opaque,KzsfCharacterType type,KzsfCharacterRegistry *out) {
    KzsfCharacterRegistry first,second;
    if(!out || !kzsf_character_registry_collect(base,read,opaque,type,&first) ||
       !kzsf_character_registry_collect(base,read,opaque,type,&second) ||
       memcmp(&first,&second,sizeof(first)))return 0;
    *out=first;return 1;
}
#endif
