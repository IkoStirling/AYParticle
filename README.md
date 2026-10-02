# AYParticle

CPU particle simulation shared by 2D and 3D. The runtime library depends only on
the C++ standard library and can build independently of AliyatEngine.

## Engine use

Enable `AY_ENABLE_PARTICLE` (default ON). With Renderer enabled the compatibility
Entity facade and default client ModuleManager install the particle integration.
Headless builds retain serialized emitter metadata and the CPU library, but do
not install a visual simulation tick automatically.

Create an Entity in the active World, add Transform and ParticleEmitterComponent:

```cpp
#include <AYEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/ParticleEmitterComponent.h>

auto* entity = ayt::entity::World::instance().createEntity();
entity->addComponent<ayt::entity::Transform>();
auto* emitter = entity->addComponent<ayt::entity::ParticleEmitterComponent>();
emitter->setEffect(ayt::particle::smoke());
emitter->play();
```

Add **Particle Emitter** from the editor's Effects component category. Reflected
fields can be edited and saved in a Scene; the normal scene presentation path
provides live preview. Empty texturePath uses a built-in soft disc. For textured
effects use an existing loose/cooked texture reference. Live particles and RNG
state are never serialized. A valid configuration edit restarts an autoPlay
emitter; invalid input reports `error` and clears its particles.

Presets: `smoke()`, `sparks()`, `explosion()`. To make 3D particles set
`dimension = Dimension::ThreeD`; optionally use Box or Sphere shapes.
See `examples/ParticleScene.cpp` for three scene emitters.

## Runtime contract

One emitter owns one dense ParticleInstance. play() restarts and reseeds;
pause()/resume() preserve time; stop(false) drains; stop(true) clears immediately.
Continuous emission uses fractional counts and ages births within the current
frame. Burst fires at play; with looping enabled it repeats every duration seconds.
Looping preserves existing particles and RNG state across cycles. Nonlooping
sparks()/explosion() presets fire once; call play() to restart them explicitly.
Large stalls and full capacities drop births without a future backlog.
Gravity is expressed in the selected simulation space. 2D suppresses local Z
velocity/gravity. World births capture the current emitter pose; Local particles
follow the current pose. Size is in world units for World particles and follows
the 2D emitter basis/scale for Local particles.

The default World budget is 100000 live particles, with a per-emitter maximum
of 65536. Allocation priority is World query order. Change
ParticleSimulationSystem::maxParticles before spawning. Lowering it while
particles are alive does not forcibly delete existing particles.

2D uses the independent SceneOverlay camera, layer and sortingKey. All emitter
batches participate in the Sprite/Tilemap stable global layer sort. Alpha uses
straight-alpha blending; Additive uses alpha-weighted RGB addition. No depth
test or depth write occurs in the particle overlay lane.

3D uses camera-facing unlit billboards through TransparentPass with depth
testing and no depth writes. Alpha particles sort within each emitter; batches
sort with scene transparent meshes by their center. Intersecting emitters or
transparent meshes can therefore have approximate ordering. Soft particles,
global per-particle/mesh sorting, mesh particles and effect cook
remain future work.

Draws use transient GPU buffers, capped at 16383 quads per batch. If the frame's
transient pool is exhausted, the affected batch is skipped. Simulation continues.
Frame payloads must remain valid until synchronous Renderer::render returns.
Cached textures/materials are renderer-owned; World shutdown detaches callbacks
and releases the emitter system's quad.

## Repository and verification

This standalone Git repository is registered as an AliyatEngine submodule.
Initialize submodules before configuring a root build with AY_ENABLE_PARTICLE ON.

```text
cmake -S AYRuntime/AYParticle -B out/build/particle-standalone
cmake --build out/build/particle-standalone
ctest --test-dir out/build/particle-standalone --output-on-failure
```

AYParticle_Tests exercises emission, budgets, replay, lifecycle, analytic motion,
spaces, shapes, atlas appearance and geometry limits. AYParticle_Demo runs three
presets and reports a 10000-particle CPU benchmark; it is a console benchmark,
not a graphical preview. Engine integration tests cover scene round-trip and
2D/3D render submission.

The generated `examples/ParticleShowcase.scn` is ready to open in AYEditor.
It contains an orthographic camera plus Smoke, Sparks and Explosion emitters.
Smoke emits continuously; Sparks and Explosion replay every 2 seconds so opening
the scene after its first frame still shows all three effects. For authored burst
previews set looping=true and duration to the replay interval; use looping=false
for gameplay effects that should fire once.

