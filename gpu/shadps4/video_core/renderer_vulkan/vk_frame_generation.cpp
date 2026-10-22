// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from lesca-191204/killzone-shadowfall-pc (01266b0), FSR-only integration.
#include "video_core/renderer_vulkan/vk_frame_generation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ffx_vk_fsr3_3_1_5_bridge.h"
#include "kzsf_settings.h"
#include "video_core/host_shaders/frame_gen_inputs_comp.h"
#include "video_core/host_shaders/frame_gen_ui_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

// Killzone's projection has no far plane (depth = 1 - near / z): a distance past the scene.
constexpr float CameraFar = 10000.0f;

constexpr vk::ImageUsageFlags InputUsage =
    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;

} // namespace

FrameGenerator::FrameGenerator(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    const auto features = instance.GetPhysicalDevice().getFeatures();
    if (Wanted() && (!features.shaderStorageImageReadWithoutFormat ||
                     !features.shaderStorageImageWriteWithoutFormat)) {
        Fail(KzsfSettings::FrameGenFsr3, "GPU lacks unformatted storage image support");
    }
    if (Wanted()) std::printf("Frame generation: experimental FSR 3.1 2x requested\n");
}

FrameGenerator::~FrameGenerator() { DestroyFsr3(); }

bool FrameGenerator::Wanted() const {
    const auto& s = KzsfSettings::Get();
    return s.startup_frame_gen == KzsfSettings::FrameGenFsr3 &&
           s.frame_gen == KzsfSettings::FrameGenFsr3;
}

u32 FrameGenerator::GeneratedCount() const { return Wanted() ? 1 : 0; }

void FrameGenerator::CreatePipeline() {
    if (pipeline) {
        return;
    }
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{{
        {0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, 2 * sizeof(float)};
    pipeline_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    }));
    const auto compute = [&](std::span<const u32> code, vk::PipelineLayout layout) {
        const auto module = CompileSPV(code, device);
        auto made = Check(device.createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo{
                    .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                              .module = module,
                              .pName = "main"},
                    .layout = layout,
                }));
        device.destroyShaderModule(module);
        return made;
    };
    pipeline = compute(FRAME_GEN_INPUTS_COMP, *pipeline_layout);
    // The UI over generated frames: the generated frame (storage), the frame and the frame
    // without UI (sampled).
    const std::array<vk::DescriptorSetLayoutBinding, 3> ui_bindings{{
        {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
    }};
    ui_desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(ui_bindings.size()),
        .pBindings = ui_bindings.data(),
    }));
    const vk::PushConstantRange ui_push{vk::ShaderStageFlagBits::eCompute, 0, 2 * sizeof(s32)};
    ui_pipeline_layout = Check(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &*ui_desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &ui_push,
    }));
    ui_pipeline = compute(FRAME_GEN_UI_COMP, *ui_pipeline_layout);
}

void FrameGenerator::EnsureInputs(Slot& slot, u32 w, u32 h) {
    if (slot.depth_image && slot.width == w && slot.height == h) {
        return;
    }
    // An earlier frame's generation may still read the old images.
    scheduler.WaitSubmitted();
    const auto device = instance.GetDevice();
    const auto make = [&](VideoCore::UniqueImage& image, vk::UniqueImageView& view,
                          vk::Format format) {
        view.reset();
        image = VideoCore::UniqueImage(device, instance.GetAllocator());
        image.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {w, h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = InputUsage,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        view = Check(device.createImageViewUnique({
            .image = vk::Image(image),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        }));
    };
    make(slot.depth_image, slot.depth_view, vk::Format::eR32Sfloat);
    make(slot.motion_image, slot.motion_view, vk::Format::eR16G16Sfloat);
    slot.width = w;
    slot.height = h;
    reset = true;
}

vk::ImageView FrameGenerator::BeginCapture(vk::CommandBuffer cmdbuf, vk::ImageView depth, u32 w,
                                           u32 h) {
    if (!Wanted() || !depth || !w || !h) {
        return {};
    }
    CreatePipeline();
    // A free slot, else the oldest one the presenter is not using.
    int pick = -1;
    for (int i = 0; i < int(SlotCount); ++i) {
        if (i == selected) {
            continue;
        }
        if (slots[i].state == SlotState::Free) {
            pick = i;
            break;
        }
        if (pick < 0 || slots[i].sequence < slots[pick].sequence) {
            pick = i;
        }
    }
    filling = pick;
    Slot& slot = slots[pick];
    EnsureInputs(slot, w, h);
    slot.state = SlotState::Filling;
    slot.sequence = ++next_sequence;
    slot.display = 0;
    slot.has_hudless = false;
    // Its previous contents are not needed.
    std::array<vk::ImageMemoryBarrier2, 2> to_write{};
    for (u32 i = 0; i < 2; ++i) {
        to_write[i] = {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = i == 0 ? vk::Image(slot.depth_image) : vk::Image(slot.motion_image),
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
    }
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = to_write.data()});
    const vk::DescriptorImageInfo depth_info{.imageView = depth,
                                             .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo depth_out{.imageView = *slot.depth_view,
                                            .imageLayout = vk::ImageLayout::eGeneral};
    const std::array<vk::WriteDescriptorSet, 2> writes{{
        {.dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &depth_info},
        {.dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &depth_out},
    }};
    const std::array<float, 2> size{float(w), float(h)};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *pipeline_layout, 0, writes);
    cmdbuf.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(size),
                         size.data());
    cmdbuf.dispatch((w + 7) / 8, (h + 7) / 8, 1);
    return *slot.motion_view;
}

