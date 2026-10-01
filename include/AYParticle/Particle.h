#pragma once

#include <cstdint>
#include <array>
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
/// CPU preserves existing files. GPU/Auto request GPU with a diagnosed CPU fallback.
enum class Backend : uint8_t { Cpu, Gpu, Auto };
/// Value-only transport retained by scene components; independent of render devices.
struct PlaybackControl {
    PlaybackControl() noexcept;
    /// Distinguishes re-created scene components, including allocator address reuse.
    uint64_t identity=0;
    uint64_t revision=0, restart=0, clear=0;
    bool playing=false, paused=false;
    float stepSeconds=0;
    void play() noexcept { restart=++revision; playing=true; paused=false; stepSeconds=0; }
    void pause() noexcept { ++revision; paused=true; }
    void resume() noexcept { ++revision; paused=false; }
    void stop(bool discard) noexcept { ++revision; playing=false; paused=false; stepSeconds=0; if(discard) clear=revision; }
};

/// Piecewise-linear keys at normalized particle age, with fixed endpoints 0 and 1.
/// Empty keys preserve the legacy appearance. Nonempty tracks contain 2..32 keys.
struct CurveKey {
    float time = 0, value = 1;
    bool operator==(const CurveKey&) const = default;
};
struct GradientKey {
    float time = 0;
    Color color{};
    bool operator==(const GradientKey&) const = default;
};
inline constexpr size_t kMaxLifetimeKeys = 32;
/// Samples validated keys; clamps time to [0,1]. Empty tracks return fallback.
float sampleCurve(std::span<const CurveKey> keys, float time, float fallback);
Color sampleGradient(std::span<const GradientKey> keys, float time, Color fallback);

/// Random choices are frozen for a play, a composite cycle, or a particle birth.
/// EffectCycle means the enclosing composition cycle, not a new choice every frame.
enum class SampleScope : uint8_t { EffectPlay, EffectCycle, ParticleBirth };
enum class NumberMode : uint8_t { Legacy, Constant, Uniform, Weighted, Curve, BetweenCurves };
struct WeightedNumber {
    float value=0, weight=1;
    bool operator==(const WeightedNumber&) const = default;
};
/// Typed scalar source. Legacy preserves the original field/range/track.
/// Curve time is normalized particle age for lifetime tracks; birth-only values
/// sample at the normalized emitter phase at birth. BetweenCurves retains one
/// random blend throughout a life. Rate/burst disallow curves and birth scope.
/// Numeric sources apply to the explicit FloatAttribute list, not arbitrary fields.
struct NumberProperty {
    NumberMode mode=NumberMode::Legacy;
    SampleScope scope=SampleScope::ParticleBirth;
    float value=0, min=0, max=0;
    std::vector<WeightedNumber> choices;
    std::vector<CurveKey> curve, upperCurve;
    bool validate(float minimum,float maximum,bool curves=true,std::string* error=nullptr) const;
    float sample(float time,float random,float legacy) const;
    /// Uniform whole-number choices include both ends; other modes round down.
    float sampleWhole(float time,float random,float legacy) const;
    bool operator==(const NumberProperty&) const = default;
};
enum class BoolMode : uint8_t { Fixed, Probability };
struct BoolProperty {
    BoolMode mode=BoolMode::Fixed;
    SampleScope scope=SampleScope::ParticleBirth;
    bool value=false;
    float probability=.5f;
    bool validate(std::string* error=nullptr) const;
    bool sample(float random) const noexcept { return mode==BoolMode::Fixed?value:random<probability; }
    bool operator==(const BoolProperty&) const = default;
};
inline constexpr size_t kMaxPropertyChoices=8;
/// Hash-based independent streams: changing one property never shifts other choices.
float propertyRandom(uint32_t seed,uint32_t property,uint64_t cycle,uint64_t birth,SampleScope scope) noexcept;
enum class FloatAttribute : uint8_t {
    Lifetime,Size,Rotation,AngularVelocity,AnimationFps,AnimationOffset,
    SizeMultiplier,Opacity,SwayAmplitude,SwayFrequency,SwayPhase,EmissionRate,BurstCount,Count
};
inline constexpr size_t kFloatAttributeCount=static_cast<size_t>(FloatAttribute::Count);
const char* attributeName(FloatAttribute attribute) noexcept;
enum class AnimationPlayback : uint8_t { Loop,Once,PingPong };
enum class ParticleFacing : uint8_t { Billboard,Plane };

