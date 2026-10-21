// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_cache_store.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <xxhash.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Storage {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr uint64_t Magic = 0x314843414346534bull; // KSFCACH1, little endian
constexpr uint32_t Version = 1;
constexpr uint64_t MaxIndexBytes = 256ull * 1024 * 1024;
uint64_t Ns(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
}
uint64_t Hash(std::span<const uint8_t> bytes) { return XXH3_64bits(bytes.data(), bytes.size()); }
void Number(std::vector<uint8_t>& bytes, uint64_t value, unsigned count = 8) {
    for (unsigned i=0;i<count;++i) bytes.push_back(uint8_t(value>>(8*i)));
}
bool Number(std::span<const uint8_t>& bytes, uint64_t& value, unsigned count = 8) {
    if (bytes.size()<count) return false;
    value=0;
    for(unsigned i=0;i<count;++i) value|=uint64_t(bytes[i])<<(8*i);
    bytes=bytes.subspan(count); return true;
}
bool Valid(const std::string& name) {
    if(name.empty() || name.size()>128 || name=="." || name=="..") return false;
    return std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-';
    });
}
bool Packable(const std::string& name) {
    const auto ext=fs::path(name).extension();
    return Valid(name) && (ext==".key" || ext==".meta" || ext==".spv");
}
bool ReadFile(const fs::path& path, std::vector<uint8_t>& out, uint64_t maximum) {
    out.clear(); std::error_code ec; const auto size=fs::file_size(path,ec);
    if(ec || size>maximum || !size) return false;
    std::ifstream file(path,std::ios::binary);
    if(!file) return false;
    out.resize(size);
    if(!file.read(reinterpret_cast<char*>(out.data()),std::streamsize(size))) {out.clear();return false;}
    return true;
}
bool Durable(const fs::path& path) {
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    const bool ok=FlushFileBuffers(file)!=0; CloseHandle(file); return ok;
#else
    int file=open(path.c_str(),O_RDWR); if(file<0) return false;
    bool ok=fsync(file)==0; close(file); return ok;
#endif
}
bool Replace(const fs::path& from, const fs::path& to, bool durable = true) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|
                      (durable ? MOVEFILE_WRITE_THROUGH : 0))!=0;
#else
    std::error_code ec; fs::rename(from,to,ec); if(ec) return false;
    if(durable) {
        int dir=open(to.parent_path().c_str(),O_RDONLY|O_DIRECTORY);
        if(dir>=0) {fsync(dir);close(dir);}
    }
    return true;
#endif
}
bool AtomicFile(const fs::path& path, std::span<const uint8_t> bytes, bool durable = true) {
    auto tmp=path; tmp+=".tmp";
    {std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
     if(!out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()))) return false;}
    return (!durable || Durable(tmp)) && Replace(tmp,path,durable);
}
struct Entry {uint64_t offset{},size{},hash{},sequence{};};
std::vector<uint8_t> Encode(const std::string& name, const Entry& entry) {
    std::vector<uint8_t> bytes;
    Number(bytes,entry.sequence); Number(bytes,entry.offset); Number(bytes,entry.size); Number(bytes,entry.hash);
    Number(bytes,name.size(),4); bytes.insert(bytes.end(),name.begin(),name.end());
    Number(bytes,Hash(bytes)); return bytes;
}
bool Decode(std::span<const uint8_t>& bytes, std::string& name, Entry& entry) {
    const auto original=bytes; uint64_t length{},checksum{};
    if(!Number(bytes,entry.sequence)||!Number(bytes,entry.offset)||!Number(bytes,entry.size)||
       !Number(bytes,entry.hash)||!Number(bytes,length,4)||length>128||bytes.size()<length+8) return false;
    name.assign(reinterpret_cast<const char*>(bytes.data()),size_t(length)); bytes=bytes.subspan(length);
    const size_t hashed=original.size()-bytes.size();
    if(!Number(bytes,checksum)||checksum!=Hash(original.first(hashed))) return false;
    return Packable(name) && entry.size && entry.size<=ShaderCacheStore::MaxBlobBytes;
}
struct Lease {
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
    explicit Lease(const fs::path& path) {
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    }
    bool Held() const {return file!=INVALID_HANDLE_VALUE;}
    ~Lease(){if(Held())CloseHandle(file);}
#else
    int file=-1;
    explicit Lease(const fs::path& path) {
        file=open(path.c_str(),O_CREAT|O_RDWR,0600);
        if(file>=0&&flock(file,LOCK_EX|LOCK_NB)) {close(file);file=-1;}
    }
    bool Held() const {return file>=0;}
    ~Lease(){if(Held())close(file);}
#endif
};
} // namespace

