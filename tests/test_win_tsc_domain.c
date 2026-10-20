#include "../src/runtime.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
/* Unused runtime dependencies must never be reached by this clock-only test. */
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *s) { abort(); }
int32_t *runtime_errno(void) { abort(); }
void runtime_report(void) { abort(); }
int32_t runtime_native_module_handle(const char *s) { abort(); }
int runtime_native_module_valid(int32_t h) { abort(); }
uintptr_t runtime_native_symbol(int32_t h,const char *s) { abort(); }
uintptr_t runtime_resolve(const char *s,int d) { abort(); }
uint64_t runtime_read_tsc(void);
uint64_t runtime_tsc_frequency(void);
int main(void) {
    assert(runtime_tsc_frequency()==1000000000);
    uint64_t p0=runtime_process_time_counter();
    uint64_t legacy=runtime_read_tsc();
    assert(legacy>=p0 && legacy-p0<100000000);
    LARGE_INTEGER f,q0,q1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    uint64_t t0=__builtin_ia32_rdtsc();
    Sleep(30);
    uint64_t t1=__builtin_ia32_rdtsc();
    QueryPerformanceCounter(&q1);
    uint64_t hz=(uint64_t)((double)(t1-t0)*f.QuadPart/(q1.QuadPart-q0.QuadPart));
    runtime_set_native_tsc(hz);
    assert(runtime_tsc_frequency()==hz);
    uint64_t low=__builtin_ia32_rdtsc();
    uint64_t guest=runtime_read_tsc();
    uint64_t high=__builtin_ia32_rdtsc();
    assert(low<=guest && guest<=high);
    uint64_t start=runtime_read_tsc();
    Sleep(30);
    double seconds=(double)(runtime_read_tsc()-start)/runtime_tsc_frequency();
    assert(seconds>=0.02 && seconds<0.2);
    /* Process-time APIs retain nanosecond units and their original epoch. */
    assert(runtime_process_time_counter()-p0<1000000000);
    runtime_set_virtual_tsc(hz);
    assert(runtime_tsc_frequency()==1594000000);
    low=__builtin_ia32_rdtsc();
    guest=runtime_read_tsc();
    high=__builtin_ia32_rdtsc();
    assert((uint64_t)(((__uint128_t)low*1594000000)/hz)<=guest);
    assert(guest<=(uint64_t)(((__uint128_t)high*1594000000)/hz));
    start=runtime_read_tsc();
    Sleep(30);
    seconds=(double)(runtime_read_tsc()-start)/runtime_tsc_frequency();
    assert(seconds>=0.02 && seconds<0.2);
    _putenv_s("KZSF_DIRECT_TSC","1");
    runtime_set_virtual_tsc(hz);
    const uint64_t multiplier=*runtime_tsc_multiplier();
    assert(multiplier>0 && runtime_tsc_frequency()==1594000000);
    low=__builtin_ia32_rdtsc(); guest=runtime_read_tsc(); high=__builtin_ia32_rdtsc();
    assert((uint64_t)(((__uint128_t)low*multiplier)>>32)<=guest);
    assert(guest<=(uint64_t)(((__uint128_t)high*multiplier)>>32));
    start=runtime_read_tsc(); Sleep(30);
    seconds=(double)(runtime_read_tsc()-start)/runtime_tsc_frequency();
    assert(seconds>=0.02 && seconds<0.2);
    _putenv_s("KZSF_DIRECT_TSC","");
    runtime_set_native_tsc(0);
    assert(runtime_tsc_frequency()==1000000000);
    puts("TSC domain tests passed");
}
