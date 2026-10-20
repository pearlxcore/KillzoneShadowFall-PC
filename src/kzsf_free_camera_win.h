#ifndef KZSF_FREE_CAMERA_WIN_H
#define KZSF_FREE_CAMERA_WIN_H
#include "kzsf_camera_controls_win.h"
#include "kzsf_free_camera.h"
enum { KZSF_FREE_CAMERA_GUARD=0xd92e84,KZSF_FREE_CAMERA_SITE=0xd92e8e };
static const unsigned char kzsf_free_camera_signature[]={
    0xc5,0xf8,0x28,0x45,0xc0,0xc5,0xf8,0x29,0x43,0x40,
    0x48,0x8b,0x03,0x48,0x89,0xdf,0xff,0x90,0x90,0x00,0x00,0x00};
// Replay MOV RAX,[RBX] and optionally replace this view's final transform,
// before its existing virtual refresh rebuilds the derived camera matrices.
static inline int kzsf_free_camera_store(CONTEXT *c,uintptr_t base,const float *matrix,
                     KzsfCameraRead read,KzsfCameraWrite write,void *opaque) {
    uint64_t vtable=0;
    if(c->Rbx>UINTPTR_MAX-0x50 || !read(opaque,c->Rbx,&vtable,8))return 0;
    if(matrix && (!kzsf_free_camera_valid(matrix) || !write(opaque,c->Rbx+0x10,matrix,64)))return 0;
    c->Rax=vtable;c->Rip=base+KZSF_FREE_CAMERA_SITE+3;return 1;
}
#endif
