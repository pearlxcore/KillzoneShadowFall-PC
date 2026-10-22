#pragma once
#include <atomic>
#include <cstdint>
#include "../../src/kzsf_character_models.h"

// Overlay/hotkey producers and one engine consumer. Feedback carries its request
// generation so an older completion cannot dismiss a newer selection.
struct KzsfCharacterSelector {
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> available{1};
    std::atomic<uint64_t> requested{0}, consumed{0}, feedback{0};
    std::atomic<int> current{0};
    void Reset() {requested.store(0);consumed.store(0);feedback.store(0);current.store(0);available.store(1);}
    void Request(int model) {
        if (!enabled.load() || !kzsf_character_model_selectable(model)) return;
        const uint64_t epoch = generation.fetch_add(1) + 1;
        const uint64_t token=(epoch << 32) | uint32_t(model + 1);
        auto previous=requested.load();
        while((previous>>32)<epoch && !requested.compare_exchange_weak(previous,token)) {}
    }
    uint64_t Take() {
        const auto token = requested.load();
        return consumed.exchange(token) == token ? 0 : token;
    }
    void Publish(uint64_t token, int model, int status, uint64_t mask) {
        if (model >= 0 && model < KZSF_CHARACTER_MODEL_COUNT) current.store(model);
        available.store(mask & kzsf_character_model_mask());
        feedback.store((token & 0xffffffff00000000ULL) | uint32_t(status));
    }
    int Status() const {
        const auto token = requested.load(), reply = feedback.load();
        return (token >> 32) != (reply >> 32) ? 2 : int(uint32_t(reply));
    }
    int Selected() const {
        const auto token = requested.load();
        const int status = Status();
        return token && (status == 1 || status == 2) ? int(uint32_t(token)) - 1 : current.load();
    }
    void Cycle(int direction,uint64_t mask) {
        const auto token=requested.load();
        const int cursor=token ? int(uint32_t(token))-1 : current.load();
        Request(kzsf_character_next_loaded(cursor,direction,mask));
    }
};
