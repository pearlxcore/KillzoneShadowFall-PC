// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace VideoCore {
struct ArenaWindow {
    std::uint64_t base, size;
};
inline std::optional<ArenaWindow> MakeArenaWindow(std::uint64_t first_page,
                                                 std::uint64_t last_page, unsigned page_bits,
                                                 std::uint64_t max_buffer_size) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (page_bits >= 63 || last_page < first_page || last_page - first_page > 1 ||
        last_page >= (max >> page_bits)) return std::nullopt;
    const auto size = (last_page - first_page + 1) << page_bits;
    if (size > max_buffer_size) return std::nullopt;
    return ArenaWindow{first_page << page_bits, size};
}
struct ArenaWindowBind {
    std::uint64_t resource_offset, size, memory_offset;
};
inline std::optional<ArenaWindowBind> IntersectArenaBacking(ArenaWindow window,
        std::uint64_t backing_start, std::uint64_t backing_size, std::uint64_t memory_offset) {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    if (window.size > max - window.base || backing_size > max - backing_start)
        return std::nullopt;
    const auto start = std::max(window.base, backing_start);
    const auto end = std::min(window.base + window.size, backing_start + backing_size);
    if (start >= end || start - backing_start > max - memory_offset) return std::nullopt;
    return ArenaWindowBind{start - window.base, end - start,
                           memory_offset + start - backing_start};
}
} // namespace VideoCore
