/* Minimal x86-64 native loader experiment. Not a PS4 emulator or game port. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "runtime.h"
#include "kzsf_timing.h"
#include "kzsf_motion_blur.h"
#include "kzsf_depth_of_field.h"
#include "kzsf_post_effects.h"
#include "cpu_check.h"
#include "isa_rewrite.h"
#include "orb_hook.h"
#include "cheat_hook.h"
#include "actor_hook.h"
#include "damage_hook.h"
#include "kzsf_cheats.h"
#include "tsc_patch.h"
#include "gpu/kzsfgpu.h"
#if !defined(__x86_64__) || !defined(__GNUC__)
#error This prototype requires x86-64 GCC or Clang (including MinGW).
#endif
#ifdef _WIN32
#include <windows.h>
#include "kzsf_postfx_dump_win.h"
#include "kzsf_prompt_trace_win.h"
#include "kzsf_prompt_pc_win.h"
#include "kzsf_free_camera_win.h"
#include "kzsf_camera_observer_win.h"
#include "kzsf_camera_call_bridge.h"
#include "kzsf_character_render_bridge.h"
#include "kzsf_checkpoint_handle_bridge.h"
#include "kzsf_character_draw_proxy.h"
#include "kzsf_character_face_bind.h"
#include "kzsf_character_face_owner_bridge.h"
#include "kzsf_character_bind_pose.h"
#include "kzsf_camera_dispatch_win.h"
#include "kzsf_third_person.h"
#include "kzsf_camera_collision.h"
#include "kzsf_execution_partner.h"
#include "kzsf_model_view_win.h"
#include "kzsf_cheats_win.h"
#include "kzsf_campaign_cheats_win.h"
#include "kzsf_turn_speed_win.h"
#include "kzsf_player_turn_win.h"
#include "kzsf_sprint_turn_win.h"
#include "kzsf_aim_controls_win.h"
#include "kzsf_camera_controls_win.h"
#include "kzsf_custom_vertex_probe.h"
#include <tlhelp32.h>
#else
#include <sys/mman.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#endif

typedef struct { uint64_t address, size, flags; } Segment;
typedef struct { uint64_t target, kind, value, addend; } Reloc;
static char (*names)[128];
static uint64_t import_count;
static unsigned char *image;
#ifdef _WIN32
static unsigned char *kzsf_health_trace_site;
static unsigned kzsf_health_trace_count;
static int kzsf_player_protection;
static int kzsf_unlimited_sprint, kzsf_continuous_scan;
static unsigned kzsf_sprint_reported, kzsf_scan_reported;
static unsigned kzsf_player_protection_reported;
static int kzsf_turn_speed_enabled;
static int kzsf_turn_log;
/* Opt-in diagnostic: dump the per-view post-process object a few times. The
 * breakpoint stays installed for the whole run because the handler also replays
 * the displaced LEA; only the printing is bounded. */
static int kzsf_postfx_dump_enabled;
static unsigned kzsf_postfx_dump_hits;
static int kzsf_prompt_trace_enabled;
static uintptr_t kzsf_prompt_collections[KZSF_PROMPT_TRACE_SITES][4];
static uint64_t kzsf_prompt_requests[KZSF_PROMPT_TRACE_SITES][32];
static int kzsf_prompt_pc_enabled,kzsf_prompt_pc_state;
static int kzsf_prompt_texture_smoke,kzsf_prompt_texture_smoked;
static KzsfPromptPc kzsf_prompt_pc;
static KzsfPromptPc kzsf_prompt_keyboard;
static int kzsf_player_turn_enabled;
static unsigned kzsf_player_turn_events;
static unsigned kzsf_sprint_events;
static _Thread_local KzsfSprintScope kzsf_sprint_scope;
static unsigned kzsf_aim_events[3];
static int kzsf_camera_controls_enabled;
static int kzsf_free_camera_enabled;
static int kzsf_camera_observer_enabled,kzsf_camera_observer_key;
static unsigned kzsf_camera_observer_hits[3],kzsf_camera_observer_samples;
static SRWLOCK kzsf_camera_observer_lock=SRWLOCK_INIT;
static SRWLOCK kzsf_camera_follow_lock=SRWLOCK_INIT;
static int kzsf_free_camera_body_test,kzsf_free_camera_body_active;
static int kzsf_third_person_enabled;
static int kzsf_third_person_hide_body;
static int kzsf_character_body_should_show(void) {
    return __atomic_load_n(&kzsf_free_camera_body_active,__ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&kzsf_third_person_hide_body,__ATOMIC_ACQUIRE) &&
        !__atomic_load_n(&kzsf_character_level_transition,__ATOMIC_ACQUIRE);
}
static KzsfThirdPerson kzsf_third_person;
static KzsfCameraBoom kzsf_third_person_boom;
static int kzsf_third_person_collision_ready;
static unsigned kzsf_third_person_sweeps,kzsf_third_person_hits;
static unsigned kzsf_third_person_aim_traces;
static unsigned kzsf_model_view_events[2];
static unsigned kzsf_third_person_throw_contact_frames;
static KzsfFreeCamera kzsf_free_camera;
static uintptr_t kzsf_free_camera_view;
static ULONGLONG kzsf_free_camera_tick;
static int kzsf_free_camera_toggle_down,kzsf_free_camera_reset_down;
static unsigned kzsf_camera_events[2];
static unsigned kzsf_turn_speed_reported;
/* Gameplay verification for the engine look-rate hooks: per-axis windowed maximum of the
 * engine's own value (before scaling) and of the value actually stored. The local-input
 * store runs once per frame while the stick is deflected, so a walk-versus-sprint
 * comparison shows whether the engine itself lowers the rate while sprinting. */
static volatile uint32_t kzsf_rate_in[2], kzsf_rate_out[2], kzsf_rate_events;
static void kzsf_rate_max(volatile uint32_t *slot,float magnitude) {
    if(!isfinite(magnitude) || magnitude<0.0f) return;
    uint32_t bits,current;
    memcpy(&bits,&magnitude,sizeof(bits));
    current=__atomic_load_n(slot,__ATOMIC_RELAXED);
    for(;;) {
        float have;
        memcpy(&have,&current,sizeof(have));
        if(have>=magnitude) return;
        if(__atomic_compare_exchange_n(slot,&current,bits,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) return;
    }
}
static float kzsf_rate_take(volatile uint32_t *slot) {
    uint32_t bits=__atomic_exchange_n(slot,(uint32_t)0,__ATOMIC_RELAXED);
    float value;
    memcpy(&value,&bits,sizeof(value));
    return value;
}
static void kzsf_rate_log(int pitch,float in,float out,unsigned user) {
    kzsf_rate_max(&kzsf_rate_in[pitch],in);
    kzsf_rate_max(&kzsf_rate_out[pitch],out);
    if(__atomic_add_fetch(&kzsf_rate_events,1,__ATOMIC_RELAXED)&0xff) return;
    fprintf(stderr,"KILLZONE_RATE window: heading engine %.5f stored %.5f | pitch engine %.5f stored %.5f rad/s (user %u)\n",
            (double)kzsf_rate_take(&kzsf_rate_in[0]),(double)kzsf_rate_take(&kzsf_rate_out[0]),
            (double)kzsf_rate_take(&kzsf_rate_in[1]),(double)kzsf_rate_take(&kzsf_rate_out[1]),user);
    fflush(stderr);
}
static int kzsf_turn_write(void *opaque,uintptr_t address,const void *data,size_t size) {
    (void)opaque; SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),(void *)address,data,size,&written) && written==size;
}
static int kzsf_cheat_read(void *opaque,uintptr_t address,void *out,size_t size) {
    (void)opaque; SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),(void *)address,out,size,&got) && got==size;
}
static void kzsf_camera_observer_summary(void) {
    fprintf(stderr,"KILLZONE_CAMERA_OBSERVER summary selector_calls=%u bind_calls=%u view_calls=%u snapshots=%u\n",
        __atomic_load_n(&kzsf_camera_observer_hits[0],__ATOMIC_RELAXED),
        __atomic_load_n(&kzsf_camera_observer_hits[1],__ATOMIC_RELAXED),
        __atomic_load_n(&kzsf_camera_observer_hits[2],__ATOMIC_RELAXED),kzsf_camera_observer_samples);
    fflush(stderr);
}

/* Native construction mirrors generated-texture creator 31a540, with RGBA8888
 * instead of its single-channel font format. Pixel storage uses the port's
 * registered mapper; 2d0db0 receives it as externally owned memory. The engine's
 * private font heap is not necessarily initialized when a prompt is first drawn.
 * Both allocations are retained for this isolated test process's lifetime. */
static uint64_t kzsf_prompt_native_create(void *opaque,const uint32_t *pixels,size_t bytes) {
    (void)opaque;
    typedef void *(ABI *NativeNew)(size_t);
    typedef void (ABI *NativeCtor)(void *);
    typedef uint64_t (ABI *NativeTexture)(void *,uint32_t,uint32_t,const uint32_t *,uint32_t,
        uint32_t,uint16_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t,uint32_t,char);
    extern uint64_t runtime_memory_clamp(uintptr_t,uint64_t);
    uintptr_t base=(uintptr_t)image;
    if(bytes!=KZSF_PC_BYTES) return 0;
    void *data=runtime_memory_map_host_pixels(bytes);
    if(!data || (uintptr_t)data>=(UINT64_C(1)<<40) || ((uintptr_t)data&255) ||
       runtime_memory_clamp((uintptr_t)data,bytes)!=bytes ||
       !kzsf_turn_write(NULL,(uintptr_t)data,pixels,bytes)) {
        runtime_memory_unmap_host_pixels(data,bytes);
        fprintf(stderr,"KILLZONE_PROMPT_PC external pixel allocation unavailable; original prompts retained\n");return 0;
    }
    void *texture=((NativeNew)(base+0x321970))(0xc0);
    if(!texture) return 0;
    ((NativeCtor)(base+0x2cea70))(texture);
    const uint32_t description[3]={KZSF_PC_WIDTH,KZSF_PC_HEIGHT,15}; /* native RGBA8888 */
    uint64_t result=((NativeTexture)(base+0x2d0db0))(texture,0,1,description,0,0,1,0,0,2,
        (uintptr_t)data,256,0,0);
    unsigned char object[0xc0];uint32_t dims[2],pitch;uint64_t srd[4],data_address;
    if(!result || !kzsf_cheat_read(NULL,(uintptr_t)texture,object,sizeof(object))) return 0;
    memcpy(dims,object+0x18,8);memcpy(&pitch,object+0x8c,4);
    memcpy(srd,object+0x90,32);memcpy(&data_address,object+0x30,8);
    unsigned tile=(unsigned)(srd[1]>>52)&31;
    if(dims[0]!=KZSF_PC_WIDTH || dims[1]!=KZSF_PC_HEIGHT || pitch!=KZSF_PC_WIDTH*4 ||
       data_address!=(uintptr_t)data || ((srd[0]&UINT64_C(0x3fffffffff))<<8)!=(uintptr_t)data ||
       ((srd[0]>>52)&63)!=10 || ((srd[0]>>58)&15)!=0 ||
       (srd[1]&0x3fff)!=KZSF_PC_WIDTH-1 || ((srd[1]>>14)&0x3fff)!=KZSF_PC_HEIGHT-1 ||
       (tile!=8 && tile!=31)) {
        fprintf(stderr,"KILLZONE_PROMPT_PC native image refused pitch=%u tile=%u srd=%016llx %016llx %016llx %016llx\n",
            pitch,tile,(unsigned long long)srd[0],(unsigned long long)srd[1],
            (unsigned long long)srd[2],(unsigned long long)srd[3]);return 0;
    }
    fprintf(stderr,"KILLZONE_PROMPT_PC native RGBA atlas created texture=%p data=%p pitch=%u tile=%u\n",
        texture,data,pitch,tile);
    return (uintptr_t)texture;
}
static void kzsf_prompt_constructor_smoke(void) {
    if(!kzsf_prompt_texture_smoke || __atomic_exchange_n(&kzsf_prompt_texture_smoked,1,__ATOMIC_RELAXED)) return;
    uint32_t *empty=calloc(1,KZSF_PC_BYTES);
    uint64_t texture=empty ? kzsf_prompt_native_create(NULL,empty,KZSF_PC_BYTES) : 0;
    free(empty);
    fprintf(stderr,"KILLZONE_PROMPT_PC constructor smoke=%s\n",texture ? "pass" : "refused");
    fflush(stderr);
}
static int handle_kzsf_player_damage(CONTEXT *c) {
    int result=kzsf_handle_player_damage(c,(uintptr_t)image,kzsf_cheat_read,NULL);
    if(result==2) {
        if(!__atomic_exchange_n(&kzsf_player_protection_reported,1,__ATOMIC_RELAXED)) {
            fprintf(stderr,"Killzone: blocked player damage using engine IsPlayer (component=%p)\n",(void *)c->Rdi);
            fflush(stderr);
        }
    }
    return result!=0;
}
static void trace_kzsf_health(const CONTEXT *c) {
    unsigned event=__atomic_fetch_add(&kzsf_health_trace_count,1,__ATOMIC_RELAXED);
    if(event>=64) return;
    unsigned char entry[144]={0}, resource[128]={0};
    uintptr_t resource_address=0; SIZE_T got=0;
    ReadProcessMemory(GetCurrentProcess(),(void *)c->Rax,entry,sizeof(entry),&got);
    ReadProcessMemory(GetCurrentProcess(),(void *)(c->Rbp-0x258),&resource_address,sizeof(resource_address),&got);
    ReadProcessMemory(GetCurrentProcess(),(void *)resource_address,resource,sizeof(resource),&got);
    fprintf(stderr,"HEALTH_TRACE #%u r10=%016llx hp_address=%016llx manager=%016llx resource=%016llx\n",
            event,c->R10,c->Rax,c->R14,(unsigned long long)resource_address);
    float hp,damage; memcpy(&hp,&c->Xmm2,4); memcpy(&damage,&c->Xmm0,4);
    fprintf(stderr,"  values: current=%g incoming=%g r12=%016llx r13=%016llx\n",(double)hp,(double)damage,c->R12,c->R13);
    fprintf(stderr,"  health entry:"); for(unsigned i=0;i<sizeof(entry);++i) fprintf(stderr,"%s%02x",i%16==0?"\n    ":" ",entry[i]);
    fprintf(stderr,"\n  target resource:"); for(unsigned i=0;i<sizeof(resource);++i) fprintf(stderr,"%s%02x",i%16==0?"\n    ":" ",resource[i]);
    fprintf(stderr,"\n  callers:");
    uintptr_t frame=(uintptr_t)c->Rbp;
    for(unsigned depth=0;depth<8 && frame;++depth) {
        uintptr_t words[2];
        if(!ReadProcessMemory(GetCurrentProcess(),(void *)frame,words,sizeof(words),&got) || got!=sizeof(words)) break;
        fprintf(stderr," %016llx",(unsigned long long)words[1]);
        if(words[0]<=frame) break;
        frame=words[0];
    }
    fprintf(stderr,"\n"); fflush(stderr);
}
static const uint32_t virtual_tsc_sites[]={
#include "kzsf_rdtsc_sites.inc"
};
static int virtual_tsc_enabled;
static int virtual_tsc_site(uintptr_t address) {
    if (!virtual_tsc_enabled || address<(uintptr_t)image) return 0;
    uintptr_t offset=address-(uintptr_t)image;
    size_t lo=0,hi=sizeof(virtual_tsc_sites)/sizeof(*virtual_tsc_sites);
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if (virtual_tsc_sites[mid]<offset) lo=mid+1; else hi=mid;
    }
    return lo<sizeof(virtual_tsc_sites)/sizeof(*virtual_tsc_sites) && virtual_tsc_sites[lo]==offset;
}
#endif
static size_t page_size;
typedef struct { uint64_t base, size, init, tls_address, tls_memsz, tls_filesz, tls_module; } LinkedModule;
static LinkedModule modules[16];
static uint64_t module_count;
static char native_module_names[16][128];
static RuntimeNativeExport *native_exports;
static uint64_t native_export_count;
static int entered_game;
static int gpu_enabled;
static unsigned cpu_missing_sets; /* CPU_SET_* the host lacks; the guest code needs them rewritten */
static IsaTable *isa_table; /* rewritten sites (--isa-rewrite); the fault handlers and the report read it */
static void isa_report_hook(void) { isa_rewrite_report(isa_table, stdout); }
int vulkan_smoke(void);
static int trace_imports;
#define TRACE_STUB_CAP 65536
static unsigned char trace_stub_store[128*TRACE_STUB_CAP]; /* in the module: RIP-relative stores fit */
static unsigned char *trace_pool;
volatile uint32_t kzsf_hle_last;
/* Shared call-order ring: the tracer trampoline records the import index in call order,
 * so a stall watchdog can name the most recent HLE calls. */
static volatile uint32_t hle_seq;
static uint32_t hle_ring[4096];
/* Trampoline: record the import index, then jump to the resolved host function.
 * Store-only (no register or stack state touched), so it cannot perturb a call. */
static void build_trace_stub(unsigned char *o, uint32_t index, uintptr_t target) {
#ifdef _WIN32
    /* push rax; push rcx; lea rcx,[hle_ring]; eax=hle_seq++; and eax,4095;
     * mov [rcx+rax*4],index; pop rcx; pop rax; jmp target */
    uintptr_t ring=(uintptr_t)hle_ring, seq=(uintptr_t)&hle_seq;
    int32_t d_lea=(int32_t)(ring-((uintptr_t)o+9));
    int32_t d_seq1=(int32_t)(seq-((uintptr_t)o+15));
    int32_t d_seq2=(int32_t)(seq-((uintptr_t)o+21));
    size_t p=0;
    o[p++]=0x50; o[p++]=0x51;
    o[p++]=0x48; o[p++]=0x8D; o[p++]=0x0D; memcpy(o+p,&d_lea,4); p+=4;
    o[p++]=0x8B; o[p++]=0x05; memcpy(o+p,&d_seq1,4); p+=4;
    o[p++]=0xFF; o[p++]=0x05; memcpy(o+p,&d_seq2,4); p+=4;
    o[p++]=0x25; { uint32_t m=4095; memcpy(o+p,&m,4); p+=4; }
    o[p++]=0xC7; o[p++]=0x04; o[p++]=0x81; memcpy(o+p,&index,4); p+=4;
    /* Per-thread last import for the watchdog/crash dumps: mov dword gs:[trace_off], index+1.
     * Stored value is index+1 so 0 (untouched TlsSlot) reads as "no import traced yet". */
    { uint32_t trace_off=runtime_thread_trace_offset(), plus=index+1;
      o[p++]=0x65; o[p++]=0xC7; o[p++]=0x04; o[p++]=0x25; memcpy(o+p,&trace_off,4); p+=4; memcpy(o+p,&plus,4); p+=4; }
    o[p++]=0x59; o[p++]=0x58;
    int32_t rel=(int32_t)(target-((uintptr_t)o+p+5));
    o[p++]=0xE9; memcpy(o+p,&rel,4); p+=4;
#else
    int32_t disp=(int32_t)((uintptr_t)&kzsf_hle_last-((uintptr_t)o+10));
    o[0]=0xC7; o[1]=0x05; memcpy(o+2,&disp,4); memcpy(o+6,&index,4); /* mov [kzsf_hle_last], index */
    int32_t rel=(int32_t)(target-((uintptr_t)o+15));
    o[10]=0xE9; memcpy(o+11,&rel,4);                                /* jmp target */
#endif
}

static void fail(const char *message) { fprintf(stderr, "ERROR: %s\n", message); exit(1); }
static uint64_t read64(FILE *f) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) fail("truncated boot file");
    uint64_t n = 0;
    for (int i = 7; i >= 0; --i) n = (n << 8) | b[i];
    return n;
}
static size_t round_page(size_t size) { return (size + page_size - 1) & ~(page_size - 1); }
static void *allocate(size_t size) {
    void *low=runtime_low_map(size,PROT_READ|PROT_WRITE);
    if (low) return low;
#ifdef _WIN32
    void *p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) fail("VirtualAlloc failed");
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) fail("mmap failed");
#endif
    return p;
}
static void protect(void *p, size_t size, unsigned flags) {
#ifdef _WIN32
    DWORD old, mode = PAGE_NOACCESS;
    if (flags & 1) mode = (flags & 2) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    else if (flags & 2) mode = PAGE_READWRITE;
    else if (flags & 4) mode = PAGE_READONLY;
    if (!VirtualProtect(p, size, mode, &old)) fail("VirtualProtect failed");
    FlushInstructionCache(GetCurrentProcess(), p, size);
#else
    int mode = ((flags & 4) ? PROT_READ : 0) | ((flags & 2) ? PROT_WRITE : 0) | ((flags & 1) ? PROT_EXEC : 0);
    if (mprotect(p, size, mode)) fail("mprotect failed");
#endif
}
static ABI __attribute__((noreturn)) void unresolved(uint32_t id, uintptr_t argument) {
    if (id >= import_count) fail("bad import trap index");
    /* One atomic write: guest threads print concurrently and would interleave it. */
    char stop_line[256];
    int stop_len=snprintf(stop_line,sizeof(stop_line),"STOP: first unsupported PS4 import: %s (index %u) API: %s\n",
                          names[id],id,runtime_import_name(names[id]));
    if (stop_len>0) fwrite(stop_line,1,(size_t)stop_len,stderr);
    fflush(NULL);
    printf("Caller return offset: 0x%" PRIxPTR "; first argument: 0x%" PRIxPTR "\n",
           (uintptr_t)__builtin_return_address(0) - (uintptr_t)image, argument);
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    for (uint64_t m=0;m<module_count;++m)
        if (caller>=modules[m].base && caller-modules[m].base<modules[m].size)
            printf("Caller in linked module %" PRIu64 " (%s): +0x%" PRIxPTR "\n",m,m==0 ? "libc.prx" : "system module",caller-modules[m].base);
    runtime_report();
    puts(entered_game ? "Original guest entry instructions executed; game initialization is incomplete." :
                        "Native libc initialization is incomplete; game entry has not run.");
    fflush(NULL);
    _exit(20); /* no destructors: GPU, audio and guest threads are still running */
}
#ifndef _WIN32
/* enter_on_stack(entry, arg0, arg1, stack_top): call entry(arg0,arg1) on a new stack. */
void enter_on_stack(void *entry, void *arg0, void *arg1, void *top);
__asm__(".text\n.globl enter_on_stack\nenter_on_stack:\n"
        " push %rbp\n mov %rsp,%rbp\n and $-16,%rcx\n mov %rcx,%rsp\n"
        " mov %rdi,%rax\n mov %rsi,%rdi\n mov %rdx,%rsi\n call *%rax\n"
        " mov %rbp,%rsp\n pop %rbp\n ret\n");
#endif
static ABI void guest_exit(void) { puts("Runtime: process finalizer callback reached"); }
#ifdef _WIN32
static void describe_address(char *out, size_t size, uintptr_t address) {
    HMODULE module=NULL;
    char name[MAX_PATH]="?";
    int site=isa_table ? isa_rewrite_site_of_stub(isa_table,address) : -1;
    if (site>=0 || (image && address-(uintptr_t)image<0x20000000)) {
        int n=snprintf(out,size,"guest offset 0x%" PRIxPTR,address-(uintptr_t)image);
        if (site>=0 && n>=0 && (size_t)n<size)
            snprintf(out+n,size-n," While emulating site 0x%" PRIx64 " (%s)",isa_table->sites[site].offset,isa_kind_name(isa_table->sites[site].kind));
        return;
    }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)address,&module) && module) {
        GetModuleFileNameA(module,name,sizeof(name));
        const char *base=strrchr(name,'\\');
        snprintf(out,size,"%s+0x%" PRIxPTR,base ? base+1 : name,address-(uintptr_t)module);
        return;
    }
    snprintf(out,size,"%p",(void *)address);
}
static int fatal_reported;
static int kzsf_fault_read(uintptr_t address,void *out,size_t bytes) {
    SIZE_T got=0;
    return address>=0x10000 && bytes<=4096 && address<=UINTPTR_MAX-bytes &&
        ReadProcessMemory(GetCurrentProcess(),(const void*)address,out,bytes,&got) && got==bytes;
}
/* Targeted evidence for the reproduced swapped-model animation crash.
 * Only bounded reads after a fatal exception; never suppress/retry the fault. */
static void dump_kzsf_animation_fault(const CONTEXT *c) {
    if(!image || c->Rip-(uintptr_t)image!=0x308490)return;
    fprintf(stderr,"KILLZONE_ANIMATION_FAULT r8=%016llx r9=%016llx r10=%016llx r11=%016llx r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
        c->R8,c->R9,c->R10,c->R11,c->R12,c->R13,c->R14,c->R15);
    uintptr_t frame_pointer=c->Rbp;
    for(unsigned depth=0;depth<6;depth++) {
        uintptr_t frame[2];
        if(!kzsf_fault_read(frame_pointer,frame,sizeof(frame)) || frame[0]<=frame_pointer ||
           frame[0]-frame_pointer>0x100000)break;
        if(frame[1]==(uintptr_t)image+0x5383e6) {
            uintptr_t request=0,graph=0;unsigned char request_bytes[32];uint64_t header[16];
            if(kzsf_fault_read(frame[0]+0x10,&request,8) && kzsf_fault_read(request,request_bytes,sizeof(request_bytes))) {
                memcpy(&graph,request_bytes,8);
                fprintf(stderr,"KILLZONE_ANIMATION_REQUEST caller_frame=%p request=%p graph=%p bytes=",
                    (void*)frame[0],(void*)request,(void*)graph);
                for(unsigned i=0;i<sizeof(request_bytes);i++)fprintf(stderr,"%02x",request_bytes[i]);
                fputc('\n',stderr);
                if(kzsf_fault_read(graph,header,sizeof(header))) {
                    fputs("KILLZONE_ANIMATION_GRAPH",stderr);
                    for(unsigned i=0;i<16;i++)fprintf(stderr," +%02x=%016llx",i*8,(unsigned long long)header[i]);
                    fputc('\n',stderr);
                    /* Record index/layout follows exact instructions53838a..5383d7. */
                    uint32_t offset=0;memcpy(&offset,request_bytes+0x10,4);
                    uint64_t index=(uint64_t)request_bytes[0xc]+offset;
                    uintptr_t descriptors=(uintptr_t)header[5]&~(uintptr_t)15;
                    unsigned char descriptor[96];
                    if(index<4096 && descriptors<=UINTPTR_MAX-index*96 &&
                       kzsf_fault_read(descriptors+index*96,descriptor,sizeof(descriptor))) {
                        fprintf(stderr,"KILLZONE_ANIMATION_DESCRIPTOR index=%llu bytes=",(unsigned long long)index);
                        for(unsigned i=0;i<sizeof(descriptor);i++)fprintf(stderr,"%02x",descriptor[i]);
                        fputc('\n',stderr);
                    }
                }
            }
            break;
        }
        frame_pointer=frame[0];
    }
    fflush(stderr);
}
/* KZSF 01.81: the pool assertion compares a type-gated node value, not
 * necessarily a GPU fence. Use the fault registers, avoiding a guessed global chain. */
