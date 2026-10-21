// SPDX-License-Identifier: GPL-2.0-or-later
// Storage-only benchmark. It does not measure Vulkan pipeline creation.
#include "video_core/shader_cache_store.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <xxhash.h>
namespace fs=std::filesystem;
int main(int argc,char** argv) try {
    if(argc!=3)throw std::runtime_error("usage: benchmark-shader-cache original|loose|packed|migrate isolated-copy");
    const std::string mode=argv[1];const fs::path root=argv[2];
    if(!fs::is_regular_file(root/".benchmark-copy"))throw std::runtime_error("Use tools/benchmark-shader-cache.py to create an isolated copy first");
    if(mode!="original"&&mode!="loose"&&mode!="packed"&&mode!="migrate")throw std::runtime_error("Unknown mode");
    const auto start=std::chrono::steady_clock::now();
    uint64_t records=0,bytes=0,digest=0;
    Storage::ShaderCacheStore store(root);
    if(mode!="original"&&!store.Prepare(mode=="packed"||mode=="migrate"))throw std::runtime_error("Cache preparation failed");
    for(const auto* ext:{".key",".meta",".spv"}) {
        if(mode=="original") {
            // The old CountBlobs and ForEachBlob independently enumerate disk.
            size_t expected=0;
            for(const auto& item:fs::directory_iterator(root))if(item.path().extension()==ext)++expected;
            size_t count=0;
            for(const auto& item:fs::directory_iterator(root))if(item.path().extension()==ext) {
                const auto size=fs::file_size(item.path());
                if(size>Storage::ShaderCacheStore::MaxBlobBytes)throw std::runtime_error("Oversize record");
                std::vector<uint8_t> data(size);std::ifstream file(item.path(),std::ios::binary);
                if(!file.read(reinterpret_cast<char*>(data.data()),size))throw std::runtime_error("Record read failed");
                const auto name=item.path().filename().string();
                digest^=XXH3_64bits(data.data(),data.size())+XXH3_64bits(name.data(),name.size());
                ++records;++count;bytes+=data.size();
            }
            if(count!=expected)throw std::runtime_error("Cache changed during benchmark");
        } else for(const auto& name:store.Names(ext)) {
            std::vector<uint8_t> data;if(!store.Read(name,data))throw std::runtime_error("Record rejected: "+name);
            digest^=XXH3_64bits(data.data(),data.size())+XXH3_64bits(name.data(),name.size());
            ++records;bytes+=data.size();
        }
    }
    const auto stats=store.GetStats();
    std::cout<<"{\"mode\":\""<<mode<<"\",\"ms\":"
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
             <<",\"records\":"<<records<<",\"bytes\":"<<bytes<<",\"digest\":"<<digest
             <<",\"scans\":"<<stats.scans<<",\"read_ms\":"<<stats.read_ns/1e6<<"}\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
