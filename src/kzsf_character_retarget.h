#ifndef KZSF_CHARACTER_RETARGET_H
#define KZSF_CHARACTER_RETARGET_H
#include "kzsf_character_native_query.h"

/* Native 5d8dd0 builds a name-based map consumed by pose retargeter 5d2ae0.
 * Direction is source skeleton -> destination skeleton; entries are indexed
 * by DESTINATION joint. The parent-differs byte selects world-space conversion.
 * This helper only inspects a temporary map. It never applies a pose or part. */
typedef struct {int32_t source;uint8_t parent_differs,padding[3];} KzsfCharacterJointMap;
typedef struct {
    uintptr_t source,destination;
    KzsfCharacterNativeArray joints,channels;
} KzsfCharacterNativeMap;
_Static_assert(sizeof(KzsfCharacterJointMap)==8,"native joint remap entry");
_Static_assert(sizeof(KzsfCharacterNativeMap)==64,"native skeleton remap");
typedef void (KZSF_CHARACTER_ABI *KzsfCharacterBuildMap)(uintptr_t,uintptr_t,KzsfCharacterNativeMap *);
typedef struct {KzsfCharacterBuildMap build;KzsfCharacterQueryFree release;} KzsfCharacterMapApi;
typedef struct {
    int32_t source_count,count,matched,missing,parent_changes,channels;
    KzsfCharacterJointMap joints[512];
} KzsfCharacterRetargetAudit;
static inline int kzsf_character_retarget_audit(uintptr_t source,uintptr_t destination,
        uintptr_t base,KzsfZoomRead read,void *opaque,KzsfCharacterMapApi api,
        KzsfCharacterRetargetAudit *out,const char **why) {
    static const unsigned char guard[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x38,0x48,0x89,0xf3};
    unsigned char bytes[sizeof(guard)];KzsfCharacterRetargetAudit result={0};
    if(why)*why="retarget-guard";
    if(!out || !api.build || !api.release || !kzsf_character_query_guards(base,read,opaque) ||
       !kzsf_zoom_read(read,opaque,base,0x5d8dd0,bytes,sizeof(bytes)) || memcmp(bytes,guard,sizeof(bytes)))return 0;
    if(why)*why="retarget-joint-count";
    if(!kzsf_zoom_read(read,opaque,source,0x30,&result.source_count,4) || result.source_count<=0 || result.source_count>512 ||
       !kzsf_zoom_read(read,opaque,destination,0x30,&result.count,4) || result.count<=0 || result.count>512)return 0;
    KzsfCharacterNativeMap map={0};int okay=0;
    api.build(source,destination,&map);
    if(why)*why="retarget-map-shape";
    if(map.source!=source || map.destination!=destination || map.joints.count!=result.count ||
       map.joints.capacity<map.joints.count || map.joints.capacity>1048576 || !map.joints.data ||
       map.channels.count<0 || map.channels.count>4096 || map.channels.capacity<map.channels.count ||
       map.channels.capacity>1048576 || (map.channels.count && !map.channels.data) ||
       (map.channels.data && map.channels.data==map.joints.data))goto cleanup;
    result.channels=map.channels.count;
    for(int32_t i=0;i<result.count;i++) {
        KzsfCharacterJointMap *joint=&result.joints[i];
        if(why)*why="retarget-map-entry";
        if(!kzsf_zoom_read(read,opaque,map.joints.data,(size_t)i*8,joint,8) ||
           joint->source < -1 || joint->source>=result.source_count ||
           (joint->source>=0 && joint->parent_differs>1))goto cleanup;
        /* Native missing entries only initialize source=-1. Ignore padding
         * and the unspecified flag for that case; never inspect it as data. */
        memset(joint->padding,0,sizeof(joint->padding));
        if(joint->source<0){joint->parent_differs=0;result.missing++;}
        else {result.matched++;result.parent_changes+=joint->parent_differs!=0;}
    }
    okay=1;
cleanup:
    if(map.channels.data && map.channels.data!=map.joints.data)api.release(map.channels.data);
    if(map.joints.data)api.release(map.joints.data);
    if(!okay)return 0;
    if(why)*why="native-map-sampled";*out=result;return 1;
}
#endif
