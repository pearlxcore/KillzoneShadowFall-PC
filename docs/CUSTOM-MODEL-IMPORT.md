# Custom character import: CJ investigation

Status: **offline asset conversion works; CJ is not available in the game yet**.

Ownership correction: the combined live test in session
`cj-upload-20261022-125505` rejected its reference-count checks. Native
constructors `330f00/395780` initialize the reference count to zero, and
`330f20` destroys a resource only when releasing its last reference. Earlier
`created-released` logs recorded calls to release, not verified destruction;
those logs do not establish cleanup correctness. All custom resource creators
now acquire their first owning reference after construction, including nested
skin and mesh resources. Synthetic constructors now start at zero, with
release assertions and failure-path cleanup checks. Corrected session
`cj-upload-20261022-130610` passed all four assembled-graph checks, with the
primitive reference count returning from 2 to 1 and the cloned material count
returning to 1 after releasing the model part. Its texture tests also passed,
and normal game exit returned code 0. This supersedes the earlier release-call
logs for ownership validation; it does not validate drawing or animation.

The current selector loads native Killzone SoldierResource bundles. It does not
accept Unreal assets or glTF files. Adding a menu entry does not add a renderer.

## Completed

- Located the CJ player mesh, 61-joint rig, vest and materials in a local
  San Andreas Definitive Edition PS5 dump (PPSA03524).
- Exported geometry with the GTA Trilogy build of UE Viewer.
- Implemented bounded top-mip BC1/BC3/BC5 decoding for the inspected PS5 mode-5
  texture layout, including embedded exports and nonzero first mip levels.
- Prepared a textured glTF with the white vest, jeans, shoes, face and eyes.
  Checked the result with a Blender render.
- Kept each clothing mesh's inverse binds. The exported rest poses differ
  slightly, so sharing a skin without correction would introduce deformation.
- Removed GTA vertex shader masks from glTF color attributes; otherwise the
  vest and arms appear red when a standard glTF shader multiplies those masks.

## Reproduce asset preparation

Keep game assets in an ignored output directory. Source control contains tools
and synthetic tests only.

