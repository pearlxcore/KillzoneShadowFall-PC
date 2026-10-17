/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from lesca-191204/killzone-shadowfall-pc; see tsc_patch.h.
 * One linear sweep (Zydis) finds instruction starts, direct branch targets
 * and undecodable bytes; each rdtsc then gets a window of whole instructions, extended forward
 * and then backward until it is 5 bytes long, where only the first instruction may be a branch
 * target (Killzone has "rdtsc; jmp L" paths where L is the store right after another rdtsc).
 * The trampoline replays the window: rip-relative operands and direct jcc/jmp are re-encoded,
 * a call ends the window and pushes the original return address, and rdtsc becomes the guest
 * TSC with the flags kept (below the guest's 128-byte red zone). */
#include "tsc_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Zydis/Zydis.h>

enum { JMP_BYTES = 5, MAX_WINDOW = 32, MAX_INSNS = 16, DATA_MARGIN = 32 };
typedef enum { KIND_UNSUPPORTED, KIND_PLAIN, KIND_JCC, KIND_JMP, KIND_CALL, KIND_RET } Kind;
typedef struct { ZydisDecodedInstruction info; Kind kind; const unsigned char *address; } Insn;

static ZydisDecoder decoder;
static int decoder_ready;

static Kind classify(const ZydisDecodedInstruction *i) {
    int relative = i->raw.imm[0].is_relative;
    switch (i->meta.category) {
    case ZYDIS_CATEGORY_COND_BR: /* Jcc only: jrcxz/loop have no rel32 form */
        return (i->opcode_map == ZYDIS_OPCODE_MAP_DEFAULT && (i->opcode & 0xF0) == 0x70) ||
                       (i->opcode_map == ZYDIS_OPCODE_MAP_0F && (i->opcode & 0xF0) == 0x80)
                   ? KIND_JCC : KIND_UNSUPPORTED;
    case ZYDIS_CATEGORY_UNCOND_BR: return relative ? KIND_JMP : KIND_UNSUPPORTED;
    case ZYDIS_CATEGORY_CALL: return relative ? KIND_CALL : KIND_UNSUPPORTED;
    case ZYDIS_CATEGORY_RET: return KIND_RET;
    case ZYDIS_CATEGORY_SYSCALL: return KIND_UNSUPPORTED;
    default:
        if (i->meta.branch_type != ZYDIS_BRANCH_TYPE_NONE) return KIND_UNSUPPORTED;
        /* rip-relative operands are re-encoded, which needs a 32-bit displacement */
        return (i->attributes & ZYDIS_ATTRIB_IS_RELATIVE) && i->raw.disp.size != 32 ? KIND_UNSUPPORTED : KIND_PLAIN;
    }
}
static int decode(const unsigned char *p, const unsigned char *end, Insn *out) {
    ZydisDecoderContext context;
    size_t avail = (size_t)(end - p);
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, &context, p, avail < 15 ? avail : 15, &out->info)))
        return 0;
    out->kind = classify(&out->info);
    out->address = p;
    return 1;
}
static const unsigned char *branch_target(const Insn *i) {
    return i->address + i->info.length + i->info.raw.imm[0].value.s;
}
static int is_padding(const Insn *i) {
    return i->info.mnemonic == ZYDIS_MNEMONIC_NOP || i->info.mnemonic == ZYDIS_MNEMONIC_INT3;
}

static int test_bit(const uint8_t *bits, uint64_t i) { return bits[i >> 3] >> (i & 7) & 1; }
static void set_bit(uint8_t *bits, uint64_t i) { bits[i >> 3] |= (uint8_t)(1u << (i & 7)); }

