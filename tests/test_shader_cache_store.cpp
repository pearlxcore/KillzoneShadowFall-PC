// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/shader_cache_store.h"
#include "video_core/cache_write_queue.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <cstdlib>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <xxhash.h>
using Storage::ShaderCacheStore;
namespace fs=std::filesystem;
static std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
static void Write(const fs::path& path,const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    assert(file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
}
int main(int argc,char** argv) {
    if(argc==3 && std::string(argv[1])=="--crash-write") {
        ShaderCacheStore store(argv[2]);assert(store.Prepare(true));
        assert(store.Write("crash.key",std::vector<uint8_t>{22,23,24}));
        std::_Exit(0); // No destructor/snapshot: only the append journal survives.
    }
    const auto root=fs::temp_directory_path()/
        ("kzsf-storage-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const std::vector<uint8_t> a{1,2,3,4},b{5,6,7,8},c{9,10,11,12};
    {
        ShaderCacheStore store(root);
        assert(store.Write("a.key",a));assert(store.Write("b.meta",b));assert(store.Write("c.spv",c));
        assert(store.Write("profile.bin",a));
        assert(store.Prepare(false));
        assert(store.Names(".key").size()==1);assert(store.Names(".meta").size()==1);
        assert(store.GetStats().scans==1);
        std::vector<uint8_t> data;assert(store.Read("a.key",data)&&data==a);
        assert(!store.Read("../a.key",data));assert(!store.Write("../escape.key",a));
    }
    // Disk-space failure leaves the complete loose cache usable.
    {ShaderCacheStore store(root);assert(!store.Prepare(true,{},0));assert(!store.GetStats().packed);}
    // Cancellation during conversion leaves no active packed store.
    {ShaderCacheStore store(root);assert(!store.Prepare(true,[](auto,auto){return false;}));
     assert(!fs::exists(root/"packed-v1"));assert(fs::exists(root/"a.key"));}
    {ShaderCacheStore store(root);assert(!store.Prepare(true,[](auto done,auto total){return !total||done<1;}));
     assert(!fs::exists(root/"packed-v1"));assert(fs::exists(root/"b.meta"));}
    {
        ShaderCacheStore store(root);assert(store.Prepare(true));
        assert(store.GetStats().packed);assert(fs::exists(root/"a.key"));
        assert(store.Names(".key").size()==1);
        // Duplicate replacement is append-only; counts and names stay unique.
        assert(store.Write("a.key",b));std::vector<uint8_t> data;
        assert(store.Read("a.key",data)&&data==b);
        assert(store.Names(".key").size()==1);
        std::vector<std::future<void>> reads;
        for(int n=0;n<8;++n)reads.push_back(std::async(std::launch::async,[&]{
            for(int i=0;i<200;++i){std::vector<uint8_t> value;assert(store.Read("a.key",value)&&value==b);}
        }));
        for(auto& task:reads)task.get();
        // A second process/store cannot race the packed writer or cleanup.
        ShaderCacheStore other(root);assert(!other.Prepare(true));assert(!other.Write("a.key",c));
        assert(store.Flush());
    }
    {
        // Reopen using the index without enumerating loose shader files.
        ShaderCacheStore store(root);assert(store.Prepare(true));assert(store.GetStats().scans==0);
        std::vector<uint8_t> data;assert(store.Read("a.key",data)&&data==b);
        // Changed legacy records are kept, identical copies are removed.
        assert(store.CompletePreload());assert(fs::exists(root/"a.key"));
        assert(!fs::exists(root/"b.meta"));assert(fs::exists(root/"profile.bin"));
    }
    auto index=Read(root/"packed-v1/index"),payload=Read(root/"packed-v1/data");
    {
        // Isolate one bad data record, leaving another readable.
        auto bad=payload;size_t pos=40;uint64_t offset=0;
        while(pos<index.size()) {
            uint32_t length=0;for(unsigned i=0;i<4;++i)length|=uint32_t(index[pos+32+i])<<(8*i);
            const std::string name(reinterpret_cast<char*>(index.data()+pos+36),length);
            if(name=="a.key") {for(unsigned i=0;i<8;++i)offset|=uint64_t(index[pos+8+i])<<(8*i);break;}
            pos+=44+length;
        }
        bad[offset]^=0xff;Write(root/"packed-v1/data",bad);
        ShaderCacheStore store(root);assert(store.Prepare(true));std::vector<uint8_t> data;
        assert(!store.Read("a.key",data));assert(store.Read("c.spv",data)&&data==c);
    }
    Write(root/"packed-v1/data",payload);
    // Corrupt an indexed offset but preserve record checksum: reject just that
    // entry rather than trusting offset+length or reading arbitrary file data.
    {
        auto bad=index;const size_t begin=40;
        for(size_t i=begin+8;i<begin+16;++i)bad[i]=0xff;
        uint32_t length=0;for(unsigned i=0;i<4;++i)length|=uint32_t(bad[begin+32+i])<<(8*i);
        const size_t end=begin+36+length;
        uint64_t hash=XXH3_64bits(bad.data()+begin,end-begin);
        for(unsigned i=0;i<8;++i)bad[end+i]=uint8_t(hash>>(8*i));
        Write(root/"packed-v1/index",bad);
        ShaderCacheStore store(root);assert(store.Prepare(true));assert(store.GetStats().corrupt==1);
    }
    Write(root/"packed-v1/index",index);
    {
        auto bad=index;bad.resize(13);Write(root/"packed-v1/index",bad);
        ShaderCacheStore store(root);assert(!store.Prepare(true));
        std::vector<uint8_t> data;assert(store.Read("a.key",data)&&data==a);
    }
    Write(root/"packed-v1/index",index);
    {
        // A torn journal tail is discarded, and does not poison the next append.
        Write(root/"packed-v1/journal",{1,2,3});
        ShaderCacheStore store(root);assert(store.Prepare(true));
        assert(store.Write("d.key",c));assert(store.Flush());
    }
    {
        ShaderCacheStore store(root);assert(store.Prepare(true));std::vector<uint8_t> data;
        assert(store.Read("d.key",data)&&data==c);
    }
    {
        const std::string folder=root.string();
#ifdef _WIN32
        const char* args[]={argv[0],"--crash-write",folder.c_str(),nullptr};
        assert(_spawnv(_P_WAIT,argv[0],args)==0);
#else
        const pid_t child=fork();assert(child>=0);
        if(child==0){execl(argv[0],argv[0],"--crash-write",folder.c_str(),nullptr);std::_Exit(1);}
        int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
#endif
        ShaderCacheStore store(root);assert(store.Prepare(true));std::vector<uint8_t> data;
        assert(store.Read("crash.key",data)&&(data==std::vector<uint8_t>{22,23,24}));
        // Profile invalidation removes both storage representations.
        fs::create_directory(root/"pipeline-binaries");Write(root/"pipeline-binaries/driver.bin",a);
        store.Clear();assert(!fs::exists(root/"packed-v1"));assert(!fs::exists(root/"profile.bin"));
        assert(Read(root/"pipeline-binaries/driver.bin")==a);
        assert(store.Write("profile.bin",b));
    }
    {
        Storage::CacheWriteQueue queue(16);std::atomic<unsigned> accepted{},ran{};
        for(unsigned i=0;i<1000;++i)if(queue.Submit([&]{++ran;},4))++accepted;
        queue.Drain();assert(ran==accepted);queue.Close();
        assert(ran==1000);assert(!queue.Submit([]{}));
    }
    {
        Storage::CacheWriteQueue queue;std::atomic<unsigned> ran{};
        queue.Submit([]{throw std::runtime_error("intentional");});
        queue.Submit([&]{++ran;});queue.Close();assert(ran==1&&queue.Errors()==1);
    }
    {
        Storage::CacheWriteQueue queue(16);std::atomic<unsigned> accepted{},ran{};
        std::thread producer([&]{for(unsigned i=0;i<10000;++i) {
            if(!queue.Submit([&]{++ran;},4))break;
            ++accepted;
        }});
        while(accepted<10)std::this_thread::yield();
        queue.Close();producer.join();assert(accepted==ran);
    }
    fs::remove_all(root);
    std::cout<<"PASS: loose/packed cache, migration, recovery, concurrent reads and draining writer\n";
}
