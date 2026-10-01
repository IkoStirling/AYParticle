#pragma once
#include <AYParticle/Particle.h>
#include <array>
#include <memory>

namespace ayt::particle {
/// Experimental render-host API (AYParticle::Gpu); ECS hosts acquire it through backend preferences.
/// Requires an initialized D3D11 bgfx device with compute and 32-bit indices.
/// Uses build-time AYShader binaries embedded in AYParticle::Gpu; runtime needs no compiler/source paths.
/// Destroy emitters/context before the renderer shuts down. Single render thread.
class GpuParticleContext {
public:
    GpuParticleContext();
    ~GpuParticleContext();
    GpuParticleContext(const GpuParticleContext&) = delete;
    GpuParticleContext& operator=(const GpuParticleContext&) = delete;
    static bool supported() noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
    friend class GpuEmitter;
};
struct GpuParticleSnapshot {
    uint32_t alive = 0;
    /// Explicit diagnostic readback of the first 64 slots, never used to draw.
    std::array<Particle,64> slots{};
    std::array<Color,64> colors{};
    std::array<std::array<float,2>,64> uv{};
    std::array<Vec3,64> firstVertex{};
};
/// GPU-owned state and compute-generated quad vertices, no per-frame particle upload.
/// Supports 2D World/Local Point/Rectangle/Disk, lifetime tracks and sprite atlases.
/// Other dimensions/shapes are rejected; scene integration falls back to CPU.
/// A fixed slot ring replaces oldest births on overflow. No sorting/compaction.
class GpuEmitter {
public:
    GpuEmitter(GpuParticleContext& context, const ParticleEffect& effect);
    ~GpuEmitter();
    GpuEmitter(const GpuEmitter&) = delete;
    GpuEmitter& operator=(const GpuEmitter&) = delete;
    static bool validate(const ParticleEffect& effect, std::string* error = nullptr);
    /// Queue clear/reseed on the next update. pause freezes simulation and births.
    void play() noexcept;
    /// Stable layer seed and enclosing composition cycle for scoped sources.
    void play(uint32_t instanceSeed,uint64_t effectCycle) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void stop(bool clear = false) noexcept;
    /// At most once per emitter per submitted frame. dt must be finite, 0..0.25s.
    /// Compute view must execute before its draw view. This overload uses translation only.
    void update(uint16_t computeView, float dt, Vec3 position = {});
    /// Full 2D emitter basis; allowance bounds births without deferring a backlog.
    void updatePose(uint16_t computeView, float dt, const Pose& pose, uint32_t allowance=UINT32_MAX);
    /// Uses the caller-configured view transform, target and viewport; no depth writes.
    void draw(uint16_t drawView);
    /// Copies tightly packed RGBA8 once. Texture paths are resolved by the host.
    void setTexture(uint16_t width, uint16_t height, const uint8_t* rgba, size_t bytes);
    /// Optional asynchronous diagnostics. Reserve view, view+1 after update views.
    /// Calling again while pending fails. Poll with bgfx::frame's completed counter.
    void capture(uint16_t diagnosticView);
    bool pollSnapshot(uint32_t completedFrame, GpuParticleSnapshot& result);
    uint32_t capacity() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace ayt::particle
