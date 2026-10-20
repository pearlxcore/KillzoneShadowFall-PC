#ifndef KZSF_WEAPON_ZOOM_H
#define KZSF_WEAPON_ZOOM_H
#include <stdint.h>
#include <stddef.h>

/* CUSA00190 / 01.81 portable InventoryWeaponRep zoom-selection reader.
 * Read-only, no native calls. Exact dispatch checks reject other item layouts.
 * Caller supplies a verified Soldier. Successful reads describe selected zoom,
 * not completion of camera blending or universal third-person eligibility. */
typedef int (*KzsfZoomRead)(void *,uintptr_t,void *,size_t);
typedef struct {
    uintptr_t weapon,mode_interface,zoom_interface;
    int32_t mode_index,mode_count,zoom_index,zoom_count;
} KzsfWeaponZoom;
static inline int kzsf_zoom_read(KzsfZoomRead read,void *opaque,uintptr_t p,
                                size_t offset,void *out,size_t n) {
    return p && offset<=UINTPTR_MAX-p && n<=UINTPTR_MAX-(p+offset) &&
        read(opaque,p+offset,out,n);
}
static inline int kzsf_zoom_method(KzsfZoomRead read,void *opaque,uintptr_t object,
                                  size_t slot,uintptr_t expected) {
    uintptr_t vtable=0,method=0;
    return kzsf_zoom_read(read,opaque,object,0,&vtable,8) &&
        kzsf_zoom_read(read,opaque,vtable,slot,&method,8) && method==expected;
}
static inline int kzsf_weapon_zoom_read(uintptr_t soldier,uintptr_t base,
                          KzsfZoomRead read,void *opaque,KzsfWeaponZoom *out) {
    KzsfWeaponZoom z={0};uintptr_t inventory=0,handle=0,array=0,resource=0;
    if(!read || !out || base>UINTPTR_MAX-0x1190000 ||
       !kzsf_zoom_read(read,opaque,soldier,0xa48,&inventory,8) ||
       !kzsf_zoom_read(read,opaque,inventory,0x68,&handle,8) || handle<=0x48)return 0;
    z.weapon=handle-0x48;
    /* c24990/c249a0/bfe6c0: count, indexed mode, active mode. */
    if(!kzsf_zoom_method(read,opaque,z.weapon,0x498,base+0xc24990) ||
       !kzsf_zoom_method(read,opaque,z.weapon,0x4a0,base+0xc249a0) ||
       !kzsf_zoom_method(read,opaque,z.weapon,0x4b0,base+0xbfe6c0) ||
       !kzsf_zoom_read(read,opaque,z.weapon,0x628,&z.mode_count,4) ||
       !kzsf_zoom_read(read,opaque,z.weapon,0x918,&z.mode_index,4) ||
       z.mode_count<=0 || z.mode_count>64 || z.mode_index<0 || z.mode_index>=z.mode_count ||
       !kzsf_zoom_read(read,opaque,z.weapon,0x630,&array,8) ||
       !kzsf_zoom_read(read,opaque,array,(size_t)z.mode_index*8,&z.mode_interface,8) ||
       z.mode_interface<0x48 || z.mode_interface>UINTPTR_MAX-0x18 ||
       !kzsf_zoom_method(read,opaque,z.mode_interface,0x68,base+0x3d8420))return 0;
    /* Interface +68 returns THIS+18 (3d8420), not a pointer read or a call. */
    z.zoom_interface=z.mode_interface+0x18;
    if(!kzsf_zoom_method(read,opaque,z.zoom_interface,0x28,base+0xcb54a0) ||
       !kzsf_zoom_method(read,opaque,z.zoom_interface,0x50,base+0x80ad70) ||
       !kzsf_zoom_read(read,opaque,z.zoom_interface,0x38,&z.zoom_index,4) ||
       !kzsf_zoom_read(read,opaque,z.mode_interface-0x48,0x20,&resource,8) ||
       !kzsf_zoom_read(read,opaque,resource,0x68,&z.zoom_count,4) ||
       z.zoom_count<=0 || z.zoom_count>64 || z.zoom_index<0 || z.zoom_index>=z.zoom_count)return 0;
    /* Detect changed ownership/mode while sampling. This does not retain the
     * objects or make a multi-read snapshot atomic; failures mean unknown. */
    uintptr_t check_handle=0,check_inventory=0,check_array=0,check_mode=0;int32_t check_index=-1;
    if(!kzsf_zoom_read(read,opaque,soldier,0xa48,&check_inventory,8) || check_inventory!=inventory ||
       !kzsf_zoom_read(read,opaque,inventory,0x68,&check_handle,8) || check_handle!=handle ||
       !kzsf_zoom_read(read,opaque,z.weapon,0x918,&check_index,4) || check_index!=z.mode_index ||
       !kzsf_zoom_read(read,opaque,z.weapon,0x630,&check_array,8) || check_array!=array ||
       !kzsf_zoom_read(read,opaque,array,(size_t)z.mode_index*8,&check_mode,8) || check_mode!=z.mode_interface)return 0;
    *out=z;return 1;
}
#endif
