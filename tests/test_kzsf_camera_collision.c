#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_camera_collision.h"
static unsigned excludes,sweeps,releases,adds,sorts,attempt;static int scenario;
static void KZSF_COLLISION_ABI exclude(uintptr_t owner,KzsfCameraFilter *f) {
    assert(owner==42 && f->table==0x16c3880 && f->group==0x24 && f->mask==0x7f);
    assert(!f->flags && !f->accept_null && !f->count && !f->items && !f->capacity);
    ++excludes;f->items=123;f->count=scenario==4 ? 0 : 2;f->capacity=16;
}
static unsigned char KZSF_COLLISION_ABI sweep(uintptr_t world,const float *a,const float *b,
        KzsfCameraHit *out,KzsfCameraFilter *f,float radius,float tolerance) {
    assert(world==84 && f->count>=2 && radius==(scenario==5 ? .01f : .2f) && tolerance==.0001f);
    assert(!((uintptr_t)a&15) && !((uintptr_t)b&15) && !((uintptr_t)out&15));
    ++sweeps;float center[4];
    for(int i=0;i<4;i++)center[i]=a[i]+.5f*(b[i]-a[i]);
    if(scenario==2)center[0]=NAN;
    if(scenario==3)center[1]+=1;
    memcpy(out->bytes+0x40,center,16);
    if(scenario>=6) {
        uintptr_t association=attempt==0 || scenario==8 ? 100 : 200;
        if(scenario==9)association=100+attempt;
        memcpy(out->bytes,&association,8);++attempt;
        if(scenario==7 && attempt==2)return 0;
    }
    memset(out->bytes+0x10,0xcc,16); /* Must NOT use the contact position. */
    return scenario!=0;
}
static void KZSF_COLLISION_ABI release(uintptr_t items){assert(items==123);++releases;}
static void KZSF_COLLISION_ABI add(KzsfCameraFilter *f,uintptr_t entity) {
    assert(entity>=321 && entity<=325);++adds;++f->count;
}
static void KZSF_COLLISION_ABI sort(KzsfCameraFilter *f){assert(f->count>2);++sorts;}
static uintptr_t ignore(void *opaque,uintptr_t association) {
    assert(opaque==(void*)42);
    return scenario==9 ? association+221 : association==100 ? 321 : 0;
}
static FILE *boot;static uint64_t image_size;
static int read_boot(void *opaque,uintptr_t address,void *out,size_t n) {
    (void)opaque;if(address<0x10000000)return 0;address-=0x10000000;
    if(address>=image_size || n>image_size-address)return 0;
    if(address==0x16c3898 && n==8){uintptr_t method=0x10000000+0x975810;memcpy(out,&method,8);return 1;}
    return !_fseeki64(boot,-(long long)image_size+(long long)address,SEEK_END) && fread(out,1,n,boot)==n;
}
int main(void) {
    KzsfCameraPhysics api={exclude,sweep,release};float a[3]={10,20,30},b[3]={8,20,30},fraction=-1;int hit=-1;
    for(scenario=0;scenario<=4;scenario++) {
        unsigned prior=sweeps;
        int ok=kzsf_camera_collision_query(0,84,42,api,a,b,&fraction,&hit);
        assert(excludes==(unsigned)scenario+1 && releases==excludes);
        assert(sweeps==prior+(scenario!=4));
        if(scenario<2){assert(ok && hit==scenario);assert(fabsf(fraction-(scenario ? .485f : 1))<.00001f);}
        else assert(!ok);
    }
    assert(!kzsf_camera_collision_query(0,0,42,api,a,b,&fraction,&hit));
    b[0]=NAN;assert(!kzsf_camera_collision_query(0,84,42,api,a,b,&fraction,&hit));
    b[0]=10;assert(!kzsf_camera_collision_query(0,84,42,api,a,b,&fraction,&hit));
    assert(excludes==5 && releases==5); /* Invalid input makes no native calls. */
    scenario=5;b[0]=-240;
    assert(kzsf_camera_collision_trace(0,84,42,api,a,b,.01f,500,&fraction,&hit));
    assert(hit && fabsf(fraction-.5f)<.00001f && excludes==6 && releases==6);
    assert(!kzsf_camera_collision_trace(0,84,42,api,a,b,0,500,&fraction,&hit));
    assert(!kzsf_camera_collision_trace(0,84,42,api,a,b,.01f,NAN,&fraction,&hit));
    assert(excludes==6 && releases==6);
    /* A projectile in front of a wall must be removed and the complete arm
     * reswept, while native wall clipping and list cleanup remain effective. */
    KzsfCameraExtraIgnore extra={add,sort,ignore,(void*)42};b[0]=8;
    for(scenario=6;scenario<=9;scenario++) {
        attempt=adds=sorts=0;unsigned before=releases;fraction=-1;
        int ok=kzsf_camera_collision_query_ex(0,84,42,api,a,b,&fraction,&hit,&extra);
        assert(releases==before+1 && releases==excludes && adds==sorts);
        if(scenario==6)assert(ok && hit && fabsf(fraction-.485f)<.00001f && adds==1 && attempt==2);
        if(scenario==7)assert(ok && !hit && fraction==1 && adds==1 && attempt==2);
        if(scenario==8)assert(!ok && fraction==-1 && adds==1 && attempt==2);
        if(scenario==9)assert(!ok && fraction==-1 && adds==4 && attempt==5);
    }
    KzsfCameraBoom s={0};float native[16]={0},pose[16]={0};native[12]=10;pose[12]=8;
    assert(kzsf_camera_boom_apply(&s,1,100,native,pose,1));assert(pose[12]==8);
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,1,116,native,pose,.5f));assert(pose[12]==9);
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,1,132,native,pose,1));assert(pose[12]==9);
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,1,252,native,pose,1));assert(pose[12]<9 && pose[12]>8);
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,1,268,native,pose,.1f));assert(fabsf(pose[12]-9.8f)<.00001f);
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,1,276,native,pose,0));assert(pose[12]==10);
    assert(!kzsf_camera_boom_apply(&s,1,284,native,pose,NAN));
    pose[12]=8;assert(kzsf_camera_boom_apply(&s,2,300,native,pose,1));assert(pose[12]==8);
    boot=fopen("out/boot-linked.bin","rb");assert(boot);char magic[8];
    assert(fread(magic,1,8,boot)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&image_size,8,1,boot)==1);
    assert(kzsf_camera_collision_guards(0x10000000,read_boot,NULL));
    const uint32_t getters[]={0xce9ed0,0xce9f70},types[]={0x3cbf2a8,0x3cbf388};
    for(unsigned i=0;i<2;i++) {
        unsigned char code[8];int32_t relative;
        assert(read_boot(NULL,0x10000000+getters[i],code,8));
        assert(!memcmp(code,"\x48\x8d\x05",3) && code[7]==0xc3);
        memcpy(&relative,code+3,4);assert((int64_t)getters[i]+7+relative==types[i]);
    }
    fclose(boot);
    puts("Camera collision: SysV/alignment, projectile resweeps preserve walls, bounded retries, cleanup, clipping/recovery and image guards pass.");
}