static void dump_kzsf_pool(const CONTEXT *c) {
    static const unsigned char signature[]={0x49,0x23,0x47,0x08,0x4c,0x39,0xe0,0x73,0x09,0xcd,0x41};
    if (!image || c->Rip-(uintptr_t)image!=0x2e42da ||
        memcmp(image+0x2e42d1,signature,sizeof(signature))) return;
    unsigned char node[0x40]; SIZE_T got=0;
    fprintf(stderr,"KZSF pool: manager=%016llx node=%016llx required=%llu compared=%llu\n",
            c->R14,c->R15,c->R12,c->Rax);
    if (!ReadProcessMemory(GetCurrentProcess(),(void *)(uintptr_t)c->R15,node,sizeof(node),&got) || got!=sizeof(node)) {
        fprintf(stderr,"KZSF pool: node unreadable\n"); return;
    }
    uint64_t raw, start, prev, next; uint32_t lock;
    memcpy(&raw,node+8,8); memcpy(&start,node+0x10,8);
    memcpy(&prev,node+0x18,8); memcpy(&next,node+0x20,8); memcpy(&lock,node+0x30,4);
    fprintf(stderr,"KZSF pool: type=%u raw=%016" PRIx64 " low60=%" PRIu64
            " start=%016" PRIx64 " prev=%016" PRIx64 " next=%016" PRIx64 " lock=%u\n",
            node[2]&3,raw,raw&UINT64_C(0x0fffffffffffffff),start,prev,next,lock);
    if (gpu_enabled) kzsfgpu_dump_write_range(c->R15,sizeof(node));
}
static LONG fatal_exception(EXCEPTION_POINTERS *info) {
    if (__atomic_exchange_n(&fatal_reported,1,__ATOMIC_ACQ_REL)) { Sleep(INFINITE); }
    const EXCEPTION_RECORD *record=info->ExceptionRecord;
    const CONTEXT *c=info->ContextRecord;
    char where[MAX_PATH+64];
    describe_address(where,sizeof(where),(uintptr_t)c->Rip);
    if (record->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && record->NumberParameters>=2)
        fprintf(stderr,"Fault: %s access to %p at %s (thread %lu)\n",
                record->ExceptionInformation[0]==1 ? "write" : record->ExceptionInformation[0]==8 ? "execute" : "read",
                (void *)record->ExceptionInformation[1],where,GetCurrentThreadId());
    else
        fprintf(stderr,"Fault: exception 0x%08lx at %s (thread %lu)\n",record->ExceptionCode,where,GetCurrentThreadId());
    fprintf(stderr,"  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n",
            c->Rax,c->Rbx,c->Rcx,c->Rdx,c->Rsi,c->Rdi,c->Rbp,c->Rsp);
#ifdef _WIN32
    uint32_t hle_last=runtime_thread_trace_get();
#else
    uint32_t hle_last=kzsf_hle_last;
#endif
    if (trace_imports && hle_last && hle_last-1 < import_count)
        fprintf(stderr,"  last HLE import: %s (index %u)\n",names[hle_last-1],hle_last-1);
    if (trace_imports && hle_seq) {
        fprintf(stderr,"  recent HLE calls (oldest first):\n");
        uint32_t total=hle_seq, first=total>4095 ? total-4095 : 0; unsigned shown=0;
        for (uint32_t s=first;s<total && shown<40;++s) {
            uint32_t index=hle_ring[s&4095];
            if (index>=import_count) continue;
            if (!strncmp(names[index],"T72hz6ffq08",11)) continue; /* yield */
            fprintf(stderr,"    %s\n",names[index]); ++shown;
        }
    }
    dump_kzsf_pool(c);
    dump_kzsf_animation_fault(c);
    if (gpu_enabled) kzsfgpu_dump_guest_writes(info);
    /* rbp chain (guest and host code keep frame pointers). */
    uintptr_t rbp=(uintptr_t)c->Rbp;
    for (int depth=0; depth<24 && rbp; ++depth) {
        uintptr_t frame[2];
        SIZE_T got=0;
        if (!ReadProcessMemory(GetCurrentProcess(),(void *)rbp,frame,sizeof(frame),&got) || got!=sizeof(frame) || frame[0]<=rbp) break;
        describe_address(where,sizeof(where),frame[1]);
        fprintf(stderr,"  #%d %s\n",depth,where);
        rbp=frame[0];
    }
    if (isa_table) isa_rewrite_report(isa_table,stderr);
    fflush(NULL);
    TerminateProcess(GetCurrentProcess(),3);
    return EXCEPTION_CONTINUE_SEARCH;
}
/* CONTEXT <-> IsaRegs (gpr in x86 numbering). */
static void isa_context_regs(CONTEXT *c, IsaRegs *regs, int to_context) {
    DWORD64 *g[16]={&c->Rax,&c->Rcx,&c->Rdx,&c->Rbx,&c->Rsp,&c->Rbp,&c->Rsi,&c->Rdi,
                    &c->R8,&c->R9,&c->R10,&c->R11,&c->R12,&c->R13,&c->R14,&c->R15};
    for (int i=0;i<16;++i) { if (to_context) *g[i]=regs->gpr[i]; else regs->gpr[i]=*g[i]; }
    if (to_context) c->EFlags=(DWORD)regs->rflags; else regs->rflags=c->EFlags;
}
static int kzsf_free_key(int key) {return (GetAsyncKeyState(key)&0x8000)!=0;}
static void kzsf_free_input(int active) {
    runtime_pad_free_camera(active);kzsfgpu_free_camera_input(active);
}
static const float *kzsf_free_camera_update(CONTEXT *c,int combined) {
    ULONGLONG now=GetTickCount64();
    float seconds=kzsf_free_camera_elapsed(kzsf_free_camera_tick,now);
    int clock_reset=kzsf_free_camera_tick && now<kzsf_free_camera_tick;
    kzsf_free_camera_tick=now;
    if(kzsf_free_camera_view!=c->Rbx || clock_reset) {
        kzsf_free_camera.active=0;kzsf_free_input(0);
        kzsf_free_camera_view=c->Rbx;
    }
    DWORD foreground_pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    int allowed=foreground_pid==GetCurrentProcessId() && !kzsfgpu_overlay_captures_input();
    int toggle=kzsf_free_key(combined ? VK_F9 : VK_F8),reset=kzsf_free_key(combined ? VK_F10 : VK_F7);
    int camera_request=combined ? kzsfgpu_camera_request(1) : -1;
    if((allowed && toggle && !kzsf_free_camera_toggle_down) ||
       (camera_request>=0 && camera_request!=kzsf_free_camera.active)) {
        if(kzsf_free_camera.active)kzsf_free_camera.active=0;
        else {
            float native[16];
            if(c->Rbx<=UINTPTR_MAX-0x50 && kzsf_cheat_read(NULL,c->Rbx+0x10,native,64)) {
                kzsf_free_camera_start(&kzsf_free_camera,native);
                fprintf(stderr,"KILLZONE_FREE_NATIVE right=%.4f,%.4f,%.4f forward=%.4f,%.4f,%.4f up=%.4f,%.4f,%.4f pitch=%.4f\n",
                    native[0],native[1],native[2],native[4],native[5],native[6],native[8],native[9],native[10],kzsf_free_camera.pitch);
            }
        }
        kzsf_free_input(kzsf_free_camera.active);
        fprintf(stderr,"KILLZONE_FREE_CAMERA active=%d view=%p\n",kzsf_free_camera.active,(void*)c->Rbx);
        fflush(stderr);
    }
    if(allowed && reset && !kzsf_free_camera_reset_down && kzsf_free_camera.active) {
        float native[16];
        if(c->Rbx<=UINTPTR_MAX-0x50 && kzsf_cheat_read(NULL,c->Rbx+0x10,native,64))
            kzsf_free_camera_start(&kzsf_free_camera,native);
    }
    kzsf_free_camera_toggle_down=toggle;kzsf_free_camera_reset_down=reset;
    if(combined)kzsfgpu_camera_publish(1,kzsf_free_camera.active);
    if(!kzsf_free_camera.active)return NULL;
    float mouse_yaw=0,mouse_pitch=0;
    kzsfgpu_free_camera_mouse(&mouse_yaw,&mouse_pitch); // consume once, including on focus loss
    if(allowed) {
        float stick_x=0,stick_y=0,move_x=0,move_y=0,shoulder_forward=0;
        runtime_pad_free_camera_axes(&move_x,&move_y,&stick_x,&stick_y,&shoulder_forward);
        float stick_gain=kzsfgpu_controller_aim_gain(0)/100.f;
        /* F7 reserves arrow keys for the model selector while held. */
        int arrow_look=!kzsf_free_key(VK_F7);
        KzsfFreeCameraInput input={
            .right=kzsf_free_key('D')-kzsf_free_key('A')+move_x,
            .up=combined ? kzsf_free_key('E')-kzsf_free_key('Q')+kzsf_free_key(VK_PRIOR)-kzsf_free_key(VK_NEXT)+shoulder_forward :
                kzsf_free_key('W')-kzsf_free_key('S')+kzsf_free_key(VK_PRIOR)-kzsf_free_key(VK_NEXT)-move_y,
            .forward=combined ? kzsf_free_key('W')-kzsf_free_key('S')-move_y : kzsf_free_key('E')-kzsf_free_key('Q')+shoulder_forward,
            .yaw=arrow_look*(kzsf_free_key(VK_RIGHT)-kzsf_free_key(VK_LEFT))*1.5f+stick_x*2.5f*stick_gain,
            .pitch=arrow_look*(kzsf_free_key(VK_UP)-kzsf_free_key(VK_DOWN))*1.5f-stick_y*2.5f*stick_gain,
            .speed=kzsf_free_key(VK_SHIFT) ? 12.f : kzsf_free_key(VK_CONTROL) ? .5f : 3.f};
        float pitch_before=kzsf_free_camera.pitch;
        if(!kzsf_free_camera_step(&kzsf_free_camera,input,seconds) ||
           !kzsf_free_camera_turn(&kzsf_free_camera,mouse_yaw,mouse_pitch)) {
            kzsf_free_camera.active=0;kzsf_free_input(0);return NULL;
        }
        // Record both directions, including time spent at a pole, without
        // flooding the render-thread log. Needed to diagnose input vs pose.
        static ULONGLONG last_pitch_trace;
        static unsigned pitch_trace_count;
        if((mouse_pitch!=0 || input.pitch!=0) && now-last_pitch_trace>=500 && pitch_trace_count<240) {
            last_pitch_trace=now;++pitch_trace_count;
            fprintf(stderr,"KILLZONE_FREE_PITCH mouse=%.5f stick_y=%.4f rate=%.4f dt=%.4f before=%.4f after=%.4f forward_z=%.4f\n",
                mouse_pitch,stick_y,input.pitch,seconds,pitch_before,kzsf_free_camera.pitch,kzsf_free_camera.matrix[6]);
            fflush(stderr);
        }
        static unsigned look_sources;
        unsigned sources=(mouse_yaw || mouse_pitch ? 1u : 0u) | (stick_x || stick_y ? 2u : 0u) |
            (!kzsf_free_key(VK_F7) && (kzsf_free_key(VK_LEFT) || kzsf_free_key(VK_RIGHT) || kzsf_free_key(VK_UP) || kzsf_free_key(VK_DOWN)) ? 4u : 0u);
        if(sources&~look_sources) {
            look_sources|=sources;
            fprintf(stderr,"KILLZONE_FREE_LOOK sources=%u mouse=%.4f,%.4f stick=%.3f,%.3f eye=%.3f,%.3f,%.3f\n",
                sources,mouse_yaw,mouse_pitch,stick_x,stick_y,kzsf_free_camera.matrix[12],
                kzsf_free_camera.matrix[13],kzsf_free_camera.matrix[14]);fflush(stderr);
        }
    }
    return kzsf_free_camera.matrix;
}
static void kzsf_camera_observe_view(const CONTEXT *c,unsigned hit,int direct_call,uintptr_t original) {
    /* Falling/zipline camera states fail the normal follow-state gate below.
     * Maintain an already-selected CJ on the engine callback regardless of
     * camera state. Revalidate both actor type and exact owned model first;
     * never use a stale owner or create/select a model from traversal. */
    if(direct_call && kzsf_cj_part &&
       !__atomic_load_n(&kzsf_character_level_transition,__ATOMIC_ACQUIRE) &&
       __atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE)==KZSF_CHARACTER_MODEL_CJ &&
       __atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE)==kzsf_cj_owner &&
       __atomic_load_n(&kzsf_character_render_part,__ATOMIC_ACQUIRE)==kzsf_cj_part &&
       kzsf_observer_baseline_type(kzsf_cj_owner,(uintptr_t)image,kzsf_cheat_read,NULL)==(uintptr_t)image+0x3ca98b0) {
        uintptr_t owned_model=0;
        if(kzsf_zoom_read(kzsf_cheat_read,NULL,kzsf_cj_owner,0x200,&owned_model,8) && owned_model==kzsf_cj_model)
            kzsf_observer_body_visibility(stderr,kzsf_cj_owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek());
    }
    DWORD foreground_pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    int key=foreground_pid==GetCurrentProcessId() &&
        !kzsfgpu_overlay_captures_input() && kzsf_free_key(VK_F6);
    if((hit==1 || (key && !kzsf_camera_observer_key)) &&
       kzsf_camera_observer_samples<KZSF_CAMERA_OBSERVER_LIMIT) {
        fprintf(stderr,"KILLZONE_CAMERA_OBSERVER snapshot=%u tick=%llu reason=%s selector_calls=%u bind_calls=%u view_calls=%u\n",
            ++kzsf_camera_observer_samples,(unsigned long long)GetTickCount64(),hit==1 ? "first-view" : "F6",
            __atomic_load_n(&kzsf_camera_observer_hits[0],__ATOMIC_RELAXED),
            __atomic_load_n(&kzsf_camera_observer_hits[1],__ATOMIC_RELAXED),hit);
        fprintf(stderr,"  camera_execution context=%s original_rva=0x%llx physics_calls=%u pose_override=%d\n",
            direct_call ? "direct-call" : "VEH",
            (unsigned long long)(original>=(uintptr_t)image ? original-(uintptr_t)image : 0),
            kzsf_third_person_sweeps,
            kzsf_third_person_enabled && __atomic_load_n(&kzsf_free_camera_body_active,__ATOMIC_ACQUIRE));
        kzsf_observer_snapshot(stderr,c,(uintptr_t)image,kzsf_cheat_read,NULL);
        KzsfCameraEngineContext engine;
        uintptr_t tcb=runtime_thread_tcb_peek();
        if(kzsf_camera_engine_context(tcb,(uintptr_t)image,kzsf_cheat_read,NULL,&engine))
            fprintf(stderr,"  camera_engine_context status=sampled tid=%lu tcb=%p router=%p heap=%p allocate_rva=0x%llx atomic=0 read_only=1\n",
                (unsigned long)GetCurrentThreadId(),(void*)engine.tcb,(void*)engine.router,
                (void*)engine.heap,(unsigned long long)(engine.allocate_method-(uintptr_t)image));
        else fprintf(stderr,"  camera_engine_context status=unknown tid=%lu tcb=%p read_only=1\n",
                     (unsigned long)GetCurrentThreadId(),(void*)tcb);
        fputs("KILLZONE_CAMERA_OBSERVER end\n",stderr);fflush(stderr);
    }
    kzsf_camera_observer_key=key;
    static int character_swap_test=-1,character_swap_key,character_selector_test;
    if(character_swap_test<0) {
        const char *value=getenv("KZSF_CHARACTER_SWAP_TEST");
        character_swap_test=value && !strcmp(value,"1");
        value=getenv("KZSF_CHARACTER_SELECTOR_TEST");
        character_selector_test=value && !strcmp(value,"1");
    }
    if(character_swap_test && direct_call && !__atomic_load_n(&kzsf_character_level_transition,__ATOMIC_ACQUIRE)) {
        int input_active=foreground_pid==GetCurrentProcessId() && !kzsfgpu_overlay_captures_input();
        int pressed=input_active && kzsf_free_key(VK_F7);
        const char *overlay_keys_mode=getenv("KZSF_CHARACTER_OVERLAY_TEST");
        int overlay_keys=overlay_keys_mode && !strcmp(overlay_keys_mode,"1");
        static KzsfCharacterModelKeys model_keys;
        int key_action=overlay_keys ? kzsf_character_model_keys(&model_keys,input_active,pressed,
            kzsf_free_key(VK_LEFT),kzsf_free_key(VK_RIGHT)) : (pressed && !character_swap_key ? 2 : 0);
        if(key_action==1 || key_action==-1)kzsfgpu_character_cycle(key_action);
        if(key_action==2) {
            KzsfFollowBaseline baseline;const char *why=NULL;
            if(kzsf_follow_baseline_read(c->R14,c->Rbx,(uintptr_t)image,kzsf_cheat_read,NULL,
                    kzsf_observer_baseline_type,&baseline,&why) &&
               kzsf_follow_model_state(kzsf_observer_baseline_type(baseline.state,(uintptr_t)image,kzsf_cheat_read,NULL),(uintptr_t)image)) {
                const char *bundle_mode=getenv("KZSF_CHARACTER_BUNDLE_TEST");
                if(overlay_keys && bundle_mode && !strcmp(bundle_mode,"1")) {
                    int active=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
                    if(__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE)!=baseline.owner)active=0;
                    kzsfgpu_character_select(active ? 0 : kzsf_character_last_model);
                } else if(character_selector_test)
                    kzsf_observer_character_cycle(stderr,baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),-1);
                else kzsf_observer_commando_toggle(stderr,baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek());
            }
            else {fprintf(stderr,"KILLZONE_CHARACTER_SWAP status=refused reason=normal-gameplay-required baseline_reason=%s freecam=%d view=%p render=%p\n",
                why ? why : "model-state",kzsf_free_camera.active,(void*)c->R14,(void*)c->Rbx);fflush(stderr);}
        }
        character_swap_key=pressed;
        static int overlay_selector=-1;
        static uint64_t overlay_pending,overlay_reply;
        static int overlay_status,overlay_last_kind;
        static unsigned level_epoch;
        static uintptr_t overlay_discovery_owner;
        static ULONGLONG bundle_started,bundle_polled;
        static int bundle_result;
        if(level_epoch!=kzsf_character_level_epoch) {
            level_epoch=kzsf_character_level_epoch;overlay_pending=overlay_reply=0;
            overlay_status=overlay_last_kind=0;overlay_discovery_owner=0;bundle_started=bundle_polled=0;bundle_result=0;
            memset(&model_keys,0,sizeof(model_keys));character_swap_key=0;
        }
        if(overlay_selector<0) {
            const char *value=getenv("KZSF_CHARACTER_OVERLAY_TEST");
            overlay_selector=value && !strcmp(value,"1");
        }
        if(overlay_selector) {
            uint64_t request=kzsfgpu_character_request();
            if(request){overlay_pending=request;overlay_reply=request;overlay_status=2;bundle_started=GetTickCount64();bundle_polled=0;bundle_result=0;}
            KzsfFollowBaseline b;const char *reason=NULL;
            int baseline_ready=kzsf_follow_baseline_read(c->R14,c->Rbx,(uintptr_t)image,kzsf_cheat_read,NULL,
                kzsf_observer_baseline_type,&b,&reason);
            int ready=baseline_ready &&
                kzsf_follow_model_state(kzsf_observer_baseline_type(b.state,(uintptr_t)image,kzsf_cheat_read,NULL),(uintptr_t)image);
            int kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
            uint64_t available=kzsf_character_available_models;
            if(!ready) {
                static uint64_t reported_request;static const char *reported_reason;static unsigned blocked_reports;
                const char *blocked=baseline_ready ? "model-state" : reason ? reason : "unknown";
                if(overlay_pending && blocked_reports<64 &&
                   (reported_request!=overlay_pending || !reported_reason || strcmp(reported_reason,blocked))) {
                    reported_request=overlay_pending;reported_reason=blocked;++blocked_reports;
                    fprintf(stderr,"KILLZONE_CHARACTER_REQUEST status=blocked request=%llu reason=%s freecam=%d view=%p render=%p owner=%p state_rva=0x%llx\n",
                        (unsigned long long)overlay_pending,blocked,kzsf_free_camera.active,(void*)c->R14,(void*)c->Rbx,
                        (void*)(baseline_ready ? b.owner : 0),
                        (unsigned long long)(baseline_ready ? kzsf_observer_baseline_type(b.state,(uintptr_t)image,kzsf_cheat_read,NULL)-(uintptr_t)image : 0));
                    fflush(stderr);
                }
                kzsfgpu_character_publish(overlay_reply,kind,1,available);
            } else {
                kzsf_custom_model_tick(b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type,-1);
                kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
                if(__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE)!=b.owner)kind=0;
                if(overlay_pending) {
                    int wanted=(int)(uint32_t)overlay_pending-1;
                    /* The default actor can already wear a listed outfit
                     * (e.g. Chapter3 SpaceSuit). Selecting it restores default
                     * instead of searching for a second copy of its body. */
                    if(wanted>0) {
                        KzsfCharacterInventory current_player;char default_name[128];
                        if(kzsf_character_inventory_read(b.owner,kzsf_cheat_read,NULL,&current_player) &&
                           kzsf_character_resource_name(current_player.resource,(uintptr_t)image,kzsf_cheat_read,NULL,default_name) &&
                           !strcmp(default_name,kzsf_character_model_name(wanted)))wanted=0;
                    }
                    const char *bundle_mode=getenv("KZSF_CHARACTER_BUNDLE_TEST");
                    int loading=0;
                    if(wanted>0 && wanted!=KZSF_CHARACTER_MODEL_CJ && bundle_mode && !strcmp(bundle_mode,"1")) {
                        ULONGLONG now=GetTickCount64();
                        if(!bundle_polled || now-bundle_polled>=250) {
                            KzsfCameraEngineContext context;const char *why="bundle-engine-context";
                            bundle_polled=now;bundle_result=-1;
                            if(kzsf_camera_engine_context(runtime_thread_tcb_peek(),(uintptr_t)image,kzsf_cheat_read,NULL,&context) &&
                               context.allocate_method==(uintptr_t)image+0x4f67a0) {
                                uintptr_t base=(uintptr_t)image;
                                KzsfCharacterBundleApi api=kzsf_character_bundle_api(base);
                                bundle_result=kzsf_character_bundle_request(wanted,base,kzsf_cheat_read,NULL,api,&kzsf_character_bundle_leases[wanted],&why);
                            }
                            if(bundle_result!=0) {
                                fprintf(stderr,"KILLZONE_CHARACTER_BUNDLE model=%s result=%d reason=%s\n",kzsf_character_model_name(wanted),bundle_result,why);fflush(stderr);
                            }
                        }
                        loading=bundle_result!=1;
                        if(bundle_result<0 || now-bundle_started>30000) {
                            fprintf(stderr,"KILLZONE_CHARACTER_BUNDLE status=refused model=%s reason=%s\n",kzsf_character_model_name(wanted),bundle_result<0 ? "native-loader-refused" : "load-timeout");fflush(stderr);
                            overlay_pending=0;overlay_status=4;
                        }
                    }
                    if(!overlay_pending || loading) { /* Wait for native resource initialization before touching the player. */ }
                    else if(kind==wanted){overlay_pending=0;overlay_status=0;}
                    else if(kind==KZSF_CHARACTER_MODEL_CJ || wanted==KZSF_CHARACTER_MODEL_CJ) {
                        if(kind==KZSF_CHARACTER_MODEL_CJ) {
                            kzsf_custom_model_tick(b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type,0);
                        } else if(kind!=0) {
                            /* Restore a native selection before loading CJ against the player's rig. */
                            if(!kzsf_cj_available()){overlay_pending=0;overlay_status=3;}
                            else kzsf_observer_character_cycle(stderr,b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),0);
                        } else {
                            kzsf_custom_model_tick(b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type,1);
                        }
                        int previous=kind;
                        kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
                        if(kind==wanted){overlay_pending=0;overlay_status=(kind==KZSF_CHARACTER_MODEL_CJ && !kzsf_cj_expires) ? 4 : 0;if(kind && !overlay_status)kzsf_character_last_model=kind;}
                        else if(previous!=0 && kind==0)overlay_status=2;
                        else {overlay_pending=0;overlay_status=4;}
                    }
                    else {
                        int restoring_first=kind!=0;
                        /* Validate the requested named appearance while the
                         * old one is still active. A missing/rejected choice
                         * must not strand the player on the default body. */
                        if(restoring_first && wanted>0) {
                            kzsf_observer_character_cycle(stderr,b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,
                                runtime_thread_tcb_peek(),-2);
                            available=kzsf_character_available_owner==b.owner ? kzsf_character_available_models : 1;
                            if(!(available&(UINT64_C(1)<<wanted))) {
                                fprintf(stderr,"KILLZONE_CHARACTER_SWAP status=refused reason=requested-model-unavailable retained_model=%s\n",kzsf_character_model_name(kind));fflush(stderr);
                                overlay_pending=0;overlay_status=3;
                            }
                        }
                        if(overlay_pending)kzsf_observer_character_cycle(stderr,b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,
                            runtime_thread_tcb_peek(),restoring_first ? 0 : wanted);
                        kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
                        if(!overlay_pending) { /* Preflight refusal preserves active appearance. */ }
                        else if(kind==wanted){overlay_pending=0;overlay_status=0;}
                        else if(restoring_first && kind==0)overlay_status=2;
                        else {
                            available=kzsf_character_available_owner==b.owner ? kzsf_character_available_models : 1;
                            overlay_status=(available&(UINT64_C(1)<<wanted)) ? 4 : 3;overlay_pending=0;
                        }
                    }
                } else if(overlay_discovery_owner!=b.owner) {
                    /* Full native discovery/rig validation is expensive. Run
                     * once per actor/level, then revalidate on explicit model
                     * requests above; never audit all models on an idle timer. */
                    overlay_discovery_owner=b.owner;
                    kzsf_observer_character_cycle(stderr,b.owner,(uintptr_t)image,kzsf_cheat_read,NULL,
                        runtime_thread_tcb_peek(),-2);
                }
                if(!overlay_pending && kind!=overlay_last_kind)overlay_status=0;
                overlay_last_kind=kind;
                available=kzsf_character_available_owner==b.owner ? kzsf_character_available_models : 1;
                if(kzsf_cj_available())available|=UINT64_C(1)<<KZSF_CHARACTER_MODEL_CJ;
                kzsfgpu_character_publish(overlay_reply,kind,overlay_status,available);
            }
        }
        static unsigned visibility_tick;
        /* These shared-body facial variants can expose the native default
         * head when traversal re-enables a base-body slot. Check each normal
         * camera callback rather than waiting for periodic maintenance. */
        int visibility_kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
        if(++visibility_tick%30==0 || visibility_kind>=5 ||
           __atomic_load_n(&kzsf_free_camera_body_active,__ATOMIC_ACQUIRE)) {
            KzsfFollowBaseline visibility_baseline;const char *visibility_reason=NULL;
            if(kzsf_follow_baseline_read(c->R14,c->Rbx,(uintptr_t)image,kzsf_cheat_read,NULL,
                    kzsf_observer_baseline_type,&visibility_baseline,&visibility_reason) &&
                    kzsf_follow_model_state(kzsf_observer_baseline_type(visibility_baseline.state,(uintptr_t)image,kzsf_cheat_read,NULL),(uintptr_t)image))
                kzsf_observer_body_visibility(stderr,visibility_baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek());
        }
    }
    /* One resource capture after stable gameplay ownership. The explicit
     * diagnostic may call the native loaded-object query, never a setter;
     * allocator TLS/code checks apply and it never runs in the VEH path. */
    static int custom_vertex_probe_enabled=-1;
    if(custom_vertex_probe_enabled<0) {
        const char *path=getenv("KZSF_CUSTOM_VERTEX_PROBE_FILE");
        custom_vertex_probe_enabled=path && *path;
    }
    if(custom_vertex_probe_enabled && direct_call) {
        KzsfFollowBaseline baseline;const char *why=NULL;
        if(kzsf_follow_baseline_read(c->R14,c->Rbx,(uintptr_t)image,kzsf_cheat_read,NULL,
                kzsf_observer_baseline_type,&baseline,&why) &&
           kzsf_follow_model_state(kzsf_observer_baseline_type(baseline.state,(uintptr_t)image,kzsf_cheat_read,NULL),(uintptr_t)image)) {
            kzsf_custom_vertex_probe(baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type);
            kzsf_custom_model_tick(baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type,-2);
        }
    }
    static int resource_test=-1;
    static uintptr_t resource_owner;
    static unsigned resource_stable,resource_attempts;
    if(resource_test<0) {
        const char *value=getenv("KZSF_CHARACTER_RESOURCE_TEST");
        resource_test=value && !strcmp(value,"1");
    }
    if(resource_test && direct_call && resource_attempts<3) {
        KzsfFollowBaseline baseline;const char *why=NULL;
        if(kzsf_follow_baseline_read(c->R14,c->Rbx,(uintptr_t)image,kzsf_cheat_read,NULL,
                kzsf_observer_baseline_type,&baseline,&why)) {
            if(resource_owner!=baseline.owner){resource_owner=baseline.owner;resource_stable=0;}
            if(++resource_stable>=60) {
                resource_stable=0;++resource_attempts;
                if(kzsf_observer_character_resources(stderr,baseline.owner,(uintptr_t)image,kzsf_cheat_read,NULL,
                        runtime_thread_tcb_peek()))
                    resource_attempts=3;
                fflush(stderr);
            }
        } else resource_stable=0;
    }
}
/* Direct SysV call site: original camera method runs exactly once before observation. */
typedef struct {uintptr_t entity,type,partner;unsigned partner_hits;} KzsfCameraContact;
static uintptr_t kzsf_camera_ignore_grenade(void *opaque,uintptr_t association) {
    KzsfCameraContact *contact=opaque;contact->entity=contact->type=0;
    uintptr_t base=(uintptr_t)image;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,association,0x50,&contact->entity,8))return 0;
    contact->type=kzsf_observer_baseline_type(contact->entity,base,kzsf_cheat_read,NULL);
    if(contact->partner && contact->entity==contact->partner && contact->type==base+0x3ca98b0) {
        ++contact->partner_hits;return contact->entity;
    }
    /* World Grenade/GrenadeRep, not InventoryGrenade. Unknown objects stay
     * solid. The native sorted exclusion list is extended before resweeping. */
    if(contact->type!=base+0x3cbf2a8 && contact->type!=base+0x3cbf388)return 0;
    static unsigned reports;
    if(reports++<8) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON_PROJECTILE ignore=grenade entity=%p type_rva=0x%llx\n",
            (void*)contact->entity,(unsigned long long)(contact->type-base));fflush(stderr);
    }
    if(reports<=12)kzsf_third_person_throw_contact_frames=12;
    return contact->entity;
}
static void kzsf_third_person_update(uintptr_t render,uintptr_t view,unsigned frame) {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    int focused=pid==GetCurrentProcessId() && !kzsfgpu_overlay_captures_input();
    int key=focused && kzsf_free_key(VK_F8);const char *why="disabled";
    int camera_request=kzsfgpu_camera_request(0);
    if(camera_request>=0)kzsf_third_person.requested=camera_request;
    KzsfFollowBaseline baseline;
    int eligible=(kzsf_third_person.requested || key) &&
        kzsf_follow_baseline_read(view,render,(uintptr_t)image,kzsf_cheat_read,NULL,
                                 kzsf_observer_baseline_type,&baseline,&why);
    uint64_t tick=GetTickCount64();
    float native[16],pose[16];int readable=kzsf_zoom_read(kzsf_cheat_read,NULL,render,0x10,native,sizeof(native));
    int previous=kzsf_third_person.requested;
    uintptr_t previous_view=kzsf_third_person.view;uint64_t previous_tick=kzsf_third_person.tick;
    int active=kzsf_third_person_step(&kzsf_third_person,view,tick,focused,key,
                                     eligible,readable ? native : NULL,pose);
    kzsfgpu_camera_publish(0,kzsf_third_person.requested);
    if(previous && !kzsf_third_person.requested)
        why=previous_view && previous_view!=view ? "view-changed" :
            previous_tick && tick<previous_tick ? "clock-reset" : "f8-disabled";
    if(active) {
        KzsfCameraEngineContext engine;uintptr_t world=0;float fraction=1;int hit=0;
        uintptr_t base=(uintptr_t)image;
        KzsfCameraPhysics api={(KzsfCameraExclude)(base+0x64ebb0),
            (KzsfCameraSweep)(base+0x4f00e0),(KzsfCameraRelease)(base+0x3295d0)};
        KzsfCameraContact contact={0};
        contact.partner=kzsf_execution_partner(&baseline,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type);
        KzsfCameraExtraIgnore extra={(KzsfCameraAddIgnore)(base+0x4ca390),
            (KzsfCameraSortIgnore)(base+0x4ca450),kzsf_camera_ignore_grenade,&contact};
        if(!kzsf_third_person_collision_ready ||
           !kzsf_camera_engine_context(runtime_thread_tcb_peek(),base,kzsf_cheat_read,NULL,&engine) ||
           engine.allocate_method!=base+0x4f67a0 ||
           !kzsf_zoom_read(kzsf_cheat_read,NULL,base,0x3b490e0,&world,8) || !world) {
            active=0;why="collision-context-unknown";
        } else {
            ++kzsf_third_person_sweeps;
            if(kzsf_third_person_sweeps==1) {
                fprintf(stderr,"KILLZONE_THIRD_PERSON_COLLISION phase=begin owner=%p world=%p tid=%lu\n",
                    (void*)baseline.owner,(void*)world,(unsigned long)GetCurrentThreadId());fflush(stderr);
            }
            if(!kzsf_camera_collision_query_ex(base,world,baseline.owner,api,native+12,pose+12,&fraction,&hit,&extra)) {
                active=0;why="collision-result-unknown";
            } else {
                static uintptr_t partner_report_layer;static unsigned partner_reports;
                if(contact.partner_hits && partner_report_layer!=baseline.combat_layer && partner_reports<32) {
                    partner_report_layer=baseline.combat_layer;++partner_reports;
                    fprintf(stderr,"KILLZONE_THIRD_PERSON_EXECUTION_PARTNER layer=%p partner=%p ignored=%u fraction=%.3f wall_hit=%d\n",
                        (void*)baseline.combat_layer,(void*)contact.partner,contact.partner_hits,fraction,hit);fflush(stderr);
                }
                static uintptr_t execution_view,execution_layer;
                static int execution_candidate=-1;
                if(execution_view!=view || execution_layer!=baseline.combat_layer) {
                    execution_view=view;execution_layer=baseline.combat_layer;execution_candidate=-1;
                }
                float primary_clearance=kzsf_third_person_clearance(native,pose,fraction);
                if(baseline.combat_layer && (execution_candidate>=0 || primary_clearance<.65f)) {
                    float best[16],best_fraction=fraction,best_clearance=primary_clearance;
                    int best_hit=hit,best_index=-1;KzsfCameraContact primary_contact=contact;
                    /* Prefer the already chosen shoulder while it has room.
                     * A different direction is a separately swept placement;
                     * never interpolate between shoulders through scenery. */
                    for(unsigned attempt=0;attempt<3;attempt++) {
                        unsigned index=execution_candidate>=0 ? ((unsigned)execution_candidate+attempt)%3 : attempt;
                        float candidate[16],candidate_fraction=1;int candidate_hit=0;
                        if(!kzsf_third_person_combat_candidate(native,index,candidate))continue;
                        ++kzsf_third_person_sweeps;
                        if(!kzsf_camera_collision_query_ex(base,world,baseline.owner,api,native+12,candidate+12,
                                &candidate_fraction,&candidate_hit,&extra))continue;
                        float clearance=kzsf_third_person_clearance(native,candidate,candidate_fraction);
                        if(clearance<.7f || clearance<best_clearance+.15f)continue;
                        memcpy(best,candidate,sizeof(best));best_fraction=candidate_fraction;
                        best_clearance=clearance;best_hit=candidate_hit;best_index=(int)index;
                        if((int)index==execution_candidate)break;
                    }
                    static unsigned execution_reports;
                    if(best_index>=0) {
                        if(execution_candidate!=best_index)kzsf_third_person_boom.valid=0;
                        memcpy(pose,best,sizeof(best));fraction=best_fraction;hit=best_hit;
                    }
                    if(execution_reports<48 && (execution_candidate!=best_index || primary_clearance<.3f)) {
                        fprintf(stderr,"KILLZONE_THIRD_PERSON_EXECUTION frame=%u layer=%p candidate=%d primary_clearance=%.3f selected_clearance=%.3f contact=%p type_rva=0x%llx\n",
                            frame,(void*)baseline.combat_layer,best_index,primary_clearance,best_clearance,
                            (void*)primary_contact.entity,(unsigned long long)(primary_contact.type>=base ? primary_contact.type-base : 0));
                        fflush(stderr);++execution_reports;
                    }
                    execution_candidate=best_index;
                }
                kzsf_third_person_hits+=(unsigned)hit;
                if(kzsf_third_person_sweeps==1 || (hit && kzsf_third_person_hits==1)) {
                    fprintf(stderr,"KILLZONE_THIRD_PERSON_COLLISION native_call=1 hit=%d fraction=%.3f sweeps=%u hits=%u\n",
                        hit,fraction,kzsf_third_person_sweeps,kzsf_third_person_hits);fflush(stderr);
                }
                active=kzsf_camera_boom_apply(&kzsf_third_person_boom,view,tick,native,pose,fraction);
                if(!active) {
                    why="camera-arm-obstructed";
                    static unsigned contacts;
                    if(fraction<.15f && contacts++<16) {
                        fprintf(stderr,"KILLZONE_THIRD_PERSON_CONTACT fraction=%.3f entity=%p type_rva=0x%llx\n",
                            fraction,(void*)contact.entity,
                            (unsigned long long)(contact.type>=base ? contact.type-base : 0));fflush(stderr);
                    }
                }
                if(active) {
                    /* Resolve the native eye's aim line against the world, then
                     * converge the final collision-shortened shoulder camera.
                     * This camera-solid filter is not a ballistic material test. */
                    float end[3],aim_fraction=1;int aim_hit=0;
                    for(int i=0;i<3;i++)end[i]=native[12+i]+250*native[4+i];
                    ++kzsf_third_person_aim_traces;
                    if(!kzsf_camera_collision_trace(base,world,baseline.owner,api,native+12,end,.01f,500,
                            &aim_fraction,&aim_hit) ||
                       !kzsf_third_person_converge(native,pose,250*aim_fraction)) {
                        active=0;why="aim-target-unknown";
                    } else if(kzsf_third_person_aim_traces==1) {
                        fprintf(stderr,"KILLZONE_THIRD_PERSON_AIM native_call=1 hit=%d range=%.3f filter=camera-solid radius=0.01\n",
                            aim_hit,250*aim_fraction);fflush(stderr);
                    }
                }
            }
        }
    } else kzsf_third_person_boom.valid=0;
    if(active && (render>UINTPTR_MAX-0x50 || !kzsf_turn_write(NULL,render+0x10,pose,sizeof(pose)))) {
        active=0;kzsf_third_person.requested=0;why="pose-write-failed";
    }
    int was_hidden=__atomic_load_n(&kzsf_third_person_hide_body,__ATOMIC_RELAXED);
    int hide=active && kzsf_third_person_body_hidden(was_hidden,native,pose);
    __atomic_store_n(&kzsf_third_person_hide_body,hide,__ATOMIC_RELEASE);
    if(hide!=was_hidden) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON_BODY hidden=%d reason=close-camera\n",hide);fflush(stderr);
    }
    int old=__atomic_exchange_n(&kzsf_free_camera_body_active,active,__ATOMIC_ACQ_REL);
    static uintptr_t prior_combat_layer;static unsigned combat_reports;
    uintptr_t combat_layer=eligible ? baseline.combat_layer : 0;
    if(combat_layer && combat_layer!=prior_combat_layer && combat_reports<16) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON_COMBAT event=%u frame=%u active=%d component=%p layer=%p resource=%p transition=%p\n",
            ++combat_reports,frame,active,(void*)baseline.combat_component,(void*)combat_layer,
            (void*)baseline.combat_resource,(void*)baseline.combat_transition);fflush(stderr);
    }
    prior_combat_layer=combat_layer;
    static uintptr_t prior_ladder;static unsigned ladder_reports;
    uintptr_t ladder=eligible && baseline.ladder_controller ? baseline.camera_owner : 0;
    if(ladder && ladder!=prior_ladder && ladder_reports<16) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON_INTERACTION event=%u frame=%u active=%d owner=%p actor=%p controller=%p\n",
            ++ladder_reports,frame,active,(void*)ladder,(void*)baseline.owner,
            (void*)baseline.ladder_controller);fflush(stderr);
        kzsf_observer_object(stderr,"interaction-actor-controller",baseline.controller,
                             (uintptr_t)image,kzsf_cheat_read,NULL);
    }
    prior_ladder=ladder;
    /* Bounded release evidence, automatically paired with the frame that wrote
     * the pose. Distinguishes eligibility loss from an animated native pose or
     * model visibility change without another manual snapshot sequence. */
    static uintptr_t prior_grenade;static unsigned throw_events,throw_frames;
    uintptr_t grenade=eligible ? baseline.throwable : 0;
    if(kzsf_third_person.requested && grenade!=prior_grenade && (grenade || prior_grenade) && throw_events<12) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON_THROW event=%u phase=%s frame=%u tick=%llu item=%p\n",
            ++throw_events,grenade ? "equipped" : "released",frame,(unsigned long long)tick,(void*)grenade);
        throw_frames=12;
    }
    prior_grenade=grenade;
    if((throw_frames || kzsf_third_person_throw_contact_frames) && readable && kzsf_third_person.requested) {
        if(throw_frames)--throw_frames;
        if(kzsf_third_person_throw_contact_frames)--kzsf_third_person_throw_contact_frames;
        fprintf(stderr,"KILLZONE_THIRD_PERSON_THROW_FRAME frame=%u tick=%llu active=%d eligible=%d reason=%s native=%.3f,%.3f,%.3f eye=%.3f,%.3f,%.3f fp_hooks=%u world_hooks=%u\n",
            frame,(unsigned long long)tick,active,eligible,active ? "native-follow" : why,
            native[12],native[13],native[14],active ? pose[12] : native[12],
            active ? pose[13] : native[13],active ? pose[14] : native[14],
            __atomic_load_n(&kzsf_model_view_events[0],__ATOMIC_RELAXED),
            __atomic_load_n(&kzsf_model_view_events[1],__ATOMIC_RELAXED));fflush(stderr);
    }
    if(old!=active || previous!=kzsf_third_person.requested) {
        fprintf(stderr,"KILLZONE_THIRD_PERSON requested=%d active=%d reason=%s collision=native-sphere sweeps=%u hits=%u arm=%.3f\n",
            kzsf_third_person.requested,active,active ? "native-follow" : why,
            kzsf_third_person_sweeps,kzsf_third_person_hits,kzsf_third_person_boom.fraction);fflush(stderr);
    }
        /* Capture distinct rejected actions even when already in first person.
         * Collision collapse must not consume the action evidence budget. */
        static unsigned action_reports;
        static uintptr_t last_state,last_controller,last_override,last_source;
        static const char *last_reason;static int32_t last_layers=-2;
        if(kzsf_third_person.requested && !active && !eligible && action_reports<64) {
            uintptr_t source=0,owner=0,state=0,controller=0,override=0;int32_t layers=-1;
            kzsf_zoom_read(kzsf_cheat_read,NULL,view,0x18,&owner,8);
            if(kzsf_observer_baseline_type(owner,(uintptr_t)image,kzsf_cheat_read,NULL)==(uintptr_t)image+0x3ca98b0) {
                kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x900,&state,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x290,&controller,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x8b8,&override,8);
            }
            kzsf_zoom_read(kzsf_cheat_read,NULL,view,0x38,&source,8);
            if(source>0x48 && kzsf_observer_baseline_type(source-0x48,(uintptr_t)image,kzsf_cheat_read,NULL)==(uintptr_t)image+0x3c8ebd0)
                kzsf_zoom_read(kzsf_cheat_read,NULL,source-0x48,0x278,&layers,4);
            if(!last_reason || strcmp(why,last_reason) || state!=last_state || controller!=last_controller ||
               override!=last_override || source!=last_source || layers!=last_layers) {
            last_reason=why;last_state=state;last_controller=controller;last_override=override;
            last_source=source;last_layers=layers;
            fprintf(stderr,"KILLZONE_THIRD_PERSON_ACTION sample=%u reason=%s override=%p layers=%d\n",
                    ++action_reports,why,(void*)override,layers);fflush(stderr);
            kzsf_observer_object(stderr,"action-state",state,(uintptr_t)image,kzsf_cheat_read,NULL);
            kzsf_observer_object(stderr,"action-controller",controller,(uintptr_t)image,kzsf_cheat_read,NULL);
            kzsf_observer_object(stderr,"action-override",override,(uintptr_t)image,kzsf_cheat_read,NULL);
            if(kzsf_observer_baseline_type(override,(uintptr_t)image,kzsf_cheat_read,NULL)==(uintptr_t)image+0x3c74dc0) {
                uintptr_t associated=0;kzsf_zoom_read(kzsf_cheat_read,NULL,override,0x38,&associated,8);
                kzsf_observer_object(stderr,"action-associated",associated,(uintptr_t)image,kzsf_cheat_read,NULL);
            }
            kzsf_observer_object(stderr,"action-owner",owner,(uintptr_t)image,kzsf_cheat_read,NULL);
            uintptr_t interaction_type=kzsf_observer_baseline_type(owner,(uintptr_t)image,kzsf_cheat_read,NULL);
            if(interaction_type==(uintptr_t)image+0x3c9a390 || interaction_type==(uintptr_t)image+0x3c99180 ||
               interaction_type==(uintptr_t)image+0x3c9e7c0 || interaction_type==(uintptr_t)image+0x3c9c150) {
                uintptr_t actor=0,ladder_control=0,control_owner=0,control_source=0,user=0,stack=0;
                int32_t count=-1;
                kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x598,&actor,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x290,&ladder_control,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,ladder_control,0x40,&control_owner,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,ladder_control,0x28,&control_source,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,view,0x10,&user,8);
                kzsf_zoom_read(kzsf_cheat_read,NULL,user,0xc8,&count,4);
                kzsf_zoom_read(kzsf_cheat_read,NULL,user,0xd0,&stack,8);
                fprintf(stderr,"  ladder_owner actor_interface=%p controller=%p controller_owner=%p camera_interface=%p stack_count=%d\n",
                    (void*)actor,(void*)ladder_control,(void*)control_owner,(void*)control_source,count);
                kzsf_observer_object(stderr,"ladder-controller",ladder_control,(uintptr_t)image,kzsf_cheat_read,NULL);
                if(actor>0x48) {
                    uintptr_t use=0,associated=0,actor_state=0;
                    kzsf_zoom_read(kzsf_cheat_read,NULL,actor-0x48,0x8b8,&use,8);
                    kzsf_zoom_read(kzsf_cheat_read,NULL,use,0x38,&associated,8);
                    kzsf_zoom_read(kzsf_cheat_read,NULL,actor-0x48,0x900,&actor_state,8);
                    fprintf(stderr,"  ladder_actor use=%p associated=%p\n",(void*)use,(void*)associated);
                    kzsf_observer_object(stderr,"ladder-actor",actor-0x48,(uintptr_t)image,kzsf_cheat_read,NULL);
                    kzsf_observer_object(stderr,"ladder-state",actor_state,(uintptr_t)image,kzsf_cheat_read,NULL);
                    uintptr_t actor_control=0,actor_inventory=0,actor_item=0;
                    kzsf_zoom_read(kzsf_cheat_read,NULL,actor-0x48,0x290,&actor_control,8);
                    kzsf_observer_object(stderr,"interaction-actor-controller",actor_control,(uintptr_t)image,kzsf_cheat_read,NULL);
                    if(kzsf_zoom_read(kzsf_cheat_read,NULL,actor-0x48,0xa48,&actor_inventory,8) &&
                       kzsf_zoom_read(kzsf_cheat_read,NULL,actor_inventory,0x68,&actor_item,8) && actor_item>0x48)
                        kzsf_observer_object(stderr,"interaction-actor-item",actor_item-0x48,(uintptr_t)image,kzsf_cheat_read,NULL);
                    if(kzsf_observer_baseline_type(actor_state,(uintptr_t)image,kzsf_cheat_read,NULL)==(uintptr_t)image+0x3cac7a8) {
                        uintptr_t pose_actor=0,pose_owner=0;
                        kzsf_zoom_read(kzsf_cheat_read,NULL,actor_state,0x28,&pose_actor,8);
                        kzsf_zoom_read(kzsf_cheat_read,NULL,actor_state,0x40,&pose_owner,8);
                        fprintf(stderr,"  interaction_pose actor=%p owner=%p\n",(void*)pose_actor,(void*)pose_owner);
                    }
                }
                if(count>0 && count<=4)for(int i=0;i<count;i++) {
                    uintptr_t entry=0;kzsf_zoom_read(kzsf_cheat_read,NULL,stack,(size_t)i*0x18,&entry,8);
                    fprintf(stderr,"  ladder_stack index=%d interface=%p\n",i,(void*)entry);
                }
            }
            if(source>0x48)kzsf_observer_object(stderr,"action-source",source-0x48,(uintptr_t)image,kzsf_cheat_read,NULL);
            if(source>0x48)kzsf_observer_layers(stderr,source-0x48,
                kzsf_observer_baseline_type(source-0x48,(uintptr_t)image,kzsf_cheat_read,NULL),
                (uintptr_t)image,kzsf_cheat_read,NULL);
            uintptr_t inventory=0,item=0;
            if(kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0xa48,&inventory,8) &&
               kzsf_zoom_read(kzsf_cheat_read,NULL,inventory,0x68,&item,8) && item>0x48)
                kzsf_observer_object(stderr,"action-item",item-0x48,(uintptr_t)image,kzsf_cheat_read,NULL);
            fflush(stderr);
            }
        }
        if(active)last_reason=NULL;
}
typedef struct {uintptr_t render,view,original;unsigned hit;} KzsfCameraVisit;
static void kzsf_camera_follow_task(void *opaque) {
    KzsfCameraVisit *v=opaque;kzsf_third_person_update(v->render,v->view,v->hit);
    static int combined=-1;
    if(combined<0){const char *value=getenv("KZSF_FOLLOW_FREE_CAMERA");combined=value && !strcmp(value,"1");}
    if(combined) {
        CONTEXT context;memset(&context,0,sizeof(context));context.Rbx=v->render;
        const float *pose=kzsf_free_camera_update(&context,1);
        if(pose) {
            if(!kzsf_turn_write(NULL,v->render+0x10,pose,64)) {
                kzsf_free_camera.active=0;kzsf_free_input(0);
            } else {
                __atomic_store_n(&kzsf_free_camera_body_active,1,__ATOMIC_RELEASE);
                __atomic_store_n(&kzsf_third_person_hide_body,0,__ATOMIC_RELEASE);
            }
        }
    }
}
static void kzsf_camera_observer_task(void *opaque) {
    KzsfCameraVisit *v=opaque;CONTEXT c;memset(&c,0,sizeof(c));c.Rbx=v->render;c.R14=v->view;
    kzsf_camera_observe_view(&c,v->hit,1,v->original);
}
static ABI void kzsf_camera_bridge_visit(uintptr_t render,uintptr_t view,uintptr_t original) {
    ((void (ABI *)(uintptr_t))original)(render);
    unsigned hit=__atomic_add_fetch(&kzsf_camera_observer_hits[2],1,__ATOMIC_RELAXED);
    KzsfCameraVisit visit={render,view,original,hit};
    kzsf_camera_dispatch(&kzsf_camera_follow_lock,&kzsf_camera_observer_lock,
        kzsf_third_person_enabled ? kzsf_camera_follow_task : NULL,kzsf_camera_observer_task,&visit);
}
static uintptr_t kzsf_character_level_exit_original;
/* Enter before d65530 destroys player actors or the section lump table.
 * Balance only imported counts through native61d210/61d350. */
