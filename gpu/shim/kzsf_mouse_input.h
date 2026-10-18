#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Mouse counts become a held right-stick velocity over short time windows.
// Sampling the pad more often must not consume the movement multiple times.
struct KillzoneMouseInput {
    double pending_x{}, pending_y{};
    float x{}, y{}, gain=1.f, aim_gain=1.f, horizontal_gain=1.f, vertical_gain=1.f;
    uint64_t sampled{}, moved{};
    unsigned buttons{};
    bool captured{}, invert{};
    void Reset(bool active, uint64_t now) {
        pending_x=pending_y=0; x=y=0; buttons=0;
        sampled=moved=now; captured=active;
    }
    void Motion(float dx,float dy,uint64_t now) {
        if (!captured || !std::isfinite(dx) || !std::isfinite(dy)) return;
        const float sensitivity=(buttons&2u) ? aim_gain : gain;
        pending_x+=dx*sensitivity*horizontal_gain;
        pending_y+=dy*sensitivity*vertical_gain; moved=now;
    }
    void Sample(uint64_t now,int16_t& rx,int16_t& ry) {
        rx=ry=0;
        if (!captured) return;
        const uint64_t elapsed=now-sampled;
        if (elapsed>=8000) {
            const double scale=1000000.0/(900.0*elapsed);
            x=float(std::clamp(pending_x*scale,-1.0,1.0));
            y=float(std::clamp(pending_y*scale*(invert ? -1 : 1),-1.0,1.0));
            pending_x=pending_y=0; sampled=now;
        }
        if (now-moved>24000) x=y=0;
        rx=int16_t(x*32767); ry=int16_t(y*32767);
    }
};