struct ShaderCacheStore::Impl {
    fs::path root, packed;
    std::unique_ptr<Lease> lease;
    mutable std::shared_mutex mutex;
    std::unordered_map<std::string,Entry> entries;
    std::vector<std::string> loose;
    bool scanned=false, active=false, pending=false, dirty=false, prepared=false, migrating=false;
    uint64_t sequence=0;
    uint64_t data_end=0;
    std::ofstream output_data,output_journal;
    std::atomic<uint64_t> scan_ns{},read_ns{},reads{},bytes{},corrupt{},scans{};
    std::mutex readers_mutex;
    // One persistent stream per reader thread: no shared seek position, and no
    // file open/close per record. Streams are released before migration/cleanup.
    std::map<std::thread::id,std::unique_ptr<std::ifstream>> readers;
    explicit Impl(fs::path path):root(std::move(path)),packed(root/"packed-v1"){}
    bool Scan(const Progress& progress = {}) {
        if(scanned) return true;
        const auto start=Clock::now(); std::error_code ec; uint64_t visited=0;
        for(const auto& item:fs::directory_iterator(root,ec)) {
            const auto name=item.path().filename().string();
            if(Packable(name)&&item.is_regular_file(ec)) loose.push_back(name);
            if(progress && (++visited%256)==0 && !progress(visited,0)) {loose.clear();return false;}
        }
        if(ec){loose.clear();return false;}
        scanned=true; ++scans; scan_ns+=Ns(start);return true;
    }
    bool Bounds(const Entry& entry, uint64_t size) const {
        return entry.offset<=size && entry.size<=size-entry.offset;
    }
    bool LoadIndex(const Progress& progress = {}) {
        std::vector<uint8_t> data;
        if(!ReadFile(packed/"index",data,MaxIndexBytes)) return false;
        std::span<const uint8_t> input(data); uint64_t magic{},version{},count{},seq{};
        if(!Number(input,magic)||!Number(input,version)||!Number(input,count)||!Number(input,seq)||
           magic!=Magic||version!=Version||count>5000000) return false;
        uint64_t header_hash{};
        if(!Number(input,header_hash)||header_hash!=Hash(std::span(data).first(32)))return false;
        std::error_code ec; const auto size=fs::file_size(packed/"data",ec); if(ec)return false;
        for(uint64_t i=0;i<count;++i) {
            if(progress && i%256==0 && !progress(i,0))return false;
            std::string name; Entry entry;
            // Bad structural/index checksums cannot be safely skipped: use the
            // original loose backup. Bad data records are isolated by Read().
            if(!Decode(input,name,entry)||entry.sequence>seq) {entries.clear();return false;}
            if(Bounds(entry,size)) entries[name]=entry; else ++corrupt;
        }
        if(!input.empty()) {entries.clear();return false;}
        sequence=seq;
        data_end=size;
        std::vector<uint8_t> journal;
        uint64_t valid=0;
        if(fs::exists(packed/"journal") && fs::file_size(packed/"journal",ec)>MaxIndexBytes)return false;
        if(ReadFile(packed/"journal",journal,MaxIndexBytes)) {
            input=journal;
            while(!input.empty()) {
                std::string name; Entry entry;
                if(!Decode(input,name,entry)||!Bounds(entry,size)) break;
                valid=journal.size()-input.size();
                if(entry.sequence>sequence) {entries[name]=entry;sequence=entry.sequence;dirty=true;}
            }
        }
        // Discard only incomplete/uncommitted journal tail. Data beyond indexed
        // records is harmless and is never trusted or scanned for shaders.
        fs::resize_file(packed/"journal",valid,ec);
        pending=fs::exists(packed/"legacy.pending"); active=true; return true;
    }
    bool Snapshot() {
        if(!active) return true;
        if(output_data.is_open()&&!output_data.flush())return false;
        if(output_journal.is_open()&&!output_journal.flush())return false;
        std::vector<uint8_t> index;
        Number(index,Magic);Number(index,Version);Number(index,entries.size());Number(index,sequence);
        Number(index,Hash(index));
        for(const auto& [name,entry]:entries) {
            const auto bytes=Encode(name,entry);index.insert(index.end(),bytes.begin(),bytes.end());
            if(index.size()>MaxIndexBytes) return false;
        }
        if(!Durable(packed/"data") || !AtomicFile(packed/"index",index)) return false;
        // Entries already in the snapshot are ignored if a crash leaves the old journal.
        output_journal.close();
        if(!AtomicFile(packed/"journal",{}))return false;
        dirty=false;return true;
    }
    bool ReadPacked(const Entry& entry,std::vector<uint8_t>& out) {
        std::ifstream* file;
        {std::scoped_lock lock(readers_mutex);
         auto& stream=readers[std::this_thread::get_id()];
         if(!stream)stream=std::make_unique<std::ifstream>(packed/"data",std::ios::binary);
         file=stream.get();}
        file->clear();file->seekg(std::streamoff(entry.offset));out.resize(entry.size);
        if(!file->read(reinterpret_cast<char*>(out.data()),std::streamsize(entry.size))||Hash(out)!=entry.hash) {
            out.clear();++corrupt;return false;
        }
        return true;
    }
};

