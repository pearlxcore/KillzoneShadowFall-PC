# Killzone engine controls investigation

Updated: 18 October 2026. Supported game: CUSA00190, version 01.81.

The table separates usable controls from investigation leads. A property name in
the executable does not prove it is a global player setting: it may belong to a
weapon, vehicle, scripted camera or motion-controller input scheme.

## Engine controls

| Control | Description | Evidence and implementation details | Current status |
|---|---|---|---|
| Horizontal turn speed | Changes the player's left/right camera turn factor. | The working hook scales sensitivity outputs in the `SoldierJoystickController` path after `FUN_8eed80`, at `0xb8af5e`. It replaces the earlier spectator-camera hook. Range 50–300%; 100% preserves the original factor. | Implemented; user confirmed turning improvement. Available in launcher Controls and overlay Game → Controller aiming. |
| Vertical turn speed | Changes the player's up/down camera turn factor independently. | Uses the same guarded player-input hook with a separate pitch multiplier. Range 50–300%. | Implemented and wired to launcher/overlay. Exact angular speeds have not been measured. |
| Sprint horizontal turn speed | Multiplies horizontal camera input only while actually sprinting. | `FUN_b87dd0`, reached from the player joystick path, calls `FUN_b363f0`, the registered `IsSprinting` query. Its sprint branch multiplies both look axes by `SoldierResource.SprintAimSpeedFactor` (offset `0x3dc`). A signature-guarded hook at `0xb87e1a` scales only heading input before that native reduction when the query returns true. The separate `+0xad0` fallback is unchanged. | Implemented in launcher Controls and overlay Game; 100–500%, live, default 100%. Supports controller and emulated mouse. Automated checks pass; gameplay confirmation pending. |
| Normal/zoomed response curves | Adjusts how input becomes camera movement when looking or aiming. | `LookCurve`, `HeadingCurve`, `HeadingCurveZoomed`, `PitchCurve`, `PitchSpeedFactorCurve`, `ZoomedPitchSpeedFactorCurve`. Some belong to motion-controller resources. Ordinary joystick look processing has been identified, but these curves are not all traced to editable player settings. | Engine curve overrides are not implemented. Port-side look/aim gains are available separately. |
| Deadzones | Controls how far a stick must move before movement/aiming starts. | `LookDeadzoneCurve`, `MoveDeadzoneCurve`, `NormalDeadzone`, `ZoomedDeadzone`, `ScopedDeadzone`, `MountedDeadzone`. Different resources/input modes use different definitions. | Identified; no universal override exposed. |
| Aim assist / slowdown | May control target attraction, tracking speed and slowdown near enemies. | `SoftlockLookSpeedCurve`, `SoftlockPitchModifier`, `AimAssistDistanceCurve`, `AimAssistSpeedCurve`, `MaxAimAssistFactorAcceleration`. Another sensitivity consumer exists in `FUN_e4ae70`, but its role as aim assist is unproven. | Candidates only; no aim-assist disable/strength setting. |
| Quick turn | Could control a rapid turn action and its angle. | `PlayerQuickTurnCurve`, `PlayerQuickTurnDefaultAngle`, `PlayerQuickTurnMinAngle`, `PlayerQuickTurnsMaxAngle`. Input-scheme ownership and activation remain unresolved. | Names identified; not implemented. |
| Motion blur | Enables/disables the game's motion-blur processing. | Feature-mask bit 0 at `0x161d504` selects the blur path in `FUN_577950`. The native off branch clears the intermediate target; unrelated feature bits are preserved. Guarded for the supported game build. | Implemented; user confirmed OFF works. Launcher Graphics and overlay Image; restart required. |
| Depth of field | Blurs objects outside the camera's focal range. | `DepthOfFieldSettingsResource`, `postUltraDepthOfFieldShader`, `postUltraNearDepthOfFieldShader`. Dependent image writes must remain valid. | Resources/shaders identified; no switch implemented. Separate from motion blur. |
| Chromatic aberration | Produces color separation or lens-like distortion. | `ChromaticAberration`, `SphericalAberrationLevel`, `SphericalAberrationPower`. Scope and renderer consumers need verification. | Candidates only. |
| Vignette | Darkens/tints the image edges. | `VignetteSettingsResource`, `VignetteScale`, `VignetteBias`, `VignetteColor`. May overlap cinematic, damage or stealth feedback. | Candidates only; no global switch. |
| Film grain | Adds a grain texture/noise to the image. | `GrainSettingsResource`, `GrainTexture`, `postGrainWeightShader`. | Identified; not disabled. |
| Bloom | Spreads bright highlights into surrounding pixels. | `BloomSettingsResource`, `postBloomCaptureShader`, `postBloomIntegrateShader`. Skipping a pass without replacing its output can leave stale textures. | Candidates only. |
| Exposure / brightness | Controls scene exposure and adaptation to lighting changes. | `ExposureControl`, `SceneExposureControl`, exposure minimum/maximum and adaptation rates, `GammaControl`. Some correspond to ordinary game settings. | No additional global engine override implemented. |
| Camera shake | Could reduce camera movement from impacts or scripted events. | `CameraShakeMultiplier`, `CameraShakeResource`, `CameraShakeEventResource`. A player-camera-specific target is needed. | Candidate accessibility control; not implemented. |
| Field of view | Changes the camera's visible angle. | `FieldOfView` exists on camera resources. Gameplay, weapon, zoom and cinematic cameras must be distinguished. | No global FOV slider implemented. |
| LOD / draw distance | Could alter geometry detail and distance thresholds. | `LodDistanceScale`, `LodDistanceBias`, `LODDistanceMultiplier`, `LowLODDistanceMultiplier`. These may be per-resource/component. | No verified global performance setting. |
| Shadow resolution | Could change shadow-map sizes and distance-based quality. | `ShadowMapSize`, `ShadowMapSizeThreshold`, `ShadowMapSizeLODDistances`. Allocation and format dependencies need tracing. | Candidates only; no slider implemented. |

