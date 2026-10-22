// kzsf.ini graphics keys (kzsf_settings.cpp, kzsf_graphics.h).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "kzsf_graphics.h"
#include "kzsf_settings.h"

namespace G = KzsfGraphics;

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(G::ParseAnisotropy(16) == 16 && G::ParseAnisotropy(3) == 0);
    assert(G::TextureLodBias(-5.0f) == -2.0f && G::TextureLodBias(0.14f) == 0.125f);
    assert(G::ParseDisplayMode("borderless") == G::Borderless && G::ParseDisplayMode("bogus") == G::Windowed);
    assert(G::ParseFpsLimit(240) == 240 && G::ParseFpsLimit(75) == 0);
    assert(G::ParseResolution("1440p") == 3 && G::ParseResolution("8K") == 0);
    assert(G::ParseEngineFps(240) == 240 && G::ParseEngineFps(90) == 120);
    assert(G::ParseFramesQueued(2) == 2 && G::ParseFramesQueued(0) == 0 && G::ParseFramesQueued(5) == 1);
    assert(G::ParseRcasStrength(100) == 100 && G::ParseRcasStrength(101) == 75 && G::ParseRcasStrength(-1) == 75);
    assert(G::RcasAttenuation(100) == 0.0f && G::RcasAttenuation(0) == 2.0f && G::RcasAttenuation(75) == 0.5f);
    assert(G::FsrPassRuns(true, false, 1280, 720, 2560, 1440) && !G::FsrPassRuns(false, false, 1280, 720, 2560, 1440));
    assert(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080) && !G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    assert(!G::FsrPassRuns(true, true, 2560, 1440, 1920, 1080));  // larger image: regular scaling
    assert(G::EffectiveFps(0, 120) == 120 && G::EffectiveFps(240, 120) == 120 && G::EffectiveFps(60, 240) == 60);
    // An unpatched engine steps 1/60 s per frame: presenting faster speeds the game up.
    assert(G::PresentLimit(120, 60) == 60 && G::PresentLimit(240, 120) == 120 && G::PresentLimit(240, 240) == 240);
    assert(G::PresentLimit(0, 60) == 0 && G::PresentLimit(30, 60) == 30);  // 0: paced by vblank

    const char* path = "graphics-settings-test.ini";
    {
        std::ofstream out(path);
        out << "display_mode=borderless\nvsync=0\nfps_limit=240\nasync_shaders=0\n"
               "render_resolution=1440p\nengine_fps=240\ndeferred_readback=0\nstale_readback=1\n"
               "fsr1=1\nrcas=0\nrcas_strength=40\nframes_queued=2\n"
               "anisotropy=16\nmip_lod_bias=-0.5\nframe_gen=fsr3\noutput_res=3200x1800\n"
               "controller_look_gain=150\ncontroller_aim_gain=125\ncontroller_turn_speed=175\n"
               "controller_pitch_speed=190\nmotion_blur=0\nbloom=0\nfilm_grain=0\nprompt_style=1\n"
               "unlimited_sprint=1\ncontinuous_scan=1\n";
    }
    _putenv_s("KZSF_CONFIG", path);
    _putenv_s("KZSF_ASYNC_SHADERS", "");
    auto& v = KzsfSettings::Get();
    KzsfSettings::Load();
    assert(v.controller_look_gain==150 && v.controller_aim_gain==125);
    assert(v.controller_turn_speed==175);
    assert(v.controller_pitch_speed==190);
    assert(!v.motion_blur && !v.bloom && !v.film_grain);
    assert(v.prompt_style==1);
    assert(v.unlimited_sprint && v.continuous_scan);
    assert(v.display_mode == G::Borderless && !v.vsync && v.fps_limit == 240 && !v.async_shaders);
    assert(v.render_resolution == 3 && v.engine_fps == 240 && !v.deferred_readback && v.stale_readback);
    // Killzone supports spatial FSR independently of the temporal scene integration.
    assert(v.fsr1 && !v.rcas && v.rcas_strength == 40 && v.frames_queued == 2);
    assert(v.anisotropy == 16 && v.startup_anisotropy == 16 && v.mip_lod_bias == -0.5f);
    assert(v.frame_gen == KzsfSettings::FrameGenFsr3 && v.output_res == 4);
    v.anisotropy = 8;
    assert(KzsfSettings::GraphicsNeedRestart());
    v.anisotropy = 16;
    v.mip_lod_bias = -1.0f;
    assert(!KzsfSettings::GraphicsNeedRestart());
    v.mip_lod_bias = -0.5f;
    _putenv_s("KZSF_FSR1", "0");
    KzsfSettings::Load();
    assert(!v.fsr1);
    _putenv_s("KZSF_FSR1", "1");
    KzsfSettings::Load();
    assert(v.fsr1);
    _putenv_s("KZSF_FSR1", "");
    assert(!KzsfSettings::GraphicsNeedRestart());
    assert(v.applied_engine_fps == 240);  // no KZSF_ENGINE_FPS: the configured rate
    _putenv_s("KZSF_ENGINE_FPS", "60");    // run.py: the frame rate patch did not apply
    KzsfSettings::Load();
    assert(v.engine_fps == 240 && v.applied_engine_fps == 60 && !KzsfSettings::GraphicsNeedRestart());
    _putenv_s("KZSF_ENGINE_FPS", "");
    v.engine_fps = 120;
    assert(KzsfSettings::GraphicsNeedRestart());
    v.fps_limit = 60;  // live: no restart because of it
    v.engine_fps = 240;
    assert(!KzsfSettings::GraphicsNeedRestart());

    KzsfSettings::Save();
    const std::string saved = Read(path);
    assert(saved.find("unlimited_sprint=1\n") != std::string::npos);
    assert(saved.find("continuous_scan=1\n") != std::string::npos);
    for (const char* line : {"display_mode=borderless\n", "vsync=0\n", "fps_limit=60\n", "async_shaders=0\n",
                             "render_resolution=1440p\n", "engine_fps=240\n", "deferred_readback=0\n",
                             "stale_readback=1\n", "fsr1=1\n", "rcas=0\n", "rcas_strength=40\n",
                             "frames_queued=2\n", "controller_look_gain=150\n", "controller_aim_gain=125\n",
                             "controller_turn_speed=175\n", "controller_pitch_speed=190\n",
                             "motion_blur=0\n", "bloom=0\n", "film_grain=0\n", "prompt_style=1\n"}) {
        assert(saved.find(line) != std::string::npos);
    }
    assert(saved.find("anisotropy=16\n") != std::string::npos);
    assert(saved.find("mip_lod_bias=-0.500\n") != std::string::npos);
    assert(saved.find("frame_gen=fsr3\n") != std::string::npos);
    assert(saved.find("output_res=3200x1800\n") != std::string::npos);

    {
        std::ofstream out(path);
        out << "display_mode=huge\nfps_limit=7\nrender_resolution=8K\nengine_fps=1\n"
               "rcas_strength=250\nframes_queued=9\nprompt_style=9\n";
    }
    _putenv_s("KZSF_ASYNC_SHADERS", "1");  // the environment still wins at start
    v.async_shaders = false;
    KzsfSettings::Load();
    assert(v.display_mode == G::Windowed && v.fps_limit == 0 && v.render_resolution == 0 && v.engine_fps == 120);
    assert(v.rcas_strength == 75 && v.frames_queued == 1);
    assert(v.prompt_style==0);
    assert(v.async_shaders);
    {
        std::ofstream out(path);
        out << "cheat_double_jumps=1\ncheat_very_hard=1\n";
    }
    KzsfSettings::Load();
    KzsfSettings::Save();
    const std::string cheats = Read(path);
    assert(cheats.find("cheat_double_jumps=1\n") != std::string::npos);
    assert(cheats.find("cheat_very_hard=1\n") != std::string::npos);
    std::remove(path);
    std::puts("PASS: graphics settings");
}
