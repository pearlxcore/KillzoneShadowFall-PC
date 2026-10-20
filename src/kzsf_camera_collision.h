#ifndef KZSF_CAMERA_COLLISION_H
#define KZSF_CAMERA_COLLISION_H
#include "kzsf_camera_engine_context.h"
#include <stddef.h>
#include <math.h>
#include <string.h>
#define KZSF_COLLISION_ABI __attribute__((sysv_abi))
/* a59940's native filter, including its sorted owner/descendant exclusions.
 * The sweep result is 80 bytes: +40 is the swept sphere's CENTER, not +10. */
typedef struct {
    uintptr_t table;
    int32_t group,mask,flags;
    uint8_t accept_null,pad[3];
    int32_t count,pad2;
    uintptr_t items;
    int32_t capacity,pad3;
} KzsfCameraFilter;
typedef struct { _Alignas(16) unsigned char bytes[80]; } KzsfCameraHit;
_Static_assert(sizeof(KzsfCameraFilter)==48,"native camera filter size");
_Static_assert(offsetof(KzsfCameraFilter,items)==0x20,"native exclusion list");
_Static_assert(sizeof(KzsfCameraHit)==80,"native sphere result size");
typedef void (KZSF_COLLISION_ABI *KzsfCameraExclude)(uintptr_t,KzsfCameraFilter *);
typedef unsigned char (KZSF_COLLISION_ABI *KzsfCameraSweep)(uintptr_t,const float *,const float *,
    KzsfCameraHit *,KzsfCameraFilter *,float,float);
typedef void (KZSF_COLLISION_ABI *KzsfCameraRelease)(uintptr_t);
typedef struct {KzsfCameraExclude exclude;KzsfCameraSweep sweep;KzsfCameraRelease release;} KzsfCameraPhysics;
typedef void (KZSF_COLLISION_ABI *KzsfCameraAddIgnore)(KzsfCameraFilter *,uintptr_t);
typedef void (KZSF_COLLISION_ABI *KzsfCameraSortIgnore)(KzsfCameraFilter *);
typedef uintptr_t (*KzsfCameraIgnoreHit)(void *,uintptr_t);
typedef struct {KzsfCameraAddIgnore add;KzsfCameraSortIgnore sort;KzsfCameraIgnoreHit ignore;void *opaque;} KzsfCameraExtraIgnore;
typedef struct {uintptr_t view;uint64_t tick,blocked;float fraction;int valid;} KzsfCameraBoom;
static inline int kzsf_camera_collision_guards(uintptr_t base,KzsfZoomRead read,void *opaque) {
    if(!read || !base || base>UINTPTR_MAX-0x16c38a0)return 0;
    static const struct {uint32_t rva;unsigned char bytes[20];unsigned n;} guards[]={
        {0x64ebb0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x10,0x48,0x89,0xf3,0x49,0x89},20},
        {0x4f00e0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x01,0x00,0x00},20},
        {0x3295d0,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x85,0xdb,0x74,0x15,0x48,0x89,0xdf},17},
        {0x4f67a0,{0x85,0xf6,0x74,0x08,0x48,0x63,0xfe,0xe9,0x24,0x2c,0xe3,0xff,0x31,0xc0,0xc3},15},
        {0x4ca390,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xf6,0x8b,0x77,0x18,0x3b,0x77,0x28,0x48,0x8d,0x5f,0x18},20},
        {0x4ca450,{0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x30,0x48,0x8b,0x05,0xf9,0x40,0x39,0x01,0x48,0x8b,0x00,0x48,0x89},20}};
    unsigned char code[20];
    for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++)
        if(!kzsf_zoom_read(read,opaque,base,guards[i].rva,code,guards[i].n) ||
           memcmp(code,guards[i].bytes,guards[i].n))return 0;
    uintptr_t method=0;
    return kzsf_zoom_read(read,opaque,base,0x16c3898,&method,8) && method==base+0x975810;
}
/* Native calls only from the installed direct camera callback, on its existing
 * engine thread. Unknown context returns failure, never an unchecked camera. */
