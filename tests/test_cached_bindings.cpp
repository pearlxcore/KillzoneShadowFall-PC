#include "../gpu/shadps4/video_core/renderer_vulkan/vk_cached_binding_check.h"
#include <cassert>
#include <iostream>
int main() {
    // Minimal descriptor-instruction fixture; independent of Vulkan/device creation.
    std::vector<std::uint32_t> spv={0x07230203,0x10000,0,100,0,
        (4u<<16)|71,42,33,16, // descriptor binding16 (the reproduced failure)
        (4u<<16)|71,42,34,0,  // descriptor set0
        (4u<<16)|59,1,42,12}; // storage-buffer variable
    const std::uint32_t wrong[]={0,1,2},right[]={0,16};
    assert(!Vulkan::CachedBindingsMatch(spv,wrong));
    assert(Vulkan::CachedBindingsMatch(spv,right));
    spv[12]=1; assert(!Vulkan::CachedBindingsMatch(spv,right)); spv[12]=0;
    spv.pop_back(); assert(!Vulkan::CachedBindingsMatch(spv,right));
    assert(!Vulkan::CachedBindingsMatch({},right));
    spv={0x07230203,0x10000,0,100,0,0};
    assert(!Vulkan::CachedBindingsMatch(spv,right));
    std::cout<<"Cached binding guard: missing binding16, matching layout, wrong set and malformed stream passed\n";
}
