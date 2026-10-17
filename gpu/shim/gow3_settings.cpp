// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_settings.h"
#include "gow3_orbs.h"
#include "gow3_graphics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace Gow3Settings {

namespace {

const char* Path() {
    const char* env = std::getenv("GOW3_CONFIG");
    return env && env[0] ? env : "gow3.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    static const char* const overlay_keys[] = {"overlay_fps", "overlay_frametime", "overlay_upscaler",
                                               "overlay_gpu", "overlay_cpu", "overlay_ram", "overlay_vram"};
    for (unsigned item = 0; item < 7; ++item) {
        if (key == overlay_keys[item]) {
            const unsigned bit = 1u << item;
            v.overlay_items = i ? (v.overlay_items | bit) : (v.overlay_items & ~bit);
            return;
        }
    }
    if (key == "overlay_scale") {
        v.overlay_scale = std::clamp(i, 50, 300);
        return;
    }
    if (key == "overlay_corner") {
        v.overlay_corner = i >= 0 && i <= 3 ? i : 1;
        return;
    }
    if (key == "overlay_opacity") {
        v.overlay_opacity = std::clamp(i, 0, 100);
        return;
    }
    if (key == "overlay_layout") {
        v.overlay_layout = i == 1 ? 1 : 0;
        return;
    }
    if (key == "menu_tab") {
        v.menu_tab = i >= 0 && i <= 3 ? i : 0;
        return;
    }
    const char* cheat_keys[] = {"cheat_health", "cheat_magic", "cheat_item", "cheat_rage", "cheat_orbs",
                                "cheat_very_hard", "cheat_double_jumps"};
    for (unsigned cheat = 0; cheat < GOW3_CHEAT_COUNT; ++cheat) {
        if (key == cheat_keys[cheat]) {
            v.cheats[cheat] = i != 0;
            return;
        }
    }
    if (key == "mouse_aim") { v.mouse_aim=i!=0;
    } else if (key == "mouse_invert_y") { v.mouse_invert_y=i!=0;
    } else if (key == "mouse_sensitivity") { v.mouse_sensitivity=std::clamp(i,50,300);
    } else if (key == "mouse_ads_sensitivity") { v.mouse_ads_sensitivity=std::clamp(i,25,300);
    } else if (key == "mouse_horizontal_speed") { v.mouse_horizontal_speed=std::clamp(i,25,300);
    } else if (key == "mouse_vertical_speed") { v.mouse_vertical_speed=std::clamp(i,25,300);
    } else if (key == "motion_blur") { v.motion_blur=i!=0;
    } else if (key == "unlimited_health") { v.unlimited_health=i!=0;
    } else if (key == "infinite_ammo") { v.infinite_ammo=i!=0;
    } else if (key == "look_curve") { v.look_curve=i>=0 && i<=2?i:0;
    } else if (key == "look_deadzone") { v.look_deadzone=std::clamp(i,0,30);
    } else if (key == "look_ramp") { v.look_ramp=i!=0;
    } else if (key == "look_assist") { v.look_assist=i!=0;
    } else if (key == "sprint_turn_speed") {
        v.sprint_turn_speed = i >= 100 && i <= 500 ? i : 100;
    } else if (key == "controller_turn_speed") {
        v.controller_turn_speed = i >= 50 && i <= 300 ? i : 100;
    } else if (key == "controller_pitch_speed") {
        v.controller_pitch_speed = i >= 50 && i <= 300 ? i : 100;
    } else if (key == "controller_drift_deadzone") {
        v.controller_drift_deadzone=std::clamp(i,0,30);
    } else if (key == "controller_look_gain") {
        v.controller_look_gain = i >= 50 && i <= 300 ? i : 100;
    } else if (key == "controller_aim_gain") {
        v.controller_aim_gain = i >= 50 && i <= 300 ? i : 100;
    } else if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "display_mode") {
        v.display_mode = Gow3Graphics::ParseDisplayMode(value);
    } else if (key == "vsync") {
        v.vsync = i != 0;
    } else if (key == "fps_limit") {
        v.fps_limit = Gow3Graphics::ParseFpsLimit(i);
    } else if (key == "async_shaders") {
        v.async_shaders = i != 0;
    } else if (key == "fsr1") {
        // Killzone's spatial presentation path; independent of temporal scene hooks.
        v.fsr1 = i != 0;
    } else if (key == "rcas") {
        v.rcas = i != 0;
    } else if (key == "rcas_strength") {
        v.rcas_strength = Gow3Graphics::ParseRcasStrength(value.empty() ? -1 : i);
    } else if (key == "frames_queued") {
        v.frames_queued = Gow3Graphics::ParseFramesQueued(value.empty() ? -1 : i);
    } else if (key == "render_resolution") {
        v.render_resolution = Gow3Graphics::ParseResolution(value);
    } else if (key == "engine_fps") {
        v.engine_fps = Gow3Graphics::ParseEngineFps(i);
    } else if (key == "deferred_readback") {
        v.deferred_readback = i != 0;
    } else if (key == "stale_readback") {
        v.stale_readback = i != 0;
    } else if (key == "restart_unconfirmed") {
        v.restart_unconfirmed = i != 0;
    } else if (key == "enemy_health_bar") {
        v.enemy_health_bar = i != 0;
    } else if (key == "red_orb_multiplier") {
        v.red_orb_multiplier = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "damage_dealt") {
        v.damage_dealt = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "damage_taken") {
        v.damage_taken = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "green_orb_multiplier") {
        v.green_orb_multiplier = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "blue_orb_multiplier") {
        v.blue_orb_multiplier = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "gold_orb_multiplier") {
        v.gold_orb_multiplier = Gow3Orbs::ClampMultiplier(f);
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "output_res") {
        for (int r = 0; r < OutputCount; ++r) {
            if (value == std::to_string(OutputWidths[r]) + "x" + std::to_string(OutputHeights[r])) {
                v.output_res = r;
            }
        }
    }
}

} // namespace

