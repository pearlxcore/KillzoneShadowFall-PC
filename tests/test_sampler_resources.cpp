// Regression: translated depth/post-operation variants may exceed 16 guest sampler slots.
#include "shader_recompiler/info.h"
#include "common/serdes.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
// Standalone test: any serialization bounds assertion must fail the process.
void assert_fail_impl() { std::abort(); }
namespace Common::Log {
bool ShouldLog(Level) { return false; }
void Write(std::string_view,Level,const char*,int,const char*,std::string&&) { std::abort(); }
}
int main() {
    Shader::SamplerResourceList list;
    try {
        for (unsigned i=0;i<17;++i) {
            Shader::SamplerResource sampler{};
            sampler.sharp_fetch.immediates[0]=i;
            sampler.is_depth=(i%2)!=0;
            list.push_back(sampler);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr,"17th translated sampler failed: %s\n",error.what());
        return 1;
    }
    assert(list.size()==17);
    auto copy=list;
    for (unsigned i=0;i<17;++i) {
        assert(copy[i].sharp_fetch.immediates[0]==i);
        assert(copy[i].is_depth==((i%2)!=0));
    }
    copy.clear();
    assert(copy.empty() && list.size()==17);
    Shader::InfoPersistent original{Shader::HwStage::Fragment,Shader::SwStage::Fragment,0x123456};
    original.samplers=list;
    original.mrt_mask=3;
    original.shared_memory_scratch_size=128;
    original.tess_consts_dword_offset=42;
    original.has_bitwise_xor=true;
    Serialization::Archive archive;
    Serialization::Writer writer{archive};
    Shader::InfoPersistent::PersistentFields(original,[&](const auto& field){writer.Write(field);});
    Serialization::Archive input{archive.TakeOff()};
    Shader::InfoPersistent restored{};
    Serialization::Reader reader{input};
    Shader::InfoPersistent::PersistentFields(restored,[&](auto& field){reader.Read(field);});
    assert(input.IsEoS());
    assert(restored.samplers.size()==17 && restored.pgm_hash==original.pgm_hash);
    assert(restored.mrt_mask==3 && restored.shared_memory_scratch_size==128);
    assert(restored.tess_consts_dword_offset==42 && restored.has_bitwise_xor);
    assert(restored.hw_stage==Shader::HwStage::Fragment);
    for (unsigned i=0;i<17;++i) {
        assert(restored.samplers[i].sharp_fetch.immediates[0]==i);
        assert(restored.samplers[i].is_depth==((i%2)!=0));
    }
    for (unsigned cut=0;cut<sizeof(uint64_t);++cut) {
        Serialization::Archive damaged{std::vector<u8>(cut),true};
        Serialization::Reader input{damaged};uint64_t value{};bool rejected=false;
        try{input.Read(value);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);
    }
    {
        std::vector<u8> bytes(sizeof(size_t),0xff);
        Serialization::Archive damaged{std::move(bytes),true};
        Serialization::Reader input{damaged};std::string value;bool rejected=false;
        try{input.Read(value);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected&&value.empty());
    }
    puts("Sampler/cache round trip and recoverable truncation/length checks pass");
}