static inline int kzsf_camera_collision_trace_ex(uintptr_t base,uintptr_t world,uintptr_t owner,
        KzsfCameraPhysics api,const float start[3],const float end[3],float radius,float max_length,
        float *fraction,int *hit_out,const KzsfCameraExtraIgnore *extra) {
    if(!world || !owner || !api.exclude || !api.sweep || !api.release || !start || !end || !fraction || !hit_out)return 0;
    if(!isfinite(radius) || radius<=0 || radius>1 || !isfinite(max_length) || max_length<=0 || max_length>500)return 0;
    if(extra && (!extra->add || !extra->sort || !extra->ignore))return 0;
    _Alignas(16) float a[4]={start[0],start[1],start[2],1},b[4]={end[0],end[1],end[2],1};
    float length2=0;
    for(int i=0;i<3;i++) {
        if(!isfinite(a[i]) || !isfinite(b[i]))return 0;
        float d=b[i]-a[i];length2+=d*d;
    }
    if(!isfinite(length2) || length2<0.0001f || length2>max_length*max_length)return 0;
    KzsfCameraFilter filter={0};filter.table=base+0x16c3880;filter.group=0x24;filter.mask=0x7f;
    KzsfCameraHit result;memset(&result,0,sizeof(result));
    float normal[4]={0,0,1,0};memcpy(result.bytes+0x20,normal,16);
    uint16_t material=0xffff;memcpy(result.bytes+0x34,&material,2);
    api.exclude(owner,&filter);
    /* Even an invalid constructed list must be released exactly once. */
    int valid=filter.count>0 && filter.capacity>=filter.count && filter.items!=0;
    int hit=0;uintptr_t ignored[4]={0};unsigned ignores=0;
    while(valid) {
        hit=api.sweep(world,a,b,&result,&filter,radius,0.0001f)!=0;
        if(!hit || !extra)break;
        uintptr_t association=0;memcpy(&association,result.bytes,8);
        uintptr_t entity=extra->ignore(extra->opaque,association);
        if(!entity)break;
        if(ignores==4){valid=0;break;}
        for(unsigned i=0;i<ignores;i++)if(ignored[i]==entity)valid=0;
        if(!valid)break;
        ignored[ignores++]=entity;int32_t before=filter.count;
        if(before==INT32_MAX){valid=0;break;}
        extra->add(&filter,entity);extra->sort(&filter);
        if(filter.count!=before+1 || filter.capacity<filter.count || !filter.items)valid=0;
        /* Resweep the whole arm: ignoring a projectile must not ignore a wall
         * immediately behind it. Bound retries and release the list once. */
    }
    api.release(filter.items);
    if(!valid)return 0;
    float f=1;
    if(hit) {
        float center[4];memcpy(center,result.bytes+0x40,16);
        float projection=0,error=0;
        for(int i=0;i<3;i++) {if(!isfinite(center[i]))return 0;projection+=(center[i]-a[i])*(b[i]-a[i]);}
        f=projection/length2;
        if(!isfinite(f) || f<-.01f || f>1.01f)return 0;
        for(int i=0;i<3;i++){float d=center[i]-(a[i]+f*(b[i]-a[i]));error+=d*d;}
        if(error>.0025f)return 0; /* Reject a result outside this sweep. */
        f=fmaxf(0,fminf(1,f));
    }
    *fraction=f;*hit_out=hit;return 1;
}
static inline int kzsf_camera_collision_trace(uintptr_t base,uintptr_t world,uintptr_t owner,
        KzsfCameraPhysics api,const float start[3],const float end[3],float radius,float max_length,
        float *fraction,int *hit_out) {
    return kzsf_camera_collision_trace_ex(base,world,owner,api,start,end,radius,max_length,fraction,hit_out,NULL);
}
static inline int kzsf_camera_collision_query_ex(uintptr_t base,uintptr_t world,uintptr_t owner,
        KzsfCameraPhysics api,const float start[3],const float end[3],float *fraction,int *hit_out,
        const KzsfCameraExtraIgnore *extra) {
    if(!fraction || !hit_out)return 0;
    float f;int hit;
    if(!kzsf_camera_collision_trace_ex(base,world,owner,api,start,end,.20f,4,&f,&hit,extra))return 0;
    if(hit) {
        float length2=0;for(int i=0;i<3;i++){float d=end[i]-start[i];length2+=d*d;}
        f=fmaxf(0,f-.03f/sqrtf(length2));
    }
    *fraction=f;*hit_out=hit;return 1;
}
static inline int kzsf_camera_collision_query(uintptr_t base,uintptr_t world,uintptr_t owner,
        KzsfCameraPhysics api,const float start[3],const float end[3],float *fraction,int *hit_out) {
    return kzsf_camera_collision_query_ex(base,world,owner,api,start,end,fraction,hit_out,NULL);
}
/* Inward clipping is immediate. Delayed, bounded recovery cannot interpolate
 * through a wall. A collapsed arm retains follow mode; close body visibility
 * is handled separately so collision never selects first-person arms. */
static inline int kzsf_camera_boom_apply(KzsfCameraBoom *s,uintptr_t view,uint64_t tick,
        const float native[16],float pose[16],float limit) {
    if(!isfinite(limit) || limit<0 || limit>1)return 0;
    int reset=!s->valid || s->view!=view || tick<s->tick || tick-s->tick>250;
    float f=reset ? limit : s->fraction;
    if(limit<f){f=limit;s->blocked=tick;}
    else if(!reset && tick>=s->blocked && tick-s->blocked>=120)
        f=fminf(limit,f+(float)(tick-s->tick)*.002f);
    if(reset)s->blocked=tick;
    s->valid=1;s->view=view;s->tick=tick;s->fraction=f;
    for(int i=0;i<3;i++)pose[12+i]=native[12+i]+f*(pose[12+i]-native[12+i]);
    return 1;
}
#endif
