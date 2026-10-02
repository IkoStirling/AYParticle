# GPU particle simulation and scene integration

This example uses **GPU simulation and GPU-generated draw vertices**. CPU code
only schedules bounded births, uploads emitter parameters and submits compute/
draw commands. It never uploads a live particle array or reads particles back
for rendering. Explicit diagnostic capture is used only by validation.

Motion uses a semi-implicit Euler step with exponential velocity damping;
its integration, birth aging and RNG do not promise CPU-identical trajectories.

Build from the engine root in the configured Windows developer environment:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_GpuDemo
out/build/windows-debug-vs2026-insider/AYRuntime/AYParticle/AYParticle_GpuDemo.exe
```

The window shows additive sparks on the left and alpha-blended rain on the
right. Escape closes it. Optional `--seconds 5` bounds playback; `--capacity N`
sets each emitter's capacity (1..65536). Default capacity is 50000 per emitter,
but ordinary emission does not fill every slot. `--hidden` runs without showing
the window. `--screenshot file.bmp` captures the actual backbuffer after one
second (use a run longer than one second and an existing parent directory).

```powershell
out/build/windows-debug-vs2026-insider/AYRuntime/AYParticle/AYParticle_GpuDemo.exe --verify --no-child-processes
```

Verification uses a hidden real D3D11 window and asynchronous GPU diagnostics.
It checks a partial final workgroup, exact fixed-velocity/gravity motion, age,
pause/resume, stop/drain, death, clear/reseed, rectangular spawn bounds, slot-ring
saturation, independent emitters with **100000 live particles**, and visible
pixels rendered from compute-generated vertices. It also reports CPU submission
time for 100k GPU slots and CPU simulation/sample/geometry time for a 100k CPU
reference in Debug. These are different work scopes, exclude shader compilation,
and are **not** end-to-end frame times or a GPU/CPU speedup claim.
When device timestamps are available, validation also reports profiled compute
and draw view times for the 100k-slot, 128x128 stress case. This small target is
a pipeline stress check, not a full-resolution quality/performance benchmark.

Observed on the development machine (Debug, 100000 live slots, 128x128 target):
GPU compute views averaged about **0.17ms**, draw about **0.08ms**, and CPU host
update/draw submission about **0.007ms**. The CPU reference's simulation/sample/
geometry averaged about **106ms** for the same count in Debug. These single-run
results describe their respective work scopes and do not predict game frame rate.

## Render-host contract

Link optional `AYParticle::Gpu` and include `AYParticle/GpuParticle.h`. The host
must initialize a D3D11 bgfx device first and check `GpuParticleContext::supported()`.
The context shares compiled programs; each `GpuEmitter` owns independent state,
compute-generated quad storage, immutable indices and a texture. Destroy these
objects before renderer shutdown. APIs are confined to one render submission
thread. CMake invokes `AYParticle_ShaderBake` at build time. The tool uses the
engine's `AYShadercDriver`, configured bgfx include paths and a 30-second timeout
per stage; it compiles two compute stages plus vertex/fragment for D3D11 s_5_0.
The generated header embeds byte arrays and stage hashes in `AYParticleGpu`.
Runtime verifies container stage, size and hash, then loads bgfx shaders directly;
it does not launch a compiler or require source paths or a writable shader cache.
The runtime library has no AYShader link dependency. Compiler/include paths are
required only on the build machine for these particle shaders.

Generated artifacts live in the configuration's `generated/<config>/d3d11`
directory. Source/compiler/include changes trigger a rebuild. Four `.bin` files
and `manifest.json` are diagnostic build artifacts; packages need only the host
executable and its normal runtime libraries, because the shader bytes are embedded.
Compilation failure preserves the previous consumed header and fails the build.

```cpp
ayt::particle::GpuParticleContext context;
ayt::particle::ParticleEffect effect;
effect.capacity = 50000;
effect.rate = 10000;
ayt::particle::GpuEmitter emitter(context, effect);
emitter.play();
// Each frame, after the host configures the draw view's target/camera/viewport:
emitter.update(/* compute view */ 0, dt, {0, 2, 0});
emitter.draw(/* later draw view */ 10);
// Host submits bgfx::frame(); emitter/context outlive these submissions.
```

View execution must place compute before draw. Call update once per emitter per
submitted frame. `dt` is finite and in [0,0.25]; bound large stalls in the host.
Changing the emitter position affects future births; existing world particles
keep their positions. `updatePose` accepts a full emitter basis; Local particles
follow later pose changes, and World births capture rotation/scale. `play()` queues
clear/reseed; `pause` freezes time/births; `stop(false)` drains living particles;
`stop(true)` queues clearing. Clear becomes visible after the next update.
Nonlooping emission stops at duration; looping repeats burst cycles.

The backend accepts **2D, World/Local, Point/Rectangle/Disk**, size/opacity
curves, RGBA gradients, uniform-grid atlas animation, gravity/drag and spin.
Rectangle extents match CPU half-extents; Disk uses elliptical radii. It rejects
3D and other shapes. Use `validate` before construction. `setTexture` uploads tightly packed RGBA8 once; paths in authoring
data are not automatically loaded. The default texture is a soft disc.

Each emitter has a fixed slot ring. New births replace oldest slots on overflow;
excess births are bounded with no deferred backlog. Births have age zero at the
end of their submission frame, unlike CPU births aged within the frame. Dead
slots generate zero-area transparent quads. The backend submits one fixed-size
indexed draw per emitter; it does not compact, depth-sort, issue indirect draws,
write depth, collide or provide soft particles. Additive effects and planar rain
are suitable initial cases. Shader randomness is repeatable on the current GPU
but does not promise CPU-identical or cross-device results.

## Scene, editor and packaged example

Recommended gameplay acquisition is the existing particle component, with
`ParticleEffect.backend` or `EffectAsset.backend` set to `Backend::Gpu`/`Auto`.
Legacy resources remain CPU by default. `ParticleEffectComponent.backend=3`
inherits its resource; 0/1/2 override CPU/GPU/Auto. The integration switches a
whole composition and displays `backendStatus` when it falls back: unsupported
build/device, oriented 3D planes, RGBA8 texture acquisition, GPU program creation or World budget.

The editor's **Scene backend** choice supports undo/save/reload. Scene Inspector
play/pause/restart/stop/step affects GPU transport; the Scene viewport renders the
selected backend. The isolated document canvas is labeled **CPU reference**.
World metrics reserve full GPU capacity without readback; `gpuReservedParticles`
is allocated slots and `liveParticles` is the exact CPU count. Renderer-owned
scene callbacks release GPU resources before World/device shutdown. Frame compute
view 243 precedes overlay 246, which composites before Bloom/postprocessing.

Open `ParticleLibrary/Assets/worlds/ParticleLibraryGpu.scn` for Smoke, Sparks,
Explosion, Flame and Rain. The same cook profile packages both scenes and their
sprite closure. After building/repackaging, run:

```powershell
out/particle-library/package/AYParticle_LibraryPlayer.exe --gpu --play-seconds 5
out/particle-library/package/AYParticle_LibraryPlayer.exe --gpu --verify
```

The first runs actual D3D11 scene simulation/drawing with seven GPU emitters; the
second checks diagnosed CPU fallback on Noop using only packaged resources.
Particle shaders load the embedded bundle in this host too. The ordinary
GameProject Renderer/Phoskia pipeline still compiles other scene shaders at
runtime, so this complete host retains its existing development compiler/include
dependency. GPU verification additionally checks interior lifetime keys, atlas
UVs, Local pose geometry and Disk spawn bounds.

## Independent packaged GPU runtime

Build and package from the engine root in the configured Windows developer
environment:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_GpuDemo AYParticle_ShaderBinaryTests project_build_tool
out/build/windows-debug-vs2026-insider/AYRuntime/AYTool/project_build_tool/project_build_tool.exe --project . --profile AYRuntime/AYParticle/examples/GpuParticles/BuildProfiles/windows.bld --skip-code
out/particle-gpu-runtime/package/AYParticle_GpuDemo.exe --verify --no-child-processes
out/particle-gpu-runtime/package/AYParticle_GpuDemo.exe --no-child-processes
```

This direct bgfx host uses procedural sprites and embeds its shaders; the content
rules intentionally select no assets. ProjectBuild still stages code, adjacent
runtime libraries and an empty Content database. No shaderc, `.sc`/`.sh` sources
or separate shader binaries need to be copied. `--no-child-processes` installs a
Windows policy prohibiting child processes before device initialization, proving
the renderer works without launching shaderc. `--verify` exits with failure if
this policy cannot be set or GPU/pixel checks fail. It runs a hidden window.

The packaged executable has also been verified with PATH limited to Windows and
System32. This checks compiler-free execution on the development D3D11 device;
it does not extend the backend to Vulkan/Metal/D3D12 or certify every Windows
device. The normal platform/Debug CRT and graphics driver requirements remain.
`AYParticle_ShaderBinaryTests` checks real baked stages and rejects corruption,
wrong stage, wrong hash, truncation and empty data without creating a GPU device.
