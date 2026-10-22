# Killzone Shadow Fall PC roadmap

## Rendering investigations — 23 October 2026

- [x] CJ body overlap during falling/dropping: user confirmed the traversal
  camera-state maintenance fix works on 23 October 2026. The earlier
  visibility-transfer change alone was insufficient.
- [ ] Recheck CJ ziplining and switching back after the falling fix.
- [ ] Fix native pause/Collectibles menu rendering: pause-menu layers are
  incomplete and Collectibles shows a black/missing background. Frame captures
  suggest UI composition starts on a temporary panel instead of the main menu
  surface. The candidate that recognized the earlier backdrop shader
  (VS6a4e3fb8) was reverted after the user reported an FPS drop. Keep this issue
  unresolved; investigate a fix that preserves gameplay performance and verify
  both menu tabs before promoting it.
- [ ] Verify overlay scrolling fix in game: user reports Down stops at the last
  visible setting. Added edge scrolling for keyboard/controller navigation,
  Page Up/Down support and a persistent scrollbar. Headless ImGui tests reach
  both ends of a40-row settings table with arrow keys, page keys and D-pad.
  The standalone baseline also scrolled, so the reported in-game cause is not
  proven; runtime confirmation is required. Test: tests/test_overlay_scroll.cpp.
  Follow-up: reproduced failure with a long disabled tail. Disabled widgets now
  register for navigation/highlight while native interaction stays disabled.
  The test reaches/focuses the last disabled row and verifies Space cannot edit
  it. Added no-target scrolling for noninteractive sections. Build/test pass;
  in-game confirmation pending.

## Planned: external model import helper

- [ ] Build an assisted import/install tool using the validated CJ pipeline.
  Begin with one supported input format and humanoid models fitted to the
  supported98-joint player rig. Validate geometry, skin weights, textures,
  materials and rig compatibility before installation into the model selector.
- [ ] Add automatic scale/orientation detection, bone mapping and a preview
  with clear correction reports. Broaden fitting/rig/accessory support only
  after visual validation. One-click import is a goal for supported inputs,
  not a promise that arbitrary models can be ported without manual work.

## Current priority: hidden engine controls

Finish the input-controls investigation first: look deadzones and response,
acceleration/ramp behavior, aim assistance and quick turn. Validate new controls
in gameplay before treating them as confirmed. Then investigate FOV, camera
shake, depth of field, grain, bloom, exposure, LOD and shadows.

## Later feature: Modern Warfare 2019-style controls

Selected by the user on 18 October 2026; subsequently queued after the hidden
engine controls above. Implementation has not started.

Adapt the input and aiming feel of **Call of Duty: Modern Warfare (2019)** to
Killzone's existing gameplay using an original implementation in this port.

Planned investigation and implementation:

- Direct mouse camera input to replace joystick emulation, with independent
  horizontal/vertical and hip-fire/ADS sensitivity.
- Familiar keyboard/mouse and controller binding presets, with remapping.
- Adjustable controller deadzones and aiming response curves.
- Consistent sprint turning using the existing independent sprint control.
- Investigation of native aim slowdown/assist before exposing adjustments.
- Settings in both the launcher and in-game overlay, with persistent values and
  an option to restore the original Killzone controls.

Verify walking, sprinting, hip-fire, ADS, weapon interactions and scripted camera
transitions with both input methods. Preserve campaign progression and timing
fixes. Specific behavior and achievable parity require investigation; the feature
is not a claim of exact Modern Warfare engine behavior.
