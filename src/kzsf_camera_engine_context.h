#ifndef KZSF_CAMERA_ENGINE_CONTEXT_H
#define KZSF_CAMERA_ENGINE_CONTEXT_H
#include "kzsf_weapon_zoom.h"
/* Inspect the allocator path used by d23b0. Readability/code-range checks do
 * not establish allocator type, lock ownership or permission to call physics. */
typedef struct {uintptr_t tcb,router,heap,allocate_method;} KzsfCameraEngineContext;
static inline int kzsf_camera_engine_context(uintptr_t tcb,uintptr_t base,KzsfZoomRead read,
                                             void *opaque,KzsfCameraEngineContext *out) {
    KzsfCameraEngineContext value={0};int64_t offset=0;uintptr_t self=0,table=0,again=0;
    if(!read || !out || tcb<152 || base>UINTPTR_MAX-0x185e728 ||
       !kzsf_zoom_read(read,opaque,base,0x185e720,&offset,8) || offset!=-152 ||
       !kzsf_zoom_read(read,opaque,tcb,0,&self,8) || self!=tcb ||
       !kzsf_zoom_read(read,opaque,tcb-152,0,&value.router,8) ||
       !kzsf_zoom_read(read,opaque,value.router,0x58,&value.heap,8) ||
       !kzsf_zoom_read(read,opaque,value.heap,0,&table,8) ||
       !kzsf_zoom_read(read,opaque,table,0x10,&value.allocate_method,8) ||
       value.allocate_method<base || value.allocate_method-base>=0x1190000)return 0;
    if(!kzsf_zoom_read(read,opaque,tcb-152,0,&again,8) || again!=value.router ||
       !kzsf_zoom_read(read,opaque,value.router,0x58,&again,8) || again!=value.heap ||
       !kzsf_zoom_read(read,opaque,value.heap,0,&again,8) || again!=table ||
       !kzsf_zoom_read(read,opaque,table,0x10,&again,8) || again!=value.allocate_method)return 0;
    value.tcb=tcb;*out=value;return 1;
}
#endif