/* ---- trampoline emission: 0 when out of space or out of rel32 range ---- */
typedef struct { unsigned char *p, *end; int ok; } Out;
static void put(Out *o, const void *bytes, size_t n) {
    if (!o->ok || (size_t)(o->end - o->p) < n) { o->ok = 0; return; }
    memcpy(o->p, bytes, n);
    o->p += n;
}
static void put_u8(Out *o, uint8_t v) { put(o, &v, 1); }
static void put_rel32(Out *o, const unsigned char *target) { /* rel32 ending 4 bytes from here */
    int64_t delta = (int64_t)((intptr_t)target - (intptr_t)(o->p + 4));
    if (delta < INT32_MIN || delta > INT32_MAX) { o->ok = 0; return; }
    int32_t rel = (int32_t)delta;
    put(o, &rel, 4);
}
static void put_jmp(Out *o, const unsigned char *target) { put_u8(o, 0xE9); put_rel32(o, target); }
static void put_guest_rdtsc(Out *o, const uint64_t *multiplier) {
    static const uint8_t head[] = {
        0x48, 0x8D, 0x64, 0x24, 0x80, /* lea rsp,[rsp-128] */
        0x9C,                         /* pushfq */
        0x0F, 0x31,                   /* rdtsc */
        0x48, 0xC1, 0xE2, 0x20,       /* shl rdx,32 */
        0x48, 0x09, 0xD0,             /* or rax,rdx */
        0x48, 0xBA};                  /* mov rdx,imm64 */
    static const uint8_t tail[] = {
        0x48, 0xF7, 0x22,                         /* mul qword [rdx]: rdx:rax = host * multiplier */
        0x48, 0x0F, 0xAC, 0xD0, 0x20,             /* shrd rax,rdx,32 */
        0x48, 0x89, 0xC2,                         /* mov rdx,rax */
        0x48, 0xC1, 0xEA, 0x20,                   /* shr rdx,32 */
        0x89, 0xC0,                               /* mov eax,eax */
        0x9D,                                     /* popfq */
        0x48, 0x8D, 0xA4, 0x24, 0x80, 0, 0, 0};   /* lea rsp,[rsp+128] */
    uint64_t address = (uint64_t)(uintptr_t)multiplier;
    put(o, head, sizeof head);
    put(o, &address, 8);
    put(o, tail, sizeof tail);
}
static void emit_window(Out *o, const Insn *w, int n, const unsigned char *back, const uint64_t *multiplier) {
    for (int k = 0; k < n; ++k) {
        const Insn *i = &w[k];
        uint8_t length = i->info.length;
        switch (i->kind) {
        case KIND_PLAIN:
            if (i->info.mnemonic == ZYDIS_MNEMONIC_RDTSC) put_guest_rdtsc(o, multiplier);
            else if (i->info.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
                uint8_t disp = i->info.raw.disp.offset;
                const unsigned char *target = i->address + length + i->info.raw.disp.value;
                put(o, i->address, disp);
                int64_t delta = (int64_t)((intptr_t)target - (intptr_t)(o->p + 4 + (length - disp - 4)));
                if (delta < INT32_MIN || delta > INT32_MAX) { o->ok = 0; return; }
                int32_t rel = (int32_t)delta;
                put(o, &rel, 4);
                put(o, i->address + disp + 4, (size_t)(length - disp - 4));
            } else put(o, i->address, length);
            break;
        case KIND_JCC:
            put_u8(o, 0x0F);
            put_u8(o, (uint8_t)(0x80 | (i->info.opcode & 0x0F)));
            put_rel32(o, branch_target(i));
            break;
        case KIND_JMP: put_jmp(o, branch_target(i)); return; /* the rest is dead padding */
        case KIND_RET: put(o, i->address, length); return;
        case KIND_CALL: {
            /* push the original return address: the callee returns to guest code */
            static const uint8_t save[] = {0x48, 0x8D, 0x64, 0x24, 0xF8, 0x50, 0x48, 0xB8}; /* lea rsp,[rsp-8]; push rax; mov rax,imm64 */
            static const uint8_t store[] = {0x48, 0x89, 0x44, 0x24, 0x08, 0x58};            /* mov [rsp+8],rax; pop rax */
            uint64_t ret = (uint64_t)(uintptr_t)(i->address + length);
            put(o, save, sizeof save);
            put(o, &ret, 8);
            put(o, store, sizeof store);
            put_jmp(o, branch_target(i));
            return;
        }
        case KIND_UNSUPPORTED: o->ok = 0; return;
        }
    }
    put_jmp(o, back);
}