Use [repak](https://github.com/trumank/repak) to extract just
`Gameface/Content/SanAndreas/Characters/Player/` and
`Gameface/Content/SanAndreas/Textures/player/` into a local working directory.
Use the UE Viewer build linked by its maintainer in the
[GTA Trilogy support topic](https://www.gildor.org/smf/index.php?topic=7993.0).
The tools are not bundled with this project.

```powershell
python tools/prepare-cj-model.py --content "<extracted>/Gameface/Content" --umodel "<tools>/umodel_gta_trilogy_v2.exe" --output out/cj-prepared
python -m unittest discover -s tests -p test_cj_texture.py
```

The output directory must be new. `CJ.gltf`, textures, original exported mesh
buffers, conversion logs and `report.json` stay together. Input assets are
read-only. The clear eye shell is omitted, and materials currently use base
color only. Normal maps, clothing morphs and facial animation remain unfinished.

The decoder's address equations were checked against AMD's
[GFX10 swizzle tables](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/imported/addrlib/src/gfx10/gfx10SwizzlePattern.h).
Unsupported texture layouts and invalid export/bulk ranges are rejected.

## Remaining game integration

1. Capture native mesh resource/vertex/index/material layouts and their upload
   lifecycle. Existing render hooks operate on registered native draw records,
   not arbitrary host mesh buffers.
2. Implement ownership and cleanup for imported geometry/material resources,
   preserving normal default-model restoration and chapter transitions.
3. Map Killzone animations onto CJ's differently named and oriented joints,
   correcting bind spaces. His simplified fingers and spine cannot use a
   bone-index copy. Facial joints need a separate policy.
4. Verify weapons, reloads, movement, jumping, falls, executions, model cycling,
   freecam, checkpoint reloads and exit before enabling a selector entry.

## Live capture

The default-player session captured 55 mesh/LOD resources and 62 validated
primitive vertex/index buffer pairs, with approximately 4.6 MB of bounded reads.
Two additional primitive descriptors were refused by layout guards; they must
not be inferred from the other records. Source and render skeleton snapshots
were also saved locally. Captures are not atomic or reusable native objects.

The observed vertex strides are 32, 36, 40 and 44 bytes, with distinct element
records for different skin-weight counts. Vertex and index buffer lengths and
index ranges are verified before accepting each capture. Their actual game
assets remain under ignored `out/`.

The accepted captures contain 91,206 vertices across all captured LODs. Bone
indices stay within the 98-entry native binding table. Captured weight channels
never sum above 255; rigid vertices have zero explicit weights, so a conventional
four-weight copy would be incorrect. The exact shader weight interpretation
still requires verification. Runtime property metadata identifies primitive
skin-info type 3 as `NBTGPU`, with optional CPU skin arrays absent in this path.

The native vertex stream reader at `0x2e7ef0` reads vertex count, stream count,
layout flags/stride/elements, a 16-byte cache identity and vertex payload. The
offline `tools/kzsf_vertex_stream.py` writer follows this observed read sequence.
It round-trips all 62 accepted captured layouts with unchanged vertex bytes.
This verifies payload construction only, not native allocation or GPU upload.

An opt-in detached upload diagnostic is available through
`KZSF_CUSTOM_VERTEX_PROBE_FILE`. It validates one bounded native vertex stream,
waits for a stable gameplay actor and the native allocator's thread context,
then constructs, loads, inspects and releases a new vertex resource. It never
attaches that resource to an actor or retains it. Runtime code checks include
the loader's actual Windows TLS-slot displacement. Normal launcher sessions
remove this diagnostic variable.

The synthetic lifecycle test covers successful release, allocation failure,
read failure, inspection failure and malformed input:

```powershell
cmake --build out/gpu --target custom-vertex-upload-test
out/gpu/custom-vertex-upload-test.exe
```

Passing that test verifies the host-side lifecycle logic. A live
`status=created-released` result is required separately before treating native
upload as established; neither result proves that CJ can be rendered.

The vertex diagnostic passed in session `cj-upload-20261022-030718`: a new
156-vertex, 32-byte-stride resource was created, its descriptor and GPU buffer
were inspected, and it was released without attachment. The game subsequently
exited normally with code 0. This validates that specific detached allocation
path, not model installation or gameplay stability with imported geometry.

The index writer follows `0x500e90` / `0x2e20c0`: count, usage flags, index
width selector, 16-byte identity, a consumed auxiliary word, then index bytes.
All 62 accepted captured index buffers round-trip with unchanged bytes. The
optional `KZSF_CUSTOM_INDEX_PROBE_FILE` diagnostic runs only after a successful
vertex diagnostic and checks detached index creation/release separately.
Host-side tests cover allocation, read and inspection failures as well as
out-of-range triangle indices.

Session `cj-upload-20261022-035355` passed both detached uploads: all 5,048
vertex-stream bytes were consumed with no archive error, descriptor inspection
passed, and the 576-entry uint16 index resource was created and released. The
game then exited normally with code 0. The
intermediate session `cj-upload-20261022-034603` returned a failed vertex check
without crashing; its earlier logging did not identify the failed stage. Do
not treat the passing runs as proof of repeatable imported-model stability.

Material inspection identified two render-effect variants and named texture
bindings, including `inSampler_texturesetNormal`,
`inSampler_texturesetColorAlpha` and (on one variant) `inSampler_LM`. The effect
contains per-pass shader resources and owning texture-binding arrays. A
shallow resource copy would share owning pointers and is not a valid import
implementation. The read-only material capture tool follows these bounded
arrays and verifies their headers again before accepting the result:

```powershell
python tools/capture-model-materials.py --pid <game-pid> --effect <RenderEffectResource-address> --output out/material-capture.json
```

```powershell
python tools/capture-model-geometry.py --pid <game-pid> --body <ModelPartResource-address> --output out/native-geometry-capture
python -m unittest discover -s tests -p test_model_geometry_capture.py
python -m unittest discover -s tests -p test_kzsf_vertex_stream.py
```

Read-only analysis of the matching Killzone executable identified native draw
dispatch at `0x703e10` and skinned render preparation at `0x5e3610`. This is
evidence of the integration boundary, not proof that custom geometry is safe to
submit. The next step is decoding the captured skin/material descriptors and
proving resource creation, registration and cleanup without modifying existing
owning resources. No CJ selector entry or runtime injection has been enabled.

## Material ownership and CJ texture upload

`kzsf_custom_material.h` creates an independent base-variant render effect.
It copies parameter blocks and texture-binding arrays and retains each shader,
texture and string reference. It refuses buffer-binding arrays and unsupported
layouts. It does not copy cached render packets or register a drawable part.
Synthetic tests fail each allocation in turn and verify balanced cleanup and
independent buffers. Session `cj-upload-20261022-041449` created, inspected and
released a three-pass clone, then exited normally with code 0.

The opt-in `KZSF_CUSTOM_TEXTURE_PROBE_FILE` reads a bounded `KZTX` RGBA payload.
It uses the existing registered pixel mapper and the native generated-texture
path also used by PC prompts, verifies the image descriptor, destroys the
detached native texture, then unmaps its pixels. The image is never drawn or
placed in a queued Texture resource, so no drawable retains those pixels.
Creation/inspection failures are covered by synthetic lifecycle tests.
Session `cj-upload-20261022-042721` passed this check with CJ's 2048x2048 face
texture and exited normally with code 0. Normal launcher sessions strip all
custom upload diagnostic variables.

## Offline animation fit

`tools/retarget-cj-model.py` produces a review-only glTF using captured native
bind matrices and CJ's separate body/clothing inverse binds. It checks
influence ranges, normalized weights, finite inputs and handedness. Face bones
currently follow the head and simplified fingers use one native finger chain.
This is not an animation loader. The initial fit showed severe shoulder/wrist
distortion; the revised fit improves the arms but still has neck/shoulder
proportion issues. Neither preview is approved for runtime installation.

```powershell
python tools/retarget-cj-model.py --gltf out/cj-prepared/CJ.gltf --bindings out/native-bindings.json --output out/cj-fit-review
python -m unittest discover -s tests -p test_cj_retarget.py
cmake --build out/gpu --target custom-material-test custom-texture-test
```

The offline fit requires NumPy; texture preparation requires Pillow. Game
assets and generated review files remain in ignored output directories.

## Native CJ payload preparation

`tools/build-cj-native-streams.py` prepares seven vertex/index payload pairs
from the textured CJ glTF. It preserves CJ's source pose with a uniform scale
and coordinate conversion, maps bone names explicitly, merges repeated joint
influences, quantizes weights with an exact total of 255, and generates a
tangent basis from triangles and UVs. It does not attach resources, register
materials or apply the unapproved bind-fit preview.

The CPU path at `0x5e5730` (including the `XOR ECX,0xff` at `0x5e61fa`)
reconstructs the first weight from the complement of seven stored bytes.
All 12,949 captured four-index vertices passed the bounded offline decoder;
the fourth explicit-weight channel was zero in this layout. Quantization tests
cover 2,000 synthetic weight sets, duplicate joints and malformed weights.
This supports the encoding but still needs GPU shader/render validation.
Packed-normal interpretation is also supported by captured distributions, not
yet by a custom-model rendering check.

```powershell
python tools/build-cj-native-streams.py --gltf out/cj-prepared/CJ.gltf --bindings out/native-bindings.json --output out/cj-native
python -m unittest discover -s tests -p test_kzsf_skin_weights.py
python -m unittest discover -s tests -p test_cj_native_streams.py
```

Session `cj-upload-20261022-043846` passed detached upload of a combined payload
with all 6,406 CJ vertices and 31,368 uint16 indices. The loader consumed all
230,676 vertex-stream bytes, inspected the new buffer, and released both
resources. The material-copy and 2048x2048 face-texture tests passed in the
same run. This proves resource loading only; CJ has not been drawn or added to
the model selector. The session exited normally with code 0 after the test.

## Owned textures and primitive assembly

Session `cj-upload-20261022-045242` created a Texture resource whose pixel
allocation belongs to the engine, bound CJ's face texture to three color
slots in an independent material, and released the material and texture.
Normal game exit returned code 0. This avoids retaining a host-side pixel
mapping for a drawable texture; native deferred destruction owns its lifetime.

`kzsf_custom_primitive.h` builds an independent RenderingPrimitiveResource
from retained vertex/index resources, finite bounds and a triangle index count.
It preserves the constructor's resource header and takes its own references;
the caller still owns the original buffer references. Tests cover balanced
ownership, allocation failure, invalid bounds and malformed triangle counts.
The opt-in upload probe now constructs, inspects and releases this primitive
before releasing the buffer references. This is still detached from the player.

Session `cj-upload-20261022-124054` passed primitive construction/release with
all 6,406 vertices and 31,368 indices, then exited with code 0. An intermittent
inspection rejection was traced to allocation flags in the size field's high
nibble, which is now masked consistently for vertex and index buffers. The
read-only geometry capture uses the same rule and tests flagged sizes while
still rejecting incorrect byte lengths.

`kzsf_custom_mesh.h` assembles a detached ModelPartResource containing a new
RegularSkinnedMeshResource and new GPU PrimitiveSkinInfo entries. Each entry
has the imported vertex count. Native skeleton, bindings and bone bounds are
retained, and primitive/material arrays have independent allocations. Six
allocation-failure paths and a two-section success case have balanced cleanup
in the ownership tests. The live diagnostic also verifies primitive/material
reference counts after releasing the assembled graph.

The diagnostic's borrowed bone bounds and material are for ownership checks
only. They are not approved rendering inputs for CJ. It does not register
physics, reuse native render packets, or install the part. Native bounds,
material/shader compatibility, corrected animation binding and drawable
attachment still need validation before adding CJ to the selector.

The payload preparation tool now exports six deduplicated base-color KZTX
textures and records the texture for each of the seven mesh sections. It
checks local paths, PNG dimensions and payload bounds; synthetic tests cover
sharing, RGBA preservation and invalid inputs. These are base colors only;
native material/shader interpretation still needs a rendering check.

The revised offline head mapping uses `neck`, rather than `cameraJoint`.
Captured native bind matrices place the camera above and forward of the neck
pivot. The revised preview improves head placement but still has neck/shoulder
proportion issues. `--bind-fit` exports that review fit as native streams and
records each section's bounds; it is opt-in and not installed in the game.

## Complete model assembly and local preview

The exporter writes `model.kzmd`, a bounded manifest with a native-rig checksum,
section/texture counts, expected payload sizes and per-file CRC32 checksums.
The opt-in `KZSF_CUSTOM_MODEL_PROBE_DIR` loader validates all files and the live
98-bone binding profile before creating resources. It creates seven independent
materials, six shared color textures and a neutral normal texture. Bone-local
culling boxes are calculated from the complete model's AABB and inverse binds.
The render-record size is adjusted for the new section count, using the
observed native 1/4/8/9-section layouts. No actor physics is replaced.

Session `cj-upload-20261022-134856` successfully assembled and released the
complete seven-section graph with seven material slots and seven skin entries.
Its first F11 attachment attempt crashed at `70b526`, called from the native
part transaction. `70b3e0` unconditionally merges bounds from skinned ModelPart
field `+20`; that link was null. The builder now gives the ModelPart its own
reference to the bounds resource as well as the mesh's reference. Synthetic
ownership tests cover both releases. Session `cj-upload-20261022-135353`
subsequently attached CJ and restored the original part repeatedly without an
immediate crash. This does not establish fall/death or exit stability.

Live screenshots exposed two UV mistakes: semantic 6 samples base color from
half4 `.zw`, and the native V convention is opposite the exported glTF's.
The corrected export now uses `(0, 0, u, 1-v)` and computes tangents in that
same UV convention. The updated screenshots confirm recognizable, correctly
oriented face, clothing and shoe textures.

The triangles bridging the shoes exposed a separate primitive topology bug.
Constructor `52ef10` sets header `+0xc` topology bits 8..11 to 4; native
triangle counter `5010b0` treats 4/5 as count minus two and 3 as count divided
by three. Captured original primitives use 3. New triangle-list primitives now
explicitly set only that nibble to 3, preserving reference counts and other
flags. The ownership test also verifies preservation of unrelated header bits.
Session `cj-upload-20261022-142723` then attached the corrected preview. User
front/rear screenshots show that the triangles bridging the feet are gone.

The torso fit also mapped CJ's `Spine_2` (scaled pivot height about 1.373 m)
to native `spine4` (1.495 m), lifting chest-weighted vertices toward the neck.
The next export used native `spine3` (1.363 m). Its preview improved shoulder
height but showed the head forward of the upper back during aiming. Inspection
of source influences found upper-back vertices dominated by `Spine_2`.
The subsequent fit separates the rest anchor (`spine3`) from the animation
joint (`spine4`), retaining the corrected geometry height while following the
native upper chest's aim bend. Synthetic tests verify that these two choices
remain independent. This latest aiming fit awaits visual review; shoulder/neck
shape, hands and animation fit remain unfinished.

The next side screenshot still showed the head forward of the neck. A bounded
read-only capture of the native 98-bone aiming pose and offline seam comparison
found 34 matching face/upper-body boundary vertices, with at most about 2.1 mm
separation both before and after skinning. This points to fit rather than an
opening material seam. The subsequent local preview adds a bind-space offset
of 4 cm backward and 1.5 cm downward to the entire head group (head, facial
joints and head-weighted upper neck). Tests check that facial/head transforms
remain identical. User screenshots still showed the raised back-neck shape, so
this positional adjustment was removed. The next preview instead blends the
central upper-neck weights toward the head, with smooth height/side falloffs
that leave outer shoulders and the lower body unchanged. It retains at most
four influences, reports discarded weight and rejects losses above 5%; the CJ
export discards none. Geometry positions remain unchanged. Both offline glTF
and native stream exports use this same weight fit. The next screenshot showed
an improved rear neck but a raised front-chest patch. The low rear transition
had also affected the sternum and shirt neckline. The revised region uses
separate front/rear height boundaries and existing neck influence to protect
the front chest. An asset comparison verified all 132 sampled front-chest
vertices keep exactly the earlier effective weights; all rest positions and
triangle indices are unchanged. Synthetic tests cover front-chest exclusion.
This revised region still awaits visual validation.

The next top/rear views showed folded neckline triangles. A subsequent offline
comparison found 67 coincident shirt/body rest vertices above 1.6 m remained
coincident in the captured aiming pose, so the visible defects were folds,
not a separating material seam. The upper-head aim motion plus an influence-
dependent transition produced excessive strain. The revised preview retains
the head's original rest pivot but drives the complete rigid head group with
`neckRoot`; neck weights also blend toward that lower-neck joint. The smooth
front/rear region no longer gates its strength on existing influence weights,
which had caused abrupt changes between nearby vertices. In one captured
98-bone aiming pose, tested neckline edge stretch (rest edges over 2 mm) fell
from a maximum 5.80x to 1.43x, with no tested edges over 2x. This is offline
evidence for one pose only, not validation of all animations or runtime draws.
The latest preview awaits user screenshots. Independent upper-head aim motion
is intentionally omitted for CJ in this fit.

## Diagnostic watchdog freeze

The long-running `cj-upload-20261022-142723` session stopped updating video
while audio continued. External thread capture found the main thread inside
logging with suspend count 1; the internal stall watchdog had stopped while
logging its own report. Resuming the main thread once recovered the picture,
as confirmed by the user. `dump_thread_rips` formerly suspended a target and
then called module lookup and `fprintf` before resuming it, deadlocking when
that target owned a needed logging/loader lock. It now queries the TEB before
suspension, captures only registers/raw trace state while suspended, and resumes
and closes the target handle before symbol/module lookup or formatting. Failed
suspensions are skipped. The corrected executable builds; long-session live
validation still requires a restart onto it. Native primitive, mesh ownership
and manifest tests pass. This recovery does not establish CJ gameplay stability.

Session `cj-upload-20261022-153743` runs the corrected watchdog executable.
It completed repeated periodic thread reports, assembled/attached CJ, and
restored the original body. Front/rear screenshots show the earlier chest fold
and foot-bridging triangles removed; the shoulder straps still form noticeable
peaks, so this is not a final fit. Walking, weapon switching, jumping, dropping
and transition/exit checks with CJ are still pending. The previous session's
status eventually recorded normal exit code 0 after the user quit.

The movement test then exposed overlapping CJ/default bodies after switching
weapons, jumping or dropping. The preview was not publishing the shared managed
replacement identity, so normal default-body maintenance could re-enable Lucas.
The next build publishes CJ through `kzsf_character_render_select_compatible`
before the part transaction and reuses `kzsf_observer_body_visibility` while
active. This preserves all native draw records/attachments and enables one
replacement slot while hiding exact duplicate/default full-body slots. Restore
and owner changes clear the preview's shared identity before releasing its own
references; restoration also invokes normal default-body visibility recovery.
This corrects the state integration, but the same weapon/jump/drop test must
be repeated on the rebuilt executable before claiming the overlap fixed.

Session `cj-upload-20261022-160519` then received user confirmation that Lucas
no longer appeared over CJ during the requested movement/weapon test. Pistol
screenshots still exposed armhole/strap deformation. A read-only capture of
that run's native 98-bone pose supported an offline comparison: some adjacent
shoulder vertices differed by over 30 percentage points in shoulder/upper-arm
weight over roughly 4 mm, due to the simplified CJ helper mapping. The next
export smoothly redistributes that combined influence along the native
shoulder-to-elbow axis, preserving other influence mass before four-influence
reduction. It also distributes CJ's single chest influence along native
spine3/spine4/chest by height. Reported discarded mass is bounded at 5% per
stage. Synthetic tests cover continuity, mirrored shoulders and preservation of
unrelated chest mass. Both glTF and native exports use these same fits.
In the two captured rifle/pistol poses, tested upper-body edge stretch maxima
changed from 3.54x/5.11x to 1.78x/2.38x; over-2x directed edges fell from
22/54 to 0/4. These are offline metrics only; visual validation remains pending.

The next screenshots showed a closer overall fit, with a small skin poke-through
near the vest armhole. The touch-up export adds at most 2.5 mm outward clearance
to the upper white vest, smoothly fading to zero at the waist. An asset comparison
verified all other sections are byte-identical, all triangle indices and weights
are unchanged, and the shirt's positions remain within the stated displacement
bound. Shirt tangents regenerate from its adjusted geometry. Ten retarget math
tests and five native-stream export tests pass. Visual review of the touch-up
is pending; shoulder silhouette remains subject to review.

Vest/shoulder follow-up (v19/v20, offline analysis only). The vest texture has
no brown content, so the remaining marks are geometric. CJ has no skin under
the vest: the arm/neck skin section is stitched to the vest through 85 shared
armhole/neckline vertices, and the welded vest is watertight apart from its
four openings. The 2.5 mm clearance therefore lifted the vest off those seams
and opened 2.5-3.8 mm cracks at three UV-split vertices whose normals differ by
up to 98 degrees. With backface culling the lifted edges expose culled vest
interiors: over eight camera angles, offline rasterisation counted 2983/3229
see-through vest pixels (rifle/pistol captures) for v18, 1/0 without clearance.
Clearance is retired from both exporters (function kept for its test).

The front-left under-pectoral crease was also the largest new fold in the rifle
capture (+70 degrees). The chest fit skipped native spine2 and squeezed the
spine handovers into ~12 cm bands. The captured native body (mesh 46 of
native-geometry-capture-28336) weights spine2 heavily around the belly, so all
spine mass now interpolates linearly over spine1/spine2/spine3/spine4/chest.
Rifle-capture new vest folds fell 16 -> 4, the worst crease growth 70 -> 45
degrees, vest stretch max 1.71x -> 1.53x; pistol is unchanged. The covered
jeans top band gains spine2 weight like the native body; jeans-through-vest is
one 1.3 mm sample. Directly transferring native torso weights and tapering
clavicle weight off the pec were both tried and rejected (more folds).

The native body never skins to raw upperArm/lowerArm/upperLeg; it uses their
co-located *_Helper1 joints. Both captures report helpers identical to the raw
joints, so `--deform-helpers` (v20 only) changes nothing offline. It is an
unverified trial that matters only if the runtime drives helpers differently.
Elbow and armpit LBS folds of the arm skin remain; they are unchanged by this.

### 22 October: shader input mismatch supersedes the two-influence hypothesis

The supplied 22:29 video and screenshot show v23 rendering but still producing
angular vest shoulders. Session `cj-upload-20261022-222218` runs the eight-bone
executable, and every active VA/IA/manifest hash matched immutable v23.

A split-vest diagnostic assigned identical chest128/wrist127 blends to both
halves, using zero-based slots0/1 on one side and slots0/2 on the other. It
passed stream decoding, checksum and native geometry validation. User screenshots
showed a continuous vest, but this alone cannot distinguish both weights being
read from both weights being ignored. The diagnostic has been removed from the
active folder and normal v23 restored.

Read-only inspection of the live native materials exposed the missing distinction:
the first leaf's shader declares semantics0,1,4,6,17, with NO weight semantics16
or18. Its shader resource is `0x1a0000a250` in this session. The native body main
shader (`0x1a0000a890`) declares0,1,4,6,16,17,18,19. These are session addresses,
not constants used by the implementation. Captures are in ignored
`out/cj-import/current-review/native-material-inputs.json` and
`live-material-comparison.json`. The shader object's vertex-stage link is +8;
the stage's 27-entry semantic map is +30. The rigid shader's absence of weight
inputs invalidates the earlier claim that stride36 itself supports only two
influences. Likewise, merely rendering stride44 never proved eight-weight use.
The exporter sorts influences, so dominant-bone changes can produce angular
discontinuities when the shader ignores the weight channels.

An opt-in `KZSF_CUSTOM_MODEL_WEIGHTED_MATERIAL=1` preview build now searches the
current native rig's materials for a main vertex shader with exactly the eight
required semantics. It validates shader RTTI, shader/stage object tables, bounded
input indices, uniqueness and repeat-read stability; it refuses four-index assets
in this mode. It snapshots/clones the selected native effect through the existing
ownership path, rebinds CJ base color and neutral base normal, and retains native
auxiliary detail/mask textures and parameters. Thus this is a geometry-validation
trial, not a completed CJ surface material. Other preview builds remain available.

The corrected executable is staged as `kzsf-probe-cj-weighted-material.exe`, with
`out/CJ Tools/10 Launch corrected weighted material.bat`. It requires a normal
quit/relaunch. In-game shoulder results for this executable remain unverified.
Material snapshot/input validation, clone ownership, manifest/geometry and mesh
ownership tests passed. The split-slot generator's test verifies equal decoded
bone blends for every selectable test slot and preservation of non-skin attributes.

### 23 October: weighted material snapshot rejection

The first weighted-material build detected F11 but repeatedly reported
`failed reason=source-material`. A read-only C harness using the exact production
snapshot and input-map functions against PID19380 reproduced this: every suitable
weighted effect was rejected because its entry+40 array was nonempty.
The original `buffers` field name was misleading. Live 64-byte records contain
inline values, a name String at+10, kind at+18, optional resource at+20, parameter
String at+28, and state at+30. The observed weighted material has nine scalar
values, including detail tiling. Native entry destructor503db0 confirms a64-byte
stride, String releases at+10/+28 and resource release at+20.

Snapshot and clone now support bounded resource-free material variables, with
independent arrays and retained String references. Non-null optional resources
remain rejected. Tests cover snapshot copying, bad counts/resources, independent
values and balanced cleanup at all eight clone allocation failure points.
All four material-snapshot/material-ownership/manifest/mesh tests pass.
The new executable is `kzsf-probe-cj-weighted-material-v2.exe`; shortcut10 now
launches it. The earlier live owner/material addresses became unreadable before
the final live selector check, so successful F11 attachment remains unverified.

### 23 October: weighted attachment confirmed; remove native surface atlases

Session `cj-upload-20261023-004756` (PID31560) reports weighted effect280a0,
input mask000f0053, successful assembly/attachment and restored references.
The user reports the body geometry looks fixed; three screenshots show much
better shoulders but native-looking black patterns across skin, vest and jeans.
This validates attachment and the shown poses, not all animations/lifecycle cases.

The next material trial preserves v23 geometry and the weighted shader while
replacing the inherited detail normals, mask/reflectance, roughness/AO and dust
color atlases with owned uniform textures. CJ base color remains section-specific;
the native lit-sphere lookup is retained because it is a lighting lookup rather
than a character atlas. The explicit binding-role list rejects unknown samplers.
Flat normals use128/128/255/255, masks0/0/0/255, roughness128/0/0/255 and dust
color255/255/255/255. Packed channel interpretation and final surface response
remain a visual trial. All neutral textures follow the existing retained clone
ownership and cleanup path. Material and snapshot tests pass.

Shortcut `out/CJ Tools/11 Launch CJ clean material.bat` launches the separately
staged `kzsf-probe-cj-neutral-material.exe`; shortcut10 remains the working
weighted-material-v2 fallback. The clean-material result is not yet observed.

### 23 October: clean atlas trial loses shadow lighting

The user's next three screenshots show clean CJ textures in direct light but
an almost black silhouette in shadow. The neutral maps removed the atlas patterns
but did not establish correct material channels. In particular, the shared
roughness/AO value128/0/0/255 assumed green/blue could be zero; that is unverified.
The game process had exited before the attempted pixel-shader capture, so no
shader channel decode was obtained. Do not describe the AO hypothesis as proven.

The next controlled trial changes only this uniform value to128/255/255/255.
It also accepts optional64x64 KZTX `neutral-0.rgba` through `neutral-3.rgba`
files in the preview folder, read fresh when F11 creates resources, with exact
size/header/dimension checks and CRC logging. No existing live resource changes.
These overrides allow subsequent isolated channel comparisons without restarting.
The new build is `kzsf-probe-cj-shadow-material.exe`, shortcut12. Material and
texture tests pass; its shadow result still requires visual confirmation.

### 23 October: metallic response channel comparisons

The full-green/full-blue neutral map (128/255/255/255) rendered silver across
skin and clothing. The following green-zero/blue-full override (128/0/255/255,
CRC 0c80fd4f) was confirmed loaded in session cj-upload-20261023-010633 and
the user's next screenshot remained metallic. Green zero alone did not resolve it.
The next staged override is 128/255/0/255 (CRC 05bfe36a), to compare blue zero
against the original full-green/full-blue baseline. Its visual result is pending.
These trials share a texture across base, detail and dust packed maps; they do
not yet establish the meaning of any individual shader channel. Geometry is
unchanged. F11 reads the override fresh; no restart is required.

The blue-zero trial was confirmed loaded (CRC 05bfe36a) and the next user
screenshot again shows a black silhouette. Read-only capture of native binding
state identifies shader descriptor slot11 as texturesetMaskReflectance and
slot13 as texturesetRoughnessMask_AlphaAO. A bounded decoder using the repository's
GCN opcode definitions produced current-review/weighted-pixel-decoded.txt.
At 0108 the slot11 RGBA sample yields v15-v18; its alpha reaches v29 at 01c4,
then v18 at 0220, and the packed MRT4 export at 08a8. The slot13 blue sample
feeds v41 at 01e0 and the log/power calculation using AO_power (captured as1.5).
This points to the full-alpha mask as the excessive reflectance input and blue
as an AO input. Exact material constant transforms remain unverified; the
decoder is a diagnostic artifact, not a general disassembler.

Next staged test: neutral-1 =0/0/0/0 (CRC a9c5c023) instead of0/0/0/255,
neutral-2 restored to128/0/255/255 (CRC0c80fd4f). This compares against the
previous chrome state by changing mask alpha only. Visual confirmation is
pending; production defaults remain unchanged. Script: current-review/
stage-reflectance-trial.py. No running resource was edited.

User confirmed this final trial fixed the appearance ("finally!"). Promoted
mask0/0/0/0 and packed128/0/255/255 to the source defaults. The active preview
folder retains identical overrides for the already-running executable. This
confirms the reported visual defect is resolved; it does not extend the
preview's existing gameplay/stability validation.

### 23 October: CJ model-selector integration

Added model17, `Custom_CJ` / `CJ (custom)`, to the overlay and F7 model cycle.
The custom entry loads local `user/models/cj` assets, bypassing the native
chapter-bundle loader. Missing assets disable the entry and exclude it from
cycling. The launcher clears inherited `KZSF_CJ_MODEL_DIR` overrides.
Managed selection uses the confirmed weighted material defaults without a
preview marker or 60-second timeout. Switching away restores the original
part before applying another model. Owner/model replacement clears the old
references; level exit restores when possible and releases retained resources
after actor teardown. Level transitions reset selection to default, matching
the existing selector behavior. CJ keeps the tested native98 draw path rather
than opting into NPC bind-pose correction solely because its ID exceeds3.

Installed21 manifest/geometry/texture files locally with size/CRC validation
and copy hash verification. Rebuilt and staged `out/kzsf-probe-launcher.exe`
and `out/cj-import/kzsf-probe-cj-selector.exe`; the prior launcher binary is
backed up at `out/cj-import/kzsf-probe-launcher-before-cj-selector.exe`.
Selector key/cycle and mailbox tests, five custom resource tests and21 launcher
tests pass. Actual selector switching, death/reload and chapter transitions
still require in-game validation; no claim of runtime validation is implied.

### 23 October: hide incorrect Lucas outfit selections

User reports Lucas Echo and spacesuit both display the default suit. Historical
session dialogue-20261023-002945-877400-modelauncher records spacesuit selection
with identical old/new part pointers; Echo records a distinct part but has no
successful visual validation. Their complete outfit behavior is unresolved.
Hide IDs13 and16 from the overlay, exclude them from F7 cycling and reject
mailbox requests for them. Preserve stable IDs and resource definitions for a
future repair. Selector cycle/mailbox regression tests and rebuild pass; updated
the local launcher executable. This is removal of broken selections, not an
outfit rendering fix.

### 23 October: traversal body visibility

User reports Lucas overlaps CJ while dropping and ziplining. The visibility
planner previously left the native body enabled if CJ was disabled and the
normal camera show-body gate was false. Added a CJ-only option to transfer an
enabled native body's visibility to one CJ slot and disable native duplicates
in the same validated transaction. Both-hidden states stay hidden and unrelated
attachments remain untouched. The fixture reproduces that state and passes;
rebuilt the local launcher. Falling/zipline visuals still need runtime checking.
Native bundle loading remains enabled across chapters. Its signature test requires an unpacked ELF; out/boot-linked.bin is not an ELF, so that test was not completed in this turn.
This does not establish universal chapter compatibility: CJ still validates
the exact98-joint binding fingerprint and refuses other rigs rather than
using an incompatible skin. Two broken Lucas outfit entries remain hidden.

User subsequently confirmed another model still overlaps CJ while dropping.
The visibility-transfer change did not resolve the reported issue. Keep it
open for gameplay/draw-record investigation; see docs/ROADMAP.md. An in-game
menu rendering investigation is also queued there.

### 23 October: maintain CJ outside follow-camera states

Continued falling investigation: both normal visibility maintenance and CJ
maintenance in the selector were guarded by a successful normal follow-camera
baseline. Traversal can bypass both calls. Added an existing-selection-only
visibility pass on direct engine camera callbacks, outside that state gate.
It requires the retained CJ part, matching published selection, exact Soldier
RTTI, matching owned model, no level transition, and the existing allocator/
visibility native code checks. It does not select or create resources there.
Visibility fixture and build pass; runtime outcome remains pending.

Current game PID23592 was left running. Candidate staged as
out/cj-import/kzsf-probe-cj-traversal.exe, launched through
out/CJ Tools/14 Launch CJ falling fix.bat. This uses the normal selector and
installed CJ assets. Read-only capture out/cj-fall-assembly-23592.json found
the earlier log owner had been destroyed and the current player was on the
default body; it does not capture the reported falling overlap.

User subsequently confirmed "it fixed" in response to the falling test.
Mark falling overlap resolved by user validation. Separate zipline and
switch-back coverage remains pending; do not infer those from this reply.

The local `KZSF_CUSTOM_MODEL_PREVIEW=1` diagnostic uses F11 and a
`preview.enable` file in the export folder after the detached graph is reviewed.
F11 attaches/restores through the native part transaction; a 60-second timeout
also restores the original body. Owner/model changes release only diagnostic
references, without calling a stale actor. Normal launcher sessions strip both
diagnostic variables. This is not a released selector entry. CJ renders and
follows the player animation in the local preview, with remaining fit issues
and incomplete gameplay/stability validation.