static ABI void kzsf_character_level_exit_visit(uintptr_t state) {
    void (ABI *original)(uintptr_t)=(void (ABI *)(uintptr_t))kzsf_character_level_exit_original;
    if(__atomic_exchange_n(&kzsf_character_level_transition,1,__ATOMIC_ACQ_REL)){original(state);return;}
    kzsfgpu_character_selector_enable(0);
    uintptr_t base=(uintptr_t)image;KzsfCameraEngineContext context;
    int engine=kzsf_camera_engine_context(runtime_thread_tcb_peek(),base,kzsf_cheat_read,NULL,&context) &&
        context.allocate_method==base+0x4f67a0 && kzsf_character_bundle_guards(base,kzsf_cheat_read,NULL);
    fprintf(stderr,"KILLZONE_CHARACTER_LEVEL_EXIT phase=begin engine_context=%d\n",engine);fflush(stderr);
    if(engine) {
        KzsfCharacterBundleApi api=kzsf_character_bundle_api(base);
        uintptr_t manager=0,worker=0;unsigned char busy=0;
        if(kzsf_zoom_read(kzsf_cheat_read,NULL,base,0x3c67760,&manager,8) && manager &&
           kzsf_zoom_read(kzsf_cheat_read,NULL,manager,0xd8,&worker,8) && worker &&
           kzsf_zoom_read(kzsf_cheat_read,NULL,worker,0x88,&busy,1) && busy==1)api.wait(worker);
        uintptr_t owner=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE);
        if(owner && kzsf_observer_baseline_type(owner,base,kzsf_cheat_read,NULL)==base+0x3ca98b0) {
            if(kzsf_cj_part)kzsf_custom_model_tick(owner,base,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),kzsf_observer_baseline_type,0);
            else kzsf_observer_character_cycle(stderr,owner,base,kzsf_cheat_read,NULL,runtime_thread_tcb_peek(),0);
        }
        for(int kind=1;kind<KZSF_CHARACTER_MODEL_COUNT;kind++) {
            if(!kzsf_character_bundle_leases[kind].queued)continue;
            int result=kzsf_character_bundle_release(kind,base,kzsf_cheat_read,NULL,api,&kzsf_character_bundle_leases[kind]);
            fprintf(stderr,"KILLZONE_CHARACTER_LEVEL_EXIT model=%s release=%d\n",kzsf_character_model_name(kind),result);fflush(stderr);
        }
    }
    original(state);
    if(engine)kzsf_cj_release(base);
    /* Actors and old render nodes are now gone. Drop host identity tokens,
     * including cached discovery results, before the next level can use them. */
    kzsf_character_render_publish(0,0,-1,0,0,0);
    __atomic_store_n(&kzsf_character_face_source_soldier,0,__ATOMIC_RELEASE);
    memset(&kzsf_character_selected_face,0,sizeof(kzsf_character_selected_face));
    memset(kzsf_character_bundle_leases,0,sizeof(kzsf_character_bundle_leases));
    for(int kind=0;kind<KZSF_CHARACTER_MODEL_COUNT;kind++)__atomic_store_n(&kzsf_character_known_parts[kind],0,__ATOMIC_RELEASE);
    kzsf_character_available_models=1;kzsf_character_available_owner=0;
    kzsf_character_level_epoch++;
    kzsfgpu_character_reset();
    __atomic_store_n(&kzsf_character_level_transition,0,__ATOMIC_RELEASE);
    kzsfgpu_character_selector_enable(1);
    fputs("KILLZONE_CHARACTER_LEVEL_EXIT phase=complete host_state=reset\n",stderr);fflush(stderr);
}
static uintptr_t kzsf_character_draw_original;
/* Keep the unverified revision11 body omission/native103 path separate from
 * the face-only experiment. Baseline revision10 routing remains the default. */
static int kzsf_character_body_route_test;
static int kzsf_character_generic_leaf(uintptr_t part,uintptr_t skeleton,uintptr_t base,KzsfCharacterMeshLeaf *out);
static ABI uintptr_t kzsf_character_face_animation_owner(uintptr_t owner,uintptr_t face) {
    static _Thread_local int player_control=-1;
    if(player_control<0) {
        const char *value=getenv("KZSF_CHARACTER_FACE_PLAYER_ANIMATION_CONTROL");
        player_control=value && !strcmp(value,"1");
    }
    if(player_control) {
        uintptr_t original=0;
        kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x1d0,&original,8);return original;
    }
    uintptr_t source=kzsf_character_face_animation_source(owner,face,
        __atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE),
        __atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED),
        __atomic_load_n(&kzsf_character_face_source_soldier,__ATOMIC_ACQUIRE),
        (uintptr_t)image,kzsf_cheat_read,NULL,kzsf_observer_baseline_type);
    uintptr_t original=0;
    if(kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x1d0,&original,8) && source && source!=original) {
        fprintf(stderr,"KILLZONE_CHARACTER_FACE_ANIMATION source=selected-character soldier=%p player_resource=%p\n",(void*)source,(void*)original);
        fflush(stderr);
    }
    return source;
}
static uintptr_t kzsf_character_face_source_variant(uintptr_t variant,uintptr_t rep,uintptr_t face,unsigned sequence) {
    uintptr_t base=(uintptr_t)image,node=0,table=0,getter=0,pose=0,actual=0,expected=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,rep,0x1a0,&node,8) || node<8)return 0;
    node-=8; /* native661ca0, used by e46889 before its pose getter */
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,node,0,&table,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,table,0x48,&getter,8) || getter<base || getter-base>=0x1190000)return 0;
    pose=((uintptr_t (ABI *)(uintptr_t))getter)(node);
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,pose,0x20,&actual,8) || !actual ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,variant,0x60,&expected,8))return 0;
    static _Thread_local _Alignas(16) unsigned char prepared[0xf0];
    static _Thread_local _Alignas(16) float corrected[512][16];
    static _Thread_local int32_t remapped[512];
    static _Thread_local uintptr_t cached_variant,cached_actual;
    static _Thread_local unsigned cached_sequence;
    if(cached_variant==variant && cached_actual==actual && cached_sequence==sequence)return (uintptr_t)prepared;
    cached_variant=0;
    int bind_correct=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE)>=3 &&
        face==__atomic_load_n(&kzsf_character_selected_face.face,__ATOMIC_ACQUIRE);
    if(actual==expected && !bind_correct)return variant;
    int ready=0;
    if(bind_correct) {
        KzsfCharacterMeshLeaf original,target;
        static _Thread_local KzsfCharacterGenericPlan plan;
        uintptr_t original_part=__atomic_load_n(&kzsf_character_known_parts[0],__ATOMIC_ACQUIRE);
        uintptr_t selected_part=__atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED);
        ready=kzsf_character_generic_leaf(original_part,0,base,&original) &&
            kzsf_character_generic_leaf(selected_part,0,base,&target) &&
            kzsf_character_face_bind_variant(variant,actual,&original,&target,kzsf_cheat_read,NULL,
                prepared,remapped,corrected,&plan);
    } else ready=kzsf_character_face_reindex(variant,actual,kzsf_cheat_read,NULL,prepared,remapped);
    if(!ready) {
        static _Thread_local unsigned refused_sequence;
        if(refused_sequence!=sequence) {
            refused_sequence=sequence;
            fprintf(stderr,"KILLZONE_CHARACTER_FACE_MAP status=native-fallback reason=joint-reindex-refused\n");
        }
        return variant;
    }
    if(sequence!=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE))return 0;
    cached_variant=variant;cached_actual=actual;cached_sequence=sequence;
    fprintf(stderr,"KILLZONE_CHARACTER_FACE_MAP status=reindexed body_bind_corrected=%d expected=%p actual=%p\n",bind_correct,(void*)expected,(void*)actual);
    return (uintptr_t)prepared;
}
static ABI uintptr_t kzsf_character_face_mapping_variant(uintptr_t face) {
    uintptr_t base=(uintptr_t)image,rep=0,owner=0,resource=0,soldier=0,array=0;
    int32_t count=0,index=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,face,0x20,&rep,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,rep,0x198,&owner,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,face,0x18,&resource,8))return 0;
    uintptr_t selected_owner=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_ACQUIRE);
    uintptr_t selected_part=__atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED);
    if(owner==selected_owner && selected_part) {
        soldier=__atomic_load_n(&kzsf_character_face_source_soldier,__ATOMIC_ACQUIRE);
        KzsfCharacterInventory inventory;
        if(kzsf_character_resource_variants_read(soldier,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&inventory))
            for(int i=0;i<inventory.count;i++)if(inventory.variants[i].part==selected_part) {
                uintptr_t variant=inventory.array+(size_t)i*0xf0;
                if(kzsf_character_face_mapping(variant,resource,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type))
                    return kzsf_character_face_source_variant(variant,rep,face,__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE));
            }
    }
    /* Preserve the original path for NPCs and other native animators. Their
     * prepared mapping belongs to the engine; experimental player preflight
     * must neither reject it nor traverse all bones every NPC frame. */
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x1d0,&soldier,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x98c,&index,4) || index<0 ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,soldier,0x648,&count,4) || count<1 || count>64 || index>=count ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,soldier,0x650,&array,8) || !array)return 0;
    uintptr_t variant=array+(size_t)index*0xf0;
    return variant;
}
/* Read-only visibility evidence, bounded to changes on owned draw nodes. The
 * native draw skips runtime parts with index==-1 or enabled==0 (703e10). */
