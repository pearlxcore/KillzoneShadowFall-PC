# Killzone Shadow Fall for PC

A Windows 10/11 PC port of **Killzone Shadow Fall**, with Vulkan graphics, mouse and controller support, and a dedicated launcher.

**Current release: v0.2.0.**

**Bring your own decrypted PS4 game dump: CUSA00190, update 01.81. No game files are included.**

## Support

[![Support me on Ko-fi](launcher/kofi.png)](https://ko-fi.com/R6R524N7X)

## Get started

1. Download a [Windows package](https://github.com/pearlxcore/KillzoneShadowFall-PC/releases) and extract it.
2. Open **Killzone Launcher.exe** and select your game folder on **Home**.
3. Choose your settings, then press **Play Game**. First-time setup runs automatically.
4. Let the shader-cache loading finish before playing.

The Windows package includes its required runtime. Keep the **user** folder when updating to preserve saves, settings and caches.

This README describes the current source. Older downloads may have fewer features.

## Features

- **Single-player:** gameplay, cutscenes, saving/loading, dialogue synchronization and chapter transitions.
- **Display:** windowed, borderless and fullscreen modes; separate window size; 30, 60 and 120 FPS limits; VSync and FPS display.
- **Image quality:** Native, TAA and FSR 3.1 AA; FSR output options from 1080p to 4K; FSR 1 spatial scaling and adjustable RCAS sharpening. The scene itself renders at 1080p.
- **Frame generation:** FSR 3.1 2×. Displayed FPS includes generated frames; game simulation FPS is shown separately.
- **Textures:** anisotropic filtering up to 16× and distant texture sharpness adjustment.
- **Effects and camera:** motion blur, bloom, film grain, depth of field, camera shake and field of view controls.
- **Mouse:** separate look/aim sensitivity, horizontal/vertical speed and invert Y.
- **Controller:** adjust look and aiming sensitivity separately, plus horizontal/vertical camera turn speed. Sprint turning has its own multiplier. Deadzone controls reduce stick drift; response curves and smoothing change how the camera reacts. Aim assistance controls slowdown and tracking near targets.
- **Remapping:** keyboard and controller bindings, with a restore-defaults option.
- **Shader cache:** shaders are saved between sessions and cached shaders are precompiled before gameplay, reducing repeated compilation stutters. Keep the cache when updating. First visits to new areas can still trigger shader compilation, and caching does not eliminate streaming or frame-pacing stalls.
- **Advanced options:** memory profiles, background shader compilation, GPU readback settings and queued frames.
- **Launcher and in-game menu:** graphics, controls, cheats, rendering options and session logs. Settings can be saved between sessions.

Most launcher changes apply on the next game launch. The in-game menu identifies settings that need a restart.

## Third person and freecam



https://github.com/user-attachments/assets/4256e0fe-54b6-4d97-af83-335598717047


https://github.com/user-attachments/assets/fd7b9263-4175-46a6-92dc-ef5fe2345b40



https://github.com/user-attachments/assets/5b4f6e1b-e3ce-4d49-8d40-e344f71c3e42






Enable their checkboxes on Home, then use **F8** for third person or **F9** to detach the camera and explore without moving the player. Freecam supports keyboard, mouse and controller movement; **F10** recenters it. See the shortcuts below for movement controls.

## Player models



https://github.com/user-attachments/assets/f92258e0-bf7a-4fe3-8411-33209a6baa48

<img width="2233" height="1291" alt="image" src="https://github.com/user-attachments/assets/8f225b96-2f9c-4e5c-b5a6-d07f0e5e72b9" />


Play as characters such as Echo, Sinclair or VSA Engineer. Select appearances in the in-game menu or cycle them with **F7 + Left/Right**; **F7 alone** toggles the selected appearance and default player. Models from other chapters load on selection, so switching may take a moment.

## Cheats

Unlimited health protects against normal damage, infinite ammo removes ammunition depletion/reloading, and unlimited sprint prevents stamina exhaustion. Continuous Tactical Echo refreshes orange enemy marks at the normal scan range and interval after you activate it once per load. It does not automatically aim; scripted deaths can still occur.

## Shortcuts

| Key | Action |
|---|---|
| WASD | Move |
| Space | Jump |
| Left Shift | Sprint / L3 |
| Left Ctrl | Circle |
| Left / right mouse | Fire / aim |
| Left Alt | Release captured mouse |
| Insert | Open the in-game menu |
| R3 + L2 | Open the menu with a controller |
| F8 | Toggle third person |
| F9 | Toggle freecam |
| F10 | Recenter freecam |
| F7 + Left / Right | Previous / next player model |
| F7 alone | Toggle selected / default model |

Enable camera/model controls under **Home → Camera & player model**. Models can also be selected under **Insert → Game → Player model**.

In freecam: **WASD** moves, **Q/E** moves down/up, mouse/arrows look, **Shift** moves faster and **Ctrl** slower. Launcher tabs also support **Ctrl+1–5**.

## Compatibility and known issues

The full single-player game has been completed on the development PC: **Ryzen 5 7500F, Radeon RX 9070 XT, 32 GB RAM and Windows 11**. Other hardware and drivers still need testing. A **Vulkan 1.3-capable GPU/driver** is required.

**PC spec and game dump used in this port:**


| Title ID | Version |
| --- | --- |
| CUSA00190 | 01.81 |


| Component | Tested configuration |
|---|---|
| CPU | AMD Ryzen 5 7500F (6 cores) |
| GPU | AMD Radeon RX 9070 XT |
| RAM | 32 GB |
| OS | Windows 11 Home, build 26300 |
| GPU driver | 32.0.31041.1004 (Windows driver version) |

- Performance varies by scene. FPS limits are caps, not guaranteed frame rates.
- Shader, streaming and frame-pacing stutters can remain.
- TAA, FSR and frame generation can produce trails or other image artifacts.
- Some character models still have hand, face or accessory deformation. Some outfit choices share the same appearance.
- Some scripted actions can briefly return to first person.
- Offline Botzone has limited testing. Online multiplayer and PSN services are not implemented.
- FSR 4 and DLSS are not supported launcher options. Linux has not been validated.

An optional [starter shader cache](https://github.com/pearlxcore/KillzoneShadowFall-PC/releases/download/v0.1.1-beta/KillzoneShadowFall-PC-CUSA00190-01.81-StarterCache-beta0.1.1.zip) is available. With the game closed, extract it beside the launcher, merging `user/cache/CUSA00190/`. Back up an existing cache first. It was tested only on the development PC; other systems may reject it.

## Screenshots

<img width="1922" height="1112" alt="1" src="https://github.com/user-attachments/assets/b04b61cc-b000-4459-8a26-7183e9b458a1" />
<img width="1922" height="1112" alt="2" src="https://github.com/user-attachments/assets/3c97596c-dc0b-48b4-b89d-04b339b2b07e" />
<img width="1922" height="1112" alt="3" src="https://github.com/user-attachments/assets/f9c832cd-a764-473b-bc95-f71f190125f0" />
<img width="1922" height="1112" alt="4" src="https://github.com/user-attachments/assets/a10f8460-2763-4a6c-b8da-1a9917793a6d" />
<img width="1922" height="1112" alt="5" src="https://github.com/user-attachments/assets/79dda350-5229-482c-9419-de0aea1fccae" />
<img width="2240" height="1298" alt="image" src="https://github.com/user-attachments/assets/e63e7b57-82a7-47ab-bebd-4286bfdc85c9" />

## Saves and troubleshooting

| Location | Contents |
|---|---|
| `user/savedata/1/CUSA00190/` | Game saves — back up the entire folder |
| `user/launcher-local.json` | Launcher preferences and bindings |
| `user/launcher-settings.ini` | Runtime and in-game menu settings |
| `user/cache/` | Persistent shader caches |
| `out/dialogue-…-modelauncher/` | Session logs and exit status |

Choose optional **Shader cache folder** and **Saved-data folder** paths under **Home → Folders** in the launcher. Empty fields use the defaults above. Game-specific subfolders are added automatically. Changes apply on the next launch; existing files are not moved.

Use **Advanced → Open latest session logs** when reporting a problem. Include your GPU/driver, chapter and checkpoint, selected model/settings, and relevant logs. **Do not upload game files.**

For source builds and release packaging, see [Building on Windows](docs/BUILD-WINDOWS.md).

## Credits

The original x86-64 game code runs through a custom PS4 runtime, with GPU commands and shaders translated to Vulkan. This project builds on:

- [Felipe Prado's kzsf_pc](https://github.com/FelipePrado0/gow3_pc) — direct upstream runtime, renderer integration and launcher/menu foundation.
- [deadinside28's bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) and [Supermedo's Windows port](https://github.com/Supermedo/bloodborne_pc) — native-port foundations.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and [cuesta4's renderer work](https://github.com/cuesta4/shadPS4/tree/eltutz) — GPU and shader translation.
- Celogamez and the GoldHEN cheat community — published cheat work used as a starting point.
- [lesca-191204's Killzone port](https://github.com/lesca-191204/killzone-shadowfall-pc) — reference for selected graphics and frame-generation work.

Also used: [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9), [FFmpeg](https://ffmpeg.org), [SDL3](https://github.com/libsdl-org/SDL), [Dear ImGui](https://github.com/ocornut/imgui), [sirit](https://github.com/shadps4-emu/sirit), [magic_enum](https://github.com/Neargye/magic_enum), [miniz](https://github.com/richgel999/miniz), [xbyak](https://github.com/herumi/xbyak), [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan), AMD FidelityFX SDK, MSYS2, LLVM and Pillow.

## License

[GNU GPL v2 or later](LICENSE). Third-party components retain their own licenses.
