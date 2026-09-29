#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ayt::particle {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
    Vec3 operator+(Vec3 b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
};
struct Color { float r = 1, g = 1, b = 1, a = 1; bool operator==(const Color&) const = default; };
struct Range { float min = 1, max = 1; bool operator==(const Range&) const = default; };
enum class Shape : uint8_t { Point, Rectangle, Disk, Box, Sphere };
enum class Space : uint8_t { Local, World };
enum class Dimension : uint8_t { TwoD, ThreeD };
enum class Blend : uint8_t { Alpha, Additive };

/// Immutable authoring values shared by 2D and 3D CPU instances. No GPU state.
struct ParticleEffect {
    static constexpr uint32_t kMaxCapacity = 65536;
    uint32_t capacity = 1024, burst = 0, seed = 1;
    float rate = 20, duration = 1;
    bool looping = true;
    Dimension dimension = Dimension::TwoD;
    Shape shape = Shape::Point;
    Space space = Space::World;
    Blend blend = Blend::Alpha;
    Vec3 extent{1,1,1};
    Vec3 velocityMin{-0.2f,0.5f,0}, velocityMax{0.2f,1,0};
    Vec3 gravity{0,-0.5f,0};
    float drag = 0;
    Range lifetime{1,2}, size{0.05f,0.15f}, rotation{0,0}, angularVelocity{0,0};
    float endSizeScale = 1;
    Color startColor{}, endColor{1,1,1,0};
    std::string texturePath;
    uint32_t columns = 1, rows = 1, firstFrame = 0, frameCount = 1;
    float framesPerSecond = 0;
    /// Returns a diagnostic for invalid input. Invalid effects never replace a running instance.
    bool validate(std::string* error = nullptr) const;
    bool operator==(const ParticleEffect&) const = default;
};

/// Emitter transform basis. World births capture this pose; local particles follow it.
struct Pose {
    Vec3 position{}, right{1,0,0}, up{0,1,0}, forward{0,0,1};
    Vec3 vector(Vec3 v) const { return right*v.x + up*v.y + forward*v.z; }
    Vec3 point(Vec3 v) const { return position + vector(v); }
};
struct Particle {
    Vec3 position{}, velocity{};
    float age = 0, life = 1, initialSize = 1, rotation = 0, angularVelocity = 0;
    uint64_t birthId = 0;
};
struct ParticleSample {
    Vec3 position{};
    Color color{};
    float size = 1, rotation = 0;
    uint32_t frame = 0;
    uint64_t birthId = 0;
};

/// Own one instance per emitter, not per particle. Dense storage; swap removal.
/// Calls are single-threaded. Visual simulation uses presentation dt, not gameplay physics.
class ParticleInstance {
public:
    explicit ParticleInstance(const ParticleEffect& effect = {});
    bool setEffect(const ParticleEffect& effect, std::string* error = nullptr);
    const ParticleEffect& effect() const noexcept { return _effect; }
    /// Restart clears particles, elapsed time and RNG. resume() preserves all three.
    void play();
    void pause() noexcept { _paused = true; }
    void resume() noexcept { _paused = false; }
    /// Stop emission; optionally discard living particles immediately.
    void stop(bool clear = false) noexcept;
    /// Global owner supplies its remaining spawn budget. Dropped births never accumulate.
    void update(float dt, const Pose& pose = {}, uint32_t spawnBudget = ParticleEffect::kMaxCapacity);
    std::span<const Particle> particles() const noexcept { return _particles; }
    ParticleSample sample(const Particle& particle, const Pose& pose = {}) const;
    bool emitting() const noexcept { return _emitting; }
    bool paused() const noexcept { return _paused; }
    bool finished() const noexcept { return !_emitting && _particles.empty(); }
    uint64_t droppedBirths() const noexcept { return _dropped; }
private:
    float random(float min, float max);
    Particle spawn(const Pose& pose);
    void advance(Particle& p, float dt) const;
    ParticleEffect _effect;
    std::vector<Particle> _particles;
    double _elapsed = 0, _fraction = 0;
    uint32_t _rng = 1;
    uint64_t _nextBirth = 0, _dropped = 0;
    bool _emitting = false, _paused = false, _burstPending = false;
};

ParticleEffect smoke();
ParticleEffect sparks();
ParticleEffect explosion();
} // namespace ayt::particle