## Added port-side input controls

These are input-processing features of the PC port, not newly discovered native
engine settings. All are in launcher Controls and the overlay's Game tab.

| Control | Description | Range / behavior |
|---|---|---|
| Controller look sensitivity | Scales right-stick input without aiming. | 50–300%; live. Reaches the native maximum sooner, but does not remove its turn cap. |
| Controller aim sensitivity | Scales right-stick input while L2 is held. | 50–300%; live, independent of normal look response. |
| Mouse look sensitivity | Scales mouse movement during normal look. | 50–300%; live. Mouse movement is translated into the game's right-stick camera input. |
| Mouse ADS sensitivity | Separate sensitivity while right-click aiming is held. | 25–300%; live. |
| Mouse horizontal / vertical speed | Independent per-axis mouse multipliers. | 25–300% each; live; multiplies both normal look and ADS sensitivity. Does not change controller response settings. |
| Mouse Y inversion | Reverses vertical mouse movement. | On/off; live. |
| Mouse capture | Locks relative mouse movement to the game window. | Click to capture; Left Alt releases. Focus loss and the port menu also release capture. |

## Sprint turning: what is and is not implemented

**Sprint horizontal turn speed** is available for both input methods. Start at
200% and compare against 100% in the same open area. This is a multiplier of the
original sprint response, not a fixed degrees-per-second setting or removal of
every downstream camera limit.

- **Activation:** the game's actual `IsSprinting` result is used, rather than
  whether L3 or a keyboard binding is held. Toggle sprint is therefore supported.
- **Scope:** horizontal turning only. Walking, vertical look, locomotion speed,
  resource data and simulation timing are unchanged. It combines with the general
  horizontal player-turn setting; mouse input reaches the same native camera path.
- **Verification:** tests cover all byte-valued query results, original/default
  and invalid settings, unchanged registers/SIMD, TEST flags and both branches,
  non-finite input, the actual guest-image signature, and settings persistence.
  The log marker `KILLZONE_SPRINT_TURN` confirms execution while sprinting at a
  non-default multiplier. Gameplay feel still needs user testing.

Gameplay calibration of the new mouse controls is still pending. Motion blur OFF
and the replacement player-controller turn hook have received user confirmation.
