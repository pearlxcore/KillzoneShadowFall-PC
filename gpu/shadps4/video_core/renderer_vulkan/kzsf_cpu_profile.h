// Opt-in wall-clock attribution on the calling render thread; includes nested waits.
#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "gow3_toggles.h"

namespace Vulkan::CpuRenderProfile {
enum Stage { Filter, Pipeline, Targets, Resources, RenderState, Geometry, Commands,
             ComputePipeline, ComputeSetup, ComputeResources, ComputeCommands, Count };
inline constexpr const char* Names[]={"draw filter/pending work","graphics pipeline lookup",
    "draw target preparation","draw resource binding","render state/attachments",
    "vertex/index/motion preparation","graphics driver commands/cleanup",
    "compute pipeline lookup","compute HLE/setup","compute resource binding","compute driver commands/cleanup"};
inline bool Enabled() {
    static const bool on=[] {const char* v=std::getenv("KZSF_CPU_PROFILE");return v && v[0]=='1';}();
    return on;
}
using Clock=std::chrono::steady_clock;
struct Totals {
    std::array<uint64_t,Count> ns{},calls{};
    Clock::time_point start=Clock::now();
    uint64_t frames=Gow3Stats::gpu_frames.load(std::memory_order_relaxed);
};
inline thread_local Totals totals;
class Span {
public:
    explicit Span(Stage value):active(Enabled()),stage(value) {if(active) last=Clock::now();}
    void Step(Stage next) {
        if(!active) return;
        Add(Clock::now());stage=next;
    }
    void Finish() {
        if(!active) return;
        const auto now=Clock::now();Add(now);active=false;
        if(now-totals.start<std::chrono::seconds(5)) return;
        const auto frame=Gow3Stats::gpu_frames.load(std::memory_order_relaxed);
        const auto frames=frame-totals.frames;
        if(!frames) return;
        std::printf("CPU render profile: frames=%llu elapsed_ms=%.1f (wall time includes waits; stages exclude each other)\n",
            (unsigned long long)frames,std::chrono::duration<double,std::milli>(now-totals.start).count());
        for(unsigned i=0;i<Count;++i) if(totals.calls[i])
            std::printf("  CPU stage: %.3f ms/frame %.1f calls/frame %s\n",
                double(totals.ns[i])/1e6/frames,double(totals.calls[i])/frames,Names[i]);
        std::fflush(stdout);totals.ns={};totals.calls={};totals.start=Clock::now();totals.frames=frame;
    }
    ~Span(){Finish();}
private:
    void Add(Clock::time_point now) {
        totals.ns[stage]+=std::chrono::duration_cast<std::chrono::nanoseconds>(now-last).count();
        ++totals.calls[stage];last=now;
    }
    bool active;
    Stage stage;
    Clock::time_point last{};
};
}
