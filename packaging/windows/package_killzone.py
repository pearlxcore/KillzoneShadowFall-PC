"""Assemble a portable Killzone release from the current Windows build.

Run with a Windows Python containing Tkinter and Pillow after build.sh.
Only allowlisted build outputs/source helpers are shipped; game data, user
settings, logs, shader caches and diagnostic programs are never copied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--msys', type=Path, default=Path('C:/msys64'))
    args = parser.parse_args()
    import PIL
    import tkinter  # Require the complete launcher runtime before staging.
    runtime = Path(sys.base_prefix)
    name = 'KillzoneShadowFall-PC-Windows-' + time.strftime('%Y%m%d-%H%M%S')
    dest = ROOT / 'dist' / name
    dest.mkdir(parents=True, exist_ok=False)

    def copy(source, relative):
        target = dest / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    strip = args.msys / 'clang64/bin/llvm-strip.exe'
    for binary, target in [('gow3-probe.exe', 'out/gow3-probe-launcher.exe'),
                           ('gow3-gpu-capabilities.exe', 'out/gow3-gpu-capabilities.exe')]:
        (dest / 'out').mkdir(exist_ok=True)
        subprocess.run([str(strip), '--strip-debug', '-o', str(dest / target),
                        str(ROOT / 'out' / binary)], check=True)
    copy(ROOT / 'Killzone Launcher.exe', 'Killzone Launcher.exe')
    for filename in ('kzsf_local.py', 'killzone.png', 'killzone.ico'):
        copy(ROOT / 'launcher' / filename, 'launcher/' + filename)
    for filename in ('prepare.py', 'link_modules.py', 'link_libc.py',
                     'content_profile.py', 'graphics_options.py'):
        copy(ROOT / 'scripts' / filename, 'scripts/' + filename)
    copy(ROOT / 'packaging/windows/prepare_killzone.py', 'packaging/windows/prepare_killzone.py')
    copy(ROOT / 'LICENSE', 'LICENSE')
    copy(ROOT / 'README.md', 'SOURCE-README.md')
    # Capture dependencies recursively with MSYS2 ldd, then resolve Windows paths.
    env = dict(os.environ, MSYSTEM='CLANG64',
               KZSF_PACKAGE_ROOT=str(ROOT), KZSF_PACKAGE_MSYS=str(args.msys))
    command = r'''
cd "$(cygpath -u "$KZSF_PACKAGE_ROOT")"
export PATH="$(cygpath -u "$KZSF_PACKAGE_MSYS")/clang64/bin:$PATH"
ldd out/gow3-probe.exe out/gow3-gpu-capabilities.exe |
  awk '/\/clang64\/bin\// {print $3}' | sort -u |
  while IFS= read -r dll; do cygpath -w "$dll"; done
'''
    result = subprocess.run([str(args.msys / 'usr/bin/bash.exe'), '-lc', command],
                            env=env, text=True, capture_output=True, check=True)
    dlls = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()]
    if not dlls or not any(p.name.lower() == 'sdl3.dll' for p in dlls):
        raise RuntimeError('Dependency discovery failed: SDL3 was not resolved.')
    for dll in dlls:
        copy(dll, 'out/' + dll.name)
    # Keep redistribution notices supplied with the CLANG64 installation.
    shutil.copytree(args.msys / 'clang64/share/licenses', dest / 'licenses/CLANG64')
    for filename in ('python.exe', 'pythonw.exe', 'python3.dll', 'LICENSE.txt'):
        copy(runtime / filename, 'runtime/' + filename)
    for dll in runtime.glob('*.dll'):
        copy(dll, 'runtime/' + dll.name)
    ignore = shutil.ignore_patterns('__pycache__', '*.pyc', 'site-packages', 'test', 'tests', 'idlelib', 'ensurepip')
    for directory in ('Lib', 'DLLs', 'tcl'):
        shutil.copytree(runtime / directory, dest / 'runtime' / directory, ignore=ignore)
    shutil.copytree(Path(PIL.__file__).parent, dest / 'runtime/Lib/site-packages/PIL',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    for extra in (runtime / 'Lib/site-packages').glob('pillow*.libs'):
        shutil.copytree(extra, dest / 'runtime/Lib/site-packages' / extra.name)
    for extra in (runtime / 'Lib/site-packages').glob('pillow-*.dist-info'):
        shutil.copytree(extra, dest / 'runtime/Lib/site-packages' / extra.name,
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    (dest / 'out/renderer-stability-toggles.txt').write_text('155664\n', encoding='ascii')
    (dest / 'Prepare Game.bat').write_text(
        '@echo off\ncd /d "%~dp0"\n"runtime\\python.exe" "packaging\\windows\\prepare_killzone.py" %*\npause\n',
        encoding='ascii')
    (dest / 'README.txt').write_text(
        'KILLZONE SHADOW FALL PC - Windows preview\n\n'
        '1. Extract this entire folder to a writable location.\n'
        '2. Run Prepare Game.bat and enter your decrypted CUSA00190 / 01.81 dump folder.\n'
        '   The dump must include all_programs.prx, sce_module and sce_sys.\n'
        '3. Open Killzone Launcher.exe. Start with Standard, 60 FPS, Native.\n\n'
        'Python and runtime DLLs are included. MSYS2 is not required to play.\n'
        'A Vulkan 1.3 capable GPU/driver and 64-bit Windows 10/11 are required.\n'
        'No game files, saves, settings or shader caches are included.\n'
        'Preparation generates game-derived files locally under out/.\n'
        'Keep user/ for saves/settings; session logs are under out/.\n'
        'Offline Botzone loading has been tested on The Divide; extended combat\n'
        'and other maps remain unverified. Online multiplayer is unsupported.\n'
        'Temporal AA options are experimental; shader compilation can stutter.\n\n'
        'Source: https://github.com/pearlxcore/KillzoneShadowFall-PC/tree/killzone-source\n'
        'See LICENSE and licenses/ for redistribution terms and notices.\n', encoding='utf-8')
    files = [p for p in dest.rglob('*') if p.is_file()]
    forbidden = {'eboot.bin', 'eboot.elf', 'boot.bin', 'boot-linked.bin', 'content.bin',
                 'launcher-local.json', 'launcher-settings.ini'}
    for path in files:
        rel = path.relative_to(dest)
        if path.name.lower() in forbidden or path.suffix.lower() in ('.dmp', '.log', '.prx', '.sprx', '.pkg'):
            raise RuntimeError('Unexpected private/game artifact: ' + str(rel))
    # Exercise imports from the actual bundled runtime before creating the ZIP.
    subprocess.run([str(dest / 'runtime/python.exe'), '-I', '-B', '-c',
                    'import tkinter,PIL.Image,ctypes,ssl; t=tkinter.Tcl(); print("Bundled runtime:",t.eval("info patchlevel"))'],
                   cwd=dest, check=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest = {'source_commit': revision, 'built_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                'files': {str(p.relative_to(dest)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in files}}
    (dest / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive = dest.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in dest.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                output.write(path, name + '/' + path.relative_to(dest).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
    print(archive)
    print('SHA256:', digest)


if __name__ == '__main__':
    main()
