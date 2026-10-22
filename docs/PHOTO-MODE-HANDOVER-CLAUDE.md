# Photo Mode handover to Claude

Prepared 23 October 2026, Asia/Kuala_Lumpur.
Workspace: `C:\Projects\KillzoneShadowFall-PC`.

## User request and boundaries

Add Photo Mode using the existing freecam, including model switching while
Photo Mode remains active. The user explicitly said: **do not touch what
already works**. Extend the working paths with a small, reversible Photo Mode
layer. Do not rewrite the camera, model selector, input system or renderer.
This handover documents the requested work; Photo Mode is not implemented yet.
The previous feature list was a proposal, not approval to implement every item.

Only this document was created for the handover. Do not infer that its creation
tested or changed any runtime behavior.

## First version

Build a dedicated Photo Mode panel around existing freecam functionality:

- Enter/exit using the current camera request mechanism. Remember the prior
  camera state and restore it on exit. Handle death/reload and chapter changes
  without retaining stale actor, view or model pointers.
- Keep model selection available while the mode is active. Reuse the existing
  selector and its availability/status feedback.
- Camera movement speed, recenter and reset. FOV/zoom and roll can be added
  only after confirming how they interact with existing camera controls.
- A clean-capture control and screenshot action, after tracing existing capture
  and HUD paths. Distinguish the native game HUD from the port's overlay/FPS
  counter. Do not label a feature supported until its actual path is verified.
- Movement hints and a clear exit action. No em dashes in launcher text.

Later investigations: safe world/animation freeze, exposure, focus distance and
depth-of-field strength. These are not working capabilities established by this
handover. Existing effect toggles often require restarting. Do not present them
as live photographic controls without evidence. Avoid freezing the engine thread
or changing the global timing fixes merely to obtain a still image.

## Working behavior to preserve

- F8 toggles third-person view. F9 toggles freecam in the combined camera path.
- Freecam: WASD movement, Q/E down/up, mouse/arrows look, Shift faster,
  Ctrl slower, F10 recenter. Preserve the current control mappings.
- Holding F7 suppresses arrow-key camera look; F7 + Left/Right changes models.
  Arrow-key look returns when F7 is released. Keep mouse/controller look intact.
- Overlay Game tab has a Camera section above Player model with third-person
  and freecam checkboxes. Controls are disabled if their hooks were not enabled
  at launch. UI requests are consumed by engine callbacks, not executed on the
  presentation thread.
- Overlay navigation has explicit edge scrolling, Page Up/Down and a vertical
  scrollbar. Headless tests pass; the user's original in-game scroll complaint
  still needs runtime confirmation.
- Native models use chapter-independent bundle loading when model switching
  is enabled. Universal rig compatibility is not proven.
- CJ is model ID17 (`Custom_CJ`, label `CJ (custom)`) in the normal model list,
  loaded from `user/models/cj`. It has no preview timeout. It is not a native
  bundle. Its exact98-joint binding fingerprint must match or loading refuses.
- CJ geometry/animation and appearance were confirmed by the user. Neutral
  material mask is0/0/0/0; packed roughness/AO is128/0/255/255. Altering these
  previously produced chrome or a black silhouette. Do not retune them.
- User confirmed the CJ falling overlap fix after visibility maintenance was
  moved outside the normal follow-camera state gate. Preserve its ownership,
  actor-type, model-identity and level-transition guards. Separate zipline and
  switch-back coverage remains pending.
- Lucas Echo outfit ID13 and spacesuit ID16 are hidden, skipped by F7 cycling,
  and rejected by the selector mailbox because they rendered the wrong outfit.
  Preserve stable IDs and bundle definitions; do not re-enable them incidentally.
- Launcher Logs tab displays only the current launcher's session `stderr.log`.
  No session selector or log-file selector. No em dashes in launcher strings.

## Code map

- `src/probe.c`: camera engine callbacks, freecam update, third-person update,
  F7 input, model selection orchestration, CJ traversal visibility maintenance.