/// Immutable authoring values shared by 2D and 3D CPU instances. No GPU state.
struct ParticleEffect {
    static constexpr uint32_t kMaxCapacity = 65536;
    uint32_t capacity = 1024, burst = 0, seed = 1;
    float rate = 20, duration = 1;
    /// Repeat emission cycles of duration seconds, including a burst at each start.
    bool looping = true;
    Dimension dimension = Dimension::TwoD;
    Shape shape = Shape::Point;
    Space space = Space::World;
    Blend blend = Blend::Alpha;
    Backend backend = Backend::Cpu;
    Vec3 extent{1,1,1};
    Vec3 velocityMin{-0.2f,0.5f,0}, velocityMax{0.2f,1,0};
    Vec3 gravity{0,-0.5f,0};
    float drag = 0;
    Range lifetime{1,2}, size{0.05f,0.15f}, rotation{0,0}, angularVelocity{0,0};
    float endSizeScale = 1;
    Color startColor{}, endColor{1,1,1,0};
    /// Size multiplier replaces the 1..endSizeScale ramp; values are nonnegative.
    std::vector<CurveKey> sizeCurve;
    /// Absolute alpha in [0,1], overriding gradient/legacy alpha when present.
    std::vector<CurveKey> opacityCurve;
    /// RGBA gradient replaces startColor/endColor. RGB may be HDR; alpha is [0,1].
    std::vector<GradientKey> colorGradient;
    std::string texturePath;
    uint32_t columns = 1, rows = 1, firstFrame = 0, frameCount = 1;
    float framesPerSecond = 0;
    /// Optional typed sources; emission rate/burst sample once per play/cycle.
    std::array<NumberProperty,kFloatAttributeCount> values;
    NumberProperty& value(FloatAttribute key) { return values.at(static_cast<size_t>(key)); }
    const NumberProperty& value(FloatAttribute key) const { return values.at(static_cast<size_t>(key)); }
    BoolProperty flipX{},flipY{},reverseAnimation{};
    AnimationPlayback animationPlayback=AnimationPlayback::Loop;
    /// Map age 0..1 onto the complete frame range instead of using FPS.
    bool animationOverLifetime=false;
    /// Plane is real CPU 3D orientation; billboard keeps camera-facing behavior.
    ParticleFacing facing=ParticleFacing::Billboard;
    Range tiltX{0,0},tiltY{0,0},tumbleX{0,0},tumbleY{0,0};
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
    float animationFps=0,animationOffset=0,sizeRandom=0,opacityRandom=0;
    float swayAmplitude=0,swayFrequency=0,swayPhase=0;
    bool flipX=false,flipY=false,reverseAnimation=false;
    float tiltX=0,tiltY=0,tumbleX=0,tumbleY=0;
    Vec3 birthRight{1,0,0},birthUp{0,1,0},birthForward{0,0,1};
};
struct ParticleSample {
    Vec3 position{};
    Color color{};
    float size = 1, rotation = 0;
    uint32_t frame = 0;
    uint64_t birthId = 0;
    bool flipX=false,flipY=false,oriented=false;
    Vec3 right{1,0,0},up{0,1,0};
};

/// Own one instance per emitter, not per particle. Dense storage; swap removal.
/// Calls are single-threaded. Visual simulation uses presentation dt, not gameplay physics.
/// A burst fires on play; looping repeats it every effect.duration seconds.
/// sample() applies validated normalized lifetime tracks equally for 2D and 3D;
/// empty tracks preserve endpoint appearance, opacity overrides gradient alpha.
class ParticleInstance {
public:
    explicit ParticleInstance(const ParticleEffect& effect = {});
    bool setEffect(const ParticleEffect& effect, std::string* error = nullptr);
    const ParticleEffect& effect() const noexcept { return _effect; }
    /// Restart clears particles, elapsed time and RNG. resume() preserves all three.
    void play();
    /// Composite hosts supply a stable layer seed and a parent-cycle index.
    void play(uint32_t instanceSeed,uint64_t effectCycle);
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
    Particle spawn(const Pose& pose,double birthTime);
    void advance(Particle& p, float dt) const;
    ParticleEffect _effect;
    std::vector<Particle> _particles;
    double _elapsed = 0, _fraction = 0;
    uint32_t _rng = 1;
    uint64_t _nextBirth = 0, _dropped = 0;
    uint32_t _instanceSeed=1,_resolvedBurst=0;
    uint64_t _effectCycle=0;
    double _totalTime=0;
    float _resolvedRate=0;
    bool _emitting = false, _paused = false, _burstPending = false;
};

ParticleEffect smoke();
ParticleEffect sparks();
ParticleEffect explosion();
/// Sprite flipbook leaves, independent animation phases/mirroring and smooth sway.
ParticleEffect leaves();
} // namespace ayt::particle
