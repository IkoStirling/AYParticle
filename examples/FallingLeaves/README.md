# Falling Leaves

Open `Assets/effects/FallingLeaves.ayparticle` in AYEditor, or use the packaged
`FallingLeaves.ayscene`. The effect combines three independently timed layers:

- Foreground particle leaves fall with smooth lateral sway, random spin/size.
- Distant leaves have 80% enable probability per composition cycle and random delay.
- Hero leaf is an independent SpriteAnimation card with its own life and timing.

The original 4×2 RGBA flipbook depicts front/back rotation. Each birth chooses
animation speed (5–11 FPS), a starting frame (including the last frame), mirror
and playback direction. Those choices stay fixed through the particle's life.
This illustrates an animated 2D silhouette; `TumblingLeaves3D.ayparticle` also
uses actual CPU plane orientation with random tilt and angular tumble in 3D.
The native editor opens it in a perspective scene with depth occlusion; right-drag
to inspect the tumble. GPU supports the 2D asset; 3D uses CPU.

In the inspector select Property source, then Existing values, Constant, Random
range, Weighted choices, Curve or Random between curves. Explicit scopes choose
once per effect play, composite cycle or particle birth. Opacity/size multiplier
curves follow normalized age; birth values follow emitter phase. Enable, delay,
rate and burst use play/cycle scope. Only the listed typed properties support
these modes; shape, material, arbitrary expressions and custom shaders do not.

New variation changes preview seed without modifying the asset. Asset seed
restores the authored seed. Activity bars click-seek by deterministic 60 Hz replay
up to 60 seconds. Save/history/recovery include all schema-3 values; schemas 1/2
remain readable. Stable nonzero layer IDs retain random streams across renames
and reordering. No mesh/light/audio/trail layers are provided in this phase.

Build and package from the engine root:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_LibraryPlayer project_build_tool
out/build/windows-debug-vs2026-insider/AYRuntime/AYTool/project_build_tool/project_build_tool.exe --project . --profile AYRuntime/AYParticle/examples/FallingLeaves/BuildProfiles/windows.aybuild.json --skip-code
out/particle-leaves/package/AYParticle_LibraryPlayer.exe --leaves --gpu --verify
out/particle-leaves/package/AYParticle_LibraryPlayer.exe --leaves --gpu
```

The package carries cooked leaf texture, schema-3 effects, scene and resources.db;
playback reads the package independently of the source Assets folder. The normal
GameProject renderer still has its existing development shader tool dependency.
Particle shaders themselves are compiled with AYShader during the build and
embedded. `AYParticle_GpuDemo --verify --no-child-processes` checks that boundary.

Regenerate deterministic original assets with `tools/generate_leaves.py` (Python
standard library only). Regression targets: AYParticle_Tests, AYParticle_AssetTests,
AYParticle_PackageTests, AYEntity_ParticleTests and AYEditor_ParticleTests. GPU
verification covers source hashes, frozen booleans, flipbook UVs, curve blending,
sway and 100000 live slots. CPU/GPU trajectories and slot reuse need not match.

Optional packaged texture pixel check:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_GpuTest
out/build/windows-debug-vs2026-insider/AYRuntime/AYParticle/AYParticle_GpuTest.exe --package-leaves out/particle-leaves/package/Content
```

### Native editor runtime preview

Open `TumblingLeaves3D.ayparticle`: the canvas starts in perspective 3D with a
simple ground/grid and test cube. Right-drag to orbit, wheel to zoom and Focus to
fit the leaves. Object depth hides leaves behind the cube; Ground/Object toggles
let you isolate the visual. Select 2D screen and CPU/GPU/Auto to compare the 2D
`FallingLeaves.ayparticle`. GPU mode simulates the same composition without CPU
particle arrays; the status reports a whole-composition fallback when unsupported.
Hide/Solo and background do not alter the effect file. Timeline seeking is capped
to 60 seconds; GPU replay advances one 60 Hz step per rendered frame with progress.
`GpuRain.ayparticle` provides a texture-free 2D GPU rain/drop preview.

### Authoring a composite effect

Open `Assets/effects/AutumnGust.ayparticle` for a complete 2D example: near
flipbook leaves, delayed dust (the default soft disc), and a separate hero-leaf
SpriteAnimation. `Assets/worlds/AutumnGust.ayscene` mounts the same resource on a
ParticleEffectComponent, so the saved editor result is usable by Scene playback
and included in the package profile.

After building and packaging with the commands above, verify this scene's ECS
resource, cooked flipbook, default-disc layer and extracted geometry with:

```powershell
out/particle-leaves/package/AYParticle_LibraryPlayer.exe --gust --gpu --verify
```

In the layer list choose a layer, then use Duplicate, Move up/down, Hide or Solo.
For a 2D layer, drag its crosshair in the canvas to place it. Rectangle emitters
have X/Y extent handles; Disk has a single radius handle. Releasing the
drag creates one undo entry, and cancelling capture leaves the resource unchanged.
The inspector's Textures gallery shows selectable thumbnails from the project's
`textures` folder; Browse texture opens the system picker for more files in that
folder. Both store a portable resource path. The image and current
base-FPS atlas frame appear beside the live runtime canvas. Set grid columns,
rows, first frame and frame count below it. Start/end colors and gradient keys
use the RGBA picker; random bool/number modes, delays and per-particle animation
sources remain in the existing property controls.
