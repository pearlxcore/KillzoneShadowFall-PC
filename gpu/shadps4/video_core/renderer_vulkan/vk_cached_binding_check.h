// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
#include <algorithm>
namespace Vulkan {
// Reject cached stages whose descriptor locations do not match the restored
// layout BEFORE passing them to a driver. This checks descriptor locations,
// not full SPIR-V validity or descriptor types.
inline bool CachedBindingsMatch(std::span<const std::uint32_t> words,
                                 std::span<const std::uint32_t> allowed) {
    if(words.size()<5 || words[0]!=0x07230203) return false;
    std::unordered_map<std::uint32_t,std::uint32_t> bindings,sets;
    std::vector<std::uint32_t> variables;
    for(std::size_t i=5;i<words.size();) {
        const auto count=words[i]>>16,opcode=words[i]&0xffff;
        if(!count || count>words.size()-i) return false;
        if(opcode==71 && count>=4) { // OpDecorate
            if(words[i+2]==33) bindings[words[i+1]]=words[i+3];
            if(words[i+2]==34) sets[words[i+1]]=words[i+3];
        }
        if(opcode==59 && count>=4 && // OpVariable: UniformConstant, Uniform, StorageBuffer
           (words[i+3]==0 || words[i+3]==2 || words[i+3]==12)) variables.push_back(words[i+2]);
        i+=count;
    }
    for(const auto variable:variables) {
        const auto binding=bindings.find(variable),set=sets.find(variable);
        if(binding==bindings.end() || set==sets.end() || set->second!=0 ||
           std::ranges::find(allowed,binding->second)==allowed.end()) return false;
    }
    return true;
}
} // namespace Vulkan