void FrameGenerator::EndCapture(vk::CommandBuffer cmdbuf, std::array<float, 2> jitter,
                                bool motion_valid, const Camera& frame_camera) {
    if (filling < 0) {
        return;
    }
    Slot& slot = slots[filling];
    std::array<vk::ImageMemoryBarrier2, 2> to_read{};
    for (u32 i = 0; i < 2; ++i) {
        to_read[i] = {
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = i == 0 ? vk::Image(slot.depth_image) : vk::Image(slot.motion_image),
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
    }
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = to_read.data()});
    slot.state = SlotState::Captured;
    slot.motion_valid = motion_valid;
    slot.jitter = jitter;
    // FSR 3.1 rejects values that are not finite and positive.
    slot.camera.near_plane = std::isfinite(frame_camera.near_plane) &&
                                     frame_camera.near_plane > 0.0f
                                 ? frame_camera.near_plane : 0.1f;
    slot.camera.vertical_fov = std::isfinite(frame_camera.vertical_fov) &&
                                       frame_camera.vertical_fov > 0.01f &&
                                       frame_camera.vertical_fov < 3.1f
                                   ? frame_camera.vertical_fov : 1.0f;
}

void FrameGenerator::CaptureHudless(vk::CommandBuffer cmdbuf, vk::Image source, u32 source_width,
                                    u32 source_height, vk::Format format, u32 w, u32 h) {
    if (!Wanted() || filling < 0 || !w || !h || format == vk::Format::eUndefined) {
        return;
    }
    Slot& slot = slots[filling];
    if (slot.state != SlotState::Captured || slot.has_hudless) {
        return; // only for a frame with the game's scene, and before its UI
    }
    if (!slot.hudless_image || slot.hudless_format != format || slot.hudless_width != w ||
        slot.hudless_height != h) {
        // An earlier presenter frame may still read the old copy.
        scheduler.WaitSubmitted();
        slot.hudless_views.clear();
        slot.hudless_image = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        slot.hudless_image.Create(vk::ImageCreateInfo{
            .flags = vk::ImageCreateFlagBits::eMutableFormat,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {w, h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        slot.hudless_format = format;
        slot.hudless_width = w;
        slot.hudless_height = h;
    }
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const std::array before{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = source,
            .subresourceRange = range},
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = vk::Image(slot.hudless_image),
            .subresourceRange = range},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = before.data()});
    // The same conversion as the display blit (TemporalUpscaler::RedirectColor): equal pixels
    // outside the UI.
    const vk::ImageBlit region{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                 vk::Offset3D{s32(source_width), s32(source_height), 1}},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
    };
    cmdbuf.blitImage(source, vk::ImageLayout::eGeneral, vk::Image(slot.hudless_image),
                     vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest);
    const std::array after{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = source,
            .subresourceRange = range},
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = vk::Image(slot.hudless_image),
            .subresourceRange = range},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = after.data()});
    slot.has_hudless = true;
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::printf("Frame generation: the picture before the UI, %ux%u %s\n", w, h,
                    vk::to_string(format).c_str());
    }
}

