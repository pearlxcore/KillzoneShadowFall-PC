#ifndef KZSF_THIRD_PERSON_H
#define KZSF_THIRD_PERSON_H
#include <stdint.h>
#include "kzsf_free_camera.h"
/* Visible follow-view prototype. Native controls/aim direction are retained.
 * Produces the desired short boom. The caller must resolve wall collision;
 * its final orientation can converge on a separately resolved native aim point. */
typedef struct {int requested,key_down;uintptr_t view;uint64_t tick;} KzsfThirdPerson;
/* Alternative execution views use the same native basis and must each pass
 * the caller's full world sweep. Never use an unswept position or force a
 * minimum collision distance through an obstruction. */
static inline int kzsf_third_person_combat_candidate(const float native[16],unsigned index,float out[16]) {
    static const float offsets[][3]={{-1.8f,-.8f,.25f},{-.6f,1.6f,.35f},{-.6f,-1.6f,.35f}};
    if(!native || !out || index>=3 || !kzsf_free_camera_valid(native))return 0;
    float pose[16];memcpy(pose,native,sizeof(pose));
    for(int i=0;i<3;i++)pose[12+i]+=offsets[index][0]*native[4+i]+
        offsets[index][1]*native[i]+offsets[index][2]*native[8+i];
    if(!kzsf_free_camera_valid(pose))return 0;
    memcpy(out,pose,sizeof(pose));return 1;
}
static inline float kzsf_third_person_clearance(const float native[16],const float pose[16],float fraction) {
    if(!native || !pose || !isfinite(fraction) || fraction<0 || fraction>1)return -1;
    float distance2=0;
    for(int i=0;i<3;i++){float d=pose[12+i]-native[12+i];distance2+=d*d;}
    return isfinite(distance2) ? sqrtf(distance2)*fraction : -1;
}
/* Hide the local world mesh before the collision-shortened eye enters it.
 * Hysteresis prevents flicker; this never enables first-person geometry. */
static inline int kzsf_third_person_body_hidden(int hidden,const float native[16],const float pose[16]) {
    float d2=0;
    for(int i=0;i<3;i++){float d=pose[12+i]-native[12+i];d2+=d*d;}
    float radius=hidden ? .65f : .5f;
    return isfinite(d2) && d2<radius*radius;
}
static inline int kzsf_third_person_step(KzsfThirdPerson *s,uintptr_t view,uint64_t tick,
        int focused,int key,int baseline,const float native[16],float out[16]) {
    int pressed=focused && key && !s->key_down;s->key_down=focused && key;
    /* Streaming, appearance transactions and rig discovery can take more
     * than250ms on the engine thread. Elapsed time is not a camera change:
     * keep the user's F8 intent and validate the new native pose each frame.
     * Actual view changes and clock rollback still clear stale intent. */
    if((s->view && s->view!=view) || (s->tick && tick<s->tick))s->requested=0;
    s->view=view;s->tick=tick;
    if(pressed)s->requested=!s->requested;
    if(!s->requested || !focused || !baseline || !native || !out || !kzsf_free_camera_valid(native))return 0;
    float pose[16];memcpy(pose,native,sizeof(pose));
    for(int i=0;i<3;i++)pose[12+i]+=-1.8f*native[4+i]+0.35f*native[i]+0.12f*native[8+i];
    if(!kzsf_free_camera_valid(pose))return 0;
    memcpy(out,pose,sizeof(pose));return 1;
}
/* Keep the native shot direction/controls intact. Point only the display camera
 * at a point on that line, after collision has placed its eye. No fixed reticle
 * shift or guessed constant target distance. The caller supplies target range. */
static inline int kzsf_third_person_converge(const float native[16],float pose[16],float distance) {
    if(!native || !pose || !kzsf_free_camera_valid(native) || !kzsf_free_camera_valid(pose) ||
       !isfinite(distance) || distance<0 || distance>500)return 0;
    float m[16];memcpy(m,pose,sizeof(m));float forward[3],right[3],length2=0;
    for(int i=0;i<3;i++) {forward[i]=native[12+i]+distance*native[4+i]-pose[12+i];length2+=forward[i]*forward[i];}
    if(!isfinite(length2))return 0;
    /* At a fully collapsed boom the eye and hit point can coincide. Retain
     * the valid native orientation rather than selecting first-person view. */
    if(length2<.0001f)return 1;
    float scale=1/sqrtf(length2);for(int i=0;i<3;i++)forward[i]*=scale;
    right[0]=forward[1]*native[10]-forward[2]*native[9];
    right[1]=forward[2]*native[8]-forward[0]*native[10];
    right[2]=forward[0]*native[9]-forward[1]*native[8];
    length2=right[0]*right[0]+right[1]*right[1]+right[2]*right[2];
    if(!isfinite(length2) || length2<.0001f)return 0;
    scale=1/sqrtf(length2);for(int i=0;i<3;i++){m[i]=right[i]*scale;m[4+i]=forward[i];}
    m[8]=m[1]*m[6]-m[2]*m[5];m[9]=m[2]*m[4]-m[0]*m[6];m[10]=m[0]*m[5]-m[1]*m[4];
    if(!kzsf_free_camera_valid(m))return 0;
    memcpy(pose,m,sizeof(m));return 1;
}
#endif
