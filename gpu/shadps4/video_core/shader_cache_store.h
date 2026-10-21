// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Storage {
// Storage versioning is independent of shader/pipeline serialization versions.
// Synchronous storage operations; DataBase owns the single background writer.
class ShaderCacheStore {
public:
    using Progress = std::function<bool(uint64_t, uint64_t)>;
    struct Stats {
        uint64_t scan_ns{}, read_ns{}, reads{}, bytes{}, corrupt{}, scans{};
        uint64_t index_bytes{};
        bool packed{};
    };
    explicit ShaderCacheStore(std::filesystem::path root);
    ~ShaderCacheStore();
    ShaderCacheStore(const ShaderCacheStore&) = delete;
    ShaderCacheStore& operator=(const ShaderCacheStore&) = delete;

    // Call only after the renderer validates profile.bin. Returns false on
    // cancelled/failed migration; the original loose files remain usable.
    bool Prepare(bool packed, const Progress& progress = {},
                 uint64_t available_bytes = UINT64_MAX);
    bool Read(const std::string& filename, std::vector<uint8_t>& data);
    bool Write(const std::string& filename, std::span<const uint8_t> data);
    std::vector<std::string> Names(const std::string& extension);
    bool Flush();
    void Clear();
    // Only after a completed full warm-up without corrupt records/build failures.
    bool CompletePreload(const Progress& progress = {});
    Stats GetStats() const;
    static constexpr uint64_t MaxBlobBytes = 64ull * 1024 * 1024;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Storage
