# KZSF naming migration

23 October2026. Renamed first-party project filenames and identifiers from
`gow3` / `Gow3` / `GOW3` to `kzsf` / `Kzsf` / `KZSF`.

The normal launcher uses `out/kzsf-game.exe`. Native build targets are now
`kzsf-probe`, `kzsf-gpu-capabilities`, `kzsf-play`, and `killzone-launcher`.
The GPU library/API, settings/overlay modules, launcher modules, resource
filenames, build/package scripts, tests and documentation references use the
new names. Environment options previously beginning `GOW3_` now begin `KZSF_`.
Use the current launcher to populate them; external scripts using the old
variables must be updated. Default runtime configuration is `kzsf.ini`;
the existing root configuration was moved there without changing its contents.
Launcher preferences and saved-game/cache paths were preserved.

Two implementation details required explicit handling: the general frame-map
helper became `tools/kzsf_frame_map_common.py` because the Killzone-specific
`kzsf_frame_map.py` already existed; the inherited damage-hook byte array uses
`kzsf_damage_hook_original` to avoid colliding with Killzone's separate signature.

Third-party source, upstream URLs and the contents of third-party patches keep
their original identifiers/provenance. The profiling patch filename was renamed
but its patch contents were preserved so it still applies to the vendor source.
Old executables, captures, generated build-cache entries and diagnostics under
ignored output directories are historical artifacts and were not bulk-renamed.
Their old names do not identify the executable used by the current launcher.

Before migration, changed first-party files (including all prior local edits)
were archived with a filename mapping at
`out/kzsf-rename-backup-20261023-031127.zip`. The original root config was then
moved separately. No commits, pushes or releases were made.

Validation: native game/runtime, GPU capabilities, helper and launcher builds
pass. The renamed runtime displays its command-line usage successfully.
Python suite:349 tests,59 skipped, no failures using the bundled Python with
NumPy. Camera, model keys/mailbox, notifications, material/snapshot, texture,
manifest and mesh tests pass. In-game verification after this migration is
still required. Installed the renamed binaries and native launcher locally.
