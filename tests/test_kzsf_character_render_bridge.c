#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "../src/kzsf_character_render_bridge.h"
static int calls;
static uintptr_t draw_trampoline;
static void __attribute__((sysv_abi)) visit(uintptr_t node) {assert(node==0x12345678);calls++;}
static void __attribute__((sysv_abi)) draw_visit(uintptr_t node) {
    assert(node==0x12345678);calls++;
    assert(((uintptr_t (__attribute__((sysv_abi)) *)(uintptr_t))draw_trampoline)(node)==0x77);
}
int main(void) {
    unsigned char original[5];FILE *f=fopen("out/boot-linked.bin","rb");assert(f);char magic[8];uint64_t size;
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8));assert(fread(&size,8,1,f)==1);
    assert(!fseek(f,0,SEEK_END));long offset=ftell(f)-(long)size;
    assert(!fseek(f,offset+KZSF_CHARACTER_RENDER_SITE,SEEK_SET));assert(fread(original,1,5,f)==5);fclose(f);
    int32_t native_relative=0;memcpy(&native_relative,original+1,4);
    assert(KZSF_CHARACTER_RENDER_SITE+5+native_relative==0x711ba0);
    unsigned char *memory=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    /* Entry aligns the stack before issuing the replacement CALL, as the
     * original native caller does. Stub is a tail jump, so no second return. */
    unsigned char entry[]={0x48,0x83,0xec,8,0,0,0,0,0,0x48,0x83,0xc4,8,0xc3};
    memcpy(memory,entry,sizeof(entry));
    assert(kzsf_character_render_bridge(memory+64,64,(uintptr_t)memory+64,memory+4,(uintptr_t)memory+4,original,(uintptr_t)visit));
    DWORD old=0;assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),memory,4096);
    ((void (__attribute__((sysv_abi)) *)(uintptr_t))memory)(0x12345678);
    assert(calls==1);assert(VirtualFree(memory,0,MEM_RELEASE));
    unsigned char stub[12],patch[5],before[12];memset(stub,0xa5,12);memcpy(before,stub,12);
    original[1]^=1;
    assert(!kzsf_character_render_bridge(stub,12,0x1000,patch,0x2000,original,(uintptr_t)visit));
    assert(!memcmp(stub,before,12));original[1]^=1;
    assert(!kzsf_character_render_bridge(stub,11,0x1000,patch,0x2000,original,(uintptr_t)visit));
    assert(!kzsf_character_render_bridge(stub,12,UINTPTR_MAX,patch,0x2000,original,(uintptr_t)visit));
    unsigned char draw_code[20],draw_patch[20];
    f=fopen("out/boot-linked.bin","rb");assert(f);
    assert(!fseek(f,offset+KZSF_CHARACTER_DRAW_ENTRY,SEEK_SET));assert(fread(draw_code,1,20,f)==20);fclose(f);
    memory=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    memcpy(memory,draw_code,20);
    unsigned char finish[]={0xb8,0x77,0,0,0,0x48,0x81,0xc4,0x38,0x02,0,0,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    memcpy(memory+20,finish,sizeof(finish));draw_trampoline=(uintptr_t)memory+128;
    assert(kzsf_character_draw_bridge(memory+128,64,draw_trampoline,draw_patch,(uintptr_t)memory,draw_code,(uintptr_t)draw_visit));
    memcpy(memory,draw_patch,20);assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),memory,4096);
    ((void (__attribute__((sysv_abi)) *)(uintptr_t))memory)(0x12345678);
    assert(calls==2);assert(VirtualFree(memory,0,MEM_RELEASE));
    draw_code[0]^=1;
    assert(!kzsf_character_draw_bridge(stub,12,0x1000,draw_patch,0x2000,draw_code,(uintptr_t)draw_visit));
    puts("character render bridge: real native call target, executable SysV forwarding, exact return and guard/range rejection pass");
    return 0;
}
