# Killzone runtime controls

Use the launcher on `main`. Windows build and packaging instructions are in
[BUILD-WINDOWS.md](BUILD-WINDOWS.md).

## Cameras and appearances

Enable third person, freecam and model switching on Home before launching.
F8 toggles third person, F9 toggles freecam, and F10 recenters freecam.
F7 plus Left/Right cycles appearances; releasing F7 alone toggles the selected
appearance and the default player. The same options are available through
Insert > Game > Player model. Imported model resources load on selection.

The model bridge preserves the native draw batch, maps the animated player pose
onto the selected rig, and restores the original resources on level exit.
Helper joints are rebuilt from validated parent transforms. Echo additionally
anchors wrist positions to the lower arms before rebuilding finger chains.
Model swaps are visual changes; gameplay, collision and animation remain driven
by the native player. Some character proportions and scripted actions still
have visual limitations, listed in the main README.

## Graphics and input

Graphics, effects, keyboard mapping, mouse and controller settings are exposed
in the launcher and the Insert menu. Settings that require a restart are marked
in the menu. All launcher profiles use the same built runtime executable;
memory-profile differences are selected through launch settings.

The scene renders at 1920 x 1080. FSR output size and window size are separate.
Frame generation inserts an intermediate frame and does not double simulation
speed. Texture filtering and distant texture sharpness affect sampler settings.

## Player assists

Health, ammunition, sprint and Tactical Echo options are independently selected
in Cheats. Continuous Tactical Echo requires one manual activation after each
load. It preserves the normal scan range and interval and does not aim weapons.
Native hooks validate the expected game instruction signatures before applying.

## Regression checks

`tests/` contains source tests for camera transforms, model rig mapping, draw
records, facial and helper poses, player-scoped cheats, graphics settings and
launcher options. Numeric camera fixtures are stored as a C header rather than
as runtime captures. Development capture and inspection tools live in `tools/`;
logs, generated executables and captured data belong under ignored `out/`.