static void kzsf_character_draw_visibility(uintptr_t node,uintptr_t source,unsigned sequence) {
    int32_t count=0;uintptr_t parts=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,node,0x70,&count,4) || count<0 || count>128 ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,node,0x78,&parts,8) || (count && !parts))return;
    uintptr_t tagged[128];int32_t state[128][2];uint64_t hash=1469598103934665603ULL;
    for(int i=0;i<count;i++) {
        if(!kzsf_zoom_read(kzsf_cheat_read,NULL,parts,(size_t)i*8,&tagged[i],8) ||
           !kzsf_zoom_read(kzsf_cheat_read,NULL,tagged[i]&~(uintptr_t)3,0x90,state[i],8))return;
        hash=(hash^(uint64_t)tagged[i])*1099511628211ULL;
        hash=(hash^(uint32_t)state[i][0])*1099511628211ULL;
        hash=(hash^(uint32_t)state[i][1])*1099511628211ULL;
    }
    static _Thread_local struct {uintptr_t node;uint64_t hash;unsigned sequence;} samples[16];
    static _Thread_local unsigned emitted;
    if(emitted>=32)return;
    unsigned slot=0;for(;slot<16;slot++)if(samples[slot].node==node || !samples[slot].node)break;
    if(slot==16)return;
    if(samples[slot].node==node && samples[slot].hash==hash && samples[slot].sequence==sequence)return;
    samples[slot].node=node;samples[slot].hash=hash;samples[slot].sequence=sequence;emitted++;
    fprintf(stderr,"KILLZONE_CHARACTER_DRAW_PART_STATE sequence=%u node=%llx parts=%d state=",sequence,(unsigned long long)node,count);
    for(int i=0;i<count;i++)fprintf(stderr,"%s%d:%d:%d",i ? "," : "",i,state[i][0],state[i][1]);
    uintptr_t scalars=0,end=0;float values[4];
    if(kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x50,&scalars,8) && scalars &&
       kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x58,&end,8) && end>=scalars && end-scalars>=sizeof(values) &&
       kzsf_zoom_read(kzsf_cheat_read,NULL,scalars,0,values,sizeof(values)))
        fprintf(stderr," scalars=%.6g,%.6g,%.6g,%.6g",values[0],values[1],values[2],values[3]);
    fputc('\n',stderr);fflush(stderr);
}
/* Experimental soldier rigs use a bounded named/bind-space conversion via
 * the original98 body. Every draw keeps native ownership and record order. */