void FrameGenerator::TagDisplay(VAddr address) {
    // The newest captured frame not yet copied to a display buffer.
    int newest = -1;
    for (int i = 0; i < int(SlotCount); ++i) {
        if (slots[i].state == SlotState::Captured &&
            (newest < 0 || slots[i].sequence > slots[newest].sequence)) {
            newest = i;
        }
    }
    if (newest < 0) {
        return;
    }
    // An older frame tagged with the same buffer was never shown: free its slot.
    for (int i = 0; i < int(SlotCount); ++i) {
        if (i != selected && slots[i].state == SlotState::Tagged && slots[i].display == address) {
            slots[i].state = SlotState::Free;
        }
    }
    slots[newest].state = SlotState::Tagged;
    slots[newest].display = address;
}

void FrameGenerator::Select(VAddr address) {
    selected = -1;
    for (int i = 0; i < int(SlotCount); ++i) {
        if (slots[i].state == SlotState::Tagged && slots[i].display == address &&
            (selected < 0 || slots[i].sequence > slots[selected].sequence)) {
            selected = i;
        }
    }
    static bool reported = false;
    if (selected >= 0 && !reported) {
        for (const auto& slot : slots) {
            if (slot.state != SlotState::Free && slot.sequence > slots[selected].sequence) {
                reported = true;
                std::printf("Frame generation: the presenter shows a frame after the next one was "
                            "captured (matched by display buffer)\n");
                break;
            }
        }
    }
}

vk::ImageView FrameGenerator::Hudless(vk::Format format, vk::Extent2D& size) {
    if (selected < 0 || !slots[selected].has_hudless || !slots[selected].hudless_image) {
        return {};
    }
    Slot& slot = slots[selected];
    size = {slot.hudless_width, slot.hudless_height};
    for (const auto& view : slot.hudless_views) {
        if (view.format == format) {
            return *view.view;
        }
    }
    slot.hudless_views.push_back({
        format,
        Check(instance.GetDevice().createImageViewUnique({
            .image = vk::Image(slot.hudless_image),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne},
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        })),
    });
    return *slot.hudless_views.back().view;
}

