#ifndef KZSF_CHARACTER_BUNDLE_LOADER_H
#define KZSF_CHARACTER_BUNDLE_LOADER_H
#include "kzsf_character_models.h"
#include "kzsf_character_native_query.h"

/* Native lump descriptor from61c500/f165a0, not a file-system path.
 * 61cd70 copies the pointer array; its worker61f270 calls61c8f0, taking a
 * resident count and performing resource registration/initialization. Keep
 * this lease through gameplay, then balance only its two counts before level unload.
 * A new chapter requests a fresh lease through the same selector.
 * Call only on the engine callback after checking allocator TLS. */
typedef struct {
    uintptr_t name;
    unsigned char resident,mode,stream,padding;
    int32_t kind;
    KzsfCharacterNativeArray dependencies;
    unsigned char flag,padding2[3];
    int32_t priority;
} KzsfCharacterBundleDescriptor;
_Static_assert(sizeof(KzsfCharacterBundleDescriptor)==0x30,"native lump descriptor");
typedef void (KZSF_CHARACTER_ABI *KzsfBundleStringInit)(uintptr_t *);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleStringSet)(uintptr_t *,const char *);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleStringFree)(uintptr_t *);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleRegister)(uintptr_t,const KzsfCharacterBundleDescriptor *,int);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleQueue)(uintptr_t,const KzsfCharacterNativeArray *,uintptr_t,uintptr_t,int);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleReleaseResident)(uintptr_t,uintptr_t,uintptr_t);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleReleasePrimary)(uintptr_t,uintptr_t);
typedef void (KZSF_CHARACTER_ABI *KzsfBundleWait)(uintptr_t);
typedef struct {
    KzsfBundleStringInit init;
    KzsfBundleStringSet set;
    KzsfBundleStringFree free_string;
    KzsfBundleRegister register_bundle;
    KzsfBundleQueue queue;
    KzsfBundleReleaseResident release_resident;
    KzsfBundleReleasePrimary release_primary;
    KzsfBundleWait wait;
} KzsfCharacterBundleApi;
typedef struct {uintptr_t manager,entry;int queued;uintptr_t soldier,resources;int32_t resource_count;} KzsfCharacterBundleLease;
static inline KzsfCharacterBundleApi kzsf_character_bundle_api(uintptr_t base) {
    KzsfCharacterBundleApi api={(KzsfBundleStringInit)(base+0x2a2e10),(KzsfBundleStringSet)(base+0x2a3710),
        (KzsfBundleStringFree)(base+0x2a3490),(KzsfBundleRegister)(base+0x61c500),(KzsfBundleQueue)(base+0x61cd70),
        (KzsfBundleReleaseResident)(base+0x61d210),(KzsfBundleReleasePrimary)(base+0x61d350),
        (KzsfBundleWait)(base+0x61cf40)};
    return api;
}
static inline int kzsf_character_bundle_entry_name(uintptr_t entry,const char *name,KzsfZoomRead read,void *opaque) {
    uintptr_t string=0;int32_t size=0;char value[256];size_t length=strlen(name);
    return length<sizeof(value) && kzsf_zoom_read(read,opaque,entry,0x10,&string,8) &&
        kzsf_zoom_read(read,opaque,string,8,&size,4) && size>=0 && (size_t)size==length &&
        kzsf_zoom_read(read,opaque,string,0x10,value,length) && !memcmp(value,name,length);
}

