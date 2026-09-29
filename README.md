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
frame. Burst fires once at play; looping repeats continuous emission, not bursts.
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
global per-particle/mesh sorting, mesh particles, GPU simulation, effect asset
loaders/cook and a dedicated curve editor remain future work.

Draws use transient GPU buffers, capped at 16383 quads per batch. If the frame's
transient pool is exhausted, the affected batch is skipped. Simulation continues.
Frame payloads must remain valid until synchronous Renderer::render returns.
Cached textures/materials are renderer-owned; World shutdown detaches callbacks
and releases the emitter system's quad.

## Local repository and verification

This is a standalone local Git repository with no remote and no .gitmodules
entry yet. Root builds require this local checkout when AY_ENABLE_PARTICLE is ON.
Disable the option on clones that do not contain it.

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

The generated `examples/ParticleShowcase.ayscene` is ready to open in AYEditor.
It contains an orthographic camera plus Smoke, Sparks and Explosion emitters.
Restart the scene to replay the one-shot effects.

On Windows, build and run `AYParticle_GpuTest` separately for an opt-in D3D11
pixel regression in a hidden window. It checks actual color/alpha, additive
lifetime fade, world-depth occlusion and disabled particle depth writes.
It is excluded from the default build and portable CTest suite.
