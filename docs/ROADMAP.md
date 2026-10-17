# Killzone Shadow Fall PC roadmap

## Next feature: Modern Warfare 2019-style controls

Selected by the user on 18 October 2026. Planned; implementation has not started.

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