ShaderCacheStore::ShaderCacheStore(fs::path root):impl(std::make_unique<Impl>(std::move(root))) {
    std::error_code ec;fs::create_directories(impl->root,ec);
    impl->lease=std::make_unique<Lease>(impl->root/"packed.lock");
}
ShaderCacheStore::~ShaderCacheStore(){Flush();}

bool ShaderCacheStore::Prepare(bool use_packed,const Progress& progress,uint64_t available) {
    auto& s=*impl;
    if(s.prepared) return true;
    s.prepared=true;
    if(!use_packed) return s.Scan(progress);
    if(!s.lease->Held()) {s.Scan(progress);return false;}
    if(fs::exists(s.packed)) {
        const bool ok=s.LoadIndex(progress);
        if(!ok) {
            s.active=false;s.dirty=false;s.entries.clear();s.Scan(progress);
            std::fprintf(stderr,"Shader cache: packed index rejected; using loose cache\n");
        }
        return ok;
    }
    if(!s.Scan(progress)) return false;
    if(s.loose.empty())return true;
    uint64_t total_bytes=0;
    for(const auto& name:s.loose) {
        std::error_code ec;auto size=fs::file_size(s.root/name,ec);
        if(ec||size>MaxBlobBytes||size>UINT64_MAX-total_bytes) return false;
        total_bytes+=size;
    }
    std::error_code ec;
    if(available==UINT64_MAX)available=fs::space(s.root,ec).available;
    const uint64_t headroom=256ull*1024*1024;
    if(ec||available<headroom||total_bytes>available-headroom) {
        std::printf("Shader cache: insufficient space for migration; keeping loose files\n");return false;
    }
    // Staging has a fixed, private name under the cache root; never touch any
    // live/legacy data on cancellation or an interrupted migration.
    const auto staging=s.root/"packed-v1.migrating";
    fs::remove_all(staging,ec);ec.clear();fs::create_directories(staging,ec);if(ec)return false;
    const auto active=s.packed;s.packed=staging;s.active=true;s.migrating=true;
    bool ok=true;uint64_t done=0;
    for(const auto& name:s.loose) {
        if(progress&&!progress(done,s.loose.size()*2)) {ok=false;break;}
        std::vector<uint8_t> data;
        if(!ReadFile(s.root/name,data,MaxBlobBytes)||!Write(name,data)) {ok=false;break;}
        ++done;
    }
    if(ok)ok=Flush();
    s.output_data.close();s.output_journal.close();s.migrating=false;
    // Re-read/check every record before making the store visible. Working memory
    // stays bounded by a single blob plus the index, regardless of total size.
    if(ok)for(const auto& [name,entry]:s.entries) {
        if(progress&&!progress(done,s.loose.size()*2)) {ok=false;break;}
        std::vector<uint8_t> data;if(!s.ReadPacked(entry,data)){ok=false;break;}++done;
    }
    {std::scoped_lock lock(s.readers_mutex);s.readers.clear();}
    if(ok)ok=AtomicFile(staging/"legacy.pending",std::array<uint8_t,1>{1});
    if(ok)ok=Replace(staging,active);
    s.packed=active;
    if(!ok) {s.active=false;s.dirty=false;s.entries.clear();s.sequence=0;s.data_end=0;return false;}
    s.pending=true;
    std::printf("Shader cache: migrated %zu records, %llu bytes; loose backup retained through preload\n",
                s.entries.size(),static_cast<unsigned long long>(total_bytes));
    return true;
}
bool ShaderCacheStore::Read(const std::string& name,std::vector<uint8_t>& data) {
    data.clear();if(!Valid(name))return false;
    auto& s=*impl;auto start=Clock::now();bool ok=false;
    {std::shared_lock lock(s.mutex);
     auto found=s.entries.find(name);
     if(s.active&&found!=s.entries.end())ok=s.ReadPacked(found->second,data);
     else ok=ReadFile(s.root/name,data,MaxBlobBytes);}
    ++s.reads;s.bytes+=data.size();s.read_ns+=Ns(start);return ok;
}
bool ShaderCacheStore::Write(const std::string& name,std::span<const uint8_t> data) {
    if(!Valid(name)||data.empty()||data.size()>MaxBlobBytes)return false;
    auto& s=*impl;std::unique_lock lock(s.mutex);
    if(!s.lease->Held())return false;
    if(!s.active||!Packable(name)) {
        if(Packable(name)) {s.scanned=false;s.loose.clear();}
        // Match the legacy atomic-rename behavior without a per-shader disk
        // barrier during gameplay. Packed snapshots/profile commits are durable.
        return AtomicFile(s.root/name,data,name=="profile.bin");
    }
    if(!s.output_data.is_open())s.output_data.open(s.packed/"data",std::ios::binary|std::ios::app);
    if(!s.output_journal.is_open())s.output_journal.open(s.packed/"journal",std::ios::binary|std::ios::app);
    const uint64_t offset=s.data_end;
    if(!s.output_data.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size())))return false;
    if(!s.migrating&&!s.output_data.flush())return false;
    s.data_end+=data.size();
    Entry entry{offset,data.size(),Hash(data),s.sequence+1};const auto record=Encode(name,entry);
    if(!s.output_journal.write(reinterpret_cast<const char*>(record.data()),std::streamsize(record.size())))return false;
    if(!s.migrating&&!s.output_journal.flush())return false;
    s.sequence=entry.sequence;s.entries[name]=entry;s.dirty=true;return true;
}
std::vector<std::string> ShaderCacheStore::Names(const std::string& extension) {
    auto& s=*impl;std::vector<std::string> names;
    if(!s.active)s.Scan();
    std::shared_lock lock(s.mutex);
    if(s.active) {
        for(const auto& [name,entry]:s.entries)if(fs::path(name).extension()==extension)names.push_back(name);
        // Migration appended records in legacy enumeration order. Preserve it
        // for pipeline/permutation validation, and read sequential offsets.
        std::sort(names.begin(),names.end(),[&](const auto& a,const auto& b) {
            return s.entries.at(a).offset<s.entries.at(b).offset;
        });
    }
    else {for(const auto& name:s.loose)if(fs::path(name).extension()==extension)names.push_back(name);}
    return names;
}
bool ShaderCacheStore::Flush(){auto& s=*impl;std::unique_lock lock(s.mutex);return !s.dirty||s.Snapshot();}
void ShaderCacheStore::Clear() {
    auto& s=*impl;std::unique_lock lock(s.mutex);
    if(!s.lease->Held())return;
    {std::scoped_lock readers(s.readers_mutex);s.readers.clear();}
    s.output_data.close();s.output_journal.close();
    s.active=false;s.dirty=false;s.entries.clear();s.sequence=0;s.data_end=0;s.scanned=false;s.loose.clear();s.prepared=false;
    std::error_code ec;fs::remove_all(s.packed,ec);fs::remove_all(s.root/"packed-v1.migrating",ec);
    for(const auto& item:fs::directory_iterator(s.root,ec)) {
        const auto name=item.path().filename().string();
        if(Packable(name)||name=="profile.bin")fs::remove(item.path(),ec);
    }
}
bool ShaderCacheStore::CompletePreload(const Progress& progress) {
    auto& s=*impl;if(!s.active||!s.pending||s.corrupt)return false;
    std::unique_lock lock(s.mutex);
    // Re-scan only for migration cleanup, never during normal packed startup.
    s.Scan();uint64_t done=0;
    for(const auto& name:s.loose) {
        if(progress&&!progress(done,s.loose.size()))return false;
        // A legacy tool may have updated a file after migration. Keep changed
        // files; only delete byte-identical, verified copies of packed records.
        std::vector<uint8_t> data;auto found=s.entries.find(name);
        if(found!=s.entries.end()&&ReadFile(s.root/name,data,MaxBlobBytes)&&Hash(data)==found->second.hash) {
            std::error_code ec;fs::remove(s.root/name,ec);if(ec)return false;
        }
        ++done;
    }
    std::error_code ec;fs::remove(s.packed/"legacy.pending",ec);if(ec)return false;
    s.pending=false;s.loose.clear();return true;
}
ShaderCacheStore::Stats ShaderCacheStore::GetStats() const {
    auto& s=*impl;std::shared_lock lock(s.mutex);
    return {s.scan_ns.load(),s.read_ns.load(),s.reads.load(),s.bytes.load(),s.corrupt.load(),s.scans.load(),
            s.entries.size()*(sizeof(Entry)+128),s.active};
}
} // namespace Storage