- `src/kzsf_free_camera_win.h`, `src/kzsf_third_person.h`: existing camera math
  and state. Preserve validation, collision and restoration behavior.
- `gpu/shim/kzsf_overlay.cpp`: Game tab Camera section, Player model combo,
  camera request/state atomics and overlay input handling. Q/E switches tabs
  while the overlay captures input; do not let Photo Mode movement leak through.
- `gpu/shim/kzsf_overlay.h`, `gpu/shim/kzsfgpu.cpp`, `gpu/kzsfgpu.h`:
  bridge API. `kzsfgpu_camera_request(camera)` consumes an atomic request:
  -1 none,0 disable,1 enable; camera0 third-person, camera1 freecam.
  `kzsfgpu_camera_publish(camera,active)` reports state back to the UI.
  Model requests use the existing tokenized selector mailbox separately.
- `gpu/shim/kzsf_overlay_scroll.h`: overlay navigation helper.
- `src/kzsf_character_models.h`, `gpu/shim/kzsf_character_selector.h`:
  stable model metadata, cycling and mailbox.
- `src/kzsf_character_swap_observer.h`, `src/kzsf_character_body_visibility.h`:
  validated native body visibility transactions.
- `src/kzsf_custom_model_probe.h`: CJ loader, resource ownership, managed
  selection and legacy opt-in F11 diagnostic. Normal Photo Mode must not depend
  on the old preview launcher or F11 timeout path.
- `launcher/kzsf_local.py`: launcher settings/env. Freecam and model switching
  enable the shared third-person bridge; model switching sets bundle loading.
- `docs/CUSTOM-MODEL-IMPORT.md`, `docs/ROADMAP.md`: detailed findings and open work.

## Workspace and build

HEAD: `5ee2e947703ec1509c7c84b93a5ec7c2c56d896b`. Most recent camera/CJ/launcher
work is uncommitted, including untracked source and tests. The FSR submodule
is already dirty. Preserve all existing changes. Do not reset, clean, checkout
over files, commit, push or publish without user instruction.

Build from PowerShell:

```powershell
$env:MSYSTEM='CLANG64'
& C:/msys64/usr/bin/bash.exe -lc 'cd /c/Projects/KillzoneShadowFall-PC && source ./msys2-env.sh && ninja -C out/gpu kzsf-probe > out/photo-mode-build.log 2>&1'
```

Check exit status and inspect the log on failure. Build output is
`out/gpu/kzsf-probe.exe`. Normal launcher now uses `out/kzsf-game.exe` (renamed after this handover);
`out/cj-import/kzsf-probe-latest.exe` is a staged copy. The last normal build
includes the Camera overlay controls and passed existing camera tests.

Never overwrite a running executable or terminate the user's game just to
stage a build. Check the running process path first. Stage under a new filename
if needed; copy into the normal launcher path when closed and compare hashes.
No engine rebuild is needed for launcher-only Python edits.

## Validation

Relevant tests:

- `tests/test_kzsf_free_camera.c`
- `tests/test_kzsf_third_person.c`
- `tests/test_kzsf_character_model_keys.c`
- `tests/test_character_selector_mailbox.cpp`
- `tests/test_kzsf_character_body_visibility.c`
- `tests/test_overlay_scroll.cpp` (headless ImGui; compile with the repository's
  imgui.cpp, imgui_draw.cpp, imgui_widgets.cpp, imgui_tables.cpp)
- `python -m unittest discover -s tests -p test_kzsf_launcher.py`

Use the MSYS2 CLANG64 environment for native tests. Test normal entry/exit,
F8/F9 synchronization, rapid toggles, model switching while freecam is active,
F7+arrow isolation, closing the overlay, falling, zipline, death/checkpoint reload
and chapter transition. Confirm screenshots hide only intended UI and restore
it afterward. Verify normal play still behaves the same when Photo Mode was
never entered. Report build/test results separately from user-verified visuals.

Start by reading the current implementation and outlining the smallest first
version. Preserve the working features listed above and keep new behavior scoped
to Photo Mode. Do not mark unverified capabilities complete.