static inline int kzsf_character_bundle_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    static const struct {uint32_t rva;unsigned n;unsigned char bytes[20];} guards[]={
        {0x61c500,20,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x88,0,0,0}},
        {0x61cd70,17,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38}},
        {0x2a2e10,14,{0x48,0x8d,0x05,0x19,0x90,0x23,0x01,0xf0,0xff,0,0x48,0x89,0x07,0xc3}},
        {0x2a3710,13,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xf3,0x49,0x89,0xfe}},
        {0x2a3490,15,{0x48,0x8b,0x3f,0xb8,0xff,0xff,0xff,0xff,0xf0,0x0f,0xc1,0x07,0x83,0xf8,1}},
        {0x61d210,17,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18}},
        {0x61d350,17,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x49,0x89,0xf6,0x49,0x89,0xfc}},
        {0x61cf40,16,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x81,0xec,0x98,0,0,0}}};
    if(!base || base>UINTPTR_MAX-0x3c67760)return 0;
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
        unsigned char bytes[20];
        if(!kzsf_zoom_read(read,opaque,base,guards[i].rva,bytes,guards[i].n) ||
           memcmp(bytes,guards[i].bytes,guards[i].n))return 0;
    }
    return 1;
}
/* -1 refused,0 native worker busy/retry,1 resident and initialized,2 queued. */
static inline int kzsf_character_bundle_request(int model,uintptr_t base,KzsfZoomRead read,void *opaque,
        KzsfCharacterBundleApi api,KzsfCharacterBundleLease *lease,const char **why) {
    const char *name=kzsf_character_model_bundle(model);
    uintptr_t manager=0,worker=0,backlink=0,table=0,method=0;unsigned char running=0,busy=0;
    if(why)*why="bundle-api-or-identity";
    if(!lease || !*name || !api.init || !api.set || !api.free_string || !api.register_bundle || !api.queue || !api.release_resident || !api.release_primary || !api.wait)return -1;
    if(why)*why="bundle-function-guards";
    if(!kzsf_character_bundle_guards(base,read,opaque))return -1;
    if(why)*why="bundle-manager-layout";
    if(!kzsf_zoom_read(read,opaque,base,0x3c67760,&manager,8) || !manager ||
       !kzsf_zoom_read(read,opaque,manager,0,&table,8) ||
       !kzsf_zoom_read(read,opaque,table,0x10,&method,8) || method!=base+0x61c3a0 ||
       !kzsf_zoom_read(read,opaque,manager,0xd8,&worker,8) || !worker ||
       !kzsf_zoom_read(read,opaque,worker,8,&backlink,8) || backlink!=manager ||
       !kzsf_zoom_read(read,opaque,worker,0,&running,1) || running!=1 ||
       !kzsf_zoom_read(read,opaque,worker,0x88,&busy,1) || busy>1)return -1;
    if(lease->manager && lease->manager!=manager)memset(lease,0,sizeof(*lease));
    if(busy){if(why)*why="native-streamer-busy";return 0;}
    if(lease->queued) {
        unsigned char loaded=0;int32_t references=0,resident=0;
        if(!kzsf_character_bundle_entry_name(lease->entry,name,read,opaque) ||
           !kzsf_zoom_read(read,opaque,lease->entry,0x30,&loaded,1) || loaded>1 ||
           !kzsf_zoom_read(read,opaque,lease->entry,0,&references,4) || references<0 ||
           !kzsf_zoom_read(read,opaque,lease->entry,4,&resident,4) || resident<=0) {
            if(why)*why="bundle-lease-lost";return -1;
        }
        if(why)*why=loaded ? "bundle-resident" : "bundle-awaiting-initialization";
        return loaded && references>0 ? 1 : 0;
    }
    /* The secondary residency count prevents61c8f0 from inserting the
     * freshly registered entry into the active list a second time. Both
     * counts are balanced individually at the level boundary. */
    KzsfCharacterBundleDescriptor descriptor={0};descriptor.resident=1;descriptor.kind=1;descriptor.priority=-1;
    api.init(&descriptor.name);api.set(&descriptor.name,name);
    api.register_bundle(manager,&descriptor,0);
    api.free_string(&descriptor.name);
    uintptr_t entries=0,entry=0;int32_t count=0;
    if(why)*why="registered-bundle-not-found";
    if(!kzsf_zoom_read(read,opaque,manager,8,&count,4) || count<0 || count>8192 ||
       !kzsf_zoom_read(read,opaque,manager,0x10,&entries,8))return -1;
    size_t length=strlen(name);
    for(int32_t i=0;i<count;i++) {
        uintptr_t candidate=0,string=0;int32_t size=0;char value[256];
        if(!kzsf_zoom_read(read,opaque,entries,(size_t)i*8,&candidate,8) ||
           !kzsf_zoom_read(read,opaque,candidate,0x10,&string,8) ||
           !kzsf_zoom_read(read,opaque,string,8,&size,4) || size<0 || size>255)return -1;
        if((size_t)size!=length)continue;
        if(!kzsf_zoom_read(read,opaque,string,0x10,value,length))return -1;
        if(!memcmp(value,name,length)){if(entry)return -1;entry=candidate;}
    }
    if(!entry)return -1;
    KzsfCharacterNativeArray queue={1,0,(uintptr_t)&entry,1,0};
    api.queue(worker,&queue,0,0,0);
    lease->manager=manager;lease->entry=entry;lease->queued=1;
    if(why)*why="bundle-queued-with-resident-lease";
    return 2;
}
static inline int kzsf_character_bundle_release(int model,uintptr_t base,KzsfZoomRead read,void *opaque,
        KzsfCharacterBundleApi api,KzsfCharacterBundleLease *lease) {
    if(!lease || !lease->queued)return 1;
    if(model<=0 || model>=KZSF_CHARACTER_MODEL_COUNT || !api.release_resident || !api.release_primary ||
       !kzsf_character_bundle_guards(base,read,opaque))return -1;
    uintptr_t manager=0,worker=0,backlink=0;unsigned char busy=0;int32_t references=0,resident=0;
    if(!kzsf_zoom_read(read,opaque,base,0x3c67760,&manager,8) || manager!=lease->manager ||
       !kzsf_zoom_read(read,opaque,manager,0xd8,&worker,8) || !worker ||
       !kzsf_zoom_read(read,opaque,worker,8,&backlink,8) || backlink!=manager ||
       !kzsf_zoom_read(read,opaque,worker,0x88,&busy,1) || busy>1)return -1;
    if(busy)return 0;
    if(!kzsf_character_bundle_entry_name(lease->entry,kzsf_character_model_bundle(model),read,opaque) ||
       !kzsf_zoom_read(read,opaque,lease->entry,0,&references,4) || references<=0 ||
       !kzsf_zoom_read(read,opaque,lease->entry,4,&resident,4) || resident<=0)return -1;
    /* Registration61c500 takes only entry+4; worker61c8f0 takes only entry+0.
     * Neither acquires primary references on entry+40/+48 dependencies.
     * Descriptor unload61d090 additionally recurses through that dependency
     * array when primary reaches zero, stealing the level's references.
     * Use its two native primitives in the same order, without recursion.
     * Keep native notifications, active-list removal and retired-list enqueue. */
    api.release_resident(manager,lease->entry,0);
    api.release_primary(manager,lease->entry);
    memset(lease,0,sizeof(*lease));return 1;
}
/* Ad-hoc resident lumps are not necessarily enumerated by39b350. Resolve
 * the expected SoldierResource in the exact retained lump, rather than
 * treating its initialized flag as proof of global-query visibility. */