void FrameGenerator::ComposeUi(vk::CommandBuffer cmdbuf, vk::ImageView color,
                               vk::ImageView hudless, const Output& output, u32 width,
                               u32 height) {
    const vk::ImageMemoryBarrier2 written{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = output.image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &written});
    const vk::DescriptorImageInfo output_info{.imageView = output.view,
                                              .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo color_info{.imageView = color,
                                             .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo hudless_info{.imageView = hudless,
                                               .imageLayout = vk::ImageLayout::eGeneral};
    const std::array<vk::WriteDescriptorSet, 3> writes{{
        {.dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &output_info},
        {.dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &color_info},
        {.dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &hudless_info},
    }};
    const std::array<s32, 2> size{s32(width), s32(height)};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *ui_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *ui_pipeline_layout, 0, writes);
    cmdbuf.pushConstants(*ui_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(size),
                         size.data());
    cmdbuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

u32 FrameGenerator::Generate(vk::CommandBuffer cmdbuf, vk::Image color, vk::ImageView color_view,
                             vk::ImageUsageFlags color_usage, vk::Image hudless,
                             vk::ImageView hudless_view, u32 width, u32 height,
                             std::span<const Output> outputs) {
    const auto now = std::chrono::steady_clock::now();
    float frame_ms = std::chrono::duration<float, std::milli>(now - last_generate).count();
    const bool delayed = !(frame_ms > 0.0f && frame_ms <= 100.0f);
    last_generate = now;
    if (!(frame_ms > 0.0f && frame_ms < 200.0f)) {
        frame_ms = 16.6f;
    }
    // Views the FSR 3.1 context took for frames the GPU has finished.
    while (fsr3 && !fsr3_pending.empty() && scheduler.IsFree(fsr3_pending.front().tick)) {
        ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(fsr3, fsr3_pending.front().frame_id);
        fsr3_pending.pop_front();
    }
    const int provider = KzsfSettings::Get().frame_gen;
    const int slot_index = selected;
    selected = -1;
    if (provider != last_provider) {
        last_provider = provider;
        reset = true;
    }
    if (provider == KzsfSettings::FrameGenOff || slot_index < 0 || outputs.empty() || !width ||
        !height) {
        // A frame without the game's scene (menu, movie, loading): start over afterwards.
        reset = true;
        return 0;
    }
    Slot& slot = slots[slot_index];
    // Slots of frames older than this one will not be shown any more.
    for (auto& other : slots) {
        if (&other != &slot && other.state == SlotState::Tagged && other.sequence < slot.sequence) {
            other.state = SlotState::Free;
        }
    }
    slot.state = SlotState::Free; // its images stay valid for the commands recorded below
    // FSR 3.1 interpolates from the previous frame it was given: only consecutive frames.
    const bool consecutive = slot.sequence == last_generated_sequence + 1;
    if (!consecutive || !slot.motion_valid || delayed) {
        reset = true;
    }
    last_generated_sequence = slot.sequence;
    // FSR 3.1 keeps the previous input: it cannot switch between frames with and without UI.
    const bool with_hudless = hudless && hudless_view;
    if (with_hudless != last_hudless) {
        last_hudless = with_hudless;
        reset = true;
        std::printf("Frame generation: %s\n", with_hudless ? "UI kept out of the generated frames"
                                                           : "frames without a picture before the UI");
    }
    // The composed frame is read by compute after the post pass wrote it.
    const vk::ImageMemoryBarrier2 color_ready{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite |
                         vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = color,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &color_ready});
    if (with_hudless) {
        auto hudless_ready = color_ready;
        hudless_ready.image = hudless;
        cmdbuf.pipelineBarrier2(
            {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &hudless_ready});
    }
    // The outputs' previous contents (presented already) are not needed.
    std::array<vk::ImageMemoryBarrier2, MaxGenerated> to_write{};
    u32 used = 0;
    for (; used < std::min<u32>(u32(outputs.size()), MaxGenerated) && outputs[used].image; ++used) {
        to_write[used] = {
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eNone,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = outputs[used].image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
    }
    if (!used) {
        reset = true;
        return 0;
    }
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = used, .pImageMemoryBarriers = to_write.data()});
    u32 generated = 0;
    if (with_hudless) {
        // The picture without UI is interpolated, then this frame's UI goes on top.
        generated = GenerateFsr3(cmdbuf, slot, hudless, color_usage, width, height, outputs[0],
                                 frame_ms);
        if (generated) {
            ComposeUi(cmdbuf, color_view, hudless_view, outputs[0], width, height);
        }
    } else {
        generated = GenerateFsr3(cmdbuf, slot, color, color_usage, width, height, outputs[0],
                                 frame_ms);
    }
    // A reset dispatch initializes history; its output is not an intermediate frame.
    // Never present interpolation across loading gaps, projection changes or missing cameras.
    const bool history_was_reset = reset;
    if (generated) {
        reset = false;
        // Presented after this submission: the writes go to the transfer that copies them.
        std::array<vk::ImageMemoryBarrier2, MaxGenerated> written{};
        for (u32 i = 0; i < generated; ++i) {
            written[i] = to_write[i];
            written[i].srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
            written[i].dstAccessMask = vk::AccessFlagBits2::eTransferRead;
            written[i].oldLayout = vk::ImageLayout::eGeneral;
        }
        cmdbuf.pipelineBarrier2(
            {.imageMemoryBarrierCount = generated, .pImageMemoryBarriers = written.data()});
    } else {
        reset = true;
    }
    return history_was_reset ? 0 : generated;
}

void FrameGenerator::DestroyFsr3() {
    if (!fsr3) {
        return;
    }
    scheduler.WaitSubmitted();
    ffxVkFsr3_3_1_6FrameGenerationContextRetireFrame(fsr3, fsr3_frame_id);
    ffxVkFsr3_3_1_6FrameGenerationContextDestroy(fsr3);
    fsr3 = nullptr;
    fsr3_pending.clear();
}

