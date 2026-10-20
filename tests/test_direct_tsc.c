#include "../src/tsc_patch.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static const uint32_t game_sites[]={
#include "../src/kzsf_rdtsc_sites.inc"
};
static uint64_t multiplier=UINT64_C(1)<<31;
static unsigned char *memory;
static uint64_t sample(void *fn,unsigned *ecx) {
    uint32_t low,high,c; unsigned char carry;
    __asm__ volatile("xor %%ecx,%%ecx\n stc\n call *%[fn]\n setc %[carry]"
        : "=a"(low),"=d"(high),"=c"(c),[carry] "=m"(carry)
        : [fn] "r"(fn) : "cc","memory");
    assert(carry==1); *ecx=c;
    return ((uint64_t)high<<32)|low;
}
static void test_window(const unsigned char *bytes,size_t size,unsigned expected_ecx) {
    memset(memory,0x90,65536); memcpy(memory+128,bytes,size);
    uint32_t sites[]={128}; size_t used=0;
    TscReport r=tsc_patch_segment(memory,128,32,memory+65536,&used,&multiplier,sites,1);
    assert(r.found==1 && r.moved==0 && r.windows==1 && r.skipped==0);
    FlushInstructionCache(GetCurrentProcess(),memory,65536+used);
    for(unsigned i=0;i<100;++i) {
        unsigned c; uint64_t before=__builtin_ia32_rdtsc(),value=sample(memory+128,&c),after=__builtin_ia32_rdtsc();
        assert(value>=before/2 && value<=after/2 && c==expected_ecx);
    }
}
static LONG CALLBACK clock_trap(EXCEPTION_POINTERS *p) {
    if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT || p->ExceptionRecord->ExceptionAddress!=memory+128)
        return EXCEPTION_CONTINUE_SEARCH;
    uint64_t value=__builtin_ia32_rdtsc()/2;
    p->ContextRecord->Rax=(uint32_t)value; p->ContextRecord->Rdx=value>>32;
    p->ContextRecord->Rip=(DWORD64)(uintptr_t)(memory+130);
    return EXCEPTION_CONTINUE_EXECUTION;
}
static double bench(void) {
    LARGE_INTEGER start,end,freq; unsigned c;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    for(unsigned i=0;i<50000;++i) sample(memory+128,&c);
    QueryPerformanceCounter(&end);
    return (double)(end.QuadPart-start.QuadPart)*1e9/freq.QuadPart/50000;
}
int main(int argc,char **argv) {
    memory=VirtualAlloc(NULL,65536+TSC_PATCH_AREA,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(memory);
    const unsigned char basic[]={0x0f,0x31,0xc3,0x90,0x90};
    test_window(basic,sizeof basic,0);
    unsigned char rip[]={0x0f,0x31,0x8b,0x0d,0,0,0,0,0xc3};
    int32_t disp=1024-(128+8); memcpy(rip+4,&disp,4);
    // test_window clears the target, so the RIP-relative load reads four NOP bytes.
    test_window(rip,sizeof rip,0x90909090);
    unsigned char call[]={0x0f,0x31,0xe8,0,0,0,0,0xc3,0x90,0x90,0xb9,0x7b,0,0,0,0xc3};
    disp=3; memcpy(call+3,&disp,4);
    test_window(call,sizeof call,123);
    unsigned char branch[]={0x0f,0x31,0x74,0x08,0xb9,111,0,0,0,0xc3,0x90,0x90,0xb9,222,0,0,0,0xc3};
    test_window(branch,sizeof branch,222);
    branch[2]=0x75; test_window(branch,sizeof branch,111);
    // Incoming branch to the RET forbids relocating it into the clock window.
    memset(memory,0x90,65536);
    const unsigned char boundary[]={0xe9,2,0,0,0,0x0f,0x31,0xc3};
    memcpy(memory+128,boundary,sizeof boundary); uint32_t sites[]={133}; size_t used=0;
    TscReport r=tsc_patch_segment(memory,128,16,memory+65536,&used,&multiplier,sites,1);
    assert(r.found==1 && r.skipped==1 && memory[133]==0x0f && memory[134]==0x31);
    // Allowlist prevents touching any other clock-like instruction.
    used=0; sites[0]=999;
    r=tsc_patch_segment(memory,128,16,memory+65536,&used,&multiplier,sites,1); assert(r.found==0);
    memset(memory,0x90,65536); memory[128]=0xcc; memory[130]=0xc3;
    void *handler=AddVectoredExceptionHandler(1,clock_trap); assert(handler);
    double exception_ns=bench();
    memcpy(memory+128,basic,sizeof basic); used=0; sites[0]=128;
    r=tsc_patch_segment(memory,128,16,memory+65536,&used,&multiplier,sites,1); assert(r.windows==1);
    FlushInstructionCache(GetCurrentProcess(),memory,65536+used);
    double direct_ns=bench();
    assert(RemoveVectoredExceptionHandler(handler));
    printf("Clock relocation passed: scale, flags, RIP-relative load, CALL return, taken/untaken branches, target boundary and allowlist.\n");
    printf("50000 reads: exception %.1f ns/read; direct %.1f ns/read (microbenchmark, not game FPS).\n",exception_ns,direct_ns);
    assert(VirtualFree(memory,0,MEM_RELEASE));
    if(argc>1) {
        FILE *file=fopen(argv[1],"rb"); assert(file);
        unsigned char magic[8]; uint64_t image_size;
        assert(fread(magic,1,8,file)==8 && !memcmp(magic,"G3PROBE6",8));
        assert(fread(&image_size,8,1,file)==1 && image_size<512*1024*1024);
        unsigned char *game=VirtualAlloc(NULL,image_size+TSC_PATCH_AREA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        assert(game && _fseeki64(file,-(long long)image_size,SEEK_END)==0);
        assert(fread(game,1,image_size,file)==image_size); fclose(file);
        for(size_t i=0;i<sizeof(game_sites)/sizeof(*game_sites);++i)
            assert(game[game_sites[i]]==0x0f && game[game_sites[i]+1]==0x31);
        used=0;
        r=tsc_patch_segment(game,0,0x14d52d4,game+image_size,&used,&multiplier,
                            game_sites,sizeof(game_sites)/sizeof(*game_sites));
        unsigned remaining=0;
        for(size_t i=0;i<sizeof(game_sites)/sizeof(*game_sites);++i)
            remaining+=game[game_sites[i]]==0x0f && game[game_sites[i]+1]==0x31;
        assert(r.found==sizeof(game_sites)/sizeof(*game_sites));
        assert(r.found==r.windows+r.moved+r.skipped && remaining==r.skipped);
        printf("Actual boot image: %u verified sites, %u direct, %u fallback, %zu stub bytes. Original file untouched.\n",
            r.found,r.windows+r.moved,r.skipped,used);
        assert(VirtualFree(game,0,MEM_RELEASE));
    }
}
