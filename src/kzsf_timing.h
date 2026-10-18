/* Experimental complement to the existing inline-TSC seconds correction.
 * Exact CUSA00190 01.81 rodata values; validate every site before writing any. */
#ifndef KZSF_TIMING_H
#define KZSF_TIMING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static int kzsf_timing_deadlines(unsigned char *image, size_t size, double hz) {
    static const uint32_t sites[]={0x12074f8,0x1207508,0x1207684,0x1207690,0x1207b6c};
    static const uint32_t micros[]={0x11a5ab0,0x1221858};
    const float original=1594000000.0f, replacement=(float)hz;
    const uint64_t old_micro=UINT64_C(0x3f448e9d60000000);
    const double new_micro=1e6/hz;
    if (size<0x1221860 || !(hz>=1e8 && hz<=2e10)) return 0;
    for (size_t i=0;i<sizeof(sites)/sizeof(*sites);++i)
        if (memcmp(image+sites[i],&original,4)) return 0;
    for (size_t i=0;i<sizeof(micros)/sizeof(*micros);++i)
        if (memcmp(image+micros[i],&old_micro,8)) return 0;
    for (size_t i=0;i<sizeof(sites)/sizeof(*sites);++i)
        memcpy(image+sites[i],&replacement,4);
    for (size_t i=0;i<sizeof(micros)/sizeof(*micros);++i)
        memcpy(image+micros[i],&new_micro,8);
    return 7;
}
#endif
