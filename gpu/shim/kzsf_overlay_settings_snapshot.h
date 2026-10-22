#pragma once
#include "kzsf_settings.h"
#include <map>
#include <string>
#include <initializer_list>
inline std::string KzsfNoticeChoice(int value,std::initializer_list<const char*> labels) {
    return value>=0 && value<int(labels.size()) ? *(labels.begin()+value) : std::to_string(value);
}
inline std::map<std::string,std::string> KzsfOverlaySettingsSnapshot() {
    auto& s=KzsfSettings::Get();
    std::map<std::string,std::string> result;
    result["Button prompts"]=KzsfNoticeChoice(s.prompt_style,{"Automatic","Keyboard and mouse","PlayStation"});
    result["Enable mouse look"]=(s.mouse_aim.load() ? "On" : "Off");
    result["Invert mouse Y"]=(s.mouse_invert_y.load() ? "On" : "Off");
    result["Look sensitivity"]=std::to_string(s.mouse_sensitivity.load());
    result["Aim sensitivity / ADS"]=std::to_string(s.mouse_ads_sensitivity.load());
    result["Horizontal speed"]=std::to_string(s.mouse_horizontal_speed.load());
    result["Vertical speed"]=std::to_string(s.mouse_vertical_speed.load());
    result["Motion blur (restart required)"]=(s.motion_blur.load() ? "On" : "Off");
    result["Bloom (experimental, restart required)"]=(s.bloom.load() ? "On" : "Off");
    result["Film grain (experimental, restart required)"]=(s.film_grain.load() ? "On" : "Off");
    result["Field of view"]=std::to_string(s.fov_percent.load());
    result["Camera shake"]=(s.camera_shake.load() ? "On" : "Off");
    result["Depth of field (restart required)"]=(s.depth_of_field.load() ? "On" : "Off");
    result["Look response"]=std::to_string(s.controller_look_gain.load());
    result["Stick drift protection"]=std::to_string(s.controller_drift_deadzone.load());
    result["Aim response (L2)"]=std::to_string(s.controller_aim_gain.load());
    result["Horizontal turn speed"]=std::to_string(s.controller_turn_speed.load());
    result["Sprint horizontal turn speed"]=std::to_string(s.sprint_turn_speed.load());
    result["Look response curve"]=KzsfNoticeChoice(s.look_curve,{"Original","Linear","Precision"});
    result["Look inner deadzone"]=std::to_string(s.look_deadzone.load());
    result["Look ramp smoothing"]=(s.look_ramp.load() ? "On" : "Off");
    result["Aim slowdown / tracking"]=(s.look_assist.load() ? "On" : "Off");
    result["Vertical turn speed"]=std::to_string(s.controller_pitch_speed.load());
    result["Show overlay"]=(s.show_fps.load() ? "On" : "Off");
    result["Font size"]=std::to_string(s.overlay_scale.load());
    result["Corner"]=KzsfNoticeChoice(s.overlay_corner,{"Top left","Top right","Bottom left","Bottom right"});
    result["Background opacity"]=std::to_string(s.overlay_opacity.load());
    result["Layout"]=KzsfNoticeChoice(s.overlay_layout,{"One line","Stacked"});
    result["Display mode"]=KzsfNoticeChoice(s.display_mode,{"Windowed","Borderless","Fullscreen"});
    result["VSync"]=(s.vsync.load() ? "On" : "Off");
    result["Frame rate limit"]=std::to_string(s.fps_limit.load());
    result["Compile new shaders in the background"]=(s.async_shaders.load() ? "On" : "Off");
    result["Frame generation (restart)"]=KzsfNoticeChoice(s.frame_gen,{"Off","FSR 3.1 2x"});
    result["Anisotropic filtering (restart)"]=std::to_string(s.anisotropy.load());
    result["Distant texture sharpness"]=std::to_string(s.mip_lod_bias.load());
    result["FSR 1 spatial upscaling"]=(s.fsr1.load() ? "On" : "Off");
    result["Sharpening (RCAS)"]=(s.rcas.load() ? "On" : "Off");
    result["Sharpening strength"]=std::to_string(s.rcas_strength.load());
    result["Frames queued"]=std::to_string(s.frames_queued.load());
    result["Render resolution (restart)"]=KzsfNoticeChoice(s.output_res,{"720p","1080p","1440p","4K","1800p"});
    const char* items[]={"FPS","Frametime","Upscaler","GPU","CPU","RAM","VRAM"};
    for(unsigned i=0;i<7;i++)result[std::string("Show ")+items[i]]=(s.overlay_items.load()&(1u<<i)) ? "On" : "Off";
    return result;
}
