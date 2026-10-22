// SPDX-License-Identifier: GPL-2.0-or-later
#include "kzsf_actors.h"
#include "kzsf_settings.h"
#include <cstdint>
#include "../../src/actor_hook.h"

alignas(64) KzsfActorSlot kzsf_actor_slots[2];

extern "C" uintptr_t kzsf_actor_slots_address(void) {
    return reinterpret_cast<uintptr_t>(kzsf_actor_slots);
}

extern "C" void kzsf_actor_status(int supported) {
    KzsfSettings::Get().actor_watch_supported = supported != 0;
}
