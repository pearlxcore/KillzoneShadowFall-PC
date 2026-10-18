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
#include "cpu_check.h"
#include "isa_rewrite.h"
#include "orb_hook.h"
#include "cheat_hook.h"
#include "actor_hook.h"
#include "damage_hook.h"
#include "kzsf_cheats.h"
#include "tsc_patch.h"
#include "gpu/gow3gpu.h"
#if !defined(__x86_64__) || !defined(__GNUC__)
#error This prototype requires x86-64 GCC or Clang (including MinGW).
#endif
#ifdef _WIN32
#include <windows.h>
#include "kzsf_cheats_win.h"
#include "kzsf_turn_speed_win.h"
#include "kzsf_player_turn_win.h"
#include "kzsf_sprint_turn_win.h"
#include "kzsf_aim_controls_win.h"
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
static unsigned kzsf_player_protection_reported;
static int kzsf_turn_speed_enabled;
static int kzsf_turn_log;
static int kzsf_player_turn_enabled;
static unsigned kzsf_player_turn_events;
static unsigned kzsf_sprint_events;
static _Thread_local KzsfSprintScope kzsf_sprint_scope;
static unsigned kzsf_aim_events[3];
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
volatile uint32_t gow3_hle_last;
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
    int32_t disp=(int32_t)((uintptr_t)&gow3_hle_last-((uintptr_t)o+10));
    o[0]=0xC7; o[1]=0x05; memcpy(o+2,&disp,4); memcpy(o+6,&index,4); /* mov [gow3_hle_last], index */
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
    if (gpu_enabled) gow3gpu_dump_write_range(c->R15,sizeof(node));
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
    uint32_t hle_last=gow3_hle_last;
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
    if (gpu_enabled) gow3gpu_dump_guest_writes(info);
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
static LONG CALLBACK vectored_fault(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record=info->ExceptionRecord;
    DWORD code=record->ExceptionCode;
    if(code==EXCEPTION_BREAKPOINT && kzsf_player_turn_enabled) {
        CONTEXT *c=info->ContextRecord;int ok=0,changed=0,kind=-1;
        if(record->ExceptionAddress==image+KZSF_RESPONSE_SITE) {
            kind=0;ok=kzsf_look_response(c,(uintptr_t)image,gow3gpu_look_curve(),gow3gpu_look_deadzone(),
                kzsf_cheat_read,kzsf_turn_write,NULL,&changed);
        } else if(record->ExceptionAddress==image+KZSF_RAMP_SITE) {
            kind=1;ok=kzsf_look_ramp(c,(uintptr_t)image,gow3gpu_look_ramp(),
                kzsf_cheat_read,kzsf_turn_write,NULL,&changed);
        } else if(record->ExceptionAddress==image+KZSF_ASSIST_SITE) {
            kind=2;ok=kzsf_look_assist(c,(uintptr_t)image,gow3gpu_look_assist(),kzsf_cheat_read,NULL,&changed);
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
        const int percent=gow3gpu_sprint_turn_speed();
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
            sprinting?100:gow3gpu_controller_turn_speed(),gow3gpu_controller_pitch_speed(),
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
    if(code==EXCEPTION_BREAKPOINT && kzsf_turn_speed_enabled &&
       (record->ExceptionAddress==image+KZSF_TURN_SITE ||
        record->ExceptionAddress==image+KZSF_PITCH_SITE)) {
        const int pitch=record->ExceptionAddress==image+KZSF_PITCH_SITE;
        float engine_rate;
        memcpy(&engine_rate,&info->ContextRecord->Xmm0,sizeof(engine_rate));
        float applied=0.0f;
        int percent=pitch ? gow3gpu_controller_pitch_speed() : gow3gpu_controller_turn_speed();
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
        if (gpu_enabled && gow3gpu_handle_fault(info,(void *)record->ExceptionInformation[1])) return EXCEPTION_CONTINUE_EXECUTION;
        if (runtime_fault_recover) {
            /* A speculative guest memory read (GPU draw preparation) faulted: the thread resumes
             * in gow3_longjmp, outside the exception dispatcher, back at its recovery point. */
            unsigned long long *buffer=*runtime_fault_recover;
            runtime_fault_recover=NULL;
            CONTEXT *c=info->ContextRecord;
            c->Rsp=((c->Rsp-128)&~(DWORD64)15)-40;
            c->Rcx=(DWORD64)(uintptr_t)buffer;
            c->Rdx=1;
            c->Rip=(DWORD64)(uintptr_t)gow3_longjmp;
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
    /* GOW3_STRICT_HANDLES=1: a closed or invalid handle used anywhere is reported (diagnostics). */
    if (code==0xC0000008 /* STATUS_INVALID_HANDLE */) return fatal_exception(info);
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI unhandled_fault(EXCEPTION_POINTERS *info) { return fatal_exception(info); }
/* Stall watchdog: after GOW3_STALL_SECONDS, dump every thread's RIP so a hang can be
 * located (guest offsets when the RIP is inside the image). */
/* Print every process thread's RIP (guest offsets when inside the image) and, with
 * GOW3_TRACE_IMPORTS=1, the last HLE import that thread called (read from its TEB slot). */
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
            SuspendThread(thread);
            CONTEXT context; memset(&context,0,sizeof(context));
            context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
            if (GetThreadContext(thread,&context)) {
                describe_address(where,sizeof(where),(uintptr_t)context.Rip);
                uint32_t last=0;
                ThreadBasicInfo tbi; memset(&tbi,0,sizeof(tbi));
                if (query_thread_info && query_thread_info(thread,0,&tbi,sizeof(tbi),NULL)==0 && tbi.TebBaseAddress)
                    last=*(volatile uint32_t *)((char *)tbi.TebBaseAddress+trace_off);
                /* The tracer stores index+1; 0 means the thread never entered a traced import. */
                char lastbuf[160]="(none)";
                if (last && last-1<import_count)
                    snprintf(lastbuf,sizeof(lastbuf),"%s (%s)",names[last-1],runtime_import_name(names[last-1]));
                fprintf(stderr,"  tid=%lu rip=%s rsp=%p rax=%p rdi=%p last=%s\n",
                        (unsigned long)entry.th32ThreadID,where,(void *)context.Rsp,
                        (void *)context.Rax,(void *)context.Rdi,lastbuf);
            }
            ResumeThread(thread);
            CloseHandle(thread);
        } while (Thread32Next(snapshot,&entry));
        CloseHandle(snapshot);
    }
}
/* Freeze watchdog: if no frame is presented for GOW3_FREEZE_SECONDS, dump the thread RIPs
 * (and exit when GOW3_FREEZE_EXIT=1), so a hang reports itself instead of waiting. */
extern unsigned long long gow3gpu_flip_count(void);
/* GPU-side equeue state (registered events, blocked waiters, recent activity). */
extern void gow3gpu_equeue_dump(void);
static DWORD WINAPI freeze_watchdog(LPVOID argument) {
    DWORD seconds=(DWORD)(uintptr_t)argument;
    unsigned long long last=gow3gpu_flip_count();
    unsigned stalled=0;
    for (;;) {
        Sleep(1000);
        unsigned long long now=gow3gpu_flip_count();
        if (now!=last) { last=now; stalled=0; continue; }
        if (++stalled<seconds) continue;
        fprintf(stderr,"STOP: no frame presented for %lu s (frozen); hle_seq=%u; thread RIPs:\n",
                (unsigned long)seconds,(unsigned)hle_seq);
        dump_thread_rips();
        runtime_sema_dump(); /* blocked waiters + the semaphore activity just before the stall */
        if (gpu_enabled) gow3gpu_equeue_dump(); /* GPU/kernel event-queue waiters and last events */
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
        if (getenv("GOW3_FREEZE_EXIT")) TerminateProcess(GetCurrentProcess(),4);
    }
}
/* Reports every GOW3_STALL_SECONDS (repeat), so a hang that starts after the first interval is
 * still captured. GOW3_STALL_ONCE=1 restores the old single report. Set GOW3_STALL_SECONDS=0 to
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
        if (gpu_enabled) gow3gpu_equeue_dump();
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
        if (getenv("GOW3_STALL_ONCE")) return 0;
    }
}
/* Guest code reads its TCB with `mov rax, gs:[0]` (link_*.py rewrote fs:[0]); on Windows
 * the displacement becomes the TEB TLS slot that holds the guest TCB (runtime_thread.c). */
uint32_t runtime_thread_tcb_offset(void);
/* The guest converts its inline rdtsc to seconds with a constant baked for the retail PS4 TSC
 * (~1.594 GHz). The guest runs natively, so rdtsc is this host's TSC (typically 3-5 GHz) and any
 * time-derived gameplay runs too fast. Rewrite that constant to 1/host-TSC. Only the known value
 * is touched; GOW3_TSC_HZ overrides the measured frequency. */
#define GOW3_PS4_TSC_SECONDS 6.273525587907613e-10
static uint64_t patch_guest_clock(unsigned char *img, uint64_t size, Segment *segments, uint64_t ns) {
    const char *disabled=getenv("KZSF_ORIGINAL_CLOCK");
    if (disabled && !strcmp(disabled,"1")) {
        printf("Guest clock: original constants retained (recovery baseline)\n");
        return 0;
    }
    uint64_t hz=0;
    const char *env=getenv("GOW3_TSC_HZ");
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
    double want=1.0/(double)hz, from=GOW3_PS4_TSC_SECONDS;
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
    if (gpu_enabled && sig == SIGSEGV && gow3gpu_handle_fault(context, info->si_addr)) return;
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
    if (gpu_enabled) gow3gpu_dump_guest_writes(context);
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
extern ABI float gow3_orbs_gain(void *player, float requested);
extern void gow3_orbs_status(int supported);
extern uintptr_t gow3_cheat_flag(unsigned id);
extern uintptr_t gow3_actor_slots_address(void);
extern void gow3_actor_status(int supported);
extern void gow3_cheat_status(unsigned id);
extern uintptr_t gow3_damage_multiplier(int taken);
extern uintptr_t gow3_orb_pickup_multiplier(unsigned kind);
extern void gow3_stat_multipliers_status(int damage, int orb_pickup);

/* The hooks below find their sites by signature (src/signature.h), so they work in any region
 * and version of the game whose code matches; a site that is not found leaves its feature off. */
static void install_cheats(Segment *segments, uint64_t count, uint64_t size) {
    const char *names[] = {"health", "magic", "item meter", "Rage of Sparta", "red orbs",
                           "Very Hard unlock", "infinite double jumps"};
    uint64_t at[GOW3_CHEAT_HOOK_COUNT];
    for (unsigned i = 0; i < GOW3_CHEAT_HOOK_COUNT; ++i)
        at[i] = gow3_signature_find(image, size, &gow3_cheat_hooks[i].site);
    for (unsigned id = 0; id < GOW3_CHEAT_COUNT; ++id) {
        int valid = 1;
        for (unsigned i = 0; i < GOW3_CHEAT_HOOK_COUNT; ++i) {
            const Gow3CheatHook *hook = &gow3_cheat_hooks[i];
            if (hook->id != id) continue;
            if (at[i] == UINT64_MAX || !mapped(segments, count, at[i], hook->length) ||
                !gow3_cheat_signature_match(hook, image + at[i], size - at[i])) valid = 0;
        }
        if (!valid) {
            printf("Cheats: %s unavailable (code not found in this eboot.bin)\n", names[id]);
            continue;
        }
        unsigned char *stubs = allocate(page_size);
        int32_t relative[GOW3_CHEAT_HOOK_COUNT] = {0};
        for (unsigned i = 0; i < GOW3_CHEAT_HOOK_COUNT; ++i) {
            const Gow3CheatHook *hook = &gow3_cheat_hooks[i];
            if (hook->id != id) continue;
            unsigned char *stub = stubs + i * 256;
            int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + at[i] + 5);
            if (delta < INT32_MIN || delta > INT32_MAX) { valid = 0; break; }
            relative[i] = (int32_t)delta;
            gow3_cheat_build_stub(stub, hook, gow3_cheat_flag(id),
                                 (uintptr_t)(image + at[i] + hook->length));
        }
        if (!valid) {
            printf("Cheats: %s unavailable (hook outside relative-jump range)\n", names[id]);
            continue;
        }
        protect(stubs, page_size, 5);
        for (unsigned i = 0; i < GOW3_CHEAT_HOOK_COUNT; ++i) {
            const Gow3CheatHook *hook = &gow3_cheat_hooks[i];
            if (hook->id != id) continue;
            image[at[i]] = 0xe9;
            memcpy(image + at[i] + 1, &relative[i], 4);
            memset(image + at[i] + 5, 0x90, hook->length - 5);
        }
        gow3_cheat_status(id);
        printf("Cheats: %s ready (live toggle, initially disabled unless saved)\n", names[id]);
    }
}

/* Damage hook for the enemy health bar (src/actor_hook.h). Always
 * installed when the code matches: it only records values; the overlay decides what to show. */
static void install_actor_watch(Segment *segments, uint64_t count, uint64_t size) {
    const uint64_t at = gow3_signature_find(image, size, &gow3_actor_site);
    if (at == UINT64_MAX || !mapped(segments, count, at, GOW3_ACTOR_HOOK_LENGTH) ||
        !gow3_actor_signature_match(image + at, size - at)) {
        printf("Health display: unavailable (code not found in this eboot.bin)\n");
        return;
    }
    unsigned char *stub = allocate(page_size);
    const int64_t delta = (int64_t)(uintptr_t)stub - (int64_t)(uintptr_t)(image + at + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        printf("Health display: unavailable (hook outside relative-jump range)\n");
        return;
    }
    gow3_actor_build_stub(stub, gow3_actor_slots_address(), (uintptr_t)(image + at + GOW3_ACTOR_HOOK_LENGTH));
    protect(stub, page_size, 5);
    const int32_t relative = (int32_t)delta;
    image[at] = 0xe9;
    memcpy(image + at + 1, &relative, 4);
    memset(image + at + 5, 0x90, GOW3_ACTOR_HOOK_LENGTH - 5);
    gow3_actor_status(1);
    printf("Health display: damage hook ready (enemy health bar in the menu)\n");
}

static void install_orb_multiplier(Segment *segments, uint64_t count, uint64_t size) {
    uint64_t calls[3];
    const uint64_t setter_at = gow3_orb_find(image, size, calls);
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
    uintptr_t helper = (uintptr_t)gow3_orbs_gain, setter = (uintptr_t)(image + setter_at);
    unsigned char code[26];
    gow3_orb_build_stub(code, helper, setter);
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
    gow3_orbs_status(1);
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
    const uint64_t damage_at = gow3_signature_find(image, size, &gow3_damage_site);
    const uint64_t pickup_at = gow3_signature_find(image, size, &gow3_orb_pickup_site);
    int damage = damage_at != UINT64_MAX && mapped(segments, count, damage_at, GOW3_DAMAGE_HOOK_LENGTH) &&
                 gow3_damage_signature_match(image + damage_at, size - damage_at);
    int pickup = pickup_at != UINT64_MAX && mapped(segments, count, pickup_at, GOW3_ORB_PICKUP_LENGTH) &&
                 gow3_orb_pickup_signature_match(image + pickup_at, size - pickup_at);
    unsigned char *stubs = damage || pickup ? allocate(page_size) : NULL;
    if (damage) {
        gow3_damage_build_stub(stubs, gow3_damage_multiplier(1), gow3_damage_multiplier(0),
                               (uintptr_t)(image + damage_at + GOW3_DAMAGE_HOOK_LENGTH));
    }
    if (pickup) {
        gow3_orb_pickup_build_stub(stubs + 256, gow3_orb_pickup_multiplier(0), gow3_orb_pickup_multiplier(1),
                                   gow3_orb_pickup_multiplier(2),
                                   (uintptr_t)(image + pickup_at + GOW3_ORB_PICKUP_LENGTH));
    }
    if (stubs) protect(stubs, page_size, 5);
    damage = damage && install_jump(damage_at, GOW3_DAMAGE_HOOK_LENGTH, stubs);
    pickup = pickup && install_jump(pickup_at, GOW3_ORB_PICKUP_LENGTH, stubs + 256);
    gow3_stat_multipliers_status(damage, pickup);
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
    /* run.py sets GOW3_RESTART_COMMAND: the launch command line, started again (new patches). */
    const char *command=getenv("GOW3_RESTART_COMMAND");
    puts("Runtime: restarting through run.py");
    if (!command || !*command) { fputs("runtime_restart: GOW3_RESTART_COMMAND is not set\n",stderr); _exit(1); }
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
    const char *command = getenv("GOW3_RESTART_COMMAND");
    syscall(SYS_close_range, 3u, ~0u, 0u);
    if (command && *command) {
        puts("Runtime: restarting through GOW3_RESTART_COMMAND");
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    } else {
        puts("Runtime: restarting through run.sh");
        execlp("bash", "bash", "run.sh", (char *)NULL);
    }
    perror("runtime_restart: exec");
    _exit(1);
#endif
}

/* The guest code needs the PS4's instruction sets (#33, #66); GOW3_CPU_CHECK=0 skips this. A
 * missing fatal set prints STOP and returns 0; the rewritable ones go to cpu_missing_sets, named
 * in missing. */
static int cpu_supported(char *missing, size_t size) {
    missing[0] = 0;
    const char *check = getenv("GOW3_CPU_CHECK");
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
    /* GOW3_ISA_REWRITE=0 never rewrites, =1 rewrites every site; by default the sets the CPU lacks. */
    const char *policy = getenv("GOW3_ISA_REWRITE");
    unsigned isa_sets = policy && !strcmp(policy, "1") ? 0xffffffffu : policy && !strcmp(policy, "0") ? 0 : cpu_missing_sets;
    if (cpu_missing_sets && !isa_sets) {
        printf("STOP: this CPU lacks %s and the rewrite is disabled (GOW3_ISA_REWRITE=0)\n", missing);
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
    gow3gpu_register_kernel();
#ifdef _WIN32
    SYSTEM_INFO system_info; GetSystemInfo(&system_info); page_size = system_info.dwPageSize;
    AddVectoredExceptionHandler(1, vectored_fault);
    SetUnhandledExceptionFilter(unhandled_fault);
    const char *stall=getenv("GOW3_STALL_SECONDS");
    if (stall) {
        int seconds=atoi(stall);
        if (seconds>0) CreateThread(NULL,0,stall_watchdog,(LPVOID)(uintptr_t)seconds,0,NULL);
    }
    const char *freeze=getenv("GOW3_FREEZE_SECONDS");
    if (freeze) {
        int seconds=atoi(freeze);
        if (seconds>0) CreateThread(NULL,0,freeze_watchdog,(LPVOID)(uintptr_t)seconds,0,NULL);
    }
    if (getenv("GOW3_STRICT_HANDLES")) {
        PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY strict={0};
        strict.RaiseExceptionOnInvalidHandleReference=1;
        strict.HandleExceptionsPermanentlyEnabled=1;
        if (!SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy,&strict,sizeof(strict)))
            fprintf(stderr,"GOW3_STRICT_HANDLES: policy not set (%lu)\n",GetLastError());
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
    image = allocate(round_page(size) + stub_area);
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
        Gow3GpuConfig gpu={title,serial,user_dir ? user_dir : "user",(uint32_t)sdk,attributes,1920,1080};
        gpu_enabled=1; /* page tracking starts while the rasterizer registers guest memory */
        if (gow3gpu_init(&gpu)) fail("GPU initialization failed");
        printf("GPU: window and Vulkan presenter ready; SDK 0x%08x, %u HLE symbols\n",(unsigned)sdk,gow3gpu_symbol_count());
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
    if (getenv("GOW3_TRACE_IMPORTS")) {
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
#ifdef _WIN32
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
        if (isa_sets == 0xffffffffu) printf("ISA: forced rewrite of all %u sites (GOW3_ISA_REWRITE=1), %u skipped\n", r.rewritten, r.skipped);
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
