#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "../src/kzsf_character_face_owner_bridge.h"
static uintptr_t __attribute__((sysv_abi)) visit(uintptr_t owner,uintptr_t face) {
    assert(owner==12 && face==34);
    __asm__ volatile("xor %%rcx,%%rcx; xor %%rdx,%%rdx; xor %%rdi,%%rdi; xor %%rsi,%%rsi;"
        "xor %%r8,%%r8; xor %%r9,%%r9; xor %%r10,%%r10; xor %%r11,%%r11;"
        "pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3;"
        "pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7;"
        "pxor %%xmm8,%%xmm8; pxor %%xmm9,%%xmm9; pxor %%xmm10,%%xmm10; pxor %%xmm11,%%xmm11;"
        "pxor %%xmm12,%%xmm12; pxor %%xmm13,%%xmm13; pxor %%xmm14,%%xmm14; pxor %%xmm15,%%xmm15;"
        :::"rcx","rdx","rdi","rsi","r8","r9","r10","r11","cc",
        "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7",
        "xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15");
    return 0x12345678;
}
int main(void) {
    FILE *f=fopen("out/boot-linked.bin","rb");assert(f);uint64_t size=0;char magic[8];unsigned char original[7];
    assert(fread(magic,1,8,f)==8 && !memcmp(magic,"G3PROBE6",8) && fread(&size,8,1,f)==1);
    assert(!_fseeki64(f,-(long long)size+KZSF_CHARACTER_FACE_OWNER_SITE,SEEK_END));
    assert(fread(original,1,7,f)==7 && !memcmp(original,kzsf_character_face_owner_guard,7));fclose(f);
    unsigned char *memory=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    const unsigned char entry[]={0x41,0x54,0x48,0x89,0xf8,0x49,0x89,0xf4,
        0xf3,0x0f,0x6f,0x02,0xf3,0x44,0x0f,0x6f,0x42,0x10,0xf9};
    const unsigned char finish[]={0xf3,0x0f,0x7f,0x42,0x20,0xf3,0x44,0x0f,0x7f,0x42,0x30,
        0x48,0x89,0x4a,0x40,0x4c,0x89,0x42,0x48,0x4c,0x89,0x4a,0x50,
        0x9c,0x59,0x48,0x89,0x4a,0x58,0x41,0x5c,0xc3};
    memcpy(memory,entry,sizeof(entry));memcpy(memory+sizeof(entry)+7,finish,sizeof(finish));
    int n=kzsf_character_face_owner_bridge(memory+256,384,(uintptr_t)memory+256,memory+sizeof(entry),
        (uintptr_t)memory+sizeof(entry),original,(uintptr_t)visit);assert(n>0 && n<=384);
    DWORD old;assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),memory,4096);
    uint64_t data[12]={11,22,33,44};
    uintptr_t result=((uintptr_t (__attribute__((sysv_abi)) *)(uintptr_t,uintptr_t,void *,uintptr_t,uintptr_t,uintptr_t))memory)(12,34,data,55,66,77);
    assert(result==0x12345678 && !memcmp(data,data+4,32));
    assert(data[8]==55 && data[9]==66 && data[10]==77 && (data[11]&1));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    unsigned char stub[384],patch[7];
    assert(!kzsf_character_face_owner_bridge(stub,383,0x1000,patch,0x2000,original,(uintptr_t)visit));
    original[0]^=1;
    assert(!kzsf_character_face_owner_bridge(stub,384,0x1000,patch,0x2000,original,(uintptr_t)visit));
    puts("face constructor bridge: shipped guard, executable SysV callback, GPR/XMM/flags preservation and exact return pass");
}
