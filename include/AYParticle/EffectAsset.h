#pragma once
#include <AYParticle/Particle.h>
namespace ayt::particle {
enum class EffectLayerKind : uint8_t { Particles,SpriteAnimation };
struct EffectEmitter {
    std::string name = "Emitter";
    Vec3 offset{};
    float delay = 0;
    ParticleEffect effect;
    /// Stable nonzero IDs retain random streams across rename/reordering. Zero is legacy.
    uint32_t id=0;
    EffectLayerKind kind=EffectLayerKind::Particles;
    BoolProperty enabled{BoolMode::Fixed,SampleScope::EffectPlay,true,.5f};
    NumberProperty delayValue;
    bool operator==(const EffectEmitter&) const = default;
};
/// Reusable composition of particle and sprite-animation layers. Offsets use the
/// parent's basis; delays and enable probabilities sample once per play/cycle.
/// SpriteAnimation normalizes to one stationary card; life/atlas/appearance/sway
/// remain authored. Nonzero layer IDs make random choices survive rename/reorder.
/// Group looping restarts all children every duration seconds and clears prior tails.
struct EffectAsset {
    static constexpr size_t kMaxEmitters = 64;
    static constexpr uint32_t kMaxTotalCapacity = 100000;
    std::string name = "Particle Effect";
    float duration = 3;
    bool looping = false;
    /// Composition backend preference; all children switch together.
    Backend backend = Backend::Cpu;
    uint32_t seed=1;
    std::vector<EffectEmitter> emitters;
    bool validate(std::string* error = nullptr) const;
    bool operator==(const EffectAsset&) const = default;
};
/// Canonical runtime recipe for both CPU and GPU. Sprite layers emit one stationary quad.
ParticleEffect layerEffect(const EffectEmitter& layer);
uint32_t layerSeed(const EffectAsset& asset,size_t index) noexcept;
bool layerEnabled(const EffectAsset& asset,size_t index,uint64_t cycle) noexcept;
float layerDelay(const EffectAsset& asset,size_t index,uint64_t cycle);
/// One composite runtime, independent of ECS/GPU. Children retain individual settings.
/// Global spawn allowance is shared by all children; delayed emission stops with stop().
class EffectInstance {
public:
    explicit EffectInstance(const EffectAsset& asset);
    const EffectAsset& asset() const noexcept { return _asset; }
    void play();
    void pause() noexcept { _paused = true; }
    void resume() noexcept { _paused = false; }
    void stop(bool clear = false) noexcept;
    void update(float dt, const Pose& pose = {}, uint32_t spawnBudget = EffectAsset::kMaxTotalCapacity,
                const CollisionQuery2D* collision = nullptr);
    size_t liveParticles() const noexcept;
    bool paused() const noexcept { return _paused; }
    bool finished() const noexcept;
    double time() const noexcept { return _time; }
    const std::vector<ParticleInstance>& instances() const noexcept { return _instances; }
    Pose emitterPose(size_t index, const Pose& parent = {}) const;
private:
    void resetCycle();
    EffectAsset _asset;
    std::vector<ParticleInstance> _instances;
    std::vector<bool> _started;
    std::vector<bool> _enabled;
    std::vector<float> _delays;
    uint64_t _cycle=0;
    double _time = 0, _phase = 0;
    bool _playing = false, _paused = false;
};
EffectAsset combinedExplosion();
EffectAsset fallingLeaves();
}
