// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "video_core/renderer_vulkan/upscale_formats.h"

int main() {
    using Vulkan::UpscaleFormats::StorageView;
    // Regression: Killzone's 32-bit packed-float output was registered as 32-bit
    // RGBA8, producing posterized colors even though dimensions and byte sizes match.
    assert(StorageView(VK_FORMAT_B10G11R11_UFLOAT_PACK32) == VK_FORMAT_B10G11R11_UFLOAT_PACK32);
    assert(StorageView(VK_FORMAT_R16G16B16A16_SFLOAT) == VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(StorageView(VK_FORMAT_R8G8B8A8_UNORM) == VK_FORMAT_R8G8B8A8_UNORM);
    assert(StorageView(VK_FORMAT_R8G8B8A8_SRGB) == VK_FORMAT_R8G8B8A8_UNORM);
    assert(StorageView(VK_FORMAT_B8G8R8A8_SRGB) == VK_FORMAT_B8G8R8A8_UNORM);
    std::puts("PASS: upscale storage preserves packed-float and channel encoding");
}