On Windows, build and run `AYParticle_GpuTest` separately for an opt-in D3D11
pixel regression in a hidden window. It checks actual color/alpha, additive
lifetime fade, world-depth occlusion and disabled particle depth writes.
It is excluded from the default build and portable CTest suite.

## Reusable effects and editing

Use **File > New Particle Effect** in AYEditor. The new .pfx is placed in
Assets/effects; double-click it to edit. The page has Play, Pause, Restart, Stop,
Step (1/60 s), Undo and Redo, plus time/live-count/CPU-update statistics.
Choose an emitter to edit; add Smoke, Sparks or Explosion, or remove it.
Offsets and start delays combine flash, sparks and smoke into one effect.
All simulation/appearance/atlas settings save in schema-3 JSON; schemas 1/2 remain
readable with its original endpoint appearance. Invalid edits
leave the previous values intact. Playback never marks the asset dirty.
Save/Save As/Reload/recovery use the normal document commands.

Add **Particle Effect** from the Scene Inspector Effects category, with a
Transform, and set effectPath to effects/YourEffect.pfx. The editor's
resource browser button can select this file. Existing **Particle Emitter**
components continue to work. Both have Edit-mode transport controls; resource
components also have Reload effect to retry failed loads. Only authored values
and paths save in scenes; playback starts fresh after loading.

The sample under examples/ParticleComposition contains Assets/effects/
CombinedExplosion.pfx and Assets/worlds/ParticleComposition.scn.
Copy those two files into the matching folders of a project's Assets directory,
then open the scene. Its composition repeats every 3 seconds.

AYParticle::Runtime retains only standard-library dependencies and now includes
EffectAsset/EffectInstance. AYParticle::Assets is an optional AYIO/JSON adapter;
AYParticle::Resources registers .pfx with AYResource. Default particle
integration installs its loader before requests. Resource snapshots are immutable;
editor saves refresh the cache and changed configurations restart autoPlay.
Texture paths resolve relative to the effect or mounted asset root.

A composition supports 1..64 unique named emitters and a total capacity up to
100000. Group repeating restarts every child and clears previous-cycle tails.
Emitter-local looping is separate. Stop cancels delayed emission; pause freezes
the group. Stalls skip old cycles, and all children share one spawn allowance.
Headless callers can load via AYParticle::Assets and prepare/tick a component
explicitly; metadata-only core keeps the JSON authoring adapter without Resource or Renderer dependencies.

The native editor canvas switches 2D screen and 3D perspective/depth orbit views, including
real CPU oriented-plane particles. It does not reproduce scene depth or the runtime
default soft-disc shader exactly. Verify final appearance in the Scene viewport.
Effect cook/dependency packaging is implemented by the optional resource
authoring adapters. The isolated native viewport has a session-only CPU/GPU/Auto
selector, ground/test cube, background, focus and layer hide/solo. GPU selected
previews have no live CPU particle arrays. Supported 3D camera-facing billboards
use GPU in the perspective/depth preview; oriented planes fall back to CPU with
a diagnostic.

## Lifetime curves and gradients

Select an emitter and choose **Size multiplier**, **Opacity** or **Color gradient
(RGBA)** under **Lifetime**. Time 0 means birth, time 1 means death, regardless of
random lifetime. Drag keys on the plot or select one and enter its time/value.
**Add key** splits the largest gap with an interpolated key; **Remove** removes
only interior keys. Birth/death times remain fixed. Each drag is one undo step;
losing pointer capture or leaving the page cancels the staged edit. **Use curve**
initializes legacy endpoint values; **Use endpoints** clears the selected track.
Play/Restart previews committed edits; the gradient strip shows RGBA on a checker.

Tracks use 2..32 strictly ordered keys with piecewise-linear interpolation;
Bezier/Hermite tangents are not supported. Size values multiply initial random
size and replace the old end-size ramp. Opacity is absolute alpha in [0,1],
overriding gradient alpha; an empty opacity curve uses gradient/endpoint alpha.
Gradient replaces endpoint RGBA, with finite nonnegative RGB (HDR supported).
Empty tracks preserve old files and manually authored endpoint values.

```cpp
auto effect = ayt::particle::ParticleEffect{};
effect.sizeCurve = {{0, 0.5f}, {0.2f, 2}, {1, 0}};
effect.opacityCurve = {{0, 0}, {0.1f, 1}, {0.7f, 1}, {1, 0}};
effect.colorGradient = {{0, {1, 1, 0.7f, 1}},
                        {0.3f, {1, 0.6f, 0.1f, 1}}, {1, {1, 0.1f, 0, 0}}};
```

