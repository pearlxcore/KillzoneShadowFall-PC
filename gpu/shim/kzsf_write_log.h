// SPDX-License-Identifier: GPL-2.0-or-later
// kzsf: KZSF_WRITE_LOG=1 — a ring of the writes the GPU emulation makes to guest memory (fences,
// WriteData, buffer and image downloads), dumped when the guest faults: which of them landed
// where the guest's data got corrupted. Diagnostic only.
#pragma once

#include <cstdint>

namespace KzsfWriteLog {
enum Source : std::uint32_t { Backing, WriteData, Fence, FenceIntent, WriteDataIntent, EosIntent };
bool Enabled();
/// KZSF_WRITE_LOG=1: at the write itself (changes the timing of those writes).
void Note(std::uint64_t address, const void* data, std::uint64_t size, Source source);
/// KZSF_WRITE_LOG=2: where the GPU command thread decodes a fence or WriteData, off the write
/// path (the address and value it will write later).
/// KZSF_WRITE_LOG=3 records both intents and actual writes in the same ring.
void NoteIntent(std::uint64_t address, const void* data, std::uint64_t size, Source source);
}

extern "C" void kzsfgpu_dump_guest_writes(void* ucontext);