static inline int kzsf_character_bundle_soldier(int model,uintptr_t base,KzsfZoomRead read,void *opaque,
        KzsfCharacterType type,KzsfCharacterBundleLease *lease,uintptr_t *out) {
    uintptr_t manager=0,worker=0,backlink=0,array=0;int32_t count=0,references=0,resident=0;
    unsigned char loaded=0,busy=0;
    if(!out || !type || !lease || !lease->queued || model<=0 || model>=KZSF_CHARACTER_MODEL_COUNT)return -1;
    if(!kzsf_zoom_read(read,opaque,base,0x3c67760,&manager,8) || manager!=lease->manager ||
       !kzsf_zoom_read(read,opaque,manager,0xd8,&worker,8) || !worker ||
       !kzsf_zoom_read(read,opaque,worker,8,&backlink,8) || backlink!=manager ||
       !kzsf_zoom_read(read,opaque,worker,0x88,&busy,1) || busy>1)return -1;
    if(busy)return 0;
    if(!kzsf_character_bundle_entry_name(lease->entry,kzsf_character_model_bundle(model),read,opaque) ||
       !kzsf_zoom_read(read,opaque,lease->entry,0,&references,4) || references<=0 ||
       !kzsf_zoom_read(read,opaque,lease->entry,4,&resident,4) || resident<=0 ||
       !kzsf_zoom_read(read,opaque,lease->entry,0x30,&loaded,1) || loaded!=1 ||
       !kzsf_zoom_read(read,opaque,lease->entry,0x18,&count,4) || count<=0 || count>20000 ||
       !kzsf_zoom_read(read,opaque,lease->entry,0x20,&array,8) || !array)return -1;
    uintptr_t found=0;
    if(lease->soldier && lease->resources==array && lease->resource_count==count)found=lease->soldier;
    else for(int32_t i=0;i<count;i++) {
        uintptr_t resource=0;char name[128];
        if(!kzsf_zoom_read(read,opaque,array,(size_t)i*8,&resource,8))return -1;
        if(type(resource,base,read,opaque)!=base+0x3caac10)continue;
        if(!kzsf_character_resource_name(resource,base,read,opaque,name))return -1;
        if(strcmp(name,kzsf_character_model_name(model)))continue;
        if(found && found!=resource)return -1;
        found=resource;
    }
    char name[128];uintptr_t again=0;int32_t size=0;
    if(!found || type(found,base,read,opaque)!=base+0x3caac10 ||
       !kzsf_character_resource_name(found,base,read,opaque,name) || strcmp(name,kzsf_character_model_name(model)) ||
       !kzsf_zoom_read(read,opaque,lease->entry,0x20,&again,8) || again!=array ||
       !kzsf_zoom_read(read,opaque,lease->entry,0x18,&size,4) || size!=count ||
       !kzsf_zoom_read(read,opaque,worker,0x88,&busy,1))return -1;
    if(busy)return 0;
    lease->soldier=found;lease->resources=array;lease->resource_count=count;*out=found;return 1;
}
#endif