Values& Get() {
    static Values values;
    return values;
}

void Load() {
    auto& v = Get();
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = std::getenv("GOW3_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"GOW3_MOUSE_AIM", "mouse_aim"}, {"GOW3_MOUSE_SENSITIVITY", "mouse_sensitivity"},
        {"GOW3_MOUSE_ADS_SENSITIVITY", "mouse_ads_sensitivity"},
        {"GOW3_MOUSE_HORIZONTAL_SPEED", "mouse_horizontal_speed"},
        {"GOW3_MOUSE_VERTICAL_SPEED", "mouse_vertical_speed"}, {"GOW3_MOUSE_INVERT_Y", "mouse_invert_y"},
        {"GOW3_MOTION_BLUR", "motion_blur"}, {"KZSF_UNLIMITED_HEALTH", "unlimited_health"},
        {"KZSF_INFINITE_AMMO", "infinite_ammo"},
        {"GOW3_PAD_TURN_SPEED", "controller_turn_speed"},
        {"KZSF_SPRINT_TURN_SPEED", "sprint_turn_speed"},
        {"KZSF_LOOK_CURVE", "look_curve"}, {"KZSF_LOOK_DEADZONE", "look_deadzone"},
        {"KZSF_LOOK_RAMP", "look_ramp"}, {"KZSF_LOOK_ASSIST", "look_assist"},
        {"GOW3_PAD_PITCH_SPEED", "controller_pitch_speed"},
        {"GOW3_PAD_LOOK_GAIN", "controller_look_gain"}, {"GOW3_PAD_AIM_GAIN", "controller_aim_gain"},
        {"KZSF_PAD_DRIFT_DEADZONE", "controller_drift_deadzone"},
        {"GOW3_FSR_SHARPNESS", "sharpness"},        {"GOW3_JITTER", "jitter"},
        {"GOW3_REACTIVE", "reactive"},              {"GOW3_REACTIVE_SCALE", "reactive_scale"},
        {"GOW3_REACTIVE_THRESHOLD", "reactive_threshold"}, {"GOW3_REACTIVE_MAX", "reactive_max"},
        {"GOW3_UPSCALE_PRESET", "preset"},            {"GOW3_OBJECT_MOTION", "object_motion"},
        {"GOW3_ASYNC_SHADERS", "async_shaders"},      {"GOW3_DEFERRED_READBACK", "deferred_readback"},
        {"GOW3_STALE_READBACK", "stale_readback"},     {"GOW3_FSR1", "fsr1"},
        {"GOW3_RCAS", "rcas"},                        {"GOW3_FRAMES_AHEAD", "frames_queued"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env); value && value[0]) {
            Set(v, key, value);
        }
    }
    v.startup_preset = v.preset;
    if (const char* off=std::getenv("KZSF_DISABLE_MOTION_BLUR")) v.motion_blur=off[0]!='1';
    const char* turn_hook=std::getenv("KZSF_ENGINE_TURN");
    v.engine_turn_hook=turn_hook && turn_hook[0]=='1';
    const char *player_turn_hook=std::getenv("KZSF_PLAYER_TURN");
    v.engine_turn_hook=v.engine_turn_hook.load() || (player_turn_hook && player_turn_hook[0]=='1');
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    v.startup_output_res = v.output_res;
    v.startup_live_resolution = v.live_resolution;
    v.startup_render_resolution = v.render_resolution;
    v.startup_engine_fps = v.engine_fps;
    const char* applied = std::getenv("GOW3_ENGINE_FPS");
    v.applied_engine_fps = applied && applied[0] ? Gow3Graphics::ParseEngineFps(std::atoi(applied))
                                                 : v.engine_fps.load();
    v.startup_deferred_readback = v.deferred_readback;
    v.startup_stale_readback = v.stale_readback;
}

