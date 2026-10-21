// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/emulator_settings.h"

#include "video_core/cache_storage.h"
#include "video_core/cache_write_queue.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

#include <miniz.h>

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>

namespace {

std::unique_ptr<Storage::CacheWriteQueue> writer;
std::unique_ptr<Storage::ShaderCacheStore> store;
mz_zip_archive zip_ar{};
bool ar_is_read_only{true};

} // namespace

namespace Storage {

constexpr std::string GetBlobFileExtension(BlobType type) {
    switch (type) {
    case BlobType::ShaderMeta: {
        return "meta";
    }
    case BlobType::ShaderBinary: {
        return "spv";
    }
    case BlobType::PipelineKey: {
        return "key";
    }
    case BlobType::ShaderProfile: {
        return "bin";
    }
    default:
        UNREACHABLE();
    }
}

void DataBase::Open() {
    if (opened) {
        return;
    }

    const auto& game_info = Common::ElfInfo::Instance();

    using namespace Common::FS;
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_zero_struct(&zip_ar);

        cache_path = GetUserPath(PathType::CacheDir) /
                     std::filesystem::path{game_info.GameSerial()}.replace_extension(".zip");

        if (!mz_zip_reader_init_file(&zip_ar, cache_path.string().c_str(),
                                     MZ_ZIP_FLAG_READ_ALLOW_WRITING) ||
            !mz_zip_validate_archive(&zip_ar, 0)) {
            LOG_INFO(Render, "Cache archive {} is not found or archive is corrupted",
                     cache_path.string().c_str());
            mz_zip_reader_end(&zip_ar);
            mz_zip_writer_init_file(&zip_ar, cache_path.string().c_str(), 0);
        }
    } else {
        cache_path = GetUserPath(PathType::CacheDir) / game_info.GameSerial();
        if (!std::filesystem::exists(cache_path)) {
            std::filesystem::create_directories(cache_path);
        }
    }

    if (!EmulatorSettings.IsPipelineCacheArchived())
        store = std::make_unique<ShaderCacheStore>(cache_path);
    writer = std::make_unique<CacheWriteQueue>();
    opened = true;
}

void DataBase::Close() {
    if (!IsOpened()) {
        return;
    }

    writer->Close();
    if (writer->Errors()) LOG_ERROR(Render, "Cache writer: {} failed jobs", writer->Errors());
    writer.reset();
    if (store && !store->Flush()) LOG_ERROR(Render, "Cache index could not be saved");
    store.reset();

    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_finalize_archive(&zip_ar);
        mz_zip_writer_end(&zip_ar);
    }

    opened = false;
    LOG_INFO(Render, "Cache dumped");
}

template <typename T>
bool WriteVector(const BlobType type, std::filesystem::path&& path, std::vector<T>&& v) {
    const size_t bytes = v.size() * sizeof(T);
    return writer->Submit([type, path = std::move(path), v = std::move(v)]() mutable {
        path.replace_extension(GetBlobFileExtension(type));
        if (EmulatorSettings.IsPipelineCacheArchived()) {
            ASSERT_MSG(!ar_is_read_only, "FinishPreload must precede ZIP writes");
            if (!mz_zip_writer_add_mem(&zip_ar, path.string().c_str(), v.data(),
                                      v.size()*sizeof(T), MZ_BEST_COMPRESSION))
                throw std::runtime_error("Cache archive write failed");
        } else if (!store->Write(path.filename().string(),
                                  {reinterpret_cast<const u8*>(v.data()),v.size()*sizeof(T)})) {
            throw std::runtime_error("Shader cache record write failed");
        }
    }, bytes);
}

template <typename T>
void LoadVector(BlobType type, std::filesystem::path& path, std::vector<T>& v) {
    using namespace Common::FS;
    path.replace_extension(GetBlobFileExtension(type));
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        int index{-1};
        index = mz_zip_reader_locate_file(&zip_ar, path.string().c_str(), nullptr, 0);
        if (index < 0) {
            LOG_WARNING(Render, "File {} is not found in the archive", path.string().c_str());
            return;
        }
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&zip_ar, index, &stat);
        v.resize(stat.m_uncomp_size / sizeof(T));
        mz_zip_reader_extract_to_mem(&zip_ar, index, v.data(), stat.m_uncomp_size, 0);
    } else {
        std::vector<u8> bytes;
        v.clear();
        if (store->Read(path.filename().string(), bytes) && bytes.size()%sizeof(T)==0) {
            v.resize(bytes.size()/sizeof(T));
            std::memcpy(v.data(),bytes.data(),bytes.size());
        }
    }
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u8>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u32>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u8>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u32>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

size_t DataBase::CountBlobs(BlobType type) {
    const auto& ext = GetBlobFileExtension(type);
    size_t count = 0;
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            count += std::string{file_name.data()}.ends_with(ext);
        }
        return count;
    }
    return store->Names("." + ext).size();
}

bool DataBase::ConcurrentReads() const {
    return !EmulatorSettings.IsPipelineCacheArchived();
}

void DataBase::ForEachBlob(BlobType type, const std::function<void(std::vector<u8>&& data)>& func,
                           const std::function<bool()>& stop) {
    const auto& ext = GetBlobFileExtension(type);
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            file_name.fill(0);
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            if (std::string{file_name.data()}.ends_with(ext)) {
                mz_zip_archive_file_stat stat{};
                mz_zip_reader_file_stat(&zip_ar, index, &stat);
                std::vector<u8> data(stat.m_uncomp_size);
                mz_zip_reader_extract_to_mem(&zip_ar, index, data.data(), data.size(), 0);
                func(std::move(data));
            }
        }
    } else {
        for (const auto& name : store->Names("." + ext)) {
            if (stop && stop()) return;
            std::vector<u8> data;
            store->Read(name, data);
            // Even a rejected record counts toward progress; the renderer skips
            // empty data before invoking the legacy deserializer.
            func(std::move(data));
        }
    }
}

void DataBase::Clear() {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return;
    }
    writer->Drain();
    store->Clear();
    LOG_WARNING(Render, "Shader cache reset for incompatible profile");
}

bool DataBase::PrepareStorage(const ShaderCacheStore::Progress& progress) {
    if (!store) return true;
    writer->Drain();
    const char* packed = std::getenv("KZSF_PACKED_SHADER_CACHE");
    // Opt-in until repeat-startup benchmarks demonstrate the acceptance target.
    return store->Prepare(packed && std::string_view(packed)=="1", progress);
}

bool DataBase::CompletePreload(const ShaderCacheStore::Progress& progress) {
    if (writer) writer->Drain();
    if (store && !store->Flush()) return false;
    return store && store->CompletePreload(progress);
}

ShaderCacheStore::Stats DataBase::StorageStats() const {
    return store ? store->GetStats() : ShaderCacheStore::Stats{};
}

void DataBase::FinishPreload() {
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_init_from_reader(&zip_ar, cache_path.string().c_str());
        ar_is_read_only = false;
    }
}

} // namespace Storage
