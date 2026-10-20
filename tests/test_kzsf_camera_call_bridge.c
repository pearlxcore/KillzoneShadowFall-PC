#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "../src/kzsf_camera_call_bridge.h"
#define TEST_ABI __attribute__((sysv_abi))
static uintptr_t wanted_render,wanted_view,wanted_refresh;
static unsigned calls;
/* Inspect actual stack alignment at the supplied virtual refresh entry. */
static TEST_ABI __attribute__((naked)) uintptr_t refresh(void *render __attribute__((unused))) {
    __asm__("mov %rsp,%rax\n\tand $15,%eax\n\tret");
}
static TEST_ABI uintptr_t wrapper(uintptr_t render,uintptr_t view,uintptr_t original) {
    assert(render==wanted_render && view==wanted_view && original==wanted_refresh);++calls;
    return ((uintptr_t (TEST_ABI *)(void *))original)((void*)render);
}
int main(void) {
    unsigned char code[64],patch[12],saved_code[64],saved_patch[12];
    memset(code,0xa5,sizeof(code));memset(patch,0xa5,sizeof(patch));
    memcpy(saved_code,code,sizeof(code));memcpy(saved_patch,patch,sizeof(patch));
    unsigned char wrong[12]={0};
    assert(!kzsf_camera_call_bridge(code,sizeof(code),0x1000,patch,0x2000,wrong,1));
    assert(!kzsf_camera_call_bridge(code,32,0x1000,patch,0x2000,kzsf_camera_call_signature,1));
    assert(!kzsf_camera_call_bridge(code,sizeof(code),UINTPTR_MAX,patch,0x2000,kzsf_camera_call_signature,1));
    assert(!kzsf_camera_call_bridge(code,sizeof(code),0x1000,patch,UINTPTR_MAX,kzsf_camera_call_signature,1));
    assert(!kzsf_camera_call_bridge(code,sizeof(code),0x1000,patch,0x100000000ULL,kzsf_camera_call_signature,1));
    assert(!memcmp(saved_code,code,sizeof(code)) && !memcmp(saved_patch,patch,sizeof(patch)));
    int32_t delta;assert(kzsf_camera_relative(0x90000000,0x10000000,&delta) && delta==INT32_MIN);
    assert(!kzsf_camera_relative(0x90000001,0x10000000,&delta));
    assert(kzsf_camera_relative(0x10000000,0x8fffffff,&delta) && delta==INT32_MAX);
    assert(!kzsf_camera_relative(0x10000000,0x90000000,&delta));
    unsigned char *area=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(area);
    uintptr_t table[19]={0};table[18]=(uintptr_t)refresh;uintptr_t object=(uintptr_t)table;
    wanted_render=(uintptr_t)&object;wanted_view=0x12345678;wanted_refresh=(uintptr_t)refresh;
    /* SysV caller: preserve RBX/R14; align stack before the replaced call. */
    const unsigned char prologue[]={0x53,0x41,0x56,0x48,0x83,0xec,8,0x48,0xbb};
    memcpy(area,prologue,sizeof(prologue));memcpy(area+9,&wanted_render,8);
    area[17]=0x49;area[18]=0xbe;memcpy(area+19,&wanted_view,8);
    memcpy(area+27,kzsf_camera_call_signature,12);
    const unsigned char tail[]={0x48,0x83,0xc4,8,0x41,0x5e,0x5b,0xc3};memcpy(area+39,tail,sizeof(tail));
    /* Exact native call path works before detouring, with no wrapper entry. */
    DWORD prior;assert(VirtualProtect(area,4096,PAGE_EXECUTE_READ,&prior));
    assert(FlushInstructionCache(GetCurrentProcess(),area,4096));
    assert(((uintptr_t (TEST_ABI *)(void))area)()==8 && calls==0);
    assert(VirtualProtect(area,4096,PAGE_READWRITE,&prior));
    assert(kzsf_camera_call_bridge(area+128,64,(uintptr_t)area+128,patch,(uintptr_t)area+27,
                                  area+27,(uintptr_t)wrapper));memcpy(area+27,patch,12);
    DWORD previous;assert(VirtualProtect(area,4096,PAGE_EXECUTE_READ,&previous));
    assert(FlushInstructionCache(GetCurrentProcess(),area,4096));
    for(unsigned i=0;i<100;i++)assert(((uintptr_t (TEST_ABI *)(void))area)()==8);
    assert(calls==100);assert(VirtualFree(area,0,MEM_RELEASE));
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(size>KZSF_CAMERA_CALL_SITE+12);
    assert(!_fseeki64(f,-(long long)size+KZSF_CAMERA_CALL_SITE,SEEK_END));
    unsigned char real[12];assert(fread(real,1,12,f)==12);fclose(f);
    assert(!memcmp(real,kzsf_camera_call_signature,12));
    f=fopen("out/boot-linked.bin","rb");assert(f);
    assert(!_fseeki64(f,-(long long)size+0x5fdb30,SEEK_END));
    assert(fgetc(f)==0xc3);fclose(f);
    puts("Camera call bridge: executed SysV dispatch/return, original refresh, stack alignment, bounds and real-image guard pass.");
}
