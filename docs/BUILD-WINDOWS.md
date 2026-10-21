# Building on Windows

Use the **main** branch. Game files, executables, saves and caches are generated locally.

## Requirements

- Windows 10/11, an x86-64 CPU and a Vulkan 1.3-capable GPU/driver.
- Your decrypted **CUSA00190 / 01.81** dump, including `eboot.bin`, `all_programs.prx`, `sce_module`, `sce_sys` and the data directories.
- [MSYS2](https://www.msys2.org), using **CLANG64**, with the compiler and libraries in the [Windows build dependencies](../packaging/windows/README.md#build). That document's GOW packaging/launch instructions do not apply here.
- Python 3 with Tkinter and Pillow: `python -m pip install pillow`.

The local launcher expects MSYS2 at `C:\msys64`. Other locations require adjusting its runtime-library path in `launcher/kzsf_local.py`. Older CPUs and Linux builds have not been validated for this port.

## Build

In an MSYS2 CLANG64 shell:

```bash
git clone --recursive --branch main https://github.com/pearlxcore/KillzoneShadowFall-PC.git
cd KillzoneShadowFall-PC
export CXXFLAGS='-include cstdlib -include cstdio -include cwchar -include cmath -include new'
export CFLAGS="$CXXFLAGS"
GOW3_LTO=OFF bash build.sh
```

The build supplies `out/gow3-probe-launcher.exe` for all launcher features. Internal `gow3` names are inherited from upstream.

## Prepare the game

Change the game path, then run:

```bash
GAME='D:/Games/KillzoneShadowFall'
python scripts/prepare.py "$GAME" --out out
python scripts/link_modules.py "$GAME" --out out \
  --modules libc.prx libSceFios2.prx ../all_programs.prx --dynamic-exports
python scripts/content_profile.py "$GAME" --out out --sku full
printf '155664\n' > out/renderer-stability-toggles.txt
```

`all_programs.prx` belongs at the dump root, one directory above `sce_module`. Dynamic exports are required. Preparation writes to `out/` without changing the dump. Do not apply inherited GOW game patches.

## Launch

```bash
python launcher/kzsf_local.py
```

Select the game folder on **Home**, start with the **Standard** memory profile and press **Play Game**. The generated **Killzone Launcher.exe** is another entry point; its Python/Tk runtime must be available on PATH, in `runtime/`, or at a supported fallback location.

## Package a Windows release

After building, use Windows Python with Tkinter and Pillow:

```powershell
python packaging/windows/package_killzone.py --msys C:/msys64
```

The Killzone packager creates a portable folder, ZIP, SHA256 checksum and manifest in `dist/`, including Python/Tk and runtime DLLs. Use this packager rather than the inherited `package.sh`. Game files, user settings, saves and caches are excluded.

Before publishing, verify first-time game setup, launch/exit, settings persistence and the packaged runtime on a clean Windows installation. Set `LAUNCHER_VERSION` in `launcher/kzsf_local.py` to the intended release label.

For the unreleased shader-cache optimization, diagnostic switches and benchmark
procedure, see [Shader-cache startup](SHADER-CACHE-STARTUP.md).
