#ifndef KZSF_CHARACTER_INVENTORY_H
#define KZSF_CHARACTER_INVENTORY_H
#include "kzsf_weapon_zoom.h"
#include <string.h>

/* CUSA00190 / 01.81. Caller must establish the local Soldier ownership/type.
 * b37670 bounds body variants at resource+648; b3f190 uses stride f0,
 * part+08, model component+20, attached component array+28/+30.
 * This snapshot retains no pointers and grants no permission to call setters.
 * Two agreeing reads reduce races; they are not an atomic engine snapshot. */
enum { KZSF_CHARACTER_MAX_VARIANTS=64 };
typedef struct {
    uintptr_t part,model_component,components;
    int32_t component_count;
} KzsfCharacterVariant;
typedef struct {
    uintptr_t soldier,resource,array;
    int32_t count,current;
    KzsfCharacterVariant variants[KZSF_CHARACTER_MAX_VARIANTS];
} KzsfCharacterInventory;

/* Named SoldierResource lookup can expose nested ModelPartResource objects
 * which are absent from the global standalone part query. Never substitute
 * an actor's resource pointer or mutate this shared variant array. */
typedef uintptr_t (*KzsfCharacterType)(uintptr_t,uintptr_t,KzsfZoomRead,void *);
static inline int kzsf_character_resource_variants_collect(uintptr_t resource,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterInventory *out) {
    KzsfCharacterInventory result;memset(&result,0,sizeof(result));
    result.resource=resource;result.current=-1;
    if(!read || !type || !out || base>UINTPTR_MAX-0x3caac10 ||
       type(resource,base,read,opaque)!=base+0x3caac10 ||
       !kzsf_zoom_read(read,opaque,resource,0x648,&result.count,4) ||
       result.count<0 || result.count>KZSF_CHARACTER_MAX_VARIANTS ||
       !kzsf_zoom_read(read,opaque,resource,0x650,&result.array,8) ||
       (result.count && !result.array) || (size_t)result.count*0xf0>UINTPTR_MAX-result.array)return 0;
    for(int32_t i=0;i<result.count;i++) {
        KzsfCharacterVariant *v=&result.variants[i];uintptr_t p=result.array+(size_t)i*0xf0;
        if(!kzsf_zoom_read(read,opaque,p,8,&v->part,8) ||
           !kzsf_zoom_read(read,opaque,p,0x20,&v->model_component,8) ||
           !kzsf_zoom_read(read,opaque,p,0x28,&v->component_count,4) ||
           !kzsf_zoom_read(read,opaque,p,0x30,&v->components,8) ||
           v->component_count<0 || v->component_count>256 ||
           (v->part && type(v->part,base,read,opaque)!=base+0x3c3d7b0) ||
           (v->component_count && (!v->components || (size_t)v->component_count*8>UINTPTR_MAX-v->components)))return 0;
    }
    *out=result;return 1;
}
static inline int kzsf_character_resource_variants_read(uintptr_t resource,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterInventory *out) {
    KzsfCharacterInventory first,second;
    if(!out || !kzsf_character_resource_variants_collect(resource,base,read,opaque,type,&first) ||
       !kzsf_character_resource_variants_collect(resource,base,read,opaque,type,&second) ||
       memcmp(&first,&second,sizeof(first)))return 0;
    *out=first;return 1;
}

/* ModelPartResource layout from 74bf40 attributes at 165d860:
 * MeshResource+18, PhysicsResource+28, IsSkinned+30. This describes a
 * render/physics part, not a complete character or compatible skeleton. */
/* Resource name accessor33b790 returns the String at object+10. Only
 * read this layout after the exact virtual18 accessor check. */
