#include "../gpu/shadps4/video_core/buffer_cache/arena_window.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace VideoCore;
int main() {
    constexpr std::uint64_t GiB=1ull<<30, block=65536;
    // Repeated adjacent boundary crossings must not grow a 2-GiB arena to 3 GiB.
    for (std::uint64_t page=0;page<1000;++page) {
        auto window=MakeArenaWindow(page,page+1,30,2*GiB);
        assert(window && window->base==page*GiB && window->size==2*GiB);
    }
    assert(!MakeArenaWindow(0,2,30,2*GiB));
    assert(!MakeArenaWindow(0,1,30,GiB));
    assert(!MakeArenaWindow(2,1,30,2*GiB));
    assert(!MakeArenaWindow(0,0,64,2*GiB));
    assert(!MakeArenaWindow(std::numeric_limits<std::uint64_t>::max(),
                           std::numeric_limits<std::uint64_t>::max(),30,2*GiB));
    const ArenaWindow left{0,2*GiB},right{GiB,2*GiB};
    // Physical backing straddles two overlapping windows. The right window
    // starts partway into the allocation, so its memory offset must advance.
    auto a=IntersectArenaBacking(left,GiB-block,2*block,4*block);
    auto b=IntersectArenaBacking(right,GiB-block,2*block,4*block);
    assert(a && a->resource_offset==GiB-block && a->size==2*block && a->memory_offset==4*block);
    assert(b && b->resource_offset==0 && b->size==block && b->memory_offset==5*block);
    auto physical=[](ArenaWindow w,ArenaWindowBind binding,std::uint64_t guest) {
        return binding.memory_offset+guest-w.base-binding.resource_offset;
    };
    assert(physical(left,*a,GiB+4096)==physical(right,*b,GiB+4096));
    // A later resident block must map identically into both retained views.
    a=IntersectArenaBacking(left,GiB+2*block,block,0);
    b=IntersectArenaBacking(right,GiB+2*block,block,0);
    assert(a && b && a->size==block && b->size==block);
    assert(physical(left,*a,GiB+2*block+17)==17);
    assert(physical(right,*b,GiB+2*block+17)==17);
    assert(!IntersectArenaBacking(left,2*GiB,block,0));
    assert(!IntersectArenaBacking(right,0,block,0));
    assert(!IntersectArenaBacking({~0ull-1,8},0,block,0));
    assert(!IntersectArenaBacking(left,~0ull-1,8,0));
    assert(!IntersectArenaBacking(right,GiB-block,2*block,~0ull-1));
    std::puts("Arena window regression tests passed");
}