Smoke, Sparks and Explosion presets now demonstrate nonlinear lifetime tracks.
The generated showcase and CombinedExplosion resource include the same curves;
both dimensions use the same ParticleInstance::sample calculation.
CurvedSmoke.pfx and CurvedSparks.pfx in the composition example's
Assets/effects folder can also be opened independently in the particle editor.

## Reusable library and packaged playback

See [ParticleLibrary](examples/ParticleLibrary/README.md) for Smoke, Sparks,
Explosion, Flame and Rain, original procedural sprites, a five-effect showcase,
and an executable player. Assets work in both 2D and 3D after changing emitter
dimension; this showcase uses an orthographic 2D camera.

With `AY_ENABLE_PARTICLE` and resource authoring enabled, `import_tool` accepts
`.pfx`. The adapter validates the typed effect, finds textures inside the
nearest `Assets` directory (or `ImportOptions::sourceAssetRoot`), preserves the
effect's relative filename and rewrites texture references to portable cooked
paths. Release textures use lossless RGBA8 `.aytex` with mipmaps, retaining soft
alpha edges. Dev import can retain raw texture formats. Empty texture paths still
use the default disc. Absolute references and dependencies escaping the root fail.

Project build profiles cook effects with `cookTextures:true` and place them in
Loose or Pak storage. Scene files remain Raw/Loose. The planner validates scene
effect paths, missing/excluded textures, and reference paths that would change
under Raw/Cook mixing. It hashes the effect plus exact texture closure; changing
pixels recooks every owner. Executing a stale particle plan requires replanning.
Identical shared texture outputs are deduplicated; conflicting bytes or storage
destinations fail. Publish remains transactional through ProjectBuild.
Raw particle references to Pak textures must already be root-relative; cook
owner-relative paths first. Texture output names hash the full relative source
path to avoid basename collisions and reduce Windows cache path length.

`cook_tool` consumes already-cooked effects and `.aytex` closure. It validates
particle dependencies before replacing existing ship files and records texture
edges directly, even without sidecars. Runtime hosts mount `resources.db` through
`ResourceManager::openDatabase` and install the particle integration/loader;
virtual `effects/` and `textures/` paths retain their DB keys with loose roots
mounted. Loading an effect resolves immutable configuration, not live particles.

`AYParticle_PackageTests` covers closure validation, texture mutation, cache reuse,
shared resources, stale plans, source-free package playback and standalone import/
CookShip. `AYParticle_GpuTest --package Content` additionally uploads the package's
actual sprites and verifies visible D3D11 pixels for all five library effects.

## GPU scene playback

Optional `AYParticle::Gpu` provides a D3D11 compute simulation backend.
`AYParticle_GpuDemo` shows GPU sparks/rain and `--verify` checks real GPU state,
direct pixels and 100k live slots. See [GpuParticles](examples/GpuParticles/README.md)
for build/run instructions, scene/render-host ownership and limits.

Set `ParticleEffect.backend` or `EffectAsset.backend` to `Backend::Gpu` or
`Backend::Auto`; old resources default to CPU. Composition component `backend=3`
inherits the asset; 0/1/2 override CPU/GPU/Auto. The effect editor persists **Scene
backend** in undo/save history; Inspector controls work in the Scene viewport.
`backendStatus` shows a diagnosed CPU fallback for unsupported builds/devices,
oriented planes, textures or capacity budgets. A composition switches as a whole.

GPU supports 2D World/Local Point/Rectangle/Disk and 3D camera-facing
Point/Rectangle/Disk/Box/Sphere billboards, full emitter basis, lifetime
curves/gradients and atlas animation. The World reserves full GPU capacity;
`gpuReservedParticles` is capacity, while `liveParticles` counts CPU particles.
The isolated native document canvas uses the same GPU composition bridge with
independent compute/draw views and targets. GPU births, RNG
and integration differ from CPU; 3D scene batches depth-test and sort by center,
without per-particle sorting, depth writes, collision or compaction.
AYShader compiles the four built-in D3D11 shader stages during the build. Their
binary bundle is embedded in `AYParticleGpu` and travels with every linked host;
particle playback needs no compiler, source paths or writable shader cache.
The independent GpuParticles profile packages a compiler-free runtime example.
The standard ParticleLibrary host still uses the ordinary Renderer/Phoskia runtime
shader pipeline for other scene shaders. See ParticleLibraryGpu.scn for all
five effects and the GpuParticles README for packaging/validation commands.

## 2D scene collision

