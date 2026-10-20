// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from lesca-191204/killzone-shadowfall-pc (01266b0), FSR-only integration.
// Optional FSR 3.1 2x interpolation. Capture slots are tagged with the guest display
// buffer so the presented color, scene depth and motion belong to the same frame.
// HUD pixels are composed after interpolation. Menus and unmatched frames reset history.

#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <span>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

struct FfxVkFsr3_3_1_6FrameGenerationContext;

namespace Vulkan {

class Instance;
class Scheduler;

class FrameGenerator {
public:
    /// One intermediate frame per real game frame (2x presentation).
    static constexpr u32 MaxGenerated = 1;
    /// The format of the frames read and written (the Presenter's frame images).
    static constexpr vk::Format ColorFormat = vk::Format::eR8G8B8A8Unorm;
    struct Output {
        vk::Image image;
        vk::ImageView view;
    };
    /// The game's camera for this frame (CameraMotion): FSR 3.1 uses it for disocclusion.
    struct Camera {
        float near_plane = 0.1f;
        float vertical_fov = 1.0f;
    };

    FrameGenerator(const Instance& instance, Scheduler& scheduler);
    ~FrameGenerator();

    /// Switched on in the settings (Gow3Settings::frame_gen), with a provider that can run.
    [[nodiscard]] bool Wanted() const;
    /// Frames generated per game frame with the current settings (output images needed).
    [[nodiscard]] u32 GeneratedCount() const;

    /// At the upscale, render size `w` x `h`: records the copy of the scene depth (depth aspect
    /// view, sampled in General layout) into a free slot and returns the motion vector image to
    /// write next (RG16F storage, General: the upscaler's convention), or null when off.
    vk::ImageView BeginCapture(vk::CommandBuffer cmdbuf, vk::ImageView depth, u32 w, u32 h);
    /// After the motion vectors are written. `motion_valid` false: they are zero this frame.
    void EndCapture(vk::CommandBuffer cmdbuf, std::array<float, 2> jitter, bool motion_valid,
                    const Camera& camera);

    /// The game's picture before its UI (`source`, General layout), blitted into the slot of
    /// this capture in `format` (the display buffer's), `w` x `h`.
    void CaptureHudless(vk::CommandBuffer cmdbuf, vk::Image source, u32 source_width,
                        u32 source_height, vk::Format format, u32 w, u32 h);

    /// The game's display copy of the last captured frame into the buffer at `address`.
    void TagDisplay(VAddr address);
    /// The presenter shows the buffer at `address`: the capture of its frame, if any, is used by
    /// Hudless and Generate.
    void Select(VAddr address);

    /// The selected frame's copy without UI, sampled (ShaderReadOnlyOptimal) through a view in
    /// `format` with alpha one, like the presenter reads the display buffer; null when none.
    vk::ImageView Hudless(vk::Format format, vk::Extent2D& size);

    /// The frame was composed into `color` (ColorFormat, General layout, `width` x `height`):
    /// records the generated frame into `outputs` (storage and transfer source, left in General
    /// layout) and returns how many. Zero when off, and for frames without a capture (menus,
    /// movies). `hudless`: the same frame composed from Hudless() (General), or null: FSR 3.1
    /// then interpolates it and draws this frame's UI over the result.
    u32 Generate(vk::CommandBuffer cmdbuf, vk::Image color, vk::ImageView color_view,
                 vk::ImageUsageFlags color_usage, vk::Image hudless, vk::ImageView hudless_view,
                 u32 width, u32 height, std::span<const Output> outputs);

private:
    struct HudlessView {
        vk::Format format;
        vk::UniqueImageView view;
    };
    enum class SlotState { Free, Filling, Captured, Tagged };
    struct Slot {
        SlotState state = SlotState::Free;
        u64 sequence = 0; ///< capture order (consecutive frames: consecutive numbers)
        VAddr display = 0;
        VideoCore::UniqueImage depth_image;  ///< R32F hardware depth
        VideoCore::UniqueImage motion_image; ///< RG16F
        vk::UniqueImageView depth_view, motion_view;
        u32 width = 0, height = 0;
        std::array<float, 2> jitter{};
        Camera camera{};
        bool motion_valid = false;
        // The picture before the UI, in the display buffer's format.
        VideoCore::UniqueImage hudless_image;
        vk::Format hudless_format{};
        u32 hudless_width = 0, hudless_height = 0;
        std::vector<HudlessView> hudless_views;
        bool has_hudless = false;
    };
    static constexpr u32 SlotCount = 3;

    void EnsureInputs(Slot& slot, u32 w, u32 h);
    void CreatePipeline();
    u32 GenerateFsr3(vk::CommandBuffer cmdbuf, const Slot& slot, vk::Image color,
                     vk::ImageUsageFlags color_usage, u32 width, u32 height, const Output& output,
                     float frame_ms);
    /// Where `color` and `hudless` differ (the UI), `color` replaces the generated pixels.
    void ComposeUi(vk::CommandBuffer cmdbuf, vk::ImageView color, vk::ImageView hudless,
                   const Output& output, u32 width, u32 height);
    void DestroyFsr3();
    /// A backend failure disables interpolation and leaves real frame presentation available.
    void Fail(int provider, const char* reason);

    const Instance& instance;
    Scheduler& scheduler;

    vk::UniqueDescriptorSetLayout desc_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline pipeline;
    vk::UniqueDescriptorSetLayout ui_desc_layout;
    vk::UniquePipelineLayout ui_pipeline_layout;
    vk::UniquePipeline ui_pipeline;

    std::array<Slot, SlotCount> slots;
    int filling = -1;  ///< the slot of the capture being recorded
    int selected = -1; ///< the slot of the frame the presenter shows
    u64 next_sequence = 0;
    u64 last_generated_sequence = 0;
    bool reset = true;
    int last_provider = 0;
    bool last_hudless = false;
    std::chrono::steady_clock::time_point last_generate{};

    // FSR 3.1: the context and the frames it still holds views for until the GPU is done.
    FfxVkFsr3_3_1_6FrameGenerationContext* fsr3 = nullptr;
    u32 fsr3_width = 0, fsr3_height = 0, fsr3_render_width = 0, fsr3_render_height = 0;
    u64 fsr3_frame_id = 0;
    struct Pending {
        u64 frame_id;
        u64 tick;
    };
    std::deque<Pending> fsr3_pending;
};

} // namespace Vulkan
