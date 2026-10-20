// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vulkan/vulkan_core.h>

namespace Vulkan::UpscaleFormats {
// Storage views cannot be sRGB. Preserve every other format's numeric encoding:
// an equally wide RGBA8 view of packed R11G11B10 float is a bit reinterpretation,
// not a color conversion. The FSR resource description must match this view.
constexpr VkFormat StorageView(VkFormat image_format) {
    switch (image_format) {
    case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
    default: return image_format;
    }
}
}
