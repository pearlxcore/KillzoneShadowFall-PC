#ifndef KZSF_CHARACTER_NATIVE_QUERY_H
#define KZSF_CHARACTER_NATIVE_QUERY_H
#include "kzsf_character_inventory.h"
#define KZSF_CHARACTER_ABI __attribute__((sysv_abi))

/* 39b350 is the native loaded-object query used by 6d8160. Flags2 select
 * RTTI filtering without a name filter. It locks its own registry/pools and
 * allocates only the returned pointer array. It does not load assets or
 * retain the returned objects. Caller must verify engine allocator TLS and
 * execute on the direct engine callback, with no diagnostic VEH execution. */
typedef struct {uintptr_t type,name;uint32_t flags,padding;} KzsfCharacterNativeFilter;
typedef struct {int32_t count,padding;uintptr_t data;int32_t capacity,padding2;} KzsfCharacterNativeArray;
_Static_assert(sizeof(KzsfCharacterNativeFilter)==24,"native resource filter");
_Static_assert(sizeof(KzsfCharacterNativeArray)==24,"native resource array");
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterQuery)(uintptr_t,const KzsfCharacterNativeFilter *,KzsfCharacterNativeArray *,uint64_t);
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterQueryFree)(uintptr_t);
typedef struct {KzsfCharacterQuery query;KzsfCharacterQueryFree release;} KzsfCharacterQueryApi;
enum { KZSF_CHARACTER_NATIVE_LIMIT=4096 };
typedef struct {
    uintptr_t manager;
    int32_t total,count,unknown_types,duplicates;
    uintptr_t resources[KZSF_CHARACTER_NATIVE_LIMIT];
} KzsfCharacterNativeResources;
static inline int kzsf_character_query_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    static const struct {uint32_t rva;unsigned n;unsigned char bytes[20];} guards[]={
        {0x39b350,20,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38,0x48,0x89,0xd3}},
        {0x3295d0,17,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x85,0xdb,0x74,0x15,0x48,0x89,0xdf}}};
    if(!read || !base || base>UINTPTR_MAX-0x3c3d7b0)return 0;
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char bytes[20];
        if(!kzsf_zoom_read(read,opaque,base,guards[i].rva,bytes,guards[i].n) ||
           memcmp(bytes,guards[i].bytes,guards[i].n))return 0;
    }
    return 1;
}
static inline int kzsf_character_native_query_type(uintptr_t base,uint32_t type_rva,KzsfZoomRead read,void *opaque,
        KzsfCharacterType type,KzsfCharacterQueryApi api,KzsfCharacterNativeResources *out,const char **why) {
    KzsfCharacterNativeResources result;memset(&result,0,sizeof(result));
    if(why)*why="query-guards";
    if((type_rva!=0x3c3d7b0 && type_rva!=0x3caac10) || base>UINTPTR_MAX-type_rva ||
       !out || !type || !api.query || !api.release || !kzsf_character_query_guards(base,read,opaque))return 0;
    if(why)*why="manager-unreadable";
    if(!kzsf_zoom_read(read,opaque,base,0x3b490c0,&result.manager,8) || !result.manager)return 0;
    KzsfCharacterNativeFilter filter={base+type_rva,0,2,0};
    KzsfCharacterNativeArray array={0};
    api.query(result.manager,&filter,&array,0);
    int okay=0;
    if(why)*why="native-array-shape";
    if(array.count<0 || array.capacity<array.count || array.capacity>1048576 ||
       (array.count && !array.data))goto cleanup;
    result.total=array.count;
    int32_t count=array.count>KZSF_CHARACTER_NATIVE_LIMIT ? KZSF_CHARACTER_NATIVE_LIMIT : array.count;
    for(int32_t i=0;i<count;i++) {
        uintptr_t resource=0;
        if(why)*why="native-array-unreadable";
        if(!kzsf_zoom_read(read,opaque,array.data,(size_t)i*8,&resource,8))goto cleanup;
        if(type(resource,base,read,opaque)!=base+type_rva){result.unknown_types++;continue;}
        int duplicate=0;
        for(int32_t j=0;j<result.count;j++)if(result.resources[j]==resource){duplicate=1;break;}
        if(duplicate){result.duplicates++;continue;}
        result.resources[result.count++]=resource;
    }
    okay=1;
cleanup:
    if(array.data)api.release(array.data);
    if(!okay)return 0;
    if(why)*why="sampled";
    *out=result;return 1;
}
static inline int kzsf_character_native_query(uintptr_t base,KzsfZoomRead read,void *opaque,
        KzsfCharacterType type,KzsfCharacterQueryApi api,KzsfCharacterNativeResources *out,const char **why) {
    return kzsf_character_native_query_type(base,0x3c3d7b0,read,opaque,type,api,out,why);
}
#endif
