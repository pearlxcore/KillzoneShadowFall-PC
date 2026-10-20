#include "../src/kzsf_cheats.h"
#include <windows.h>
#include "../src/kzsf_cheats_win.h"
#include <assert.h>
#include <stdio.h>
typedef struct { unsigned char actor[4096],seat[4096],vtable[1024],seat_vtable[1024],component[128],stack[256]; } EntityFixture;
static int read_fixture(void *opaque,uintptr_t at,void *out,size_t size) {
    EntityFixture *f=opaque; uintptr_t start=(uintptr_t)f;
    if(at<start || size>sizeof(*f) || at-start>sizeof(*f)-size) return 0;
    memcpy(out,(void *)at,size); return 1;
}
static void put_pointer(void *at,uintptr_t value) { memcpy(at,&value,8); }
static void test_player_identification(void) {
    EntityFixture f={0}; const uintptr_t base=0x800000000;
    put_pointer(f.actor,(uintptr_t)f.vtable);
    put_pointer(f.vtable+0x140,base+0xa7ab20);
    put_pointer(f.vtable+0x138,base+0xb67220);
    assert(!kzsf_player_entity((uintptr_t)f.actor,base,read_fixture,&f,0)); // NPC with no player controller.
    put_pointer(f.actor+0x948,123);
    assert(kzsf_player_entity((uintptr_t)f.actor,base,read_fixture,&f,0));
    put_pointer(f.component+0x38,(uintptr_t)f.actor);
    CONTEXT context={0},expected;
    context.Rdi=(uintptr_t)f.component; context.Rsp=(uintptr_t)f.stack+64;
    context.Rbp=0x12345678; context.Rax=0xabcdef; context.EFlags=0x246;
    context.Xmm2.Low=0x3f800000;
    put_pointer(f.stack+64,base+0x456789);
    expected=context; expected.Rip=base+0x456789; expected.Rsp+=8;
    assert(kzsf_handle_player_damage(&context,base,read_fixture,&f)==2);
    assert(!memcmp(&context,&expected,sizeof(context)));
    // The same actual handler must replay the prologue for NPCs without touching SIMD/flags.
    put_pointer(f.actor+0x948,0); context=expected;
    expected.Rsp-=8; expected.Rip=base+KZSF_DAMAGE_SITE+1;
    assert(kzsf_handle_player_damage(&context,base,read_fixture,&f)==1);
    assert(!memcmp(&context,&expected,sizeof(context)));
    uintptr_t pushed; memcpy(&pushed,(void *)context.Rsp,8); assert(pushed==context.Rbp);
    put_pointer(f.actor+0x948,0); f.actor[0x282]=1;
    assert(kzsf_player_entity((uintptr_t)f.actor,base,read_fixture,&f,0));
    f.actor[0x282]=0;
    put_pointer(f.vtable+0x140,base+0x28a0b0);
    put_pointer(f.actor+0x948,123); // Non-player class must remain vulnerable despite matching data.
    assert(!kzsf_player_entity((uintptr_t)f.actor,base,read_fixture,&f,0));
    put_pointer(f.vtable+0x140,base+0xa7ab20);
    put_pointer(f.seat,(uintptr_t)f.seat_vtable);
    put_pointer(f.seat_vtable+0x140,base+0xa74980);
    put_pointer(f.seat+0x598,(uintptr_t)f.actor+0x48);
    assert(kzsf_player_entity((uintptr_t)f.seat,base,read_fixture,&f,0));
    put_pointer(f.seat_vtable+0x140,base+0xadb8f0);
    put_pointer(f.seat+0x580,(uintptr_t)f.actor+0x48);
    assert(kzsf_player_entity((uintptr_t)f.seat,base,read_fixture,&f,0));
    put_pointer(f.seat+0x580,(uintptr_t)f.seat+0x48); // Cycles cannot recurse forever.
    assert(!kzsf_player_entity((uintptr_t)f.seat,base,read_fixture,&f,0));
    assert(!kzsf_player_entity(0,base,read_fixture,&f,0));
    assert(!kzsf_player_entity(1234,base,read_fixture,&f,0));
}
static void run_health(void *code,uint32_t marker,float damage,float expected) {
    float health=100.0f,result=0.0f;
    unsigned char carry=0;
    __asm__ volatile(
        "vmovss %[hp], %%xmm2\n"
        "vmovss %[damage], %%xmm0\n"
        "mov %[pointer], %%rax\n"
        "mov %[marker], %%r10d\n"
        "stc\n"
        "call *%[code]\n"
        "setc %[carry]\n"
        "vmovss %%xmm0, %[result]\n"
        : [result] "=m"(result), [carry] "=m"(carry), [hp] "+m"(health)
        : [damage] "m"(damage), [pointer] "r"(&health), [marker] "r"(marker), [code] "r"(code)
        : "rax","r10","xmm0","xmm2","cc","memory");
    assert(health==expected && result==expected && carry==1);
}
static void run_ammo(void *code,size_t offset,uint32_t expected) {
    uint32_t weapon[8]={0};
    weapon[offset/4]=25;
    __asm__ volatile(
        "mov %[weapon], %%rax\n"
        "mov %[weapon], %%r13\n"
        "mov $7, %%r14d\n"
        "call *%[code]\n"
        : : [weapon] "r"(weapon), [code] "r"(code)
        : "rax","r13","r14","cc","memory");
    assert(weapon[offset/4]==expected);
}
int main(void) {
    test_player_identification();
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(code);
    code[128]=0xc3; // Synthetic continuation: return to the test harness.
    assert(kzsf_health_stub(code,128,(uintptr_t)(code+128)));
    assert(!kzsf_health_stub(code,1,(uintptr_t)(code+128)));
    assert(kzsf_patch_match(kzsf_health_original,sizeof(kzsf_health_original),0,
                           kzsf_health_original,sizeof(kzsf_health_original)));
    assert(!kzsf_patch_match(kzsf_health_original,4,0,kzsf_health_original,sizeof(kzsf_health_original)));
    memcpy(code+256,kzsf_reserve_original,4); code[260]=0xc3;
    memcpy(code+272,kzsf_magazine_original,4); code[276]=0xc3;
    memset(code+288,0x90,4); code[292]=0xc3;
    memset(code+304,0x90,4); code[308]=0xc3;
    DWORD previous;
    assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous));
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    run_health(code,0xeeffa144,10.0f,100.0f);
    run_health(code,0xeeffa144,9999.0f,100.0f); // Lethal damage must not trip the next death check.
    run_health(code,0x12345678,10.0f,90.0f); // Non-player damage unchanged.
    run_health(code,0x12345678,120.0f,-20.0f);
    run_ammo(code+256,24,18); run_ammo(code+272,28,18);
    run_ammo(code+288,24,25); run_ammo(code+304,28,25);
    assert(VirtualFree(code,0,MEM_RELEASE));
    puts("engine player identification, NPCs, mounted users, cycles, legacy hook, ammo and signatures passed");
}
