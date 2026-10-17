# Killzone Shadow Fall for PC

A PC port of **Killzone Shadow Fall** (PlayStation 4) for Windows 10 and 11.

The CPU side is native: the game's original x86-64 code runs on the PC's processor,
while a custom runtime replaces the PS4 system libraries. The graphics run through
[shadPS4](https://github.com/shadps4-emu/shadPS4)'s video core, translating PS4 GPU
commands and shaders to Vulkan.

This project builds on [Felipe Prado's God of War III PC port](https://github.com/FelipePrado0/gow3_pc),
with Killzone-specific work on timing, dialogue synchronization, chapter transitions,
memory, graphics, controls and the launcher. See [Credits](#credits).

> **No game files are included.** You need your own decrypted PS4 dump.
> Supported build: **CUSA00190, version 01.81**.
> This project is not affiliated with Sony Interactive Entertainment or Guerrilla Games.
> Killzone is a trademark of Sony Interactive Entertainment.

**The campaign has been completed on the development machine.** This is still an
experimental port: performance varies by scene, stutters remain, and other hardware
has not received the same testing. The development GPU is an AMD Radeon RX 9070 XT.

Development is on **`killzone-source`**. The repository's `main` branch currently
retains the upstream GOW code. Older GOW documentation and packaging scripts remain
in the tree; the instructions below are for this Killzone branch.

## What works

- Campaign gameplay, video cutscenes, saving and loading.
- Dialogue and scripted-event synchronization, with corrected guest timing.
- Chapter transitions and the previously reproducible Chapter 9 allocation crash.
- Native 1080p rendering, including fixes for white/striped foliage and purple bark
  in the tested scenes.
- **30, 60 and 120 FPS limits.** These are caps, not guaranteed frame rates.
- Controller and keyboard input, button remapping, separate controller look/aim
  gains, and heading/pitch turn-speed controls.
- **Shader-cache precompilation** on startup. Cached shaders persist between runs;
  new areas and shaders can still cause stutters.
- **Launcher** in the game's colors, with five tabs:
  - **Game:** game-folder validation, build profile, saves and logs.
  - **Graphics:** display mode, window size, frame limit, VSync, sharpening,
    FPS display and experimental temporal filtering.
  - **Controls:** keyboard/controller mappings and controller sensitivity.
  - **Cheats:** unlimited player health and infinite ammo / no reload.
  - **Advanced:** shader compilation, GPU readbacks, queued frames and session logs.
- **In-game menu:** **Insert** on the keyboard or **R3 + L2** on the controller,
  with display, performance, control and overlay options. Select campaign cheats
  in the launcher before starting the game.

## What still needs work

- **Performance:** wall-facing views can reach 60 FPS while open areas and dense
  vegetation drop into the 40–50 FPS range on the development machine.
- **Stuttering:** precompilation helps with cached shaders, but does not remove
  every shader, streaming or frame-pacing stall.
- **Graphics:** remaining effects and scene-specific rendering glitches need testing.
- **Temporal filtering:** FSR 3.1 and TAA are experimental native-1080p paths,
  with projection jitter disabled and camera-only motion. TAA has passed build,
  camera-depth and startup checks; gameplay image quality is awaiting testing.
  Moving objects and the weapon may ghost. Neither includes frame generation or
  guarantees higher FPS. Reduced-resolution FSR remains a development experiment.
- **Stability:** completing the campaign does not establish stability everywhere.
  Long paused sessions and different GPU/driver configurations need more testing.

FSR 4, FSR 4.1.1 and DLSS code inherited from upstream is **not currently connected
as a supported Killzone option**. Native rendering (`none`) is the baseline.

## Build and play

This branch is a **source checkout**, not a self-contained Windows release.
The game image, executables, saves and caches are generated locally.

**You need**

- 64-bit Windows 10 or Windows 11, and a Vulkan 1.3 graphics driver/GPU.
- An x86-64 CPU with the instruction support required by the game/runtime. The
  inherited ISA fallback has not been validated across older CPUs for Killzone.
- Your own decrypted **CUSA00190 / 01.81** dump, including `eboot.bin`,
  `all_programs.prx`, `sce_module`, `sce_sys` and the game's data directories.
- [MSYS2](https://www.msys2.org), using **CLANG64**, and Python 3 with Tkinter.
  Install the compiler and libraries listed in the inherited
  [Windows build dependencies](packaging/windows/README.md#build). Its GOW
  packaging and launch instructions do not apply to this branch.

**1. Clone and build** — in the MSYS2 CLANG64 shell:

```bash
git clone --recursive --branch killzone-source https://github.com/pearlxcore/KillzoneShadowFall-PC.git
cd KillzoneShadowFall-PC
export CXXFLAGS='-include cstdlib -include cstdio -include cwchar -include cmath -include new'
export CFLAGS="$CXXFLAGS"
GOW3_LTO=OFF bash build.sh
cp out/gow3-probe.exe out/gow3-probe-launcher.exe
```

The current launcher expects MSYS2 at `C:\msys64`. Other installation paths
require adjusting its runtime-library path in `launcher/kzsf_local.py`.
The `gow3` executable names and `GOW3_` settings are inherited internal names.

**2. Prepare your game image** — in the same shell, changing the game path:

```bash
GAME='D:/Games/KillzoneShadowFall'
python scripts/prepare.py "$GAME" --out out
python scripts/link_modules.py "$GAME" --out out \
  --modules libc.prx libSceFios2.prx ../all_programs.prx --dynamic-exports
python scripts/content_profile.py "$GAME" --out out --sku full
printf '155664\n' > out/renderer-stability-toggles.txt
```

`../all_programs.prx` is relative to the dump's `sce_module` directory: the file
belongs at the dump root. Dynamic exports are required for native module lookups.
Preparation writes generated files under `out/` without modifying the dump.
Do not apply the inherited GOW game patches to Killzone.

**3. Open the launcher** — from the project root:

```bash
python launcher/kzsf_local.py
```

Choose the game folder on **Game**, adjust the options and press **PLAY**.
Start with **Current build**, **60 FPS**, and temporal upscaler **none**.
The beta memory-layout profile and temporal options are available for experiments.

`Launch-Killzone.bat` and `Killzone Launcher.vbs` are optional Windows entry points
when Python is installed at a path recognized by `tools/start-launcher.ps1`.
The native launcher entry-point source is `launcher/start_kzsf.c`; prebuilt
launcher executables are not included.

Linux code and packaging are inherited from upstream. A Killzone Linux build
has not been validated here.

## Saves, settings and logs

Everything stays in the project folder:

| File or folder | Contents |
|---|---|
| `user/savedata/1/CUSA00190/` | Save directory; back up the entire folder |
| `user/savedata/1/CUSA00190/KZ4/` | `checkpointdata.dat` and `profile.dat` |
| `user/launcher-local.json` | Launcher preferences and mappings |
| `user/launcher-settings.ini` | Runtime and in-game menu settings |
| `user/` | Also contains persistent shader/cache data |
| `out/dialogue-…-modelauncher/` | Session `stdout.log`, `stderr.log` and exit status |

Keep `user/` when updating. Use **Advanced → Open latest session logs** when
reporting a problem. Include the chapter/checkpoint, GPU and driver version,
selected options, and relevant logs; do not upload your game dump.

## How it works

| Part | Where | What it does |
|---|---|---|
| Preparation | `scripts/` | Applies relocations and links bundled modules with dynamic exports |
| Loader | `src/probe.c` | Loads the image, installs guarded Killzone hooks and starts the original code |
| Runtime | `src/runtime_*.c` | PS4 memory, threads, files, audio, input, saves and services |
| Timing and assists | `src/kzsf_*.h`, `src/tsc_patch.c` | Guest clocks, player controls and campaign cheats |
| GPU | `gpu/shadps4/` | GPU command/shader translation to Vulkan and Killzone rendering fixes |
| In-game menu | `gpu/shim/gow3_overlay.cpp` | Dear ImGui settings and performance overlay |
| Launcher | `launcher/kzsf_local.py` | Game validation, controls, graphics, cheats and session launch |

## Credits

This project builds on the work of:

- **Felipe Prado:** [gow3_pc](https://github.com/FelipePrado0/gow3_pc), the direct
  upstream for the runtime, renderer integration, shader-cache work and launcher/menu foundation.
- **deadinside28:** [bloodborne_pc](https://github.com/deadinside28/bloodborne_pc),
  the original native-port foundation.
- **Supermedo (Mohammed Albarghouthi):**
  [bloodborne_pc for Windows](https://github.com/Supermedo/bloodborne_pc), in the upstream lineage.
- **cuesta4:** [shadPS4's GOW-focused `eltutz` branch](https://github.com/cuesta4/shadPS4/tree/eltutz),
  whose renderer work is part of the inherited foundation.
- **shadPS4 team:** [shadPS4](https://github.com/shadps4-emu/shadPS4), the GPU and shader translation core.
- **Celogamez and the GoldHEN cheat community:** published Killzone cheat work
  used as a starting point for the campaign assists.

Also used: [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9),
[FFmpeg](https://ffmpeg.org), [SDL3](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui), [sirit](https://github.com/shadps4-emu/sirit),
[magic_enum](https://github.com/Neargye/magic_enum), [miniz](https://github.com/richgel999/miniz),
[xbyak](https://github.com/herumi/xbyak),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), the AMD FidelityFX SDK,
[MSYS2](https://www.msys2.org), [LLVM](https://llvm.org) and [Pillow](https://python-pillow.org).

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Third-party components keep their own licenses.
