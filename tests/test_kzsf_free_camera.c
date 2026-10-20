#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_free_camera_win.h"
#include "fixtures/kzsf_world_cameras.h"
// Upright Killzone pose: forward +X, right -Y, world up +Z.
static float identity[16]={0,-1,0,0,1,0,0,0,0,0,1,0,10,20,30,1};
static unsigned char object[0x80];
static int rd(void *unused,uintptr_t p,void *out,size_t n) {
    (void)unused;
    if(p<(uintptr_t)object || p>(uintptr_t)object+sizeof(object)-n)return 0;
    memcpy(out,(void*)p,n);return 1;
}
static int wr(void *unused,uintptr_t p,const void *in,size_t n) {
    (void)unused;
    if(p<(uintptr_t)object || p>(uintptr_t)object+sizeof(object)-n)return 0;
    memcpy((void*)p,in,n);return 1;
}
int main(void) {
    KzsfFreeCamera a={0},b={0};
    assert(kzsf_free_camera_start(&a,identity));
    assert(kzsf_free_camera_start(&b,identity));
    KzsfFreeCamera turn={0};assert(kzsf_free_camera_start(&turn,identity));
    assert(kzsf_free_camera_turn(&turn,1.57079632679f,0));
    assert(!memcmp(turn.matrix+12,identity+12,16)); // looking must not slide the camera
    assert(fabsf(turn.matrix[5]+1)<.001f && fabsf(turn.matrix[4])<.001f); // forward turns right (-Y)
    // A landmark originally to the right must become centered forward.
    // Native GameView forward is column 1; rendered inverse-view forward
    // is minus column 2. Do not treat those as the same matrix layout.
    float landmark[3]={identity[12],identity[13]-1,identity[14]};
    float view_x=0,view_z=0;
    for(int i=0;i<3;++i) {
        view_x+=turn.matrix[i]*(landmark[i]-turn.matrix[12+i]);
        view_z-=turn.matrix[4+i]*(landmark[i]-turn.matrix[12+i]);
    }
    assert(fabsf(view_x)<.001f && fabsf(view_z+1)<.001f);
    assert(kzsf_free_camera_turn(&turn,0,.4f));
    assert(!memcmp(turn.matrix+12,identity+12,16));
    assert(turn.matrix[6]>0 && turn.matrix[10]>0); // positive pitch points toward world +Z
    for(int i=0;i<1000;++i) {
        assert(kzsf_free_camera_turn(&turn,.03f,.1f));
        assert(turn.matrix[10]>0 && turn.matrix[2]==0); // no upside-down horizon or roll
        assert(turn.pitch<=1.553344f);
    }
    assert(!memcmp(turn.matrix+12,identity+12,16));
    assert(kzsf_free_camera_turn(&turn,0,-100));
    assert(turn.pitch>=-1.553344f && turn.matrix[10]>0);
    // Downward input must cross the horizon, including after reaching the
    // upper stop; merely checking that a clamp is bounded misses a stuck view.
    KzsfFreeCamera down={0};assert(kzsf_free_camera_start(&down,identity));
    assert(kzsf_free_camera_turn(&down,0,-.5f));
    assert(down.pitch<-.49f && down.matrix[6]<-.47f);
    assert(kzsf_free_camera_turn(&down,0,100));
    KzsfFreeCameraInput down_stick={.pitch=-2.5f};
    for(int i=0;i<90;++i)assert(kzsf_free_camera_step(&down,down_stick,1.f/60));
    assert(down.pitch< -1.f && down.matrix[6]<-.84f);
    assert(kzsf_free_camera_turn(&down,0,100));
    for(int i=0;i<120;++i)assert(kzsf_free_camera_turn(&down,0,-.025f));
    assert(down.pitch< -1.f && down.matrix[6]<-.84f);
    KzsfFreeCameraInput in={.forward=1,.speed=3};
    for(int i=0;i<60;++i)assert(kzsf_free_camera_step(&a,in,1.f/60));
    for(int i=0;i<120;++i)assert(kzsf_free_camera_step(&b,in,1.f/120));
    assert(fabsf(a.matrix[12]-13)<.001f && fabsf(a.matrix[12]-b.matrix[12])<.001f);
    assert(a.matrix[14]==30 && b.matrix[14]==30); // level forward must not rise/fall
    KzsfFreeCamera back={0};assert(kzsf_free_camera_start(&back,identity));
    KzsfFreeCameraInput reverse={.forward=-1,.speed=3};
    assert(kzsf_free_camera_step(&back,reverse,.1f));
    assert(fabsf(back.matrix[12]-9.7f)<.001f && back.matrix[14]==30);
    KzsfFreeCamera strafe={0};assert(kzsf_free_camera_start(&strafe,identity));
    KzsfFreeCameraInput side={.right=1,.speed=3};
    assert(kzsf_free_camera_step(&strafe,side,.1f));
    assert(fabsf(strafe.matrix[13]-19.7f)<.001f && strafe.matrix[12]==10);
    // Renderer inverse-view capture: explicitly convert right/up/back
    // to native GameView right/forward/up before using it here.
    const float *captured=kzsf_world_cameras[0];
    float native[16];memcpy(native,captured+32,64);
    for(int i=0;i<3;++i){native[4+i]=-captured[40+i];native[8+i]=captured[36+i];}
    KzsfFreeCamera real={0};assert(kzsf_free_camera_start(&real,native));
    for(int i=0;i<3;++i)assert(fabsf(real.matrix[4+i]+captured[40+i])<.001f);
    for(int i=0;i<400;++i) {
        assert(kzsf_free_camera_turn(&real,.04f,.01f));
        assert(real.matrix[10]>0 && real.matrix[2]==0);
    }
    assert(!memcmp(real.matrix+12,captured+44,16));
    in.yaw=1;in.pitch=.6f;
    for(int i=0;i<6000;++i)assert(kzsf_free_camera_step(&a,in,1.f/60));
    assert(kzsf_free_camera_valid(a.matrix));
    assert(!kzsf_free_camera_step(&a,in,1));
    // Repeated streaming/audit gaps preserve freecam without a movement jump;
    // normal input resumes on the following frame.
    uint64_t tick=1000;
    const uint64_t gaps[]={251,1000,5000,6000};
    for(unsigned i=0;i<sizeof(gaps)/sizeof(gaps[0]);++i) {
        float before_gap[16];memcpy(before_gap,a.matrix,sizeof(before_gap));
        float elapsed=kzsf_free_camera_elapsed(tick,tick+gaps[i]);
        assert(elapsed==0 && kzsf_free_camera_step(&a,in,elapsed));
        assert(a.active && !memcmp(before_gap,a.matrix,sizeof(before_gap)));
        tick+=gaps[i];
        assert(kzsf_free_camera_step(&a,in,kzsf_free_camera_elapsed(tick,tick+16)));
        assert(a.active && memcmp(before_gap,a.matrix,sizeof(before_gap)));
        tick+=16;
    }
    assert(kzsf_free_camera_elapsed(0,1000)==0);
    assert(kzsf_free_camera_elapsed(1000,999)==0);
    assert(fabsf(kzsf_free_camera_elapsed(1000,1250)-.25f)<.00001f);
    float bad[16];memcpy(bad,identity,64);bad[0]=NAN;
    assert(!kzsf_free_camera_start(&b,bad));
    memset(object,0xa5,sizeof(object));
    uint64_t vt=0x12345678;memcpy(object,&vt,8);
    CONTEXT c={0};c.Rbx=(uintptr_t)object;c.EFlags=0x202;c.Rax=99;
    assert(kzsf_free_camera_store(&c,0x100000000,identity,rd,wr,NULL));
    assert(c.Rax==vt && c.Rip==0x100000000+KZSF_FREE_CAMERA_SITE+3 && c.EFlags==0x202);
    assert(!memcmp(object+0x10,identity,64) && object[8]==0xa5 && object[0x50]==0xa5);
    unsigned char before[sizeof(object)];memcpy(before,object,sizeof(object));
    assert(kzsf_free_camera_store(&c,0,NULL,rd,wr,NULL));
    assert(!memcmp(before,object,sizeof(object))); // disabled path makes no camera writes
    assert(!kzsf_free_camera_store(&c,0,bad,rd,wr,NULL));
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);
    char magic[8];uint64_t image_size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));
    assert(fread(&image_size,8,1,f)==1 && image_size>KZSF_FREE_CAMERA_GUARD+sizeof(kzsf_free_camera_signature));
    unsigned char sig[sizeof(kzsf_free_camera_signature)];
    assert(!_fseeki64(f,-(long long)image_size+KZSF_FREE_CAMERA_GUARD,SEEK_END));
    assert(fread(sig,1,sizeof(sig),f)==sizeof(sig));fclose(f);
    assert(!memcmp(sig,kzsf_free_camera_signature,sizeof(sig)));
    puts("Free camera: time scaling, finite rigid transform, bounded store, register replay and real-image guard pass.");
}
