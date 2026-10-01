# Particle Library

Five ready-to-use schema-2 effects. Open `Assets/worlds/ParticleLibrary.ayscene`
in the editor to see them from left to right:

| Effect | Visible behavior | Suitable uses |
| --- | --- | --- |
| Smoke | Rotating cloud sprites rise, expand and fade | Chimneys, impact dust, residual smoke |
| Sparks | A burst scatters bright streaks, falls under gravity and fades | Metal impacts, electrical sparks |
| Explosion | Animated 4×2 flash, flying sparks, then delayed smoke; repeats every 3 seconds | Small blasts, destruction feedback |
| Flame | Yellow-to-orange sprites rise and shrink, with additive blending | Torches, campfires, engine exhaust |
| Rain | Thin blue streaks spawn across a rectangle and fall continuously | A local rain patch, background weather |

These are stylized sprite effects; rain has no ground splashes, smoke has no
volumetric lighting, and fire does not illuminate the scene. For one-shot
explosions disable the asset's group looping; for one-shot sparks disable the
emitter looping. Emission dimensions can be changed to 3D in the particle editor.

The five textures are original mathematical alpha sprites. Rebuild all assets
deterministically with Python 3, standard library only:

```powershell
python AYRuntime/AYParticle/tools/generate_library.py
```

Build and package from the **engine repository root**, in its configured Windows
developer environment:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_LibraryPlayer project_build_tool
out/build/windows-debug-vs2026-insider/AYRuntime/AYTool/project_build_tool/project_build_tool.exe --project . --profile AYRuntime/AYParticle/examples/ParticleLibrary/BuildProfiles/windows.aybuild.json --skip-code
out/particle-library/package/AYParticle_LibraryPlayer.exe --verify
out/particle-library/package/AYParticle_LibraryPlayer.exe
```

The checked-in profile uses the engine's development preset and prebuilt player.
`--skip-code` still stages the executable and adjacent runtime libraries. This
profile's code paths and assetRoot are relative to the engine root; it is not a
separate standalone project manifest. Repeated packaging reuses closure caches.

The package contains a loose showcase scene, particle/texture Pak and resource
database. `--verify` mounts only that package, simulates 360 frames and checks
five live effects, their textures/mipmaps and render geometry using the Noop
backend. A normal run uses the standard GameProject application and a window.
For a bounded live application check, run the player with `--play-seconds 5`;
it closes after five seconds of updates and reports live particle counts.
`--content directory` selects a different cooked Content or source Assets root;
verification requires a package database and has no source fallback.

Optional GPU validation:

```powershell
cmake --build --preset windows-debug-vs2026-insider --target AYParticle_GpuTest
out/build/windows-debug-vs2026-insider/AYRuntime/AYParticle/AYParticle_GpuTest.exe --package out/particle-library/package/Content
```

The GPU test draws actual package textures through the D3D11 particle pass and
reads back visible pixels. This complements the executable's scene/ECS playback
verification; it is not a performance benchmark or a whole-scene screenshot.

Windows validation note: the resource CTest integration tier's deep scratch path
hits the existing AYIO atomic-write path limit in the WAV cook-cache case. The
same 126 selected cases run with 114 passes and 12 optional fixture skips using
the runner's shorter default scratch directory. Particle/package tests and both
player checks pass. This example keeps its cache under `out/particle-library`.

## GPU scene variant

Open `Assets/worlds/ParticleLibraryGpu.ayscene` in the editor, or run the rebuilt
package player with `--gpu --play-seconds 5`. All five effects use seven GPU
emitters, including curved smoke/flame and the animated explosion. The Scene
Inspector backend choice overrides the resource; unsupported configurations
fall back with a reason. `--gpu --verify` checks packaged CPU fallback on Noop.

Particle GPU shaders are compiled through AYShader at build time and embedded
in the executable; this part needs no runtime shaderc/bgfx source paths. The
standard GameProject Renderer/Phoskia pipeline still compiles other scene shaders
at runtime, so this complete development host retains its compiler/include-path
dependency. The independent GpuParticles package verifies compiler-free particle
playback. 3D GPU particles and GPU sorting remain future work. The isolated effect
editor canvas is a labeled CPU reference.
