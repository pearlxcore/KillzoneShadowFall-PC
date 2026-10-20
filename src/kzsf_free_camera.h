#ifndef KZSF_FREE_CAMERA_H
#define KZSF_FREE_CAMERA_H
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct { float matrix[16]; int active; float heading,pitch; } KzsfFreeCamera;
typedef struct { float right,up,forward,yaw,pitch,speed; } KzsfFreeCameraInput;
/* A stalled callback must not move the camera through the missed time or
 * cancel freecam. View replacement/clock rollback are handled by the caller. */
static inline float kzsf_free_camera_elapsed(uint64_t previous,uint64_t now) {
    return previous && now>=previous && now-previous<=250 ? (now-previous)*.001f : 0;
}
static inline int kzsf_free_camera_valid(const float *m) {
    for(int i=0;i<16;++i)if(!isfinite(m[i]) || fabsf(m[i])>10000000.f)return 0;
    if(fabsf(m[3])>.001f || fabsf(m[7])>.001f || fabsf(m[11])>.001f || fabsf(m[15]-1.f)>.001f)return 0;
    for(int a=0;a<3;++a)for(int b=a;b<3;++b) {
        float d=0;for(int i=0;i<3;++i)d+=m[a*4+i]*m[b*4+i];
        if(fabsf(d-(a==b ? 1.f : 0.f))>.025f)return 0;
    }
    return 1;
}
static inline void kzsf_free_camera_basis(KzsfFreeCamera *s) {
    // Native GameView uses right/forward/up, unlike the renderer's
    // right/up/back inverse-view matrix. World up is +Z.
    float ch=cosf(s->heading),sh=sinf(s->heading),cp=cosf(s->pitch),sp=sinf(s->pitch);
    float *m=s->matrix;
    m[0]=sh;m[1]=-ch;m[2]=0;
    m[4]=ch*cp;m[5]=sh*cp;m[6]=sp;
    m[8]=-ch*sp;m[9]=-sh*sp;m[10]=cp;
}
static inline int kzsf_free_camera_start(KzsfFreeCamera *s,const float *native) {
    if(!kzsf_free_camera_valid(native))return 0;
    memcpy(s->matrix,native,sizeof(s->matrix));
    float fx=native[4],fy=native[5],fz=fmaxf(-1.f,fminf(1.f,native[6]));
    s->heading=fx*fx+fy*fy>1e-8f ? atan2f(fy,fx) : atan2f(native[0],-native[1]);
    const float limit=1.55334303427f; // 89 degrees: avoid pole flips
    s->pitch=fmaxf(-limit,fminf(limit,asinf(fz)));
    kzsf_free_camera_basis(s);s->active=1;return 1;
}
static inline int kzsf_free_camera_turn(KzsfFreeCamera *s,float yaw,float pitch) {
    if(!s->active || !isfinite(yaw) || !isfinite(pitch))return 0;
    // Turn about the eye: translation is deliberately untouched.
    const float limit=1.55334303427f;
    s->heading=remainderf(s->heading-yaw,6.28318530718f);
    s->pitch=fmaxf(-limit,fminf(limit,s->pitch+pitch));
    kzsf_free_camera_basis(s);
    return kzsf_free_camera_valid(s->matrix);
}
static inline int kzsf_free_camera_step(KzsfFreeCamera *s,KzsfFreeCameraInput in,float seconds) {
    if(!s->active || !isfinite(seconds) || seconds<0 || seconds>.25f)return 0;
    if(!isfinite(in.right) || !isfinite(in.up) || !isfinite(in.forward) ||
       !isfinite(in.yaw) || !isfinite(in.pitch) || !isfinite(in.speed) || in.speed<0 || in.speed>30)return 0;
    float *m=s->matrix;
    // Yaw stays about world Z; mouse, arrows and sticks use the same pose.
    if(!kzsf_free_camera_turn(s,in.yaw*seconds,in.pitch*seconds))return 0;
    float n=sqrtf(in.right*in.right+in.up*in.up+in.forward*in.forward);
    if(n<1)n=1;
    float delta[3];float distance=0;
    for(int i=0;i<3;++i) {
        delta[i]=(m[i]*in.right+(i==2 ? in.up : 0)+m[4+i]*in.forward)/n;
        distance+=delta[i]*delta[i];
    }
    float scale=distance>1 ? 1.f/sqrtf(distance) : 1.f;
    for(int i=0;i<3;++i)m[12+i]+=delta[i]*in.speed*seconds*scale;
    return kzsf_free_camera_valid(m);
}
#endif