u32 FrameGenerator::GenerateFsr3(vk::CommandBuffer cmdbuf, const Slot& slot, vk::Image color,
                                 vk::ImageUsageFlags color_usage, u32 width, u32 height,
                                 const Output& output, float frame_ms) {
    if (fsr3 && (fsr3_width != width || fsr3_height != height ||
                 fsr3_render_width != slot.width || fsr3_render_height != slot.height)) {
        DestroyFsr3();
    }
    if (!fsr3) {
        const FfxVkFsr3_3_1_6FrameGenerationCreateInfo create{
            .physicalDevice = instance.GetPhysicalDevice(),
            .device = instance.GetDevice(),
            .maxRenderWidth = slot.width,
            .maxRenderHeight = slot.height,
            .displayWidth = width,
            .displayHeight = height,
            .colorFormat = static_cast<VkFormat>(ColorFormat),
        };
        if (ffxVkFsr3_3_1_6FrameGenerationContextCreate(&create, &fsr3) !=
            FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
            fsr3 = nullptr;
            Fail(KzsfSettings::FrameGenFsr3, "FSR 3.1 frame generation context creation failed");
            return 0;
        }
        fsr3_width = width;
        fsr3_height = height;
        fsr3_render_width = slot.width;
        fsr3_render_height = slot.height;
        reset = true;
        FfxVkFsr3_3_1_6FrameGenerationMemoryUsage usage{};
        ffxVkFsr3_3_1_6FrameGenerationContextGetMemoryUsage(fsr3, &usage);
        std::printf("Frame generation: FSR 3.1 %ux%u (depth and motion %ux%u), %.1f MB\n", width,
                    height, slot.width, slot.height, usage.totalUsageInBytes / 1e6);
    }
    const auto image = [](vk::Image handle, vk::Format format, u32 w, u32 h,
                          vk::ImageUsageFlags usage) {
        return FfxVkFsr3_3_1_6FrameGenerationImage{
            .image = handle,
            .format = static_cast<VkFormat>(format),
            .width = w,
            .height = h,
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .usage = static_cast<VkImageUsageFlags>(usage),
        };
    };
    const u64 id = ++fsr3_frame_id;
    const FfxVkFsr3_3_1_6FrameGenerationPrepareInfo prepare{
        .commandBuffer = cmdbuf,
        .color = image(color, ColorFormat, width, height, color_usage),
        .depth = image(slot.depth_image, vk::Format::eR32Sfloat, slot.width, slot.height,
                       InputUsage),
        .motionVectors = image(slot.motion_image, vk::Format::eR16G16Sfloat, slot.width,
                               slot.height, InputUsage),
        .renderWidth = slot.width,
        .renderHeight = slot.height,
        .jitterOffsetX = slot.jitter[0],
        .jitterOffsetY = slot.jitter[1],
        .motionVectorScaleX = 1.0f,
        .motionVectorScaleY = 1.0f,
        .frameTimeMilliseconds = frame_ms,
        .minLuminance = 0.0f,
        .maxLuminance = 1.0f,
        .transferFunction = FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB,
        .cameraNear = slot.camera.near_plane,
        .cameraFar = CameraFar,
        .viewSpaceToMeters = 1.0f,
        .cameraVerticalFovRadians = slot.camera.vertical_fov,
        .cameraPosition = {0.0f, 0.0f, 0.0f},
        .cameraUp = {0.0f, 1.0f, 0.0f},
        .cameraRight = {1.0f, 0.0f, 0.0f},
        .cameraForward = {0.0f, 0.0f, 1.0f},
        .frameId = id,
        .reset = reset ? VK_TRUE : VK_FALSE,
    };
    if (ffxVkFsr3_3_1_6FrameGenerationContextRecordPrepare(fsr3, &prepare) !=
        FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
        fsr3_pending.push_back({id, scheduler.CurrentTick()});
        Fail(KzsfSettings::FrameGenFsr3, "FSR 3.1 frame generation (prepare) failed");
        return 0;
    }
    const FfxVkFsr3_3_1_6FrameGenerationDispatchInfo dispatch{
        .commandBuffer = cmdbuf,
        .color = prepare.color,
        .output = image(output.image, ColorFormat, width, height,
                        InputUsage | vk::ImageUsageFlagBits::eTransferSrc),
        .distortionField = {},
        .displayWidth = width,
        .displayHeight = height,
        .frameTimeMilliseconds = frame_ms,
        .cameraNear = prepare.cameraNear,
        .cameraFar = prepare.cameraFar,
        .viewSpaceToMeters = 1.0f,
        .cameraVerticalFovRadians = prepare.cameraVerticalFovRadians,
        .minLuminance = 0.0f,
        .maxLuminance = 1.0f,
        .transferFunction = FFX_VK_FSR3_3_1_6_FRAMEGEN_TRANSFER_SRGB,
        .frameId = id,
        .reset = prepare.reset,
    };
    const auto result = ffxVkFsr3_3_1_6FrameGenerationContextRecordDispatch(fsr3, &dispatch);
    fsr3_pending.push_back({id, scheduler.CurrentTick()});
    if (result != FFX_VK_FSR3_3_1_6_FRAMEGEN_OK) {
        Fail(KzsfSettings::FrameGenFsr3, "FSR 3.1 frame generation (dispatch) failed");
        return 0;
    }
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::printf("Frame generation: recorded actual FSR 3.1 interpolation dispatch\n");
        std::fflush(stdout);
    }
    return 1;
}

void FrameGenerator::Fail(int provider, const char* reason) {
    std::printf("Frame generation: %s; disabled\n", reason);
    KzsfSettings::Get().frame_gen_problem = reason;
    KzsfSettings::Get().frame_gen = KzsfSettings::FrameGenOff;
    reset = true;
}

} // namespace Vulkan