World-space 2D CPU particles can collide with scene-authored
`ParticleSurface2DComponent` rectangles. `CollisionMode2D::Sweep` tests each XY
motion segment against `solid` rectangles, for side-view sparks and debris.
`CollisionMode2D::Ground` keeps the XY position as a landing anchor and falls
through a separate visual height onto the highest `ground` rectangle below its
spawn height. This supports top-down or pseudo-perspective rain on ground and
raised roofs. Ground mode requires zero XY velocity and gravity. Both modes can
kill or bounce, filter with `surfaceMask`, and emit a bounded contact event.
The scene integration draws a short, configurable contact pulse when enabled.

Add a surface component to an entity with Transform; its center and half extent
define an axis-aligned world-XY rectangle. Height, tag, mask, ground and solid
are serialized scene fields. Surface rectangles are effect collision data and
need separate Sprite/Tilemap art. The host gathers them each presentation frame;
they do not alter physics bodies or Tilemap collision. Standalone/headless callers
can supply `CollisionQuery2D` to `ParticleInstance::update` or
`EffectInstance::update`. With no query, Sweep has no hits and Ground has no
landing surface. A requested GPU/Auto backend diagnoses a CPU fallback for
effects using collision. See [Collision2D](examples/Collision2D/README.md) for a
loadable rain/roof scene. Surfaces currently ignore Transform rotation and scale;
Tilemap painting and editor-document collision preview are not implemented.

## Typed variation and heterogeneous layers

Schema 3 adds Particles/SpriteAnimation layer kinds, stable IDs, composition seed,
probability enable and random delay. SpriteAnimation derives one stationary card;
rotation, atlas, appearance and sway remain authored. See
[the Falling Leaves example](examples/FallingLeaves/README.md) for original animated
leaf textures, a three-layer 2D GPU scene and actual CPU 3D tumble.

FloatAttribute sources cover life, size, rotation/spin, FPS/start frame,
size multiplier, opacity, sway amplitude/frequency/phase, rate and burst.
Legacy/Constant/Uniform/Weighted/Curve/BetweenCurves use explicit
EffectPlay/EffectCycle/ParticleBirth sampling. Frozen property hash streams avoid
frame flicker and shifts when unrelated properties change. Layer gates/delays
and emission rate/burst choose per play/cycle, while opacity/size curves follow
age; other curves sample emitter phase at birth. Weighted sources accept up to
8 choices; curves accept 2..32 keys. Uniform frame/count ranges include endpoints.

Mirror X/Y and reverse playback accept fixed/probability modes. Flipbooks support
Loop, Once, PingPong and normalized lifetime timing. CPU Plane uses actual 3D
geometry and tumble; GPU supports camera-facing 3D billboards and falls back
for oriented planes.
The editor's layer list, source selector, lower/upper CurveCanvas and activity
bars support history/save/recovery. Preview seed variation and bounded seek do
not dirty the resource. Mesh/light/audio/trail layers and arbitrary expressions
remain future work.

## Runtime editor viewport

The native particle document preview uses runtime particle shaders, textures and
blend/depth rules in an isolated pooled render target. It does not tick project
Worlds/scripts or replace the main Scene camera. Select **2D screen** or **3D
scene**; the viewport CPU/GPU/Auto selector is separate from the saved **Scene
backend** field. GPU capacity shown in stats is allocated capacity, not live count.
Focus, background, ground/grid, test cube, layer Hide/Solo and preview seed remain
session-only. Stable layer IDs preserve visibility across rename/reorder.
CPU seeking replays the first 60 seconds at 60 Hz; GPU seeking catches up one
60 Hz step per rendered frame with progress. Restart/Stop/variation cancels it.
GPU focus uses authored bounds without readback. CPU restoration from long GPU
playback is bounded to 60 seconds/an initial-cycle phase; after long playback,
per-cycle random choices can differ. Supported 3D GPU billboards use the orbit
camera and depth-test against the test cube without depth writes; oriented
planes fall back to CPU. This isolated editor-document viewport does not load
scene-authored collision surfaces. Soft particle fading and scene
postprocessing are future work. Non-bgfx hosts retain a
labeled software CPU fallback.

Real D3D11 pixel/lifecycle regression: build `AYEditor_ParticlePreviewRenderTest`
and run with an output directory. It checks perspective object occlusion, real
GPU compute/draw ordering, 3D orbit-facing/depth occlusion, empty CPU arrays,
hide, seek, stop/restart, resizing and multiple preview targets. Model/history
checks live in `AYEditor_ParticleTests`.
