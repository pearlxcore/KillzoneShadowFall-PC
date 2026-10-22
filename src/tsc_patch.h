/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from lesca-191204/killzone-shadowfall-pc, 2194e4984f7053500c60af3fae845c053bb3ea81.
 * PS4 TSC for inline rdtsc. The PS4's TSC runs at 1.594 GHz and games hardcode that rate
 * (Killzone Shadow Fall's frame timer runs 2x fast on a 3.7 GHz host TSC), so every rdtsc in
 * the guest's code is moved into a trampoline that returns (host TSC * multiplier) >> 32, the
 * same clock sceKernelReadTsc returns. rdtsc is 2 bytes, too short for a jump, so neighbouring
 * instructions move with it until the patched window holds a 5-byte jump. Runs at load time,
 * before the image is protected. */
#ifndef KZSF_TSC_PATCH_H
#define KZSF_TSC_PATCH_H
#include <stddef.h>
#include <stdint.h>

enum { TSC_PATCH_AREA = 4 << 20 }; /* trampoline bytes reserved right after the image */
typedef struct { uint32_t found, windows, moved, skipped; } TscReport;

/* Patches the executable code [image+offset, image+offset+size). Trampolines are appended at
 * area + *area_used (area: TSC_PATCH_AREA writable bytes within rel32 reach of the image). */
TscReport tsc_patch_segment(unsigned char *image, uint64_t offset, uint64_t size, unsigned char *area,
                            size_t *area_used, const uint64_t *multiplier,
                            const uint32_t *verified_sites, size_t verified_count);

#endif