static int verified_site(uint64_t at, const uint32_t *sites, size_t count) {
    if (!sites) return 1; // Synthetic instruction relocation tests.
    size_t lo=0, hi=count;
    while(lo<hi) { size_t mid=lo+(hi-lo)/2; if(sites[mid]<at) lo=mid+1; else hi=mid; }
    return lo<count && sites[lo]==at;
}
TscReport tsc_patch_segment(unsigned char *image, uint64_t offset, uint64_t size, unsigned char *area,
                            size_t *area_used, const uint64_t *multiplier,
                            const uint32_t *verified_sites, size_t verified_count) {
    TscReport report = {0};
    unsigned char *begin = image + offset, *end = begin + size;
    if (!decoder_ready) {
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
            fputs("TSC: cannot initialize the decoder\n", stderr);
            return report;
        }
        decoder_ready = 1;
    }
    uint8_t *starts = calloc(size / 8 + 1, 1), *targets = calloc(size / 8 + 1, 1);
    uint64_t *rdtscs = NULL, *invalid = NULL;
    size_t nrdtsc = 0, ninvalid = 0, cap_rdtsc = 0, cap_invalid = 0;
    if (!starts || !targets) { fputs("TSC: out of memory\n", stderr); exit(1); }
    /* One linear sweep: instruction starts, direct branch targets, undecodable bytes (data). */
    for (uint64_t at = 0; at < size;) {
        Insn i;
        if (!decode(begin + at, end, &i)) {
            if (ninvalid == cap_invalid) {
                cap_invalid = cap_invalid ? cap_invalid * 2 : 1024;
                if (!(invalid = realloc(invalid, cap_invalid * sizeof *invalid))) { fputs("TSC: out of memory\n", stderr); exit(1); }
            }
            invalid[ninvalid++] = at++;
            continue;
        }
        set_bit(starts, at);
        if (i.info.mnemonic == ZYDIS_MNEMONIC_RDTSC && verified_site(offset+at,verified_sites,verified_count)) {
            if (nrdtsc == cap_rdtsc) {
                cap_rdtsc = cap_rdtsc ? cap_rdtsc * 2 : 1024;
                if (!(rdtscs = realloc(rdtscs, cap_rdtsc * sizeof *rdtscs))) { fputs("TSC: out of memory\n", stderr); exit(1); }
            }
            rdtscs[nrdtsc++] = at;
        }
        if (i.kind == KIND_JCC || i.kind == KIND_JMP || i.kind == KIND_CALL) {
            const unsigned char *t = branch_target(&i);
            if (t >= begin && t < end) set_bit(targets, (uint64_t)(t - begin));
        }
        at += i.info.length;
    }
    report.found = (uint32_t)nrdtsc;
    uint64_t next_free = 0; /* windows never overlap */
    for (size_t r = 0; r < nrdtsc; ++r) {
        uint64_t rdtsc = rdtscs[r];
        if (rdtsc < next_free) { report.moved++; continue; } /* inside a previous window */
        Insn w[MAX_INSNS];
        int n = 1;
        decode(begin + rdtsc, end, &w[0]);
        uint64_t lo = rdtsc, hi = rdtsc + w[0].info.length;
        /* Forward: anything replayable; a call ends the window, after a jmp/ret only dead padding. */
        int closed = 0;
        while (hi - lo < JMP_BYTES && hi < size && n < MAX_INSNS && test_bit(starts, hi) && !test_bit(targets, hi)) {
            Insn next;
            if (!decode(begin + hi, end, &next) || (closed ? !is_padding(&next) : next.kind == KIND_UNSUPPORTED)) break;
            w[n++] = next;
            hi += next.info.length;
            if (next.kind == KIND_CALL) break;
            closed = closed || next.kind == KIND_JMP || next.kind == KIND_RET;
        }
        /* Backward: the current first instruction becomes interior, so it must not be a target. */
        while (hi - lo < JMP_BYTES && lo > next_free && n < MAX_INSNS && !test_bit(targets, lo)) {
            uint64_t prev = lo - 1;
            while (prev > next_free && lo - prev < 15 && !test_bit(starts, prev)) --prev;
            Insn insn;
            if (!test_bit(starts, prev) || prev < next_free || !decode(begin + prev, end, &insn) ||
                prev + insn.info.length != lo || (insn.kind != KIND_PLAIN && insn.kind != KIND_JCC))
                break;
            memmove(&w[1], &w[0], (size_t)n * sizeof *w);
            w[0] = insn;
            ++n;
            lo = prev;
        }
        /* Undecodable bytes this close: probably data that happens to contain 0F 31. */
        size_t k = 0;
        while (k < ninvalid && invalid[k] + DATA_MARGIN < lo) ++k; /* invalid[] is sorted */
        int near_data = k < ninvalid && invalid[k] < hi + DATA_MARGIN;
        Out o = {area + *area_used, area + TSC_PATCH_AREA, 1};
        if (hi - lo >= JMP_BYTES && hi - lo <= MAX_WINDOW && !near_data) {
            emit_window(&o, w, n, begin + hi, multiplier);
            /* the patch jump must reach the trampoline too */
            int64_t delta = (int64_t)((intptr_t)(area + *area_used) - (intptr_t)(begin + lo + JMP_BYTES));
            if (delta < INT32_MIN || delta > INT32_MAX) o.ok = 0;
        } else o.ok = 0;
        if (!o.ok) {
            report.skipped++;
            fprintf(stderr, "TSC: rdtsc at image offset 0x%llx left unpatched\n", (unsigned long long)(offset + rdtsc));
            continue;
        }
        unsigned char *trampoline = area + *area_used;
        *area_used = (size_t)(o.p - area);
        unsigned char *site = begin + lo;
        int32_t rel = (int32_t)((intptr_t)trampoline - (intptr_t)(site + JMP_BYTES));
        site[0] = 0xE9;
        memcpy(site + 1, &rel, 4);
        memset(site + JMP_BYTES, 0x90, (size_t)(hi - lo - JMP_BYTES));
        report.windows++;
        next_free = hi;
    }
    free(starts);
    free(targets);
    free(rdtscs);
    free(invalid);
    return report;
}