static int kzsf_character_generic_leaf(uintptr_t part,uintptr_t skeleton,uintptr_t base,KzsfCharacterMeshLeaf *out) {
    KzsfCharacterPart resource;KzsfCharacterMeshTree tree;
    if(!part || !kzsf_character_part_read(part,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&resource) ||
       !resource.is_skinned || !kzsf_character_mesh_read(resource.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&tree) ||
       !tree.count || tree.unknown_count)return 0;
    for(int i=0;i<tree.count;i++)if(!skeleton || tree.leaves[i].skeleton==skeleton){*out=tree.leaves[i];return 1;}
    return 0;
}
static int kzsf_character_generic_draw(uintptr_t node,uintptr_t render,uintptr_t source,uintptr_t previous,
        uintptr_t owner,uintptr_t model,uintptr_t selected,unsigned sequence,int restoring) {
    uintptr_t base=(uintptr_t)image,source_rig=0,previous_rig=0;
    KzsfCharacterInventory inventory;KzsfCharacterMeshLeaf from,canonical,target;
    if(!kzsf_character_inventory_read(owner,kzsf_cheat_read,NULL,&inventory) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x20,&source_rig,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,previous,0x20,&previous_rig,8))return 0;
    if(previous_rig!=source_rig)previous=source;
    uintptr_t original=inventory.variants[inventory.current].part,desired=restoring ? original : selected;
    if(!kzsf_character_generic_leaf(original,0,base,&canonical) || canonical.binding_count!=98 ||
       !kzsf_character_generic_leaf(desired,0,base,&target))return 0;
    /* Native named pose mapping copies Lucas's animated worlds, even when
     * weapon/model rebuilds leave a larger NPC skeleton on the render node.
     * Its layout does not imply that NPC bind corrections were applied. */
    from=canonical;from.skeleton=source_rig;
    static _Thread_local KzsfCharacterGenericPlan to_base,to_target;
    static _Thread_local uintptr_t cached_source,cached_source_binding,cached_target,cached_target_binding,cached_base_binding;
    static _Thread_local unsigned cached_sequence;
    if(cached_sequence!=sequence || cached_source!=source_rig || cached_source_binding!=from.bindings ||
       cached_target!=target.skeleton || cached_target_binding!=target.bindings || cached_base_binding!=canonical.bindings) {
        if(!kzsf_character_generic_plan(&from,&canonical,kzsf_cheat_read,NULL,&to_base) ||
           !kzsf_character_generic_plan(&canonical,&target,kzsf_cheat_read,NULL,&to_target))return 0;
        if(source_rig!=canonical.skeleton && !kzsf_character_generic_rebuild_player_helpers(&to_base))return 0;
        int selected_kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
        const char *selected_name=kzsf_character_model_name(selected_kind);
        if(!restoring && (!strcmp(selected_name,"ISA_Echo") || !strcmp(selected_name,"ISA_Lamb_Echo")) &&
           !kzsf_character_generic_anchor_wrists(&to_target,target.skeleton,kzsf_cheat_read,NULL))return 0;
        cached_sequence=sequence;cached_source=source_rig;cached_source_binding=from.bindings;
        cached_target=target.skeleton;cached_target_binding=target.bindings;cached_base_binding=canonical.bindings;
    }
    float source_world[2][KZSF_CHARACTER_GENERIC_MAX][16],base_world[2][KZSF_CHARACTER_GENERIC_MAX][16],target_world[2][KZSF_CHARACTER_GENERIC_MAX][16];
    for(int i=0;i<2;i++)if(!kzsf_character_generic_read_worlds(i ? previous : source,to_base.from_count,kzsf_cheat_read,NULL,source_world[i]) ||
        !kzsf_character_generic_worlds(&to_base,source_world[i],base_world[i]) ||
        !kzsf_character_generic_draw_worlds(&to_target,base_world[i],target_world[i]))return 0;
    KzsfCharacterDrawPart parts[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t count=0;
    if(!kzsf_character_draw_parts(node,model,desired,target.skeleton,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,parts,&count))return 0;
    KzsfCharacterNativeMap maps[2]={{0}},views[2];KzsfCharacterJointMap joints[2][KZSF_CHARACTER_GENERIC_MAX];int32_t channels[2][128];
    _Alignas(16) unsigned char poses[4][0x70]={{0}};
    KzsfCharacterRenderContext contexts[2]={
        {.source=source,.current=(uintptr_t)poses[0],.previous=(uintptr_t)poses[1]},
        {.source=(uintptr_t)poses[0],.current=(uintptr_t)poses[2],.previous=(uintptr_t)poses[3]}};
    int drawn=0;
    ((KzsfCharacterBuildMap)(base+0x5d8dd0))(source_rig,canonical.skeleton,&maps[0]);
    if(maps[0].source!=source_rig || maps[0].destination!=canonical.skeleton ||
       !kzsf_character_generic_map_prepare(&maps[0],source,to_base.from_count,98,kzsf_cheat_read,NULL,joints[0],channels[0],&views[0]))goto cleanup;
    kzsf_character_render_pair(&contexts[0],canonical.skeleton,&views[0],(KzsfCharacterPoseAssign)(base+0x5d1d10),
        (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
    for(int i=0;i<2;i++)if(!kzsf_character_generic_write_worlds(i ? contexts[0].previous : contexts[0].current,98,
        kzsf_cheat_read,kzsf_turn_write,NULL,base_world[i]))goto cleanup;
    ((KzsfCharacterBuildMap)(base+0x5d8dd0))(canonical.skeleton,target.skeleton,&maps[1]);
    if(maps[1].source!=canonical.skeleton || maps[1].destination!=target.skeleton ||
       !kzsf_character_generic_map_prepare(&maps[1],contexts[0].current,98,to_target.to_count,kzsf_cheat_read,NULL,joints[1],channels[1],&views[1]))goto cleanup;
    kzsf_character_render_pair(&contexts[1],target.skeleton,&views[1],(KzsfCharacterPoseAssign)(base+0x5d1d10),
        (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
    for(int i=0;i<2;i++)if(!kzsf_character_generic_write_worlds(i ? contexts[1].previous : contexts[1].current,to_target.to_count,
        kzsf_cheat_read,kzsf_turn_write,NULL,target_world[i]))goto cleanup;
    KzsfCharacterDrawProxy proxy;uintptr_t methods[11];
    if(!kzsf_character_draw_proxy(node,contexts[1].current,contexts[1].previous,kzsf_cheat_read,NULL,&proxy,methods) ||
       sequence!=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE))goto cleanup;
    uintptr_t draw_tags[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t draw_count=0;
    if(!kzsf_character_draw_native_batch(parts,count,original,desired,draw_tags,&draw_count))goto cleanup;
    for(int i=0;i<draw_count;i++) {
        uintptr_t head=__atomic_load_n(&kzsf_character_render_head_part,__ATOMIC_RELAXED);
        int selected=parts[i].selected || (head && parts[i].resource==head && (parts[i].tagged&2));
        proxy.current=contexts[selected ? 1 : 0].current;proxy.previous=contexts[selected ? 1 : 0].previous;
        proxy.fields[14]=1;proxy.fields[15]=(uintptr_t)&draw_tags[i];
        ((void (ABI *)(uintptr_t,uintptr_t))kzsf_character_draw_original)((uintptr_t)&proxy,render);
    }
    drawn=1;
    static unsigned reported;
    if(__atomic_exchange_n(&reported,sequence,__ATOMIC_RELAXED)!=sequence) {
        fprintf(stderr,"KILLZONE_CHARACTER_GENERIC status=drawn source_bones=%d target_bones=%d records=%d matched=%d omitted=0 restoring=%d\n",
            to_base.from_count,to_target.to_count,count,to_target.matched,restoring);fflush(stderr);
    }
cleanup:
    for(int i=3;i>=0;i--)((KzsfCharacterPoseAssign)(base+0x5d1d10))((uintptr_t)poses[i],0);
    for(int i=0;i<2;i++) {
        if(maps[i].channels.data && maps[i].channels.data!=maps[i].joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(maps[i].channels.data);
        if(maps[i].joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(maps[i].joints.data);
    }
    return drawn;
}
static ABI void kzsf_character_draw_visit(uintptr_t node,uintptr_t render) {
    typedef void (ABI *Draw)(uintptr_t,uintptr_t);
    Draw original=(Draw)kzsf_character_draw_original;uintptr_t base=(uintptr_t)image;
    static int hook_seen;
    if(!__atomic_exchange_n(&hook_seen,1,__ATOMIC_RELAXED)) {
        fputs("KILLZONE_CHARACTER_DRAW stage=hook-live\n",stderr);fflush(stderr);
    }
    unsigned sequence=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE);
    uintptr_t selected_owner=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_RELAXED);
    uintptr_t selected_part=__atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED);
    uintptr_t selected_head=__atomic_load_n(&kzsf_character_render_head_part,__ATOMIC_RELAXED);
    int restoring=0;uintptr_t restore_model=0;
    if(!selected_owner && kzsf_character_body_route_test) {
        selected_owner=__atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_RELAXED);
        selected_part=__atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED);
        restore_model=__atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED);
        restoring=1;
    }
    uintptr_t model=0,owner=0,owned=0,array=0,table=0,getter=0,source=0,skeleton=0;
    int32_t count=0,bones=0;int found=0;
    if(!selected_owner || (sequence&1) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,node,0x18,&model,8) ||
       (restoring && model!=restore_model) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,model,0x38,&owner,8) || owner!=selected_owner ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0x200,&owned,8) || owned!=model ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,model,0x58,&count,4) || count<=0 || count>128 ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,model,0x60,&array,8))goto native_draw;
    for(int32_t i=0;i<count;i++) {uintptr_t part=0;
        if(!kzsf_zoom_read(kzsf_cheat_read,NULL,array,(size_t)i*0xf0+0xc8,&part,8))goto native_draw;
        found+=part==selected_part;
    }
    if((!found && !restoring) || sequence!=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE))goto native_draw;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,node,0,&table,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,table,0x48,&getter,8) || getter<base || getter-base>=0x1190000)goto native_draw;
    source=((uintptr_t (ABI *)(uintptr_t))getter)(node);
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x20,&skeleton,8) ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,skeleton,0x30,&bones,4))goto native_draw;
    static int observed;
    if(!__atomic_exchange_n(&observed,1,__ATOMIC_RELAXED)) {
        fprintf(stderr,"KILLZONE_CHARACTER_DRAW stage=owned-node source_bones=%d getter_rva=0x%llx\n",bones,(unsigned long long)(getter-base));fflush(stderr);
    }
    const char *generic_mode=getenv("KZSF_CHARACTER_ALL_MODELS_TEST");
    int generic_test=generic_mode && !strcmp(generic_mode,"1");
    int draw_kind=__atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE);
    int generic_needed=generic_test && ((draw_kind>=3 && draw_kind!=KZSF_CHARACTER_MODEL_CJ) || (bones!=98 && bones!=103));
    if(!generic_needed && bones!=98 && (!kzsf_character_body_route_test || bones!=103))goto native_draw;
    if(restoring && bones==98)goto native_draw;
    kzsf_character_draw_visibility(node,source,sequence);
    KzsfCharacterDrawPart draw_parts[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t draw_count=0;
    uintptr_t previous_getter=0,previous_source=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,table,0x50,&previous_getter,8) || previous_getter<base || previous_getter-base>=0x1190000)goto native_draw;
    previous_source=((uintptr_t (ABI *)(uintptr_t))previous_getter)(node);
    if(!previous_source)goto native_draw;
    /* Weapon rebuilds can replace the render rig without changing selection.
     * Report layout changes, rather than only the first draw of a selection. */
    static _Thread_local uintptr_t traced_node,traced_rig,traced_previous,traced_weapon;
    static _Thread_local unsigned traced_sequence,layout_reports;
    uintptr_t previous_rig=0,inventory_ptr=0,weapon_handle=0;
    kzsf_zoom_read(kzsf_cheat_read,NULL,previous_source,0x20,&previous_rig,8);
    if(kzsf_zoom_read(kzsf_cheat_read,NULL,owner,0xa48,&inventory_ptr,8))
        kzsf_zoom_read(kzsf_cheat_read,NULL,inventory_ptr,0x68,&weapon_handle,8);
    if(layout_reports<96 && (traced_sequence!=sequence || traced_node!=node || traced_rig!=skeleton ||
       traced_previous!=previous_rig || traced_weapon!=weapon_handle)) {
        traced_sequence=sequence;traced_node=node;traced_rig=skeleton;traced_previous=previous_rig;traced_weapon=weapon_handle;++layout_reports;
        fprintf(stderr,"KILLZONE_CHARACTER_DRAW_LAYOUT sequence=%u owner=%p model=%p node=%p selected=%p kind=%d source=%p rig=%p bones=%d previous=%p previous_rig=%p weapon_handle=%p generic=%d restoring=%d freecam=%d\n",
            sequence,(void*)owner,(void*)model,(void*)node,(void*)selected_part,
            __atomic_load_n(&kzsf_character_model_kind,__ATOMIC_ACQUIRE),(void*)source,(void*)skeleton,bones,
            (void*)previous_source,(void*)previous_rig,(void*)weapon_handle,generic_needed,restoring,kzsf_free_camera.active);
        KzsfCharacterDrawPart report_parts[KZSF_CHARACTER_MAX_DRAW_PARTS];int32_t report_count=0;
        if(kzsf_character_draw_parts(node,model,selected_part,skeleton,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,report_parts,&report_count)) {
            for(int i=0;i<report_count && i<16;i++)
                fprintf(stderr,"  draw_record index=%d tagged=%p resource=%p mesh=%p source_rig_match=%d combined=%d\n",
                    i,(void*)report_parts[i].tagged,(void*)report_parts[i].resource,(void*)report_parts[i].mesh,
                    report_parts[i].selected,report_parts[i].combined);
        }
        fflush(stderr);
    }
    if(generic_needed) {
        if(kzsf_character_generic_draw(node,render,source,previous_source,owner,model,selected_part,sequence,restoring))return;
        static unsigned refused_generic;
        if(__atomic_exchange_n(&refused_generic,sequence,__ATOMIC_RELAXED)!=sequence) {
            fprintf(stderr,"KILLZONE_CHARACTER_GENERIC status=refused source_bones=%d\n",bones);fflush(stderr);
        }
        goto native_draw;
    }
    KzsfCharacterPart body;KzsfCharacterMeshTree tree;
    if(!kzsf_character_part_read(selected_part,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&body) ||
       !kzsf_character_mesh_read(body.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&tree) || !tree.count)goto native_draw;
    uintptr_t target=tree.leaves[0].skeleton;int32_t target_count=0;int compatible98=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,target,0x30,&target_count,4))goto native_draw;
    if(target_count==98 && bones==103 &&
       __atomic_load_n(&kzsf_character_restore_owner,__ATOMIC_ACQUIRE)==owner &&
       __atomic_load_n(&kzsf_character_restore_model,__ATOMIC_RELAXED)==model) {
        uintptr_t recovery=__atomic_load_n(&kzsf_character_restore_part,__ATOMIC_RELAXED);
        if(!kzsf_character_part_read(recovery,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&body) ||
           !kzsf_character_mesh_read(body.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&tree) || !tree.count)goto native_draw;
        target=tree.leaves[0].skeleton;compatible98=1;
        if(!kzsf_zoom_read(kzsf_cheat_read,NULL,target,0x30,&target_count,4))goto native_draw;
    }
    if(target_count!=103)goto native_draw;
    if(!kzsf_character_draw_parts(node,model,selected_part,target,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,draw_parts,&draw_count)) {
        static int refused;
        if(!__atomic_exchange_n(&refused,1,__ATOMIC_RELAXED))fputs("KILLZONE_CHARACTER_DRAW stage=part-route-refused\n",stderr);
        goto native_draw;
    }
    KzsfCharacterInventory body_inventory;uintptr_t original_body=0;
    if(kzsf_character_inventory_read(owner,kzsf_cheat_read,NULL,&body_inventory))
        original_body=body_inventory.variants[body_inventory.current].part;
    if(!original_body || original_body==selected_part)goto native_draw;
    if(restoring) {
        int base_present=0;
        for(int i=0;i<draw_count;i++)base_present+=draw_parts[i].resource==original_body;
        if(!base_present)goto native_draw;
    }
    /* Weapon rebuilds adopt103 render joints and can re-add the original98
     * body. Retain every record: revision14 crashed after omitting one.
     * Only the original body consumes a reverse-corrected scratch pose. */
    if(bones==103) {
        if(skeleton!=target)goto native_draw;
        uintptr_t previous_skeleton=0;
        if(!kzsf_zoom_read(kzsf_cheat_read,NULL,previous_source,0x20,&previous_skeleton,8))goto native_draw;
        if(previous_skeleton!=target)previous_source=source;
        KzsfCharacterPart original_part;KzsfCharacterMeshTree original_tree;
        if(!kzsf_character_part_read(original_body,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&original_part) ||
           !kzsf_character_mesh_read(original_part.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&original_tree) ||
           !original_tree.count || original_tree.unknown_count)goto native_draw;
        uintptr_t original_rig=original_tree.leaves[0].skeleton;int original_bones=0;
        if(!kzsf_zoom_read(kzsf_cheat_read,NULL,original_rig,0x30,&original_bones,4) || original_bones!=98)goto native_draw;
        for(int i=0;i<original_tree.count;i++)if(original_tree.leaves[i].skeleton!=original_rig ||
            original_tree.leaves[i].binding_count!=98)goto native_draw;
        static _Thread_local KzsfCharacterReverseBindPlan reverse_plan;
        static _Thread_local KzsfCharacterBindPlan repair_plan;
        static _Thread_local unsigned reverse_sequence;
        static _Thread_local uintptr_t reverse_rig,reverse_target,reverse_binding,reverse_target_binding;
        int ready=reverse_sequence==sequence && reverse_rig==original_rig && reverse_target==target &&
            reverse_binding==original_tree.leaves[0].bindings && reverse_target_binding==tree.leaves[0].bindings;
        KzsfCharacterNativeMap forward={0},reverse={0},reverse_view;
        KzsfCharacterJointMap forward_joints[103],reverse_joints[98];int32_t reverse_channels[4];
        if(!ready) {
            KzsfCharacterBindPlan plan;
            ((KzsfCharacterBuildMap)(base+0x5d8dd0))(original_rig,target,&forward);
            if(forward.source==original_rig && forward.destination==target && forward.joints.count==103 &&
               forward.joints.capacity>=103 && forward.joints.data &&
               kzsf_zoom_read(kzsf_cheat_read,NULL,forward.joints.data,0,forward_joints,sizeof(forward_joints)) &&
               kzsf_character_bind_plan(&original_tree.leaves[0],&tree.leaves[0],forward_joints,kzsf_cheat_read,NULL,&plan) &&
               kzsf_character_reverse_bind_plan(&plan,&reverse_plan)) {
                repair_plan=plan;reverse_sequence=sequence;reverse_rig=original_rig;reverse_target=target;
                reverse_binding=original_tree.leaves[0].bindings;reverse_target_binding=tree.leaves[0].bindings;ready=1;
            }
        }
        int routed=0;
        _Alignas(16) unsigned char native_poses[2][0x70]={{0}};
        uintptr_t clean_current=(uintptr_t)native_poses[0],clean_previous=(uintptr_t)native_poses[1];
        int repaired_current=0,repaired_previous=0;
        if(ready) {
            ((KzsfCharacterPoseAssign)(base+0x5d1d10))(clean_current,target);
            ((KzsfCharacterPoseCopy)(base+0x5d2fb0))(clean_current,source,7,0);
            ((KzsfCharacterPoseAssign)(base+0x5d1d10))(clean_previous,target);
            ((KzsfCharacterPoseCopy)(base+0x5d2fb0))(clean_previous,previous_source,7,0);
            ready=kzsf_character_repair103_pose(&repair_plan,source,clean_current,kzsf_cheat_read,kzsf_turn_write,NULL,&repaired_current) &&
                kzsf_character_repair103_pose(&repair_plan,previous_source,clean_previous,kzsf_cheat_read,kzsf_turn_write,NULL,&repaired_previous);
        }
        if(ready) {
            ((KzsfCharacterBuildMap)(base+0x5d8dd0))(target,original_rig,&reverse);
            if(reverse.source==target && reverse.destination==original_rig &&
               kzsf_character_reverse_map_prepare(&reverse,clean_current,kzsf_cheat_read,NULL,reverse_joints,reverse_channels,&reverse_view)) {
                _Alignas(16) unsigned char poses[2][0x70]={{0}};
                KzsfCharacterRenderContext context={.source=clean_current,.current=(uintptr_t)poses[0],.previous=(uintptr_t)poses[1]};
                kzsf_character_render_pair(&context,original_rig,&reverse_view,(KzsfCharacterPoseAssign)(base+0x5d1d10),
                    (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
                KzsfCharacterDrawProxy proxy;uintptr_t methods[11];
                if(kzsf_character_reverse_bind_pose(&reverse_plan,clean_current,context.current,kzsf_cheat_read,kzsf_turn_write,NULL) &&
                   kzsf_character_reverse_bind_pose(&reverse_plan,clean_previous,context.previous,kzsf_cheat_read,kzsf_turn_write,NULL) &&
                   kzsf_character_draw_proxy(node,clean_current,clean_previous,kzsf_cheat_read,NULL,&proxy,methods) &&
                   sequence==__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE)) {
                    int adapted=0;
                    for(int i=0;i<draw_count;i++) {
                        int body98=(draw_parts[i].resource==original_body || (compatible98 && draw_parts[i].resource==selected_part)) && (draw_parts[i].tagged&2);
                        proxy.fields[14]=1;proxy.fields[15]=(uintptr_t)&draw_parts[i].tagged;
                        proxy.current=body98 ? context.current : clean_current;
                        proxy.previous=body98 ? context.previous : clean_previous;
                        original((uintptr_t)&proxy,render);adapted+=body98;
                    }
                    routed=1;
                    static unsigned native_sequence;
                    if(__atomic_exchange_n(&native_sequence,sequence,__ATOMIC_RELAXED)!=sequence) {
                        fprintf(stderr,"KILLZONE_CHARACTER_DRAW status=native-engineer-reverse source_bones=103 records=%d reverse98=%d repaired=%d,%d omitted=0 restoring=%d\n",draw_count,adapted,repaired_current,repaired_previous,restoring);fflush(stderr);
                    }
                }
                ((KzsfCharacterPoseAssign)(base+0x5d1d10))(context.previous,0);
                ((KzsfCharacterPoseAssign)(base+0x5d1d10))(context.current,0);
            }
        }
        ((KzsfCharacterPoseAssign)(base+0x5d1d10))(clean_previous,0);
        ((KzsfCharacterPoseAssign)(base+0x5d1d10))(clean_current,0);
        if(forward.channels.data && forward.channels.data!=forward.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(forward.channels.data);
        if(forward.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(forward.joints.data);
        if(reverse.channels.data && reverse.channels.data!=reverse.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(reverse.channels.data);
        if(reverse.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(reverse.joints.data);
        if(routed)return;
        static unsigned refused_sequence;
        if(__atomic_exchange_n(&refused_sequence,sequence,__ATOMIC_RELAXED)!=sequence) {
            fprintf(stderr,"KILLZONE_CHARACTER_DRAW stage=reverse98-refused plan_ready=%d\n",ready);fflush(stderr);
        }
        goto native_draw;
    }
    for(int32_t i=0;i<draw_count;i++)if(selected_head && draw_parts[i].resource==selected_head && (draw_parts[i].tagged&2))draw_parts[i].selected=1;
    static int head_sampled;
    if(!__atomic_exchange_n(&head_sampled,1,__ATOMIC_RELAXED)) {
        uintptr_t world=0;
        if(kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x40,&world,8))
            for(unsigned joint=7;joint<=14;joint++) {
                float matrix[16];
                if(!kzsf_zoom_read(kzsf_cheat_read,NULL,world,(size_t)joint*64,matrix,64))continue;
                fprintf(stderr,"KILLZONE_CHARACTER_HEAD_POSE joint=%u matrix=",joint);
                for(unsigned k=0;k<16;k++)fprintf(stderr,"%s%.9g",k ? "," : "",matrix[k]);
                fputc('\n',stderr);
            }
        fflush(stderr);
    }
    KzsfCharacterNativeMap map={0},view;KzsfCharacterJointMap joints[103];int32_t channels[4];
    ((KzsfCharacterBuildMap)(base+0x5d8dd0))(skeleton,target,&map);
    int drawn=0;
    if(map.source==skeleton && map.destination==target &&
       kzsf_character_render_map_prepare(&map,source,kzsf_cheat_read,NULL,joints,channels,&view)) {
        /* Cache only copied host matrices for one selection epoch. Reacquired
         * resource identities invalidate the cache without retaining resources. */
        static _Thread_local KzsfCharacterBindPlan bind_plan;
        static _Thread_local unsigned bind_sequence;
        static _Thread_local uintptr_t bind_skeleton,bind_resource,bind_source_skeleton,bind_source_resource;
        int bind_ready=bind_sequence==sequence && bind_skeleton==target && bind_source_skeleton==skeleton && bind_resource==tree.leaves[0].bindings;
        if(!bind_ready) {
            KzsfCharacterInventory inventory;KzsfCharacterMeshTree source_tree;int found_source=0;
            if(kzsf_character_inventory_read(selected_owner,kzsf_cheat_read,NULL,&inventory))
                for(int32_t i=0;i<inventory.count && !found_source;i++)if(inventory.variants[i].part && inventory.variants[i].part!=selected_part) {
                    KzsfCharacterPart candidate;KzsfCharacterMeshTree candidate_tree;
                    if(!kzsf_character_part_read(inventory.variants[i].part,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&candidate) ||
                       !kzsf_character_mesh_read(candidate.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&candidate_tree) || candidate_tree.unknown_count)continue;
                    for(int32_t j=0;j<candidate_tree.count;j++)if(candidate_tree.leaves[j].skeleton==skeleton && candidate_tree.leaves[j].binding_count==98) {
                        source_tree=candidate_tree;found_source=1;break;
                    }
                }
            if(found_source && kzsf_character_bind_plan(&source_tree.leaves[0],&tree.leaves[0],joints,kzsf_cheat_read,NULL,&bind_plan)) {
                bind_sequence=sequence;bind_skeleton=target;bind_source_skeleton=skeleton;bind_resource=tree.leaves[0].bindings;
                bind_source_resource=source_tree.leaves[0].bindings;bind_ready=1;
            }
        }
        _Alignas(16) unsigned char poses[2][0x70]={{0}};
        KzsfCharacterRenderContext context={.source=source,.current=(uintptr_t)poses[0],.previous=(uintptr_t)poses[1]};
        kzsf_character_render_pair(&context,target,&view,(KzsfCharacterPoseAssign)(base+0x5d1d10),
            (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
        KzsfCharacterDrawProxy proxy;uintptr_t methods[11];
        if(bind_ready && kzsf_character_bind_pose(&bind_plan,source,context.current,context.previous,kzsf_cheat_read,kzsf_turn_write,NULL) &&
           kzsf_character_draw_proxy(node,context.current,context.previous,kzsf_cheat_read,NULL,&proxy,methods)) {
            /* Preserve record order and use each record's actual rig. Other
             * pieces in this node (including player attachments) retain the
             * native pose instead of receiving the Engineer's 103-bone pose. */
            int adapted=0;
            for(int32_t i=0;i<draw_count;i++) {
                proxy.fields[14]=1;proxy.fields[15]=(uintptr_t)&draw_parts[i].tagged;
                proxy.current=draw_parts[i].selected ? context.current : source;
                proxy.previous=draw_parts[i].selected ? context.previous : previous_source;
                original((uintptr_t)&proxy,render);adapted+=draw_parts[i].selected;
            }
            drawn=1;
            static unsigned reported;
            if(__atomic_exchange_n(&reported,sequence,__ATOMIC_RELAXED)!=sequence) {
                fprintf(stderr,"KILLZONE_CHARACTER_DRAW status=retargeted target_bones=103 scratch_pose=1 bind_pose_corrected=103 source_binding=%llx adapted_parts=%d native_parts=%d\n",(unsigned long long)bind_source_resource,adapted,draw_count-adapted);fflush(stderr);
            }
            static _Thread_local uint64_t last_parts_hash;
            static _Thread_local unsigned part_reports;
            uint64_t parts_hash=(uint64_t)draw_count;
            for(int32_t i=0;i<draw_count;i++)parts_hash=parts_hash*1099511628211ULL^(uint64_t)(draw_parts[i].resource^draw_parts[i].mesh)^draw_parts[i].selected;
            if(parts_hash!=last_parts_hash && part_reports<12) {
                last_parts_hash=parts_hash;part_reports++;
                fprintf(stderr,"KILLZONE_CHARACTER_DRAW_ROUTES sequence=%u",sequence);
                for(int32_t i=0;i<draw_count;i++)fprintf(stderr," part%d=%llx:mesh%llx:type%llx:%s:%s",i,(unsigned long long)draw_parts[i].resource,
                    (unsigned long long)draw_parts[i].mesh,(unsigned long long)(kzsf_observer_baseline_type(draw_parts[i].mesh,base,kzsf_cheat_read,NULL)-base),
                    draw_parts[i].combined ? "combined" : "owned",draw_parts[i].selected ? "engineer" : "native");
                fputc('\n',stderr);fflush(stderr);
            }
        } else {
            static unsigned refused;
            if(__atomic_exchange_n(&refused,sequence,__ATOMIC_RELAXED)!=sequence) {
                fprintf(stderr,"KILLZONE_CHARACTER_DRAW stage=bind-pose-refused plan_ready=%d\n",bind_ready);fflush(stderr);
            }
        }
        ((KzsfCharacterPoseAssign)(base+0x5d1d10))(context.previous,0);
        ((KzsfCharacterPoseAssign)(base+0x5d1d10))(context.current,0);
    } else {
        static int warned;
        if(!__atomic_exchange_n(&warned,1,__ATOMIC_RELAXED)) {
            fprintf(stderr,"KILLZONE_CHARACTER_DRAW stage=map-preparation-refused joints=%d channels=%d\n",map.joints.count,map.channels.count);fflush(stderr);
        }
    }
    if(map.channels.data && map.channels.data!=map.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(map.channels.data);
    if(map.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(map.joints.data);
    if(drawn)return;
native_draw:
    original(node,render);
}
static ABI void kzsf_character_render_visit(uintptr_t node) {
    uintptr_t base=(uintptr_t)image;
    ((void (ABI *)(uintptr_t))(base+0x711ba0))(node);
    unsigned sequence=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE);
    uintptr_t selected_owner=__atomic_load_n(&kzsf_character_render_owner,__ATOMIC_RELAXED);
    uintptr_t selected_part=__atomic_load_n(&kzsf_character_render_part,__ATOMIC_RELAXED);
    int32_t slot=__atomic_load_n(&kzsf_character_render_slot,__ATOMIC_RELAXED);
    if((sequence&1) || !selected_owner || slot<0 ||
       sequence!=__atomic_load_n(&kzsf_character_render_sequence,__ATOMIC_ACQUIRE))return;
    KzsfCharacterRenderContext context;
    if(!kzsf_character_render_context(node,selected_owner,selected_part,slot,kzsf_cheat_read,NULL,&context)) {
        static unsigned misses;
        if(__atomic_add_fetch(&misses,1,__ATOMIC_RELAXED)<=3) {
            uintptr_t model=0,owner=0;
            kzsf_zoom_read(kzsf_cheat_read,NULL,node,0x18,&model,8);
            kzsf_zoom_read(kzsf_cheat_read,NULL,model,0x38,&owner,8);
            fprintf(stderr,"KILLZONE_CHARACTER_RENDER stage=context-miss node=%p model=%p owner=%p selected_owner=%p selected_part=%p slot_hint=%d\n",
                (void*)node,(void*)model,(void*)owner,(void*)selected_owner,(void*)selected_part,slot);fflush(stderr);
        }
        return;
    }
    static int context_seen;
    if(!__atomic_exchange_n(&context_seen,1,__ATOMIC_RELAXED)) {
        fprintf(stderr,"KILLZONE_CHARACTER_RENDER stage=context-matched node=%p model=%p part=%p\n",(void*)node,(void*)context.model,(void*)context.part);fflush(stderr);
    }
    KzsfCharacterPart body;KzsfCharacterMeshTree tree;
    if(!kzsf_character_part_read(context.part,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&body) ||
       !kzsf_character_mesh_read(body.mesh,base,kzsf_cheat_read,NULL,kzsf_observer_baseline_type,&tree) || !tree.count)return;
    uintptr_t source=context.source,source_skeleton=context.source_skeleton,target_skeleton=tree.leaves[0].skeleton;
    int32_t source_count=0,target_count=0,state=0;
    if(!kzsf_zoom_read(kzsf_cheat_read,NULL,source,0x28,&state,4) || state!=3 ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,source_skeleton,0x30,&source_count,4) || source_count!=98 ||
       !kzsf_zoom_read(kzsf_cheat_read,NULL,target_skeleton,0x30,&target_count,4) || target_count!=103) {
        static int pose_warned;
        if(!__atomic_exchange_n(&pose_warned,1,__ATOMIC_RELAXED)) {
            fprintf(stderr,"KILLZONE_CHARACTER_RENDER stage=source-pose-refused state=%d source_count=%d target_count=%d\n",state,source_count,target_count);fflush(stderr);
        }
        return;
    }
    KzsfCharacterNativeMap map={0};
    ((KzsfCharacterBuildMap)(base+0x5d8dd0))(source_skeleton,target_skeleton,&map);
    KzsfCharacterJointMap joints[103];int32_t channels[4];KzsfCharacterNativeMap view;
    int valid=map.source==source_skeleton && map.destination==target_skeleton &&
        kzsf_character_render_map_prepare(&map,source,kzsf_cheat_read,NULL,joints,channels,&view);
    if(valid) {
        kzsf_character_render_pair(&context,target_skeleton,&view,(KzsfCharacterPoseAssign)(base+0x5d1d10),
            (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
        static unsigned reported;
        if(__atomic_exchange_n(&reported,sequence,__ATOMIC_RELAXED)!=sequence) {
            fprintf(stderr,"KILLZONE_CHARACTER_RENDER status=retargeted name=VSA_Engineer source_bones=98 target_bones=103 native_pose=1 node=%p\n",(void*)node);fflush(stderr);
        }
    } else {
        /* Keep a full target-sized pose even if the temporary map is invalid;
         * rendering a103-bone mesh from98 matrices would read past the pose. */
        KzsfCharacterJointMap missing[103];memset(missing,0,sizeof(missing));
        for(unsigned i=0;i<103;i++)missing[i].source=-1;
        KzsfCharacterNativeMap fallback={0};fallback.joints.count=fallback.joints.capacity=103;
        fallback.joints.data=(uintptr_t)missing;
        kzsf_character_render_pair(&context,target_skeleton,&fallback,(KzsfCharacterPoseAssign)(base+0x5d1d10),
            (KzsfCharacterPoseApply)(base+0x5d2ae0),(KzsfCharacterPoseCopy)(base+0x5d2fb0));
        static int warned;
        if(!__atomic_exchange_n(&warned,1,__ATOMIC_RELAXED)) {
            fprintf(stderr,"KILLZONE_CHARACTER_RENDER status=identity-fallback reason=native-map-shape\n");fflush(stderr);
        }
    }
    if(map.channels.data && map.channels.data!=map.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(map.channels.data);
    if(map.joints.data)((KzsfCharacterQueryFree)(base+0x3295d0))(map.joints.data);
}
static LONG CALLBACK vectored_fault(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record=info->ExceptionRecord;
    DWORD code=record->ExceptionCode;
    if(code==EXCEPTION_BREAKPOINT && kzsf_camera_observer_enabled) {
        CONTEXT *c=info->ContextRecord;int kind=-1;
        if(record->ExceptionAddress==image+KZSF_CAMERA_SELECT_ENTRY+1)kind=0;
        else if(record->ExceptionAddress==image+KZSF_CAMERA_BIND_ENTRY+1)kind=1;
        else if(record->ExceptionAddress==image+KZSF_FREE_CAMERA_SITE)kind=2;
        if(kind>=0) {
            unsigned hit=__atomic_add_fetch(&kzsf_camera_observer_hits[kind],1,__ATOMIC_RELAXED);
            if(TryAcquireSRWLockExclusive(&kzsf_camera_observer_lock)) {
                if(kind==2) {
                    kzsf_camera_observe_view(c,hit,0,0);
                } else if(hit<=2) {
                    fprintf(stderr,"KILLZONE_CAMERA_OBSERVER callback=%s hit=%u arg1=%p arg2=%p\n",
                        kind ? "bind" : "select",hit,(void*)c->Rdi,(void*)c->Rsi);fflush(stderr);
                }
                ReleaseSRWLockExclusive(&kzsf_camera_observer_lock);
            }
            if(kzsf_observer_replay(c,(uintptr_t)image,(unsigned)kind,kzsf_cheat_read,NULL))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_free_camera_body_test) {
        CONTEXT *c=info->ContextRecord;
        int kind=-1,changed=0,ok=0;
        int active=__atomic_load_n(&kzsf_free_camera_body_active,__ATOMIC_ACQUIRE);
        if(record->ExceptionAddress==image+KZSF_MODEL_FP_SITE) {
            kind=0;ok=kzsf_model_fp(c,(uintptr_t)image,active,kzsf_cheat_read,NULL,&changed);
        } else if(record->ExceptionAddress==image+KZSF_MODEL_WORLD_SITE) {
            kind=1;
            if(active && __atomic_load_n(&kzsf_third_person_hide_body,__ATOMIC_ACQUIRE))
                kzsf_model_world_hidden(c,(uintptr_t)image,&changed);
            else kzsf_model_world(c,(uintptr_t)image,active,&changed);
            ok=1;
        }
        if(ok) {
            if(changed && __atomic_add_fetch(&kzsf_model_view_events[kind],1,__ATOMIC_RELAXED)==1) {
                fprintf(stderr,"KILLZONE_MODEL_VIEW %s reached during free camera\n",
                    kind ? "native-world-model" : "first-person-suppressed");fflush(stderr);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_free_camera_enabled &&
       record->ExceptionAddress==image+KZSF_FREE_CAMERA_SITE) {
        CONTEXT *c=info->ContextRecord;
        const float *matrix=kzsf_free_camera_update(c,0);
        __atomic_store_n(&kzsf_free_camera_body_active,kzsf_free_camera_body_test && matrix!=NULL,__ATOMIC_RELEASE);
        if(kzsf_free_camera_store(c,(uintptr_t)image,matrix,kzsf_cheat_read,kzsf_turn_write,NULL))
            return EXCEPTION_CONTINUE_EXECUTION;
        kzsf_free_camera.active=0;kzsf_free_input(0);
        __atomic_store_n(&kzsf_free_camera_body_active,0,__ATOMIC_RELEASE);
        if(kzsf_free_camera_store(c,(uintptr_t)image,NULL,kzsf_cheat_read,kzsf_turn_write,NULL))
            return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_camera_controls_enabled) {
        int kind=-1,ok=0;
        float fov=0;
        if(record->ExceptionAddress==image+KZSF_FOV_SITE) {
            kind=0;
            ok=kzsf_camera_fov_store(info->ContextRecord,(uintptr_t)image,
                kzsfgpu_fov_percent(),kzsf_turn_write,NULL,&fov);
        } else if(record->ExceptionAddress==image+KZSF_SHAKE_SITE) {
            kind=1;
            ok=kzsf_camera_shake_result(info->ContextRecord,(uintptr_t)image,
                kzsfgpu_camera_shake(),kzsf_cheat_read,kzsf_turn_write,NULL);
        }
        if(kind>=0 && ok) {
            if(__atomic_add_fetch(&kzsf_camera_events[kind],1,__ATOMIC_RELAXED)==1)
                fprintf(stderr,"KILLZONE_CAMERA %s reached: fov=%d%% shake=%d stored_fov=%.2f\n",
                    kind==0?"FOV":"shake",kzsfgpu_fov_percent(),kzsfgpu_camera_shake(),fov);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_player_turn_enabled) {
        CONTEXT *c=info->ContextRecord;int ok=0,changed=0,kind=-1;
        if(record->ExceptionAddress==image+KZSF_RESPONSE_SITE) {
            kind=0;ok=kzsf_look_response(c,(uintptr_t)image,kzsfgpu_look_curve(),kzsfgpu_look_deadzone(),
                kzsf_cheat_read,kzsf_turn_write,NULL,&changed);
        } else if(record->ExceptionAddress==image+KZSF_RAMP_SITE) {
            kind=1;ok=kzsf_look_ramp(c,(uintptr_t)image,kzsfgpu_look_ramp(),
                kzsf_cheat_read,kzsf_turn_write,NULL,&changed);
        } else if(record->ExceptionAddress==image+KZSF_ASSIST_SITE) {
            kind=2;ok=kzsf_look_assist(c,(uintptr_t)image,kzsfgpu_look_assist(),kzsf_cheat_read,NULL,&changed);
        }
        if(kind>=0) {
            if(ok) {
                if(changed && __atomic_add_fetch(&kzsf_aim_events[kind],1,__ATOMIC_RELAXED)==1)
                    fprintf(stderr,"KILLZONE_AIM_CONTROL active=%s\n",kind==0?"custom-response":kind==1?"ramp-off":"slowdown-tracking-off");
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            fprintf(stderr,"Killzone: aiming control memory access failed\n");
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_player_turn_enabled &&
       record->ExceptionAddress==image+KZSF_SPRINT_SITE) {
        float before,after;
        const int sprinting=(unsigned char)info->ContextRecord->Rax!=0;
        const int percent=kzsfgpu_sprint_turn_speed();
        kzsf_sprint_capture(&kzsf_sprint_scope,info->ContextRecord,(uintptr_t)image,
            kzsf_cheat_read,NULL);
        if(kzsf_sprint_turn(info->ContextRecord,(uintptr_t)image,percent,
            kzsf_cheat_read,kzsf_turn_write,NULL,&before,&after)) {
            if(sprinting && percent!=100) {
                unsigned event=__atomic_add_fetch(&kzsf_sprint_events,1,__ATOMIC_RELAXED);
                if(event==1 || event%512==0)
                    fprintf(stderr,"KILLZONE_SPRINT_TURN event=%u percent=%d heading %.5f -> %.5f\n",
                        event,percent,(double)before,(double)after);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        fprintf(stderr,"Killzone: sprint turn hook memory access failed\n");
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_player_turn_enabled &&
       record->ExceptionAddress==image+KZSF_PLAYER_TURN_SITE) {
        float before[2],after[2];unsigned user;
        const int sprinting=kzsf_sprint_consume(&kzsf_sprint_scope,info->ContextRecord);
        if(kzsf_player_turn(info->ContextRecord,(uintptr_t)image,
            sprinting?100:kzsfgpu_controller_turn_speed(),kzsfgpu_controller_pitch_speed(),
            kzsf_cheat_read,kzsf_turn_write,NULL,before,after,&user)) {
            unsigned event=__atomic_add_fetch(&kzsf_player_turn_events,1,__ATOMIC_RELAXED);
            if(event==1 || event%256==0) {
                fprintf(stderr,"KILLZONE_PLAYER_TURN event=%u user=%u heading_factor %.5f -> %.5f pitch_factor %.5f -> %.5f sprint_independent=%d\n",
                    event,user,(double)before[0],(double)after[0],(double)before[1],(double)after[1],sprinting);
                fflush(stderr);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        fprintf(stderr,"Killzone: player turn hook memory access failed\n");fflush(stderr);
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_prompt_trace_enabled) {
        for(unsigned i=0;i<KZSF_PROMPT_TRACE_SITES;++i) {
            if(record->ExceptionAddress!=image+kzsf_prompt_sites[i].site) continue;
            CONTEXT *c=info->ContextRecord;
            if(i==3) kzsf_prompt_constructor_smoke();
            if(kzsf_prompt_pc_enabled && i==2 && !__atomic_load_n(&kzsf_prompt_pc_state,__ATOMIC_ACQUIRE)) {
                int32_t count=0;
                if(kzsf_prompt_read(kzsf_cheat_read,NULL,c->Rdi,0x18,&count,4) && count==27) {
                    int expected=0;
                    if(__atomic_compare_exchange_n(&kzsf_prompt_pc_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
                        int ready=kzsf_pc_prepare(&kzsf_prompt_pc,(uintptr_t)image,c->Rdi,
                            1,kzsf_cheat_read,NULL,runtime_pad_prompt_binding,
                            kzsf_prompt_native_create,NULL);
                        if(ready)ready=kzsf_pc_prepare(&kzsf_prompt_keyboard,(uintptr_t)image,c->Rdi,
                            0,kzsf_cheat_read,NULL,runtime_pad_prompt_binding,
                            kzsf_prompt_native_create,NULL);
                        __atomic_store_n(&kzsf_prompt_pc_state,ready ? 2 : 3,__ATOMIC_RELEASE);
                        fprintf(stderr,"KILLZONE_PROMPT_PC preparation=%s supported=%04x\n",
                            ready ? "ready" : "refused",kzsf_prompt_pc.supported);fflush(stderr);
                    }
                }
            }
            /* Claim bounded distinct collections and requests; repeated prompts
             * keep replaying the MOV without flooding the log. */
            int collection=0,request=0;
            for(unsigned j=0;j<4;++j) {
                uintptr_t expected=0;
                if(__atomic_compare_exchange_n(&kzsf_prompt_collections[i][j],&expected,
                    (uintptr_t)c->Rdi,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {collection=1;break;}
                if(expected==(uintptr_t)c->Rdi) break;
            }
            uint64_t caller=0;
            kzsf_prompt_read(kzsf_cheat_read,NULL,c->Rsp,8,&caller,8);
            /* The same action may be used by both a contextual widget and a
             * tutorial. Preserve that distinction in the bounded request set. */
            uint64_t key=(((uint64_t)(uint32_t)(caller-(uintptr_t)image)<<32) |
                          (uint32_t)c->Rsi)+1;
            if(!key) key=UINT64_MAX;
            for(unsigned j=0;j<32;++j) {
                uint64_t expected=0;
                if(__atomic_compare_exchange_n(&kzsf_prompt_requests[i][j],&expected,key,
                    0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {request=1;break;}
                if(expected==key) break;
            }
            if(collection || request) {
                kzsf_prompt_trace_dump(stderr,c,(uintptr_t)image,i,collection,kzsf_cheat_read,NULL);
                fflush(stderr);
            }
            if(kzsf_prompt_trace_replay(c,(uintptr_t)image,i,kzsf_cheat_read,NULL))
                return EXCEPTION_CONTINUE_EXECUTION;
            /* Replay failed only if the original MOV's operand is unreadable;
             * let the normal fault reporting terminate instead of retrying INT3. */
            fprintf(stderr,"Killzone: native prompt diagnostic replay failed\n");
            break;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_prompt_pc_enabled) {
        for(unsigned i=0;i<sizeof(kzsf_pc_calls)/sizeof(*kzsf_pc_calls);++i) {
            if(record->ExceptionAddress!=image+kzsf_pc_calls[i]) continue;
            CONTEXT *c=info->ContextRecord;
            if(__atomic_load_n(&kzsf_prompt_pc_state,__ATOMIC_ACQUIRE)==2 && kzsfgpu_pc_prompts_active()) {
                KzsfPromptPc *art=kzsfgpu_mouse_aim_enabled() ? &kzsf_prompt_pc : &kzsf_prompt_keyboard;
                int glyph=kzsf_pc_draw_caller(art,c,kzsf_pc_calls[i]+5,
                    kzsf_cheat_read,kzsf_turn_write,NULL);
                unsigned draws=__atomic_load_n(&art->draws,__ATOMIC_RELAXED);
                if(glyph && (draws==1 || draws%512==0)) {
                    fprintf(stderr,"KILLZONE_PROMPT_PC native draw glyph=%d count=%u\n",glyph-1,draws);
                    fflush(stderr);
                }
            }
            if(kzsf_pc_replay_call(c,(uintptr_t)image,i,kzsf_turn_write,NULL))
                return EXCEPTION_CONTINUE_EXECUTION;
            fprintf(stderr,"Killzone: native PC prompt CALL replay failed\n");break;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_postfx_dump_enabled &&
       record->ExceptionAddress==image+KZSF_POSTFX_SITE) {
        int report=__atomic_fetch_add(&kzsf_postfx_dump_hits,1,__ATOMIC_RELAXED)<3;
        CONTEXT scratch=*info->ContextRecord;
        if(kzsf_postfx_dump(&scratch,(uintptr_t)image,kzsf_cheat_read,NULL,report ? stderr : NULL)) {
            info->ContextRecord->Rdx=scratch.Rdx;
            info->ContextRecord->Rip=scratch.Rip;
            if(report) fflush(stderr);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if(report) fprintf(stderr,"Killzone: postfx dump hook at an unexpected site\n");
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_turn_speed_enabled &&
       (record->ExceptionAddress==image+KZSF_TURN_SITE ||
        record->ExceptionAddress==image+KZSF_PITCH_SITE)) {
        const int pitch=record->ExceptionAddress==image+KZSF_PITCH_SITE;
        float engine_rate;
        memcpy(&engine_rate,&info->ContextRecord->Xmm0,sizeof(engine_rate));
        float applied=0.0f;
        int percent=pitch ? kzsfgpu_controller_pitch_speed() : kzsfgpu_controller_turn_speed();
        if(kzsf_engine_rate_store(info->ContextRecord,(uintptr_t)image,pitch,percent,
                                  kzsf_turn_write,NULL,&applied)) {
            if(kzsf_turn_log)
                kzsf_rate_log(pitch,engine_rate,applied,(unsigned)info->ContextRecord->R14);
            if(!__atomic_exchange_n(&kzsf_turn_speed_reported,1,__ATOMIC_RELAXED)) {
                fprintf(stderr,"Killzone: engine %s-rate hook reached for local user %llu\n",
                        pitch ? "pitch" : "heading",
                        (unsigned long long)info->ContextRecord->R14);
                fflush(stderr);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        fprintf(stderr,"Killzone: engine %s-rate store failed\n",pitch ? "pitch" : "heading");
        fflush(stderr);
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_unlimited_sprint &&
       (record->ExceptionAddress==image+KZSF_STAMINA_SITE_A ||
        record->ExceptionAddress==image+KZSF_STAMINA_SITE_B)) {
        int changed=0;
        if(kzsf_stamina_store(info->ContextRecord,(uintptr_t)image,kzsf_cheat_read,kzsf_turn_write,NULL,&changed)) {
            info->ContextRecord->Rip=(uintptr_t)record->ExceptionAddress+sizeof(kzsf_stamina_original);
            if(changed && !__atomic_exchange_n(&kzsf_sprint_reported,1,__ATOMIC_RELAXED))
                fprintf(stderr,"Killzone: unlimited sprint reached local player stamina store\n");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_continuous_scan &&
       record->ExceptionAddress==image+KZSF_CONTINUOUS_SCAN_SITE) {
        CONTEXT *c=info->ContextRecord;
        if(kzsf_continue_scan(c->Rdi,(uintptr_t)image,kzsf_cheat_read,kzsf_turn_write,NULL) &&
           !__atomic_exchange_n(&kzsf_scan_reported,1,__ATOMIC_RELAXED))
            fprintf(stderr,"Killzone: continuous Tactical Echo reached active local-player scan\n");
        c->R14=c->Rdi; /* replay mov r14,rdi, even for idle or non-player abilities */
        c->Rip=(uintptr_t)image+KZSF_CONTINUOUS_SCAN_SITE+3;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(code==EXCEPTION_BREAKPOINT && kzsf_player_protection &&
       record->ExceptionAddress==image+KZSF_DAMAGE_SITE && handle_kzsf_player_damage(info->ContextRecord))
        return EXCEPTION_CONTINUE_EXECUTION;
    if (code==EXCEPTION_BREAKPOINT && kzsf_health_trace_site &&
            record->ExceptionAddress==kzsf_health_trace_site) {
        trace_kzsf_health(info->ContextRecord);
        info->ContextRecord->Rip=(DWORD64)(uintptr_t)kzsf_health_trace_site+1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code==EXCEPTION_BREAKPOINT && virtual_tsc_site((uintptr_t)record->ExceptionAddress)) {
        uint64_t ticks=runtime_read_tsc();
        info->ContextRecord->Rax=(uint32_t)ticks;
        info->ContextRecord->Rdx=(uint32_t)(ticks>>32);
        info->ContextRecord->Rip=(DWORD64)(uintptr_t)record->ExceptionAddress+2;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code==EXCEPTION_BREAKPOINT && isa_table) {
        /* An int3 ISA site: emulated here; any other breakpoint goes on to the report below. */
        IsaRegs regs;
        uintptr_t rip;
        isa_context_regs(info->ContextRecord,&regs,0);
        if (isa_rewrite_trap(isa_table,(uintptr_t)record->ExceptionAddress,&regs,&rip)) {
            isa_context_regs(info->ContextRecord,&regs,1);
            info->ContextRecord->Rip=(DWORD64)rip;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (code==EXCEPTION_ACCESS_VIOLATION && record->NumberParameters>=2) {
        /* GPU page tracking (write-protected guest pages) is resolved first. */
        if (gpu_enabled && kzsfgpu_handle_fault(info,(void *)record->ExceptionInformation[1])) return EXCEPTION_CONTINUE_EXECUTION;
        if (runtime_fault_recover) {
            /* A speculative guest memory read (GPU draw preparation) faulted: the thread resumes
             * in kzsf_longjmp, outside the exception dispatcher, back at its recovery point. */
            unsigned long long *buffer=*runtime_fault_recover;
            runtime_fault_recover=NULL;
            CONTEXT *c=info->ContextRecord;
            c->Rsp=((c->Rsp-128)&~(DWORD64)15)-40;
            c->Rcx=(DWORD64)(uintptr_t)buffer;
            c->Rdx=1;
            c->Rip=(DWORD64)(uintptr_t)kzsf_longjmp;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_ILLEGAL_INSTRUCTION || code==EXCEPTION_PRIV_INSTRUCTION ||
        code==EXCEPTION_STACK_OVERFLOW || code==EXCEPTION_INT_DIVIDE_BY_ZERO || code==EXCEPTION_DATATYPE_MISALIGNMENT) {
        /* Faults in guest code, the loader or the runtime end the process with a report; drivers
         * and system libraries may handle their own exceptions. */
        uintptr_t rip=(uintptr_t)info->ContextRecord->Rip;
        HMODULE self=GetModuleHandleA(NULL), module=NULL;
        int ours=(image && rip-(uintptr_t)image<0x20000000) ||
                 (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)rip,&module) && module==self) ||
                 !module;
        if (ours) return fatal_exception(info);
    }
    /* KZSF_STRICT_HANDLES=1: a closed or invalid handle used anywhere is reported (diagnostics). */
    if (code==0xC0000008 /* STATUS_INVALID_HANDLE */) return fatal_exception(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI unhandled_fault(EXCEPTION_POINTERS *info) { return fatal_exception(info); }
/* Stall watchdog: after KZSF_STALL_SECONDS, dump every thread's RIP so a hang can be
 * located (guest offsets when the RIP is inside the image). */
/* Print every process thread's RIP (guest offsets when inside the image) and, with
 * KZSF_TRACE_IMPORTS=1, the last HLE import that thread called (read from its TEB slot). */
/* NtQueryInformationThread(ThreadBasicInformation) requires exactly the native struct size
 * (48 bytes on x64: AffinityMask/Priority/BasePriority included) or fails STATUS_INFO_LENGTH_MISMATCH. */
typedef struct { LONG ExitStatus; ULONG Pad; void *TebBaseAddress; void *UniqueProcess; void *UniqueThread;
                 uintptr_t AffinityMask; LONG Priority; LONG BasePriority; } ThreadBasicInfo;
static LONG (NTAPI *query_thread_info)(HANDLE, LONG, PVOID, ULONG, PULONG);
static void dump_thread_rips(void) {
    char where[MAX_PATH+64];
    uint32_t trace_off=runtime_thread_trace_offset();
    if (!query_thread_info)
        query_thread_info=(LONG (NTAPI *)(HANDLE,LONG,PVOID,ULONG,PULONG))(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationThread");
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry; entry.dwSize=sizeof(entry);
    if (snapshot!=INVALID_HANDLE_VALUE && Thread32First(snapshot,&entry)) {
        do {
            if (entry.th32OwnerProcessID!=GetCurrentProcessId()) continue;
            if (entry.th32ThreadID==GetCurrentThreadId()) continue; /* never suspend ourselves */
            HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
            if (!thread) continue;
            CONTEXT context; memset(&context,0,sizeof(context));
            context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
            uint32_t last=0;
            ThreadBasicInfo tbi; memset(&tbi,0,sizeof(tbi));
            if (query_thread_info) query_thread_info(thread,0,&tbi,sizeof(tbi),NULL);
            if (SuspendThread(thread)==(DWORD)-1) { CloseHandle(thread); continue; }
            int captured=GetThreadContext(thread,&context);
            if (captured && tbi.TebBaseAddress)
                last=*(volatile uint32_t *)((char *)tbi.TebBaseAddress+trace_off);
            /* A suspended thread may own stderr or the loader lock. Capture
             * only raw state while it is paused; resume before module lookup,
             * formatting, or logging. Otherwise the diagnostic itself hangs
             * the game (observed with the main thread inside fprintf). */
            ResumeThread(thread);
            CloseHandle(thread);
            if (captured) {
                describe_address(where,sizeof(where),(uintptr_t)context.Rip);
                /* The tracer stores index+1; 0 means the thread never entered a traced import. */
                char lastbuf[160]="(none)";
                if (last && last-1<import_count)
                    snprintf(lastbuf,sizeof(lastbuf),"%s (%s)",names[last-1],runtime_import_name(names[last-1]));
                fprintf(stderr,"  tid=%lu rip=%s rsp=%p rax=%p rdi=%p last=%s\n",
                        (unsigned long)entry.th32ThreadID,where,(void *)context.Rsp,
                        (void *)context.Rax,(void *)context.Rdi,lastbuf);
            }
        } while (Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);
    }
}
/* Freeze watchdog: if no frame is presented for KZSF_FREEZE_SECONDS, dump the thread RIPs
 * (and exit when KZSF_FREEZE_EXIT=1), so a hang reports itself instead of waiting. */
extern unsigned long long kzsfgpu_flip_count(void);
/* GPU-side equeue state (registered events, blocked waiters, recent activity). */
extern void kzsfgpu_equeue_dump(void);
static DWORD WINAPI freeze_watchdog(LPVOID argument) {
    DWORD seconds=(DWORD)(uintptr_t)argument;
    unsigned long long last=kzsfgpu_flip_count();
    unsigned stalled=0;
    for (;;) {
        Sleep(1000);
        unsigned long long now=kzsfgpu_flip_count();
        if (now!=last) { last=now; stalled=0; continue; }
        if (++stalled<seconds) continue;
        fprintf(stderr,"STOP: no frame presented for %lu s (frozen); hle_seq=%u; thread RIPs:\n",
                (unsigned long)seconds,(unsigned)hle_seq);
        dump_thread_rips();
        runtime_sema_dump(); /* blocked waiters + the semaphore activity just before the stall */
        if (gpu_enabled) kzsfgpu_equeue_dump(); /* GPU/kernel event-queue waiters and last events */
        runtime_report(); /* includes files opened/reads: tells a stalled load from a stalled dialog */
        if (trace_imports && hle_seq) {
            fprintf(stderr,"  recent HLE calls (oldest first):\n");
            uint32_t total=hle_seq, first=total>4095 ? total-4095 : 0; unsigned shown=0;
            for (uint32_t s=first;s<total && shown<40;++s) {
                uint32_t index=hle_ring[s&4095];
                if (index>=import_count) continue;
                if (!strncmp(names[index],"T72hz6ffq08",11)) continue; /* yield */
                fprintf(stderr,"    %s\n",names[index]); ++shown;
            }
        }
        fflush(NULL);
        stalled=0;
        if (getenv("KZSF_FREEZE_EXIT")) TerminateProcess(GetCurrentProcess(),4);
    }
}
/* Reports every KZSF_STALL_SECONDS (repeat), so a hang that starts after the first interval is
 * still captured. KZSF_STALL_ONCE=1 restores the old single report. Set KZSF_STALL_SECONDS=0 to
 * disable. Frames are still presented during a loading hang, so the freeze watchdog never fires. */
static DWORD WINAPI stall_watchdog(LPVOID argument) {
    DWORD seconds=(DWORD)(uintptr_t)argument;
    if (!seconds) return 0;
    unsigned reports=0;
    for (;;) {
        Sleep(seconds*1000);
        ++reports;
        fprintf(stderr,"STOP: stall watchdog report %u after %lu s; hle_seq=%u trace_imports=%d; pad reads=%u gamepad=%s; thread RIPs:\n",
                reports,(unsigned long)seconds,(unsigned)hle_seq,trace_imports,runtime_pad_reads(),runtime_pad_name());
        dump_thread_rips();
        runtime_sema_dump();
        if (gpu_enabled) kzsfgpu_equeue_dump();
        runtime_report();
        uint32_t total=hle_seq;
        if (total) {
            fprintf(stderr,"  recent HLE calls (excluding yield, oldest first):\n");
            uint32_t first=total>4095 ? total-4095 : 0;
            unsigned shown=0;
            for (uint32_t s=first;s<total && shown<160;++s) {
                uint32_t index=hle_ring[s&4095];
                if (index>=import_count) continue;
                if (!strncmp(names[index],"T72hz6ffq08",11)) continue; /* scePthreadYield */
                fprintf(stderr,"    %s\n",names[index]);
                ++shown;
            }
        }
        fflush(NULL);
        if (getenv("KZSF_STALL_ONCE")) return 0;
    }
}
/* Guest code reads its TCB with `mov rax, gs:[0]` (link_*.py rewrote fs:[0]); on Windows
 * the displacement becomes the TEB TLS slot that holds the guest TCB (runtime_thread.c). */
uint32_t runtime_thread_tcb_offset(void);
/* The guest converts its inline rdtsc to seconds with a constant baked for the retail PS4 TSC
 * (~1.594 GHz). The guest runs natively, so rdtsc is this host's TSC (typically 3-5 GHz) and any
 * time-derived gameplay runs too fast. Rewrite that constant to 1/host-TSC. Only the known value
 * is touched; KZSF_TSC_HZ overrides the measured frequency. */
#define KZSF_PS4_TSC_SECONDS 6.273525587907613e-10
static uint64_t patch_guest_clock(unsigned char *img, uint64_t size, Segment *segments, uint64_t ns) {
    const char *disabled=getenv("KZSF_ORIGINAL_CLOCK");
    if (disabled && !strcmp(disabled,"1")) {
        printf("Guest clock: original constants retained (recovery baseline)\n");
        return 0;
    }
    uint64_t hz=0;
    const char *env=getenv("KZSF_TSC_HZ");
    if (env) hz=strtoull(env,NULL,10);
    if (hz<100000000ull || hz>20000000000ull) {
        uint64_t t0=__builtin_ia32_rdtsc(), n0=runtime_process_time_counter();
#ifdef _WIN32
        Sleep(25);
#else
        usleep(25000);
#endif
        uint64_t t1=__builtin_ia32_rdtsc(), n1=runtime_process_time_counter();
        if (n1>n0) hz=(uint64_t)((double)(t1-t0)*1e9/(double)(n1-n0));
    }
    if (hz<100000000ull || hz>20000000000ull) {
        printf("Guest clock: host TSC frequency not measurable; left unchanged\n");
        return 0;
    }
    const char *virtual_clock=getenv("KZSF_VIRTUAL_TSC");
    if (virtual_clock && !strcmp(virtual_clock,"1")) {
        size_t count=sizeof(virtual_tsc_sites)/sizeof(*virtual_tsc_sites);
        /* All guards pass before the image or clock state is changed. */
        for (size_t i=0;i<count;++i) {
            uint64_t at=virtual_tsc_sites[i];
            if (at+2>size || img[at]!=0x0f || img[at+1]!=0x31)
                fail("virtual clock instruction signature mismatch");
        }
        runtime_set_virtual_tsc(hz);
        const char *direct=getenv("KZSF_DIRECT_TSC");
        if(direct && direct[0]=='1') {
            unsigned char *area=allocate(TSC_PATCH_AREA); size_t used=0;
            unsigned moved=0,windows=0;
            for(uint64_t s=0;s<ns;++s) if(segments[s].flags&1) {
                TscReport r=tsc_patch_segment(img,segments[s].address,segments[s].size,
                    area,&used,runtime_tsc_multiplier(),virtual_tsc_sites,count);
                moved+=r.moved+r.windows; windows+=r.windows;
            }
            protect(area,TSC_PATCH_AREA,5);
            size_t fallback=0;
            for(size_t i=0;i<count;++i) {
                uint64_t at=virtual_tsc_sites[i];
                if(img[at]==0x0f && img[at+1]==0x31) { img[at]=0xcc; img[at+1]=0x90; ++fallback; }
            }
            printf("Guest clock: direct PS4 TSC: %u sites in %u windows, %zu exception fallbacks, %zu stub bytes; shared multiplier %llu\n",
                moved,windows,fallback,used,(unsigned long long)*runtime_tsc_multiplier());
        } else {
            for (size_t i=0;i<count;++i) { img[virtual_tsc_sites[i]]=0xcc; img[virtual_tsc_sites[i]+1]=0x90; }
        }
        virtual_tsc_enabled=1;
        printf("Guest clock: virtual PS4 TSC at 1594000000 Hz; %zu instruction sites; original constants retained\n",count);
        return count;
    }
    double want=1.0/(double)hz, from=KZSF_PS4_TSC_SECONDS;
    const char *unified=getenv("KZSF_UNIFIED_TSC");
    if (unified && !strcmp(unified,"1")) {
        runtime_set_native_tsc(hz);
        printf("Guest clock: kernel TSC and inline RDTSC share native counter at %llu Hz\n",
               (unsigned long long)hz);
    }
    const char *deadline_fix=getenv("KZSF_DIALOG_TIMING");
    if (deadline_fix && !strcmp(deadline_fix,"1")) {
        int count=kzsf_timing_deadlines(img,(size_t)size,(double)hz);
        printf("KZSF dialogue timing experiment: corrected %d deadline/microsecond constants%s\n",
               count,count ? "" : " (signature mismatch; skipped)");
    }
    uint64_t patched=0;
    for (uint64_t at=0; at+sizeof(double)<=size; ++at)
        if (!memcmp(img+at,&from,sizeof(double))) { memcpy(img+at,&want,sizeof(double)); ++patched; }
    printf("Guest clock: host TSC %llu Hz; patched %llu TSC->seconds constant(s)\n",
           (unsigned long long)hz,(unsigned long long)patched);
    return patched;
}
static uint64_t patch_tcb_loads(const Segment *segments, uint64_t count) {
    static const unsigned char load[]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0};
    uint32_t displacement=runtime_thread_tcb_offset();
    uint64_t patched=0;
    for (uint64_t s=0;s<count;++s) {
        if (!(segments[s].flags&1) || segments[s].size<sizeof(load)) continue;
        unsigned char *at=image+segments[s].address, *end=at+segments[s].size-sizeof(load);
        for (; at<=end; ++at)
            if (at[0]==0x65 && !memcmp(at,load,sizeof(load))) { memcpy(at+5,&displacement,4); ++patched; at+=sizeof(load)-1; }
    }
    return patched;
}
#endif
#ifndef _WIN32
/* ucontext gregs of IsaRegs.gpr (x86 numbering). */
static const int isa_gregs[16]={REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSP,REG_RBP,REG_RSI,REG_RDI,
                                REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15};
static void fault(int sig, siginfo_t *info, void *context) {
    if (sig == SIGTRAP && isa_table) {
        /* An int3 ISA site (RIP is past the CC byte); any other trap ends in the report below. */
        greg_t *g = ((ucontext_t *)context)->uc_mcontext.gregs;
        IsaRegs regs;
        uintptr_t rip;
        for (int i = 0; i < 16; ++i) regs.gpr[i] = (uint64_t)g[isa_gregs[i]];
        regs.rflags = (uint64_t)g[REG_EFL];
        if (isa_rewrite_trap(isa_table, (uintptr_t)g[REG_RIP] - 1, &regs, &rip)) {
            for (int i = 0; i < 16; ++i) g[isa_gregs[i]] = (greg_t)regs.gpr[i];
            g[REG_EFL] = (greg_t)regs.rflags;
            g[REG_RIP] = (greg_t)rip;
            return;
        }
    }
    /* GPU page tracking (write-protected guest pages) is resolved first. */
    if (gpu_enabled && sig == SIGSEGV && kzsfgpu_handle_fault(context, info->si_addr)) return;
    /* A speculative guest memory read (runtime_memory.c) failed: resume its recovery point. */
    if ((sig == SIGSEGV || sig == SIGBUS) && runtime_fault_recover) {
        sigjmp_buf *recover = runtime_fault_recover;
        runtime_fault_recover = NULL;
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
        siglongjmp(*recover, 1);
    }
    /* The process is terminating: dladdr/snprintf are acceptable here. */
    ucontext_t *uc = context;
    uintptr_t rip = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    char line[512];
    Dl_info where;
    int site = isa_table ? isa_rewrite_site_of_stub(isa_table, rip) : -1;
    char emulating[64] = "";
    if (site >= 0)
        snprintf(emulating, sizeof(emulating), " While emulating site 0x%" PRIx64 " (%s)",
                 isa_table->sites[site].offset, isa_kind_name(isa_table->sites[site].kind));
    if (site >= 0 || rip - (uintptr_t)image < 0x10000000)
        snprintf(line, sizeof(line), "Guest fault (signal %d) at guest offset 0x%lx, address %p%s\n",
                 sig, (unsigned long)(rip - (uintptr_t)image), info->si_addr, emulating);
    else if (dladdr((void *)rip, &where) && where.dli_fname)
        snprintf(line, sizeof(line), "Host fault (signal %d) in %s+0x%lx (%s), address %p\n", sig, where.dli_fname,
                 (unsigned long)(rip - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?", info->si_addr);
    else
        snprintf(line, sizeof(line), "Fault (signal %d) at RIP %p, address %p\n", sig, (void *)rip, info->si_addr);
    { ssize_t written_=write(2, line, strlen(line)); (void)written_; }
    if (gpu_enabled) kzsfgpu_dump_guest_writes(context);
    /* Host call chain (frames with unwind info; guest frames end it). */
    void *frames[32];
    int depth = backtrace(frames, 32);
    for (int i = 2; i < depth; ++i) {
        if (dladdr(frames[i], &where) && where.dli_fname)
            snprintf(line, sizeof(line), "  #%d %s+0x%lx (%s)\n", i, where.dli_fname,
                     (unsigned long)((uintptr_t)frames[i] - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?");
        else
            snprintf(line, sizeof(line), "  #%d %p\n", i, frames[i]);
        ssize_t written_=write(2, line, strlen(line)); (void)written_;
    }
    if (isa_table) { isa_rewrite_report(isa_table, stderr); fflush(stderr); }
    _exit(128 + sig);
}
#endif
#ifndef _WIN32
/* Watchdog: dump RIP and the rbp frame chain of every thread (guest offsets
 * when inside the image). Reads use process_vm_readv so bad frames cannot fault. */
static uintptr_t exe_base;
static void write_hex(char *out, uint64_t v) {
    const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) out[i] = digits[(v >> (60 - i * 4)) & 15];
}
static void dump_frames(ucontext_t *uc) {
    char line[] = "  tid=0000000000000000 rip=0000000000000000 image-relative=0000000000000000 host-relative=0000000000000000\n";
    uintptr_t rip=(uintptr_t)uc->uc_mcontext.gregs[REG_RIP], rbp=(uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
    uint64_t tid=(uint64_t)gettid();
    /* First argument register: the lock address when a thread waits on a futex. */
    char arg[]="  tid=0000000000000000 rdi=0000000000000000\n";
    write_hex(arg+6,tid); write_hex(arg+27,(uint64_t)uc->uc_mcontext.gregs[REG_RDI]);
    { ssize_t written_=write(2,arg,sizeof(arg)-1); (void)written_; }
    for (int depth=0; depth<24; ++depth) {
        write_hex(line+6,tid); write_hex(line+27,rip); write_hex(line+59,rip-(uintptr_t)image); write_hex(line+90,rip-exe_base);
        { ssize_t written_=write(2,line,sizeof(line)-1); (void)written_; }
        uintptr_t frame[2];
        struct iovec local={frame,sizeof(frame)}, remote={(void *)rbp,sizeof(frame)};
        if (!rbp || process_vm_readv(getpid(),&local,1,&remote,1,0)!=(ssize_t)sizeof(frame)) break;
        if (frame[0]<=rbp) break;
        rbp=frame[0]; rip=frame[1];
    }
}
static void thread_dump(int sig, siginfo_t *info, void *context) { (void)sig; (void)info; dump_frames(context); }
static void watchdog(int sig, siginfo_t *info, void *context) {
    (void)info;
    const char head[]="STOP: watchdog timeout; thread stacks:\n";
    { ssize_t written_=write(2,head,sizeof(head)-1); (void)written_; }
    dump_frames(context);
    int dir=open("/proc/self/task",O_RDONLY|O_DIRECTORY);
    char buffer[4096];
    long n;
    pid_t self=gettid();
    while (dir>=0 && (n=syscall(SYS_getdents64,dir,buffer,sizeof(buffer)))>0)
        for (long at=0; at<n;) {
            struct { uint64_t ino; int64_t off; unsigned short reclen; unsigned char type; char name[]; } *d=(void *)(buffer+at);
            pid_t tid=(pid_t)strtol(d->name,NULL,10);
            if (tid>0 && tid!=self) { syscall(SYS_tgkill,getpid(),tid,SIGUSR2); usleep(20000); }
            at+=d->reclen;
        }
    usleep(100000);
    _exit(128 + sig);
}
#endif
/* param.sfo lookup: string or integer value of key, 0 when absent. */
static int sfo_value(const char *path, const char *key, char *text, size_t text_size, uint32_t *number) {
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    unsigned char data[65536];
    size_t n=fread(data,1,sizeof(data),f); fclose(f);
    if (n<20 || memcmp(data,"\0PSF",4)) return 0;
    uint32_t keys, values, count;
    memcpy(&keys,data+8,4); memcpy(&values,data+12,4); memcpy(&count,data+16,4);
    for (uint32_t i=0;i<count && 20+i*16+16<=n;++i) {
        const unsigned char *e=data+20+i*16;
        uint16_t key_offset, format; uint32_t length, offset;
        memcpy(&key_offset,e,2); memcpy(&format,e+2,2); memcpy(&length,e+4,4); memcpy(&offset,e+12,4);
        if (keys+key_offset>=n || values+offset+length>n || strcmp((const char *)data+keys+key_offset,key)) continue;
        if (format==0x0404 && number && length>=4) { memcpy(number,data+values+offset,4); return 1; }
        if (text && text_size) {
            size_t copy=length<text_size-1 ? length : text_size-1;
            memcpy(text,data+values+offset,copy); text[copy]=0;
        }
        return 1;
    }
    return 0;
}
static int mapped(Segment *segments, uint64_t count, uint64_t address, uint64_t bytes) {
    for (uint64_t i = 0; i < count; ++i)
        if (address >= segments[i].address && bytes <= segments[i].size &&
            address - segments[i].address <= segments[i].size - bytes) return 1;
    return 0;
}
extern ABI float kzsf_orbs_gain(void *player, float requested);
extern void kzsf_orbs_status(int supported);
extern uintptr_t kzsf_cheat_flag(unsigned id);
extern uintptr_t kzsf_actor_slots_address(void);
extern void kzsf_actor_status(int supported);
extern void kzsf_cheat_status(unsigned id);
extern uintptr_t kzsf_damage_multiplier(int taken);
extern uintptr_t kzsf_orb_pickup_multiplier(unsigned kind);
extern void kzsf_stat_multipliers_status(int damage, int orb_pickup);

/* The hooks below find their sites by signature (src/signature.h), so they work in any region
 * and version of the game whose code matches; a site that is not found leaves its feature off. */
static void install_cheats(Segment *segments, uint64_t count, uint64_t size) {
    const char *names[] = {"health", "magic", "item meter", "Rage of Sparta", "red orbs",
                           "Very Hard unlock", "infinite double jumps"};
    uint64_t at[KZSF_CHEAT_HOOK_COUNT];
    for (unsigned i = 0; i < KZSF_CHEAT_HOOK_COUNT; ++i)
        at[i] = kzsf_signature_find(image, size, &kzsf_cheat_hooks[i].site);
    for (unsigned id = 0; id < KZSF_CHEAT_COUNT; ++id) {
        int valid = 1;
        for (unsigned i = 0; i < KZSF_CHEAT_HOOK_COUNT; ++i) {
            const KzsfCheatHook *hook = &kzsf_cheat_hooks[i];
            if (hook->id != id) continue;
            if (at[i] == UINT64_MAX || !mapped(segments, count, at[i], hook->length) ||
                !kzsf_cheat_signature_match(hook, image + at[i], size - at[i])) valid = 0;
        }
        if (!valid) {
            printf("Cheats: %s unavailable (code not found in this eboot.bin)\n", names[id]);
            continue;
        }
        unsigned char *stubs = allocate(page_size);
        int32_t relative[KZSF_CHEAT_HOOK_COUNT] = {0};
        for (unsigned i = 0; i < KZSF_CHEAT_HOOK_COUNT; ++i) {
            const KzsfCheatHook *hook = &kzsf_cheat_hooks[i];
            if (hook->id != id) continue;
            unsigned char *stub = stubs + i * 256;
            int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + at[i] + 5);
            if (delta < INT32_MIN || delta > INT32_MAX) { valid = 0; break; }
            relative[i] = (int32_t)delta;
            kzsf_cheat_build_stub(stub, hook, kzsf_cheat_flag(id),
                                 (uintptr_t)(image + at[i] + hook->length));
        }
        if (!valid) {
            printf("Cheats: %s unavailable (hook outside relative-jump range)\n", names[id]);
            continue;
        }
        protect(stubs, page_size, 5);
        for (unsigned i = 0; i < KZSF_CHEAT_HOOK_COUNT; ++i) {
            const KzsfCheatHook *hook = &kzsf_cheat_hooks[i];
            if (hook->id != id) continue;
            image[at[i]] = 0xe9;
            memcpy(image + at[i] + 1, &relative[i], 4);
            memset(image + at[i] + 5, 0x90, hook->length - 5);
        }
        kzsf_cheat_status(id);
        printf("Cheats: %s ready (live toggle, initially disabled unless saved)\n", names[id]);
    }
}

/* Damage hook for the enemy health bar (src/actor_hook.h). Always
 * installed when the code matches: it only records values; the overlay decides what to show. */
static void install_actor_watch(Segment *segments, uint64_t count, uint64_t size) {
    const uint64_t at = kzsf_signature_find(image, size, &kzsf_actor_site);
    if (at == UINT64_MAX || !mapped(segments, count, at, KZSF_ACTOR_HOOK_LENGTH) ||
        !kzsf_actor_signature_match(image + at, size - at)) {
        printf("Health display: unavailable (code not found in this eboot.bin)\n");
        return;
    }
    unsigned char *stub = allocate(page_size);
    const int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + at + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        printf("Health display: unavailable (hook outside relative-jump range)\n");
        return;
    }
    kzsf_actor_build_stub(stub, kzsf_actor_slots_address(), (uintptr_t)(image + at + KZSF_ACTOR_HOOK_LENGTH));
    protect(stub, page_size, 5);
    const int32_t relative = (int32_t)delta;
    image[at] = 0xe9;
    memcpy(image + at + 1, &relative, 4);
    memset(image + at + 5, 0x90, KZSF_ACTOR_HOOK_LENGTH - 5);
    kzsf_actor_status(1);
    printf("Health display: damage hook ready (enemy health bar in the menu)\n");
}

static void install_orb_multiplier(Segment *segments, uint64_t count, uint64_t size) {
    uint64_t calls[3];
    const uint64_t setter_at = kzsf_orb_find(image, size, calls);
    if (setter_at == UINT64_MAX) {
        printf("Red orbs: multiplier unavailable (code not found in this eboot.bin)\n");
        return;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!mapped(segments, count, calls[i], 5)) return;
    }
    /* SysV call sites: preserve the receiver across the helper, then tail-call the original
     * setter. Its cap and upgrade notifications remain in the game's own code. */
    unsigned char *stub = allocate(page_size);
    uintptr_t helper = (uintptr_t)kzsf_orbs_gain, setter = (uintptr_t)(image + setter_at);
    unsigned char code[26];
    kzsf_orb_build_stub(code, helper, setter);
    int32_t displacements[3];
    for (unsigned i = 0; i < 3; ++i) {
        int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + calls[i] + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) {
            printf("Red orbs: multiplier unavailable (hook outside relative-call range)\n");
            return;
        }
        displacements[i] = (int32_t)delta;
    }
    memcpy(stub, code, sizeof(code));
    protect(stub, page_size, 5);
    for (unsigned i = 0; i < 3; ++i)
        memcpy(image + calls[i] + 1, &displacements[i], 4);
    kzsf_orbs_status(1);
    printf("Red orbs: live multiplier ready (3 validated gain paths, 0.1x to 100x)\n");
}
/* Writes a jump from image+at to `stub`; the rest of the replaced bytes become nops (the stub
 * replays them). 0 when the stub is out of relative-jump range. */
static int install_jump(uint64_t at, unsigned length, unsigned char *stub) {
    const int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + at + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return 0;
    const int32_t relative = (int32_t)delta;
    image[at] = 0xe9;
    memcpy(image + at + 1, &relative, 4);
    memset(image + at + 5, 0x90, length - 5);
    return 1;
}

/* Damage dealt/taken and green/blue/gold orb multipliers (src/damage_hook.h, src/orb_hook.h):
 * the stubs read the menu's floats directly, so changes apply on the next hit or pickup. */
static void install_kzsf_cheats(Segment *segments, uint64_t count, uint64_t size) {
    const char *health=getenv("KZSF_UNLIMITED_HEALTH"), *ammo=getenv("KZSF_INFINITE_AMMO");
#ifdef _WIN32
    const char *sprint=getenv("KZSF_UNLIMITED_SPRINT"), *scan=getenv("KZSF_CONTINUOUS_SCAN");
    if(sprint && sprint[0]=='1') {
        const size_t sites[]={KZSF_STAMINA_SITE_A,KZSF_STAMINA_SITE_B};
        for(unsigned i=0;i<2;++i)
            if(!mapped(segments,count,sites[i],sizeof(kzsf_stamina_original)) ||
               !kzsf_patch_match(image,size,sites[i],kzsf_stamina_original,sizeof(kzsf_stamina_original)))
                fail("Killzone stamina signature mismatch; refusing to patch this build");
        kzsf_unlimited_sprint=1;
        for(unsigned i=0;i<2;++i)image[sites[i]]=0xcc;
        printf("Killzone: unlimited sprint enabled (local-player stamina, experimental)\n");
    }
    if(scan && scan[0]=='1') {
        if(!mapped(segments,count,KZSF_CONTINUOUS_SCAN_SITE,sizeof(kzsf_scan_original)) ||
           !kzsf_patch_match(image,size,KZSF_CONTINUOUS_SCAN_SITE,kzsf_scan_original,sizeof(kzsf_scan_original)))
            fail("Killzone Tactical Echo signature mismatch; refusing to patch this build");
        kzsf_continuous_scan=1;
        image[KZSF_CONTINUOUS_SCAN_SITE]=0xcc;
        printf("Killzone: continuous Tactical Echo enabled; activate scan once after loading (experimental)\n");
    }
#endif
    if (health && health[0]=='1') {
#ifdef _WIN32
        const char *capture=getenv("KZSF_HEALTH_TRACE");
        if(!capture || capture[0]!='1') {
            if(!mapped(segments,count,KZSF_DAMAGE_SITE,sizeof(kzsf_damage_original)) ||
               !kzsf_patch_match(image,size,KZSF_DAMAGE_SITE,kzsf_damage_original,sizeof(kzsf_damage_original)))
                fail("Killzone player damage signature mismatch; refusing to patch this build");
            kzsf_player_protection=1;
            image[KZSF_DAMAGE_SITE]=0xcc;
            printf("Killzone: player invulnerability enabled (engine IsPlayer, damage-handler v2)\n");
        } else {
#endif
        if (!mapped(segments,count,KZSF_HEALTH_SITE,sizeof(kzsf_health_original)) ||
            !kzsf_patch_match(image,size,KZSF_HEALTH_SITE,kzsf_health_original,sizeof(kzsf_health_original)))
            fail("Killzone health patch signature mismatch; refusing to patch this build");
        unsigned char *stub=allocate(page_size);
        size_t prefix=0;
#ifdef _WIN32
        const char *trace=getenv("KZSF_HEALTH_TRACE");
        if(trace && trace[0]=='1') { prefix=1; stub[0]=0xcc; kzsf_health_trace_site=stub; }
#endif
        if (!kzsf_health_stub(stub+prefix,page_size-prefix,(uintptr_t)(image+KZSF_HEALTH_SITE+8)))
            fail("Killzone health hook outside jump range");
        protect(stub,page_size,5);
        if (!install_jump(KZSF_HEALTH_SITE,8,stub)) fail("Killzone health hook outside jump range");
        printf("Killzone: unlimited player health enabled (validated 01.81 hook)\n");
#ifdef _WIN32
        }
#endif
    }
    if (ammo && ammo[0]=='1') {
        if (!mapped(segments,count,KZSF_RESERVE_SITE,sizeof(kzsf_reserve_original)) ||
            !mapped(segments,count,KZSF_MAGAZINE_SITE,sizeof(kzsf_magazine_original)) ||
            !kzsf_patch_match(image,size,KZSF_RESERVE_SITE,kzsf_reserve_original,sizeof(kzsf_reserve_original)) ||
            !kzsf_patch_match(image,size,KZSF_MAGAZINE_SITE,kzsf_magazine_original,sizeof(kzsf_magazine_original)))
            fail("Killzone ammo patch signature mismatch; refusing to patch this build");
        memset(image+KZSF_RESERVE_SITE,0x90,4);
        memset(image+KZSF_MAGAZINE_SITE,0x90,4);
        printf("Killzone: infinite ammo / no reload enabled (validated 01.81 sites)\n");
    }
}
static void install_stat_multipliers(Segment *segments, uint64_t count, uint64_t size) {
    const uint64_t damage_at = kzsf_signature_find(image, size, &kzsf_damage_site);
    const uint64_t pickup_at = kzsf_signature_find(image, size, &kzsf_orb_pickup_site);
    int damage = damage_at != UINT64_MAX && mapped(segments, count, damage_at, KZSF_DAMAGE_HOOK_LENGTH) &&
                 kzsf_damage_signature_match(image + damage_at, size - damage_at);
    int pickup = pickup_at != UINT64_MAX && mapped(segments, count, pickup_at, KZSF_ORB_PICKUP_LENGTH) &&
                 kzsf_orb_pickup_signature_match(image + pickup_at, size - pickup_at);
    unsigned char *stubs = damage || pickup ? allocate(page_size) : NULL;
    if (damage) {
        kzsf_damage_build_stub(stubs, kzsf_damage_multiplier(1), kzsf_damage_multiplier(0),
                               (uintptr_t)(image + damage_at + KZSF_DAMAGE_HOOK_LENGTH));
    }
    if (pickup) {
        kzsf_orb_pickup_build_stub(stubs + 256, kzsf_orb_pickup_multiplier(0), kzsf_orb_pickup_multiplier(1),
                                   kzsf_orb_pickup_multiplier(2),
                                   (uintptr_t)(image + pickup_at + KZSF_ORB_PICKUP_LENGTH));
    }
    if (stubs) protect(stubs, page_size, 5);
    damage = damage && install_jump(damage_at, KZSF_DAMAGE_HOOK_LENGTH, stubs);
    pickup = pickup && install_jump(pickup_at, KZSF_ORB_PICKUP_LENGTH, stubs + 256);
    kzsf_stat_multipliers_status(damage, pickup);
    printf("Multipliers: damage %s, green/blue/gold orbs %s\n", damage ? "ready" : "unavailable",
           pickup ? "ready" : "unavailable");
}
/* G3PATCH2 (patches.py): the patches' image base, then byte writes at image offsets, applied
 * after relocation. A write may replace a whole base-relative pointer slot (60/90 FPS++ swap
 * function pointers): the patch holds the address at the patches' base, rebased here. */
static void apply_patches(const char *path, Segment *segments, uint64_t ns, const Reloc *relocs, uint64_t nr) {
    FILE *f=fopen(path,"rb");
    char magic[8];
    if (!f || fread(magic,1,8,f)!=8 || memcmp(magic,"G3PATCH2",8)) fail("invalid patch file");
    uint64_t base=read64(f), count=read64(f), bytes=0, rebased=0;
    unsigned char data[4096];
    for (uint64_t i=0;i<count;++i) {
        uint64_t offset=read64(f), length=read64(f);
        if (!length || length>sizeof(data) || !mapped(segments,ns,offset,length) || fread(data,1,length,f)!=length)
            fail("bad patch entry");
        uint64_t slots[sizeof(data)/8]; size_t nslots=0;
        for (uint64_t r=0;r<nr;++r) {
            if (!(relocs[r].target<offset+length && offset<relocs[r].target+8)) continue;
            uint64_t target=relocs[r].target, value;
            if (relocs[r].kind || target<offset || target+8>offset+length) fail("patch overlaps a relocation");
            memcpy(&value,data+(target-offset),8);
            if (value<base || !mapped(segments,ns,value-base,1)) fail("patch writes a pointer outside the image");
            slots[nslots++]=target;
        }
        memcpy(image+offset,data,length);
        for (size_t s=0;s<nslots;++s) {
            uint64_t value;
            memcpy(&value,image+slots[s],8);
            value=(uint64_t)(uintptr_t)image+(value-base);
            memcpy(image+slots[s],&value,8);
        }
        rebased+=nslots;
        bytes+=length;
    }
    if (fgetc(f)!=EOF) fail("trailing data in patch file");
    fclose(f);
    printf("Patches: %" PRIu64 " writes, %" PRIu64 " bytes applied, %" PRIu64 " pointers rebased\n",count,bytes,rebased);
}
/* Restarts the game through run.sh (the settings menu: a new render resolution is a patch
 * applied at start). Descriptors are closed first so the old GPU device and its memory are
 * released before the new process opens its own. */
void runtime_restart(void) {
    fflush(NULL);
#ifdef _WIN32
    /* run.py sets KZSF_RESTART_COMMAND: the launch command line, started again (new patches). */
    const char *command=getenv("KZSF_RESTART_COMMAND");
    puts("Runtime: restarting through run.py");
    if (!command || !*command) { fputs("runtime_restart: KZSF_RESTART_COMMAND is not set\n",stderr); _exit(1); }
    char *line=_strdup(command);
    /* Same output handles: a launcher reading the game's log keeps reading the new launch. */
    STARTUPINFOA startup={.cb=sizeof(startup),.dwFlags=STARTF_USESTDHANDLES,
                          .hStdInput=GetStdHandle(STD_INPUT_HANDLE),.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE),
                          .hStdError=GetStdHandle(STD_ERROR_HANDLE)};
    PROCESS_INFORMATION process;
    if (!line || !CreateProcessA(NULL,line,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process)) {
        fprintf(stderr,"runtime_restart: CreateProcess failed (%lu)\n",GetLastError()); _exit(1);
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    /* The new launch waits for this process's GPU device to go away (run.py --after). */
    TerminateProcess(GetCurrentProcess(),0);
#else
    const char *command = getenv("KZSF_RESTART_COMMAND");
    syscall(SYS_close_range, 3u, ~0u, 0u);
    if (command && *command) {
        puts("Runtime: restarting through KZSF_RESTART_COMMAND");
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    } else {
        puts("Runtime: restarting through run.sh");
        execlp("bash", "bash", "run.sh", (char *)NULL);
    }
    perror("runtime_restart: exec");
    _exit(1);
#endif
}

/* The guest code needs the PS4's instruction sets (#33, #66); KZSF_CPU_CHECK=0 skips this. A
 * missing fatal set prints STOP and returns 0; the rewritable ones go to cpu_missing_sets, named
 * in missing. */
static int cpu_supported(char *missing, size_t size) {
    missing[0] = 0;
    const char *check = getenv("KZSF_CPU_CHECK");
    if (check && !strcmp(check, "0")) return 1;
    const CpuFeatureWords words = cpu_feature_words();
    if (cpu_missing_fatal(words, missing, size)) {
        printf("STOP: this CPU lacks instruction sets the game's code uses: %s "
               "(Intel Sandy Bridge 2011 or newer, or AMD FX or newer, is needed)\n", missing);
        return 0;
    }
    cpu_missing_sets = cpu_missing_rewritable(words, missing, size);
    return 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
#ifndef _WIN32
    /* Keep host heap objects handed to the guest (thread handles, TLS) in the
       non-PIE brk heap, i.e. below 1 TiB: the guest packs pointers into 40 bits. */
    mallopt(M_ARENA_MAX,1);
    mallopt(M_MMAP_THRESHOLD,32*1024*1024);
#endif
    if (argc == 2 && !strcmp(argv[1], "--vulkan-only")) return vulkan_smoke();
    char missing[128];
    if (argc == 2 && !strcmp(argv[1], "--cpu-features")) {
        if (!cpu_supported(missing, sizeof missing)) return 20;
        if (cpu_missing_sets) printf("CPU: missing %s\n", missing);
        else puts("CPU: complete");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--gamepad")) {
        /* Before the memory reservation: this only asks SDL, nothing of the game is set up yet. */
        runtime_pad_describe();
        return 0;
    }
#ifdef _WIN32
    /* The PS4 user range must stay free of host allocations (GPU driver, heaps), and the guest
     * TCB needs one of the 64 TEB TLS slots before DLLs (Vulkan drivers) take them. */
    runtime_memory_reserve();
    runtime_thread_tcb_offset();
#endif
    int cpu_only = 0, strict_imports = 0, check_imports = 0;
    unsigned timeout_seconds = 10;
    const char *content_profile=NULL, *app0=NULL, *user_dir=NULL, *patch_file=NULL, *isa_file=NULL;
    const char *mounts[8]; int mount_count=0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--cpu-only")) cpu_only = 1;
        else if (!strcmp(argv[i], "--strict-imports")) strict_imports = 1;
        else if (!strcmp(argv[i], "--check-imports")) check_imports = 1;
        else if (!strcmp(argv[i], "--content-profile") && i+1<argc) content_profile=argv[++i];
        else if (!strcmp(argv[i], "--app0") && i+1<argc) app0=argv[++i];
        else if (!strcmp(argv[i], "--user") && i+1<argc) user_dir=argv[++i];
        else if (!strcmp(argv[i], "--patches") && i+1<argc) patch_file=argv[++i];
        else if (!strcmp(argv[i], "--isa-rewrite") && i+1<argc) isa_file=argv[++i];
        else if (!strcmp(argv[i], "--mount") && i+1<argc) {
            /* guest=host; the guest part never has '=', so the first one splits */
            const char *spec=argv[++i], *eq=strchr(spec,'=');
            if (mount_count==8 || !eq || eq==spec || !eq[1]) { fprintf(stderr, "Bad --mount (guest=host, at most 8): %s\n", spec); return 1; }
            mounts[mount_count++]=spec;
        }
        else if (!strcmp(argv[i], "--timeout") && i+1<argc) timeout_seconds=(unsigned)strtoul(argv[++i],NULL,10);
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s boot.bin [--cpu-only] [--strict-imports] [--check-imports] [--content-profile file] [--app0 dir] [--user dir] [--patches file] [--isa-rewrite file] [--mount guest=host]... [--timeout seconds] | --vulkan-only | --cpu-features | --gamepad\n", argv[0]);
        return 1;
    }
    if (!cpu_supported(missing, sizeof missing)) return 20;
    if (cpu_missing_sets) printf("CPU: missing %s\n", missing);
    /* KZSF_ISA_REWRITE=0 never rewrites, =1 rewrites every site; by default the sets the CPU lacks. */
    const char *policy = getenv("KZSF_ISA_REWRITE");
    unsigned isa_sets = policy && !strcmp(policy, "1") ? 0xffffffffu : policy && !strcmp(policy, "0") ? 0 : cpu_missing_sets;
    if (cpu_missing_sets && !isa_sets) {
        printf("STOP: this CPU lacks %s and the rewrite is disabled (KZSF_ISA_REWRITE=0)\n", missing);
        return 20;
    }
    if (cpu_missing_sets && !isa_file) {
        printf("STOP: this CPU lacks %s and no rewrite table matches the image\n", missing);
        return 20;
    }
    if (content_profile) {
        FILE *profile=fopen(content_profile,"rb");
        unsigned char data[28];
        if (!profile) fail("cannot open content profile");
        if (fread(data,1,sizeof(data),profile)!=sizeof(data) || fgetc(profile)!=EOF || memcmp(data,"G3CONT01",8)) fail("invalid content profile");
        fclose(profile);
        uint32_t values[5];
        for (unsigned i=0;i<5;++i) {
            unsigned char *p=data+8+i*4;
            values[i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
        runtime_content_configure(values);
    }
    if (app0) {
        runtime_file_configure(app0, user_dir ? user_dir : "user");
        for (int i = 0; i < mount_count; ++i) {
            const char *eq = strchr(mounts[i], '=');
            char guest[64];
            size_t glen = (size_t)(eq - mounts[i]);
            if (glen >= sizeof(guest)) { fprintf(stderr, "Cannot mount %s: guest path longer than 63 characters\n", mounts[i]); return 1; }
            memcpy(guest, mounts[i], glen); guest[glen] = 0;
            if (runtime_file_mount(guest, eq + 1)) { fprintf(stderr, "Cannot mount %s: too many mounts or host path longer than 511 characters\n", mounts[i]); return 1; }
            printf("Runtime: mount %s -> %s\n", guest, eq + 1);
        }
        char sfo[4096], id[16]="";
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0);
        if (sfo_value(sfo,"INSTALL_DIR_SAVEDATA",id,sizeof(id),NULL) || sfo_value(sfo,"TITLE_ID",id,sizeof(id),NULL))
            runtime_savedata_configure(id);
    }
    kzsfgpu_register_kernel();
#ifdef _WIN32
    SYSTEM_INFO system_info; GetSystemInfo(&system_info); page_size = system_info.dwPageSize;
    AddVectoredExceptionHandler(1, vectored_fault);
    SetUnhandledExceptionFilter(unhandled_fault);
    const char *stall=getenv("KZSF_STALL_SECONDS");
    if (stall) {
        int seconds=atoi(stall);
        if (seconds>0) CreateThread(NULL,0,stall_watchdog,(LPVOID)(uintptr_t)seconds,0,NULL);
    }
    const char *freeze=getenv("KZSF_FREEZE_SECONDS");
    if (freeze) {
        int seconds=atoi(freeze);
        if (seconds>0) CreateThread(NULL,0,freeze_watchdog,(LPVOID)(uintptr_t)seconds,0,NULL);
    }
    if (getenv("KZSF_STRICT_HANDLES")) {
        PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY strict={0};
        strict.RaiseExceptionOnInvalidHandleReference=1;
        strict.HandleExceptionsPermanentlyEnabled=1;
        if (!SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy,&strict,sizeof(strict)))
            fprintf(stderr,"KZSF_STRICT_HANDLES: policy not set (%lu)\n",GetLastError());
    }
    (void)timeout_seconds; /* no watchdog on Windows */
#else
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    struct sigaction sa = {0}; sa.sa_sigaction = fault; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL); /* int3 ISA sites */
    Dl_info self_info;
    if (dladdr((void *)main,&self_info)) exe_base=(uintptr_t)self_info.dli_fbase;
    struct sigaction dump = {0}; dump.sa_sigaction = thread_dump; dump.sa_flags = SA_SIGINFO|SA_RESTART;
    sigemptyset(&dump.sa_mask); sigaction(SIGUSR2, &dump, NULL);
    struct sigaction alarm_action = {0}; alarm_action.sa_sigaction = watchdog; alarm_action.sa_flags = SA_SIGINFO;
    sigemptyset(&alarm_action.sa_mask); sigaction(SIGALRM, &alarm_action, NULL);
    alarm(timeout_seconds); /* 0 disables the watchdog */
#endif
    FILE *f = fopen(argv[1], "rb");
    if (!f) fail("cannot open boot file; run prepare.py first");
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || (memcmp(magic, "G3PROBE1", 8) && memcmp(magic, "G3PROBE2", 8) && memcmp(magic,"G3PROBE3",8) && memcmp(magic,"G3PROBE4",8) && memcmp(magic,"G3PROBE5",8) && memcmp(magic,"G3PROBE6",8))) fail("bad boot file signature");
    uint64_t size = read64(f), entry = read64(f), ns = read64(f), nr = read64(f);
    import_count = read64(f);
    uint64_t capabilities = memcmp(magic, "G3PROBE1", 8) ? read64(f) : 0;
    if (capabilities & ~UINT64_C(1)) fail("unknown runtime capabilities");
    runtime_start(strict_imports ? 0 : capabilities);
    if (!size || size > 512*1024*1024 || entry >= size || !ns || ns > 64 || nr > 1000000 || import_count > 100000)
        fail("boot file limits exceeded");
    int dynamic_exports=!memcmp(magic,"G3PROBE6",8);
    int multi=!memcmp(magic,"G3PROBE5",8) || dynamic_exports;
    int linked=!memcmp(magic,"G3PROBE3",8) || !memcmp(magic,"G3PROBE4",8) || multi;
    int native_libc=linked && !strict_imports && (capabilities&1);
    uint64_t main_tls[4]={0};
    uint64_t nb=0,procparam=0;
    uint64_t *bindings=calloc(import_count ? import_count : 1,sizeof(*bindings));
    uint64_t *binding_kinds=calloc(import_count ? import_count : 1,sizeof(*binding_kinds));
    if (!bindings || !binding_kinds) fail("allocation failed");
    if (multi) {
        /* G3PROBE5: procparam, eboot TLS, module table, bindings (link_modules.py). */
        procparam=read64(f);
        for (int i=0;i<4;++i) main_tls[i]=read64(f);
        module_count=read64(f);
        if (!module_count || module_count>sizeof(modules)/sizeof(*modules)) fail("invalid module count");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            x->base=read64(f); x->size=read64(f); x->init=read64(f); x->tls_address=read64(f);
            x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=read64(f);
            if (dynamic_exports &&
                (fread(native_module_names[m],1,128,f)!=128 ||
                 !native_module_names[m][0] || !memchr(native_module_names[m],0,128)))
                fail("invalid native module name");
        }
        nb=read64(f);
    } else if (linked) {
        LinkedModule *x=&modules[0];
        module_count=1;
        x->base=read64(f); x->size=read64(f); x->init=read64(f);
        x->tls_address=read64(f); x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=2; nb=read64(f);
        procparam=read64(f);
        if (!memcmp(magic,"G3PROBE4",8)) for (int i=0;i<4;++i) main_tls[i]=read64(f);
    }
    if (linked) {
        if (main_tls[1]>main_tls[2] || main_tls[2]>1024*1024 || main_tls[0]>size ||
            main_tls[1]>size-main_tls[0] || (main_tls[3] & (main_tls[3]-1)) || main_tls[3]>4096)
            fail("invalid eboot TLS metadata");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            if (x->base>=size || !x->size || x->size>size-x->base || x->init<x->base || x->init-x->base>=x->size ||
                (x->tls_module && (x->tls_address>=size || x->tls_memsz>size-x->tls_address || x->tls_memsz>1024*1024 ||
                                   x->tls_filesz>x->tls_memsz || x->tls_module<2 || x->tls_module>7)))
                fail("invalid linked module metadata");
        }
        if (nb>import_count) fail("invalid binding count");
        for (uint64_t i=0;i<nb;++i) {
            uint64_t index=read64(f),address=read64(f),kind=read64(f);
            int inside=0;
            for (uint64_t m=0;m<module_count;++m)
                if (address>=modules[m].base && address-modules[m].base<modules[m].size) inside=1;
            if (index>=import_count || !inside || (kind!=1 && kind!=2) || bindings[index]) fail("invalid native binding");
            bindings[index]=address; binding_kinds[index]=kind;
        }
    }
    if (dynamic_exports) {
        native_export_count=read64(f);
        if (native_export_count>100000) fail("native export count exceeded");
        native_exports=calloc(native_export_count ? native_export_count : 1,sizeof(*native_exports));
        if (!native_exports) fail("allocation failed");
        for (uint64_t i=0;i<native_export_count;++i) {
            RuntimeNativeExport *s=&native_exports[i];
            s->module=read64(f); s->offset=read64(f); s->kind=read64(f);
            if (fread(s->nid,1,16,f)!=16 || !memchr(s->nid,0,16) || strlen(s->nid)!=11 ||
                s->module>=module_count || (s->kind!=1 && s->kind!=2)) fail("invalid native export");
            LinkedModule *m=&modules[s->module];
            if (s->offset<m->base || s->offset-m->base>=m->size) fail("native export outside module");
        }
    }
    Segment *segments = calloc(ns, sizeof(*segments));
    Reloc *relocs = calloc(nr ? nr : 1, sizeof(*relocs));
    names = calloc(import_count ? import_count : 1, sizeof(*names));
    if (!segments || !relocs || !names) fail("allocation failed");
    for (uint64_t i = 0; i < ns; ++i) {
        segments[i].address = read64(f);
        segments[i].size = read64(f);
        segments[i].flags = read64(f);
        if (segments[i].address > size || segments[i].size > size - segments[i].address ||
            segments[i].address % page_size || segments[i].flags > 7) fail("bad segment");
    }
    for (uint64_t i=0;i<native_export_count;++i) {
        RuntimeNativeExport *s=&native_exports[i];
        int valid=0;
        for (uint64_t j=0;j<ns;++j)
            if (s->offset>=segments[j].address && s->offset-segments[j].address<segments[j].size &&
                (s->kind!=1 || (segments[j].flags&1))) valid=1;
        if (!valid) fail("native export outside appropriate segment");
    }
    if (fread(names, 128, import_count, f) != import_count) fail("truncated import names");
    for (uint64_t i = 0; i < import_count; ++i)
        if (!memchr(names[i], 0, 128)) fail("unterminated import name");
    for (uint64_t i = 0; i < nr; ++i) {
        relocs[i].target = read64(f);
        relocs[i].kind = read64(f);
        relocs[i].value = read64(f);
        relocs[i].addend = read64(f);
        if (!mapped(segments, ns, relocs[i].target, 8) || relocs[i].kind > 2 ||
            (relocs[i].kind && relocs[i].value >= import_count) ||
            (relocs[i].kind != 2 && relocs[i].addend) || relocs[i].addend >= page_size) fail("bad relocation");
    }
    for (uint64_t m=0;m<module_count;++m)
        if (!mapped(segments,ns,modules[m].init,1) || (modules[m].tls_module && !mapped(segments,ns,modules[m].tls_address,modules[m].tls_memsz)))
            fail("unmapped module metadata");
    if (module_count && !mapped(segments,ns,procparam,64)) fail("unmapped procparam");
    for (uint64_t i=0;i<import_count;++i) {
        if (!bindings[i]) continue;
        if (!mapped(segments,ns,bindings[i],1)) fail("unmapped native export");
        if (binding_kinds[i]==1) {
            int executable=0;
            for (uint64_t s=0;s<ns;++s)
                if ((segments[s].flags&1) && bindings[i]>=segments[s].address &&
                    bindings[i]-segments[s].address<segments[s].size) executable=1;
            if (!executable) fail("native function is not executable");
        }
    }
    isa_table = isa_sets && isa_file ? isa_table_load(isa_file) : NULL;
    if (!isa_table && cpu_missing_sets) { printf("STOP: this CPU lacks %s and no rewrite table matches the image\n", missing); return 20; }
    /* The stubs follow the image in the same low mapping (rel32 jmps reach them). */
    size_t stub_area = isa_table ? round_page(isa_stub_area_size(isa_table)) : 0;
    size_t camera_bridge_area=0;
#ifdef _WIN32
    int camera_bridge_requested=getenv("KZSF_CAMERA_CALL_BRIDGE") &&
        !strcmp(getenv("KZSF_CAMERA_CALL_BRIDGE"),"1");
    int third_person_requested=getenv("KZSF_THIRD_PERSON_TEST") && !strcmp(getenv("KZSF_THIRD_PERSON_TEST"),"1");
    if(third_person_requested && !camera_bridge_requested)fail("Third-person prototype requires the direct-call bridge");
    if(camera_bridge_requested) {
        if(cpu_only || !getenv("KZSF_CAMERA_OBSERVER") || strcmp(getenv("KZSF_CAMERA_OBSERVER"),"1"))
            fail("Camera call bridge requires the isolated camera observer");
        camera_bridge_area=page_size;
    }
#endif
    image = allocate(round_page(size) + stub_area + camera_bridge_area);
    unsigned char *stubs = image + round_page(size);
    if (fread(image, 1, size, f) != size || fgetc(f) != EOF) fail("incorrect memory image size");
    fclose(f);
    if (isa_table && isa_table_matches(isa_table, image, size)) {
        printf("STOP: this CPU lacks %s and no rewrite table matches the image\n", missing);
        return 20;
    }
    if (!cpu_only) {
        char title[128]="God of War III", serial[16]="UNKNOWN", sfo[4096];
        uint32_t attributes=0;
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0 ? app0 : ".");
        sfo_value(sfo,"TITLE",title,sizeof(title),NULL);
        sfo_value(sfo,"TITLE_ID",serial,sizeof(serial),NULL);
        sfo_value(sfo,"ATTRIBUTE",NULL,0,&attributes);
        uint64_t sdk=0;
        if (procparam) memcpy(&sdk,image+procparam+16,8); /* procparam: size, magic, count, sdk_version */
        KzsfGpuConfig gpu={title,serial,user_dir ? user_dir : "user",(uint32_t)sdk,attributes,1920,1080};
        gpu_enabled=1; /* page tracking starts while the rasterizer registers guest memory */
        if (kzsfgpu_init(&gpu)) fail("GPU initialization failed");
        printf("GPU: window and Vulkan presenter ready; SDK 0x%08x, %u HLE symbols\n",(unsigned)sdk,kzsfgpu_symbol_count());
    }
    unsigned char *traps = allocate(round_page((import_count + 1) * 32));
    unsigned char *data_traps = allocate((import_count + 1) * page_size);
    protect(data_traps, (import_count + 1) * page_size, 0);
    for (uint64_t i = 0; i < import_count; ++i) {
        unsigned char *t = traps + i * 32;
        /* SysV: mov edi, index; movabs rax, handler; jmp rax. No fake return values. */
        uint32_t index = (uint32_t)i;
        uintptr_t handler = (uintptr_t)unresolved;
        t[0] = 0x48; t[1] = 0x89; t[2] = 0xfe; /* mov rsi,rdi: preserve arg0 */
        t[3] = 0xbf; memcpy(t + 4, &index, 4);
        t[8] = 0x48; t[9] = 0xb8; memcpy(t + 10, &handler, 8);
        t[18] = 0xff; t[19] = 0xe0;
    }
    if (getenv("KZSF_TRACE_IMPORTS")) {
        trace_imports=1;
        if (import_count > TRACE_STUB_CAP) fail("too many imports to trace");
        trace_pool=trace_stub_store;
        /* The trampolines are executed by guest code; the heap is not executable. */
#ifdef _WIN32
        DWORD trace_old=0;
        if (!VirtualProtect(trace_pool,(SIZE_T)128*(import_count ? import_count : 1),PAGE_EXECUTE_READWRITE,&trace_old))
            fail("VirtualProtect(trace_pool) failed");
#else
        if (mprotect(trace_pool,128*(import_count ? import_count : 1),PROT_READ|PROT_WRITE|PROT_EXEC))
            fail("mprotect(trace_pool) failed");
#endif
    }
    for (uint64_t i = 0; i < nr; ++i) {
        uintptr_t value = relocs[i].kind == 2 ? (uintptr_t)(data_traps + page_size * relocs[i].value + relocs[i].addend)
                        : relocs[i].kind == 1 ? (uintptr_t)(traps + 32 * relocs[i].value)
                        : (uintptr_t)image + relocs[i].value;
        if (relocs[i].kind) {
            uintptr_t resolved = runtime_resolve(names[relocs[i].value], relocs[i].kind == 2);
            /* Do not trampoline scePthreadYield: a spinning thread would flood the ring. */
            if (resolved && trace_imports && strncmp(names[relocs[i].value],"T72hz6ffq08",11)!=0) {
                unsigned char *stub=trace_pool + 128u*relocs[i].value;
                build_trace_stub(stub, (uint32_t)relocs[i].value, resolved);
                value=(uintptr_t)stub;
            } else if (resolved) value = resolved + relocs[i].addend;
            else if (native_libc && bindings[relocs[i].value]) {
                uint64_t address=bindings[relocs[i].value];
                if (binding_kinds[relocs[i].value]!=relocs[i].kind || !mapped(segments,ns,address,relocs[i].addend+1)) fail("native export kind/range mismatch");
                value=(uintptr_t)image+address+relocs[i].addend;
            }
        }
        memcpy(image + relocs[i].target, &value, 8);
    }
    if (check_imports) {
        /* Lists every import the game would stop at, without running it (needs the GPU
         * library: without --cpu-only its symbols are registered above). */
        unsigned char *checked=calloc(import_count ? import_count : 1,1);
        if (!checked) fail("allocation failed");
        uint64_t missing=0;
        for (uint64_t i=0;i<nr;++i) {
            uint64_t index=relocs[i].value;
            if (!relocs[i].kind || checked[index]) continue;
            checked[index]=1;
            if (runtime_resolve(names[index],relocs[i].kind==2) || (native_libc && bindings[index])) continue;
            printf("MISSING: %s (%s)\n",names[index],runtime_import_name(names[index]));
            ++missing;
        }
        printf("Imports: %" PRIu64 " missing\n",missing);
        fflush(NULL);
        _exit(missing ? 1 : 0); /* no destructors: GPU threads are running */
    }
    if (patch_file) apply_patches(patch_file, segments, ns, relocs, nr);
    if (gpu_enabled) install_orb_multiplier(segments, ns, size);
    if (gpu_enabled) install_cheats(segments, ns, size);
    if (gpu_enabled) install_actor_watch(segments, ns, size);
    if (gpu_enabled) install_stat_multipliers(segments, ns, size);
    if (gpu_enabled) install_kzsf_cheats(segments, ns, size);
    if (gpu_enabled && getenv("KZSF_DISABLE_MOTION_BLUR") && getenv("KZSF_DISABLE_MOTION_BLUR")[0]=='1') {
        uint32_t before=0,after=0;
        if (!mapped(segments,ns,KZSF_BLUR_FLAGS,4) ||
            !mapped(segments,ns,KZSF_BLUR_GUARD,sizeof(kzsf_blur_signature)) ||
            !kzsf_disable_motion_blur(image,size,&before,&after))
            fail("Killzone motion-blur signature mismatch; refusing to patch");
        printf("Killzone motion blur: engine mask %08x -> %08x; native off path selected\n",before,after);
    }
    if(gpu_enabled && getenv("KZSF_DISABLE_DOF") && getenv("KZSF_DISABLE_DOF")[0]=='1') {
        uint32_t before=0,after=0;
        if(!mapped(segments,ns,KZSF_DOF_FLAGS,4) ||
           !mapped(segments,ns,KZSF_DOF_GUARD,sizeof(kzsf_dof_signature)) ||
           !kzsf_disable_depth_of_field(image,size,&before,&after))
            fail("Killzone depth-of-field signature mismatch; refusing to patch");
        printf("Killzone depth of field: engine mask %08x -> %08x; native off path selected\n",before,after);
    }
    /* These run after motion blur and DOF: their guards tolerate bits 0..3. */
    if (gpu_enabled && getenv("KZSF_DISABLE_FILM_GRAIN") && getenv("KZSF_DISABLE_FILM_GRAIN")[0]=='1') {
        uint32_t before=0,after=0;
        if (!mapped(segments,ns,KZSF_FF_FLAGS,4) ||
            !mapped(segments,ns,KZSF_GRAIN_GUARD,sizeof(kzsf_grain_signature)) ||
            !kzsf_disable_film_grain(image,size,&before,&after))
            fail("Killzone film-grain signature mismatch; refusing to patch");
        printf("Killzone film grain: engine mask %08x -> %08x; native off path selected\n",before,after);
    }
    if (gpu_enabled && getenv("KZSF_DISABLE_BLOOM") && getenv("KZSF_DISABLE_BLOOM")[0]=='1') {
        uint32_t before=0,after=0;
        if (!mapped(segments,ns,KZSF_FF_FLAGS,4) ||
            !mapped(segments,ns,KZSF_BLOOM_GUARD,sizeof(kzsf_bloom_signature)) ||
            !kzsf_disable_bloom(image,size,&before,&after))
            fail("Killzone bloom signature mismatch; refusing to patch");
        printf("Killzone bloom: engine mask %08x -> %08x; native off path selected\n",before,after);
    }
#ifdef _WIN32
    const int pc_prompt_test=(getenv("KZSF_NATIVE_PROMPTS") && !strcmp(getenv("KZSF_NATIVE_PROMPTS"),"1")) ||
        (getenv("KZSF_PROMPT_PC_TEST") && !strcmp(getenv("KZSF_PROMPT_PC_TEST"),"1"));
    if(gpu_enabled && (pc_prompt_test ||
       (getenv("KZSF_PROMPT_TRACE") && !strcmp(getenv("KZSF_PROMPT_TRACE"),"1")))) {
        /* Validate every signature before installing any trap. */
        for(unsigned i=0;i<KZSF_PROMPT_TRACE_SITES;++i)
            if(!mapped(segments,ns,kzsf_prompt_sites[i].entry,kzsf_prompt_sites[i].length) ||
               !kzsf_prompt_trace_signature(image,size,i))
                fail("Killzone native prompt diagnostic signature mismatch; refusing to patch");
        for(unsigned i=0;i<KZSF_PROMPT_TRACE_SITES;++i) image[kzsf_prompt_sites[i].site]=0xcc;
        kzsf_prompt_trace_enabled=1;
        printf("Killzone: native prompt collection trace enabled%s\n",pc_prompt_test ? "" : "; no prompt replacement");
    }
    if(gpu_enabled && pc_prompt_test) {
        if(!mapped(segments,ns,0xfc4d90,sizeof(kzsf_pc_draw_signature)) || !kzsf_pc_signature(image,size))
            fail("Killzone native PC prompt prototype signature mismatch; refusing to patch");
        for(unsigned i=0;i<sizeof(kzsf_pc_calls)/sizeof(*kzsf_pc_calls);++i) image[kzsf_pc_calls[i]]=0xcc;
        kzsf_prompt_pc_enabled=1;
        kzsf_prompt_texture_smoke=getenv("KZSF_PROMPT_TEXTURE_SMOKE") &&
            !strcmp(getenv("KZSF_PROMPT_TEXTURE_SMOKE"),"1");
        printf("Killzone: native gameplay PC prompt artwork enabled; live device/style selection\n");
    }
    if (gpu_enabled && getenv("KZSF_POSTFX_DUMP") && getenv("KZSF_POSTFX_DUMP")[0]=='1') {
        if (!mapped(segments,ns,KZSF_POSTFX_GUARD,sizeof(kzsf_postfx_signature)) ||
            !kzsf_patch_match(image,size,KZSF_POSTFX_GUARD,kzsf_postfx_signature,sizeof(kzsf_postfx_signature)))
            fail("Killzone post-process dump signature mismatch; refusing to patch");
        image[KZSF_POSTFX_SITE]=0xcc;
        kzsf_postfx_dump_enabled=1;
        printf("Killzone: post-process diagnostic dump installed (KZSF_POSTFX_DUMP)\n");
    }
#endif
#ifdef _WIN32
    if(gpu_enabled && getenv("KZSF_CAMERA_OBSERVER") && !strcmp(getenv("KZSF_CAMERA_OBSERVER"),"1")) {
        if(getenv("KZSF_FREE_CAMERA_TEST") && !strcmp(getenv("KZSF_FREE_CAMERA_TEST"),"1"))
            fail("Killzone camera observer and free camera must run separately");
        const struct {size_t address;const unsigned char *bytes;size_t count;} guards[]={
            {KZSF_CAMERA_SELECT_ENTRY,kzsf_camera_select_guard,sizeof(kzsf_camera_select_guard)},
            {KZSF_CAMERA_BIND_ENTRY,kzsf_camera_bind_guard,sizeof(kzsf_camera_bind_guard)},
            {KZSF_FREE_CAMERA_GUARD,kzsf_free_camera_signature,sizeof(kzsf_free_camera_signature)}};
        for(unsigned i=0;i<3;i++)
            if(!mapped(segments,ns,guards[i].address,guards[i].count) ||
               !kzsf_patch_match(image,size,guards[i].address,guards[i].bytes,guards[i].count))
                fail("Killzone camera observer signature mismatch; refusing to patch");
        image[KZSF_CAMERA_SELECT_ENTRY+1]=0xcc;image[KZSF_CAMERA_BIND_ENTRY+1]=0xcc;
        if(camera_bridge_requested) {
            unsigned char *bridge=stubs+stub_area,patch[12];
            const unsigned char native_return=0xc3;
            if(!mapped(segments,ns,0x5fdb30,1) || image[0x5fdb30]!=native_return ||
               !kzsf_camera_call_bridge(bridge,camera_bridge_area,(uintptr_t)bridge,patch,
                    (uintptr_t)image+KZSF_CAMERA_CALL_SITE,image+KZSF_CAMERA_CALL_SITE,
                    (uintptr_t)kzsf_camera_bridge_visit))
                fail("Camera direct-call bridge guard/range mismatch; refusing to patch");
            if(getenv("KZSF_CHARACTER_ENGINEER_TEST") && !strcmp(getenv("KZSF_CHARACTER_ENGINEER_TEST"),"1")) {
                unsigned char render_patch[5];
                kzsf_character_body_route_test=getenv("KZSF_CHARACTER_BODY_ROUTE_TEST") &&
                    !strcmp(getenv("KZSF_CHARACTER_BODY_ROUTE_TEST"),"1");
                if(!third_person_requested || !kzsf_character_pose_guards((uintptr_t)image,kzsf_cheat_read,NULL))
                    fail("Character render-pose guard mismatch");
                if(getenv("KZSF_CHARACTER_ENGINEER_DRAW_TEST") && !strcmp(getenv("KZSF_CHARACTER_ENGINEER_DRAW_TEST"),"1")) {
                    unsigned char draw_patch[20];
                    if(!mapped(segments,ns,KZSF_CHARACTER_DRAW_ENTRY,20) ||
                       !kzsf_character_draw_bridge(bridge+64,camera_bridge_area-64,(uintptr_t)bridge+64,draw_patch,
                            (uintptr_t)image+KZSF_CHARACTER_DRAW_ENTRY,image+KZSF_CHARACTER_DRAW_ENTRY,(uintptr_t)kzsf_character_draw_visit))
                        fail("Character draw bridge guard/range mismatch");
                    kzsf_character_draw_original=(uintptr_t)bridge+64;
                    memcpy(image+KZSF_CHARACTER_DRAW_ENTRY,draw_patch,20);
                    printf("Killzone: Engineer draw-time scratch pose bridge installed\n");
                } else {
                    if(!mapped(segments,ns,KZSF_CHARACTER_RENDER_SITE,5) ||
                       !kzsf_character_render_bridge(bridge+64,camera_bridge_area-64,(uintptr_t)bridge+64,render_patch,
                           (uintptr_t)image+KZSF_CHARACTER_RENDER_SITE,image+KZSF_CHARACTER_RENDER_SITE,(uintptr_t)kzsf_character_render_visit))
                        fail("Character render-pose bridge guard/range mismatch");
                    memcpy(image+KZSF_CHARACTER_RENDER_SITE,render_patch,5);
                }
                if(!mapped(segments,ns,0xb57adc,sizeof(kzsf_character_face_lookup_guard)) ||
                   !kzsf_patch_match(image,size,0xb57adc,kzsf_character_face_lookup_guard,sizeof(kzsf_character_face_lookup_guard)) ||
                   !kzsf_character_face_guards((uintptr_t)image,kzsf_cheat_read,NULL))
                    fail("Character native facial animator guard mismatch");
                /* b57ac0 must use the active animator's own resource (+18).
                 * The original player's shared BodyVariant has no face. */
                memset(image+0xb57adc,0x90,sizeof(kzsf_character_face_lookup_guard));
                const unsigned char face_resource_lookup[]={0x4c,0x8b,0x7f,0x18};
                memcpy(image+0xb57adc,face_resource_lookup,sizeof(face_resource_lookup));
                unsigned char face_mapping_patch[55];
                if(!mapped(segments,ns,0xe46a4d,sizeof(kzsf_character_face_mapping_guard)) ||
                   !kzsf_patch_match(image,size,0xe46a4d,kzsf_character_face_mapping_guard,sizeof(kzsf_character_face_mapping_guard)) ||
                   !kzsf_character_face_mapping_patch(face_mapping_patch,(uintptr_t)kzsf_character_face_mapping_variant))
                    fail("Character face-to-body mapping guard mismatch");
                memcpy(image+0xe46a4d,face_mapping_patch,sizeof(face_mapping_patch));
                unsigned char face_offsets_patch[47];
                if(!mapped(segments,ns,0xe465cc,sizeof(kzsf_character_face_offsets_guard)) ||
                   !kzsf_patch_match(image,size,0xe465cc,kzsf_character_face_offsets_guard,sizeof(kzsf_character_face_offsets_guard)) ||
                   !kzsf_character_face_offsets_bridge(bridge+512,camera_bridge_area-512,(uintptr_t)bridge+512,
                       face_offsets_patch,(uintptr_t)image,(uintptr_t)kzsf_character_face_mapping_variant))
                    fail("Character facial offset map guard mismatch");
                memcpy(image+0xe465cc,face_offsets_patch,sizeof(face_offsets_patch));
                unsigned char face_owner_patch[7];
                if(!mapped(segments,ns,KZSF_CHARACTER_FACE_OWNER_SITE,sizeof(face_owner_patch)) ||
                   !kzsf_character_face_owner_bridge(bridge+1024,camera_bridge_area-1024,(uintptr_t)bridge+1024,
                       face_owner_patch,(uintptr_t)image+KZSF_CHARACTER_FACE_OWNER_SITE,
                       image+KZSF_CHARACTER_FACE_OWNER_SITE,(uintptr_t)kzsf_character_face_animation_owner))
                    fail("Character facial animation owner guard/range mismatch");
                memcpy(image+KZSF_CHARACTER_FACE_OWNER_SITE,face_owner_patch,sizeof(face_owner_patch));
                kzsf_character_render_installed=1;
                if(getenv("KZSF_CHARACTER_BUNDLE_TEST") && !strcmp(getenv("KZSF_CHARACTER_BUNDLE_TEST"),"1")) {
                    unsigned char exit_patch[20];
                    if(!mapped(segments,ns,KZSF_CHARACTER_LEVEL_EXIT,sizeof(exit_patch)) ||
                       !kzsf_character_bundle_guards((uintptr_t)image,kzsf_cheat_read,NULL) ||
                       !kzsf_character_level_exit_bridge(bridge+768,camera_bridge_area-768,(uintptr_t)bridge+768,
                            exit_patch,(uintptr_t)image+KZSF_CHARACTER_LEVEL_EXIT,image+KZSF_CHARACTER_LEVEL_EXIT,
                            (uintptr_t)kzsf_character_level_exit_visit))fail("Character level exit bridge guard mismatch");
                    kzsf_character_level_exit_original=(uintptr_t)bridge+768;
                    memcpy(image+KZSF_CHARACTER_LEVEL_EXIT,exit_patch,sizeof(exit_patch));
                    printf("Killzone: character bundle level-exit cleanup installed\n");
                    unsigned char handle_patch[KZSF_CHECKPOINT_HANDLE_BYTES];
                    if(!mapped(segments,ns,KZSF_CHECKPOINT_HANDLE_SITE,sizeof(handle_patch)) ||
                       !kzsf_checkpoint_handle_guards(image,size) ||
                       !kzsf_checkpoint_handle_bridge(bridge+832,camera_bridge_area-832,(uintptr_t)bridge+832,
                            handle_patch,(uintptr_t)image+KZSF_CHECKPOINT_HANDLE_SITE,image+KZSF_CHECKPOINT_HANDLE_SITE))
                        fail("Checkpoint slot bridge guard mismatch");
                    memcpy(image+KZSF_CHECKPOINT_HANDLE_SITE,handle_patch,sizeof(handle_patch));
                    printf("Killzone: checkpoint pool/index/object guard installed\n");
                }
                if(getenv("KZSF_CHARACTER_OVERLAY_TEST") && !strcmp(getenv("KZSF_CHARACTER_OVERLAY_TEST"),"1"))
                    kzsfgpu_character_selector_enable(1);
                printf("Killzone: isolated Engineer render-pose adapter installed; experimental F7 appearance\n");
            }
            protect(bridge,camera_bridge_area,5);
            memcpy(image+KZSF_CAMERA_CALL_SITE,patch,sizeof(patch));
            if(third_person_requested) {
                if(!mapped(segments,ns,0xa69c70,sizeof(kzsf_model_fp_signature)) ||
                   !mapped(segments,ns,0xa69dc0,sizeof(kzsf_model_world_signature)) ||
                   !kzsf_patch_match(image,size,0xa69c70,kzsf_model_fp_signature,sizeof(kzsf_model_fp_signature)) ||
                   !kzsf_patch_match(image,size,0xa69dc0,kzsf_model_world_signature,sizeof(kzsf_model_world_signature)))
                    fail("Third-person model-view guard mismatch");
                image[KZSF_MODEL_FP_SITE]=0xcc;image[KZSF_MODEL_WORLD_SITE]=0xcc;
                kzsf_free_camera_body_test=1;kzsf_third_person_enabled=1;
                kzsf_third_person_collision_ready=kzsf_camera_collision_guards((uintptr_t)image,kzsf_cheat_read,NULL);
                if(!kzsf_third_person_collision_ready)fail("Third-person native collision signature mismatch");
                printf("Killzone: third-person follow prototype installed; F8 toggle, native controls, ADS fallback; guarded native sphere collision\n");
            }
            printf("Killzone: camera direct-call bridge installed; physics disabled; follow prototype=%d\n",third_person_requested);
        } else image[KZSF_FREE_CAMERA_SITE]=0xcc;
        kzsf_camera_observer_enabled=1;
        atexit(kzsf_camera_observer_summary);
        printf("Killzone: camera observer installed; F6 snapshots, native controls active (maximum 64 snapshots)\n");
    }
    if(gpu_enabled && getenv("KZSF_FREE_CAMERA_TEST") && !strcmp(getenv("KZSF_FREE_CAMERA_TEST"),"1")) {
        if(!mapped(segments,ns,KZSF_FREE_CAMERA_GUARD,sizeof(kzsf_free_camera_signature)) ||
           !kzsf_patch_match(image,size,KZSF_FREE_CAMERA_GUARD,kzsf_free_camera_signature,sizeof(kzsf_free_camera_signature)))
            fail("Killzone free camera signature mismatch; refusing to patch");
        image[KZSF_FREE_CAMERA_SITE]=0xcc;kzsf_free_camera_enabled=1;
        if(getenv("KZSF_FREE_CAMERA_BODY") && !strcmp(getenv("KZSF_FREE_CAMERA_BODY"),"1")) {
            if(!mapped(segments,ns,0xa69c70,sizeof(kzsf_model_fp_signature)) ||
               !mapped(segments,ns,0xa69dc0,sizeof(kzsf_model_world_signature)) ||
               !kzsf_patch_match(image,size,0xa69c70,kzsf_model_fp_signature,sizeof(kzsf_model_fp_signature)) ||
               !kzsf_patch_match(image,size,0xa69dc0,kzsf_model_world_signature,sizeof(kzsf_model_world_signature)))
                fail("Killzone model-view signatures mismatch; refusing to patch");
            image[KZSF_MODEL_FP_SITE]=0xcc;image[KZSF_MODEL_WORLD_SITE]=0xcc;
            kzsf_free_camera_body_test=1;
            printf("Killzone: free-camera native model-view test enabled (active only with F8)\n");
        }
        printf("Killzone: isolated free camera installed; F8 toggle, F7 recenter, WASD/QE, arrows\n");
    }
    if(gpu_enabled && getenv("KZSF_CAMERA_CONTROLS") && getenv("KZSF_CAMERA_CONTROLS")[0]=='1') {
        if(!mapped(segments,ns,KZSF_FOV_GUARD,sizeof(kzsf_fov_signature)) ||
           !mapped(segments,ns,KZSF_SHAKE_GUARD,sizeof(kzsf_shake_signature)) ||
           !kzsf_patch_match(image,size,KZSF_FOV_GUARD,kzsf_fov_signature,sizeof(kzsf_fov_signature)) ||
           !kzsf_patch_match(image,size,KZSF_SHAKE_GUARD,kzsf_shake_signature,sizeof(kzsf_shake_signature)))
            fail("Killzone camera controls signature mismatch; refusing to patch");
        image[KZSF_FOV_SITE]=0xcc;
        image[KZSF_SHAKE_SITE]=0xcc;
        kzsf_camera_controls_enabled=1;
        printf("Killzone camera controls: GameViewGame FOV and evaluated shake hooks installed\n");
    }
    if(gpu_enabled && getenv("KZSF_PLAYER_TURN") && getenv("KZSF_PLAYER_TURN")[0]=='1') {
        if(!mapped(segments,ns,KZSF_PLAYER_TURN_GUARD,sizeof(kzsf_player_turn_signature)) ||
           !kzsf_patch_match(image,size,KZSF_PLAYER_TURN_GUARD,kzsf_player_turn_signature,sizeof(kzsf_player_turn_signature)))
            fail("Killzone player turn signature mismatch; refusing to patch");
        image[KZSF_PLAYER_TURN_SITE]=0xcc;
        if(!mapped(segments,ns,KZSF_SPRINT_GUARD,sizeof(kzsf_sprint_signature)) ||
           !kzsf_patch_match(image,size,KZSF_SPRINT_GUARD,kzsf_sprint_signature,sizeof(kzsf_sprint_signature)))
            fail("Killzone sprint turn signature mismatch; refusing to patch");
        image[KZSF_SPRINT_SITE]=0xcc;
        const struct {size_t site;const unsigned char *sig;size_t length;} aim_guards[]={
            {KZSF_RESPONSE_SITE,kzsf_response_sig,sizeof(kzsf_response_sig)},
            {KZSF_RAMP_SITE,kzsf_ramp_sig,sizeof(kzsf_ramp_sig)},
            {KZSF_ASSIST_SITE,kzsf_assist_sig,sizeof(kzsf_assist_sig)},
            {0xe4c4c9,kzsf_assist_exit_sig,sizeof(kzsf_assist_exit_sig)}};
        for(unsigned i=0;i<sizeof(aim_guards)/sizeof(*aim_guards);++i)
            if(!mapped(segments,ns,aim_guards[i].site,aim_guards[i].length) ||
               !kzsf_patch_match(image,size,aim_guards[i].site,aim_guards[i].sig,aim_guards[i].length))
                fail("Killzone aiming control signature mismatch; refusing to patch");
        image[KZSF_RESPONSE_SITE]=image[KZSF_RAMP_SITE]=image[KZSF_ASSIST_SITE]=0xcc;
        kzsf_player_turn_enabled=1;
        printf("Killzone: experimental SoldierJoystickController turn hook installed; execution unverified until KILLZONE_PLAYER_TURN\n");
    }
    if(gpu_enabled && getenv("KZSF_ENGINE_TURN") && getenv("KZSF_ENGINE_TURN")[0]=='1') {
        const size_t start=KZSF_TURN_SITE-8;
        if(!mapped(segments,ns,start,sizeof(kzsf_turn_signature)) ||
           !kzsf_patch_match(image,size,start,kzsf_turn_signature,sizeof(kzsf_turn_signature)))
            fail("Killzone engine look-rate signature mismatch; refusing to patch");
        image[KZSF_TURN_SITE]=0xcc;
        image[KZSF_PITCH_SITE]=0xcc;
        kzsf_turn_speed_enabled=1;
        {
            const char *log=getenv("KZSF_TURN_LOG");
            kzsf_turn_log=log && log[0]=='1';
        }
        printf("Killzone: experimental engine look-rate hook enabled (01.81); heading+pitch%s\n",
               kzsf_turn_log ? " with rate logging" : "");
    }
    printf("Guest TCB loads redirected to the TEB TLS slot: %" PRIu64 "\n", patch_tcb_loads(segments, ns));
      patch_guest_clock(image, size, segments, ns);
#endif
    if (isa_table) {
        IsaReport r = isa_rewrite_apply(isa_table, image, stubs, isa_sets);
        if (r.unsupported_needed) {
            printf("STOP: this CPU lacks %s and %u sites use encodings the rewrite cannot emulate\n", missing, r.unsupported_needed);
            fflush(NULL);
            _exit(20); /* no destructors: GPU threads are running */
        }
        if (isa_sets == 0xffffffffu) printf("ISA: forced rewrite of all %u sites (KZSF_ISA_REWRITE=1), %u skipped\n", r.rewritten, r.skipped);
        else printf("ISA: CPU lacks %s: %u sites rewritten (%u jmp, %u relocated, %u int3), %u skipped\n",
                    missing, r.rewritten, r.jmp, r.relocated, r.int3, r.skipped);
        runtime_set_report_hook(isa_report_hook);
        protect(stubs, stub_area, 5);
    }
    protect(traps, round_page((import_count + 1) * 32), 5);
    protect(image, round_page(size), 0);
    int executable_entry = 0;
    for (uint64_t i = 0; i < ns; ++i) {
        protect(image + segments[i].address, round_page(segments[i].size), (unsigned)segments[i].flags);
        if ((segments[i].flags & 1) && entry >= segments[i].address && entry - segments[i].address < segments[i].size)
            executable_entry = 1;
    }
    if (!executable_entry) fail("entry is not executable");
    printf("Mapped %" PRIu64 " bytes, %" PRIu64 " segments; applied %" PRIu64 " relocations\n", size, ns, nr);
    if (native_libc) {
        if (dynamic_exports)
            runtime_native_modules(native_module_names,module_count,native_exports,native_export_count,image);
        for (uint64_t m=0;m<module_count;++m) {
            int init_executable=0;
            for (uint64_t i=0;i<ns;++i)
                if ((segments[i].flags&1) && modules[m].init>=segments[i].address && modules[m].init-segments[i].address<segments[i].size) init_executable=1;
            if (!init_executable) fail("module init is not executable");
            if (modules[m].tls_module)
                runtime_set_module_tls(modules[m].tls_module,image+modules[m].tls_address,modules[m].tls_filesz,modules[m].tls_memsz);
        }
        runtime_set_main_tls(image+main_tls[0],main_tls[1],main_tls[2],main_tls[3]);
        runtime_thread_attach_main();
        runtime_set_procparam(image+procparam);
        /* Dependencies start in link order (libc first), as the PS4 dynamic linker does. */
        for (uint64_t m=0;m<module_count;++m) {
            printf("Starting linked module %" PRIu64 " at image offset 0x%" PRIx64 "; native bindings=%" PRIu64 "\n",m,modules[m].init,nb);
            typedef int (ABI *ModuleInit)(uint64_t,void *,void *);
            int result=((ModuleInit)(image+modules[m].init))(0,NULL,NULL);
            printf("Module %" PRIu64 " initializer returned %d\n",m,result);
            if (result) fail("module initializer failed");
        }
    }
    printf("Entering original x86-64 code at guest offset 0x%" PRIx64 "\n", entry);
    entered_game=1;
    struct { uint64_t argc; const char *argv[2]; } params = {1, {"/app0/eboot.bin", NULL}};
#ifdef _WIN32
    typedef void (ABI *Entry)(void *, void (ABI *)(void));
    ((Entry)(image + entry))(&params, guest_exit);
#else
    /* The guest main thread runs on a stack below 1 TiB like PS4 stacks. */
    enum { MAIN_STACK=8*1024*1024 };
    unsigned char *stack=runtime_low_map(MAIN_STACK,PROT_READ|PROT_WRITE);
    if (!stack) fail("cannot allocate guest main stack");
    enter_on_stack(image+entry,&params,(void *)guest_exit,stack+MAIN_STACK-64);
#endif
    fail("entry unexpectedly returned");
}