bool GraphicsNeedRestart() {
    const auto& v = Get();
    return v.render_resolution != v.startup_render_resolution ||
           v.engine_fps != v.startup_engine_fps ||
           v.deferred_readback != v.startup_deferred_readback ||
           v.stale_readback != v.startup_stale_readback;
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    const int requested = v.upscaler;
    if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "gow3_dlss.dll or nvngx_dlss.dll missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = std::getenv("GOW3_RENDER_RES");
    return size && size[0];
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    return FixedRenderSession() &&
        (v.preset != v.startup_preset || v.output_res != v.startup_output_res ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    FILE* file = std::fopen(Path(), "w");
    if (!file) {
        std::printf("Settings: cannot write %s\n", Path());
        return;
    }
    std::fprintf(file,
                 "# gow3 settings (in-game menu: Insert / R3+L2)\n"
                 "upscaler=%s\npreset=%d\nsharpen=%d\nsharpness=%.2f\njitter=%d\n"
                 "reactive=%d\nobject_motion=%d\nreactive_scale=%.2f\nreactive_threshold=%.2f\nreactive_max=%.2f\n"
                 "debug_view=%d\nshow_fps=%d\nfsr4_auto_exposure=%d\nfsr4_invert_jitter=%d\n",
                 UpscalerName(v.upscaler), v.preset.load(), int(v.sharpen.load()),
                 v.sharpness.load(), int(v.jitter.load()), int(v.reactive.load()),
                 int(v.object_motion.load()),
                 v.reactive_scale.load(), v.reactive_threshold.load(), v.reactive_max.load(),
                 v.debug_view.load(), int(v.show_fps.load()),
                 int(v.fsr4_auto_exposure.load()), int(v.fsr4_invert_jitter.load()));
    std::fprintf(file, "output_res=%dx%d\n", OutputWidths[v.output_res], OutputHeights[v.output_res]);
    std::fprintf(file, "red_orb_multiplier=%.3f\n", v.red_orb_multiplier.load());
    std::fprintf(file, "damage_dealt=%.3f\n", v.damage_dealt.load());
    std::fprintf(file, "damage_taken=%.3f\n", v.damage_taken.load());
    std::fprintf(file, "green_orb_multiplier=%.3f\n", v.green_orb_multiplier.load());
    std::fprintf(file, "blue_orb_multiplier=%.3f\n", v.blue_orb_multiplier.load());
    std::fprintf(file, "gold_orb_multiplier=%.3f\n", v.gold_orb_multiplier.load());
    std::fprintf(file, "cheat_health=%d\ncheat_magic=%d\ncheat_item=%d\ncheat_rage=%d\ncheat_orbs=%d\n"
                       "cheat_very_hard=%d\ncheat_double_jumps=%d\n",
                 int(v.cheats[0].load()), int(v.cheats[1].load()), int(v.cheats[2].load()),
                 int(v.cheats[3].load()), int(v.cheats[4].load()),
                 int(v.cheats[5].load()), int(v.cheats[6].load()));
    std::fprintf(file, "enemy_health_bar=%d\n", int(v.enemy_health_bar.load()));
    std::fprintf(file,
                 "overlay_fps=%d\noverlay_frametime=%d\noverlay_upscaler=%d\noverlay_gpu=%d\n"
                 "overlay_cpu=%d\noverlay_ram=%d\noverlay_vram=%d\noverlay_scale=%d\noverlay_corner=%d\n"
                 "overlay_opacity=%d\noverlay_layout=%d\nmenu_tab=%d\n",
                 int(v.overlay_items >> 0 & 1), int(v.overlay_items >> 1 & 1), int(v.overlay_items >> 2 & 1),
                 int(v.overlay_items >> 3 & 1), int(v.overlay_items >> 4 & 1), int(v.overlay_items >> 5 & 1),
                 int(v.overlay_items >> 6 & 1), v.overlay_scale.load(), v.overlay_corner.load(),
                 v.overlay_opacity.load(), v.overlay_layout.load(), v.menu_tab.load());
    std::fprintf(file,
                 "display_mode=%s\nvsync=%d\nfps_limit=%d\nasync_shaders=%d\n"
                 "fsr1=%d\nrcas=%d\nrcas_strength=%d\nframes_queued=%d\n"
                 "render_resolution=%s\nengine_fps=%d\ndeferred_readback=%d\nstale_readback=%d\n"
                 "restart_unconfirmed=%d\n",
                 Gow3Graphics::DisplayModeKeys[std::clamp(v.display_mode.load(), 0, 2)].data(),
                 int(v.vsync.load()), v.fps_limit.load(), int(v.async_shaders.load()),
                 int(v.fsr1.load()), int(v.rcas.load()), v.rcas_strength.load(), v.frames_queued.load(),
                 Gow3Graphics::Resolutions[std::clamp(v.render_resolution.load(), 0, 5)].data(),
                 v.engine_fps.load(), int(v.deferred_readback.load()), int(v.stale_readback.load()),
                 int(v.restart_unconfirmed.load()));
    std::fprintf(file, "controller_look_gain=%d\ncontroller_aim_gain=%d\n",
                 v.controller_look_gain.load(), v.controller_aim_gain.load());
    std::fprintf(file, "controller_turn_speed=%d\n",v.controller_turn_speed.load());
    std::fprintf(file, "controller_drift_deadzone=%d\n",v.controller_drift_deadzone.load());
    std::fprintf(file, "sprint_turn_speed=%d\n",v.sprint_turn_speed.load());
    std::fprintf(file,"look_curve=%d\nlook_deadzone=%d\nlook_ramp=%d\nlook_assist=%d\n",
        v.look_curve.load(),v.look_deadzone.load(),int(v.look_ramp.load()),int(v.look_assist.load()));
    std::fprintf(file, "controller_pitch_speed=%d\n",v.controller_pitch_speed.load());
    std::fprintf(file,"mouse_aim=%d\nmouse_invert_y=%d\nmouse_sensitivity=%d\nmouse_ads_sensitivity=%d\n"
                      "mouse_horizontal_speed=%d\nmouse_vertical_speed=%d\nmotion_blur=%d\n"
                      "unlimited_health=%d\ninfinite_ammo=%d\n",
        int(v.mouse_aim.load()),int(v.mouse_invert_y.load()),v.mouse_sensitivity.load(),v.mouse_ads_sensitivity.load(),
        v.mouse_horizontal_speed.load(),v.mouse_vertical_speed.load(),int(v.motion_blur.load()),
        int(v.unlimited_health.load()),int(v.infinite_ammo.load()));
    // Read by run.sh at start.
    std::fprintf(file, "live_resolution=%s\n", v.live_resolution < 0 ? "auto"
                                                  : v.live_resolution ? "1" : "0");
    std::fclose(file);
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

} // namespace Gow3Settings