static inline int kzsf_character_resource_name(uintptr_t object,uintptr_t base,KzsfZoomRead read,
        void *opaque,char out[128]) {
    uintptr_t data=0,again=0;int32_t length=0,length_again=0;char name[128]={0};
    if(!read || !out || base>UINTPTR_MAX-0x33b790 ||
       !kzsf_zoom_method(read,opaque,object,0x18,base+0x33b790) ||
       !kzsf_zoom_read(read,opaque,object,0x10,&data,8) ||
       !kzsf_zoom_read(read,opaque,data,8,&length,4) || length<0 || length>4096)return 0;
    size_t count=length>127 ? 127 : (size_t)length;
    if(count && !kzsf_zoom_read(read,opaque,data,0x10,name,count))return 0;
    for(size_t i=0;i<count;i++)if((unsigned char)name[i]<32 || (unsigned char)name[i]>126)name[i]='?';
    if(!kzsf_zoom_read(read,opaque,object,0x10,&again,8) || data!=again ||
       !kzsf_zoom_read(read,opaque,data,8,&length_again,4) || length!=length_again)return 0;
    memcpy(out,name,sizeof(name));return 1;
}
typedef struct {
    uintptr_t part,mesh,physics;
    uint8_t is_skinned;
} KzsfCharacterPart;
static inline int kzsf_character_part_collect(uintptr_t part,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterPart *out) {
    KzsfCharacterPart p;memset(&p,0,sizeof(p));p.part=part;
    if(!read || !type || !out || base>UINTPTR_MAX-0x3c3d7b0 ||
       type(part,base,read,opaque)!=base+0x3c3d7b0 ||
       !kzsf_zoom_read(read,opaque,part,0x18,&p.mesh,8) ||
       !kzsf_zoom_read(read,opaque,part,0x28,&p.physics,8) ||
       !kzsf_zoom_read(read,opaque,part,0x30,&p.is_skinned,1) || p.is_skinned>1)return 0;
    *out=p;return 1;
}
static inline int kzsf_character_part_read(uintptr_t part,uintptr_t base,
        KzsfZoomRead read,void *opaque,KzsfCharacterType type,KzsfCharacterPart *out) {
    KzsfCharacterPart first,second;
    if(!out || !kzsf_character_part_collect(part,base,read,opaque,type,&first) ||
       !kzsf_character_part_collect(part,base,read,opaque,type,&second) ||
       memcmp(&first,&second,sizeof(first)))return 0;
    *out=first;return 1;
}

static inline int kzsf_character_inventory_collect(uintptr_t soldier,KzsfZoomRead read,
        void *opaque,KzsfCharacterInventory *out) {
    KzsfCharacterInventory s;memset(&s,0,sizeof(s));s.soldier=soldier;
    if(!read || !out ||
       !kzsf_zoom_read(read,opaque,soldier,0x1d0,&s.resource,8) ||
       !kzsf_zoom_read(read,opaque,soldier,0x98c,&s.current,4) ||
       !kzsf_zoom_read(read,opaque,s.resource,0x648,&s.count,4) ||
       !kzsf_zoom_read(read,opaque,s.resource,0x650,&s.array,8) ||
       s.count<=0 || s.count>KZSF_CHARACTER_MAX_VARIANTS ||
       s.current<0 || s.current>=s.count || !s.array ||
       (size_t)s.count*0xf0>UINTPTR_MAX-s.array)return 0;
    for(int32_t i=0;i<s.count;i++) {
        uintptr_t p=s.array+(size_t)i*0xf0;
        KzsfCharacterVariant *v=&s.variants[i];
        if(!kzsf_zoom_read(read,opaque,p,8,&v->part,8) ||
           !kzsf_zoom_read(read,opaque,p,0x20,&v->model_component,8) ||
           !kzsf_zoom_read(read,opaque,p,0x28,&v->component_count,4) ||
           !kzsf_zoom_read(read,opaque,p,0x30,&v->components,8) ||
           v->component_count<0 || v->component_count>256 ||
           (v->component_count && (!v->components ||
               (size_t)v->component_count*8>UINTPTR_MAX-v->components)))return 0;
    }
    *out=s;return 1;
}
static inline int kzsf_character_inventory_read(uintptr_t soldier,KzsfZoomRead read,
        void *opaque,KzsfCharacterInventory *out) {
    KzsfCharacterInventory first,second;
    if(!out || !kzsf_character_inventory_collect(soldier,read,opaque,&first) ||
       !kzsf_character_inventory_collect(soldier,read,opaque,&second) ||
       memcmp(&first,&second,sizeof(first)))return 0;
    *out=first;return 1;
}
#endif
