#include <AYParticle/Particle.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ayt::particle {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(Color c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a); }
bool range(Range r) { return std::isfinite(r.min) && std::isfinite(r.max) && r.min <= r.max; }
float lerp(float a, float b, float t) { return a+(b-a)*t; }
}
bool ParticleEffect::validate(std::string* error) const {
    const auto fail = [&](const char* text) { if (error) *error = text; return false; };
    if (!capacity || capacity > kMaxCapacity || burst > kMaxCapacity) return fail("Invalid particle capacity/burst");
    if (!std::isfinite(rate) || rate < 0 || rate > 1000000 || !std::isfinite(duration) || duration <= 0) return fail("Invalid emission rate/duration");
    if (!finite(extent) || extent.x < 0 || extent.y < 0 || extent.z < 0 || !finite(velocityMin) || !finite(velocityMax) || !finite(gravity)) return fail("Invalid shape/velocity/gravity");
    if (velocityMin.x > velocityMax.x || velocityMin.y > velocityMax.y || velocityMin.z > velocityMax.z) return fail("Reversed velocity range");
    if (!range(lifetime) || lifetime.min <= 0 || !range(size) || size.min < 0 || !range(rotation) || !range(angularVelocity)) return fail("Invalid particle range");
    if (!std::isfinite(drag) || drag < 0 || !std::isfinite(endSizeScale) || endSizeScale < 0 || !finite(startColor) || !finite(endColor)) return fail("Invalid appearance/drag");
    if (static_cast<unsigned>(dimension)>1 || static_cast<unsigned>(space)>1 || static_cast<unsigned>(blend)>1 || static_cast<unsigned>(shape)>4) return fail("Invalid particle enum");
    if (dimension == Dimension::TwoD && (shape == Shape::Box || shape == Shape::Sphere)) return fail("3D shape requires 3D mode");
    const uint64_t cells = uint64_t(columns)*rows;
    if (!columns || !rows || cells > 1048576 || !frameCount || uint64_t(firstFrame)+frameCount > cells || !std::isfinite(framesPerSecond) || framesPerSecond < 0) return fail("Invalid atlas grid/range");
    if (error) error->clear();
    return true;
}
ParticleInstance::ParticleInstance(const ParticleEffect& effect) {
    std::string error;
    if (!setEffect(effect, &error)) throw std::invalid_argument(error);
}
bool ParticleInstance::setEffect(const ParticleEffect& effect, std::string* error) {
    if (!effect.validate(error)) return false;
    _effect = effect;
    _particles.clear();
    _particles.reserve(effect.capacity);
    _elapsed = _fraction = 0; _rng = effect.seed ? effect.seed : 1;
    _nextBirth = _dropped = 0; _emitting = _paused = _burstPending = false;
    return true;
}
void ParticleInstance::play() {
    _particles.clear(); _elapsed = _fraction = 0;
    _rng = _effect.seed ? _effect.seed : 1; _nextBirth = _dropped = 0;
    _emitting = _burstPending = true; _paused = false;
}
void ParticleInstance::stop(bool clear) noexcept {
    _emitting = _burstPending = false; _paused = false; _fraction = 0;
    if (clear) _particles.clear();
}
float ParticleInstance::random(float min, float max) {
    _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;
    return lerp(min,max,float(_rng >> 8)*(1.0f/16777216.0f));
}
Particle ParticleInstance::spawn(const Pose& pose) {
    Particle p;
    if (_effect.shape == Shape::Rectangle || _effect.shape == Shape::Box) {
        p.position = {random(-_effect.extent.x,_effect.extent.x),random(-_effect.extent.y,_effect.extent.y),0};
        if (_effect.shape == Shape::Box) p.position.z = random(-_effect.extent.z,_effect.extent.z);
    } else if (_effect.shape == Shape::Disk || _effect.shape == Shape::Sphere) {
        const float angle = random(0,6.283185307f);
        const float z = _effect.shape == Shape::Sphere ? random(-1,1) : 0;
        const float radius = _effect.shape == Shape::Sphere ? std::cbrt(random(0,1)) : std::sqrt(random(0,1));
        const float xy = std::sqrt(1-z*z)*radius;
        p.position = {std::cos(angle)*xy*_effect.extent.x,std::sin(angle)*xy*_effect.extent.y,z*radius*_effect.extent.z};
    }
    p.velocity = {random(_effect.velocityMin.x,_effect.velocityMax.x),random(_effect.velocityMin.y,_effect.velocityMax.y),random(_effect.velocityMin.z,_effect.velocityMax.z)};
    if (_effect.dimension == Dimension::TwoD) p.velocity.z = 0;
    if (_effect.space == Space::World) { p.position = pose.point(p.position); p.velocity = pose.vector(p.velocity); }
    p.life = random(_effect.lifetime.min,_effect.lifetime.max);
    p.initialSize = random(_effect.size.min,_effect.size.max);
    p.rotation = random(_effect.rotation.min,_effect.rotation.max);
    p.angularVelocity = random(_effect.angularVelocity.min,_effect.angularVelocity.max);
    p.birthId = ++_nextBirth;
    return p;
}
void ParticleInstance::advance(Particle& p, float dt) const {
    Vec3 gravity = _effect.gravity;
    if (_effect.dimension == Dimension::TwoD) gravity.z = 0;
    // Analytic integration avoids frame-rate-dependent drag and acceleration.
    if (_effect.drag > 0.00001f) {
        const float decay = std::exp(-_effect.drag*dt);
        const float integral = -std::expm1(-_effect.drag*dt)/_effect.drag;
        p.position = p.position + p.velocity*integral + gravity*((dt-integral)/_effect.drag);
        p.velocity = p.velocity*decay + gravity*integral;
    } else {
        p.position = p.position + p.velocity*dt + gravity*(0.5f*dt*dt);
        p.velocity = p.velocity + gravity*dt;
    }
    p.age += dt; p.rotation += p.angularVelocity*dt;
}
void ParticleInstance::update(float dt, const Pose& pose, uint32_t spawnBudget) {
    if (_paused || !std::isfinite(dt) || dt < 0 || !finite(pose.position) || !finite(pose.right) || !finite(pose.up) || !finite(pose.forward)) return;
    for (size_t i = 0; i < _particles.size();) {
        if (double(_particles[i].age)+dt >= _particles[i].life) {
            _particles[i] = _particles.back(); _particles.pop_back();
        } else { advance(_particles[i],dt); ++i; }
    }
    if (!_emitting) return;
    const double activeDt = _effect.looping ? dt : std::min<double>(dt,std::max(0.0,double(_effect.duration)-_elapsed));
    uint32_t available = std::min<uint32_t>(spawnBudget, _effect.capacity-static_cast<uint32_t>(_particles.size()));
    const auto birth = [&](float age) {
        if (!available) { ++_dropped; return; }
        Particle p = spawn(pose);
        if (age < p.life) { advance(p,age); _particles.push_back(p); --available; }
    };
    if (_burstPending) {
        _burstPending = false;
        for (uint32_t i = 0; i < _effect.burst; ++i) birth(dt);
    }
    const double total = _fraction + activeDt*_effect.rate;
    const uint64_t count = static_cast<uint64_t>(std::min(std::floor(total),double(UINT64_MAX/2)));
    // Bound work on stalls and saturated emitters; no delayed backlog.
    const uint64_t attempts = std::min<uint64_t>(count,_effect.capacity);
    for (uint64_t i = 0; i < attempts; ++i) {
        const double time = _effect.rate > 0 ? (double(count-attempts+i)+1-_fraction)/_effect.rate : 0;
        birth(static_cast<float>(std::max(0.0,double(dt)-time)));
    }
    _dropped += count-attempts;
    _fraction = total-std::floor(total); _elapsed += dt;
    if (!_effect.looping && _elapsed >= _effect.duration) _emitting = false;
}
ParticleSample ParticleInstance::sample(const Particle& p, const Pose& pose) const {
    ParticleSample s;
    const float t = std::clamp(p.age/p.life,0.0f,1.0f);
    s.position = _effect.space == Space::Local ? pose.point(p.position) : p.position;
    s.size = p.initialSize*lerp(1,_effect.endSizeScale,t); s.rotation = p.rotation;
    s.color = {lerp(_effect.startColor.r,_effect.endColor.r,t),lerp(_effect.startColor.g,_effect.endColor.g,t),lerp(_effect.startColor.b,_effect.endColor.b,t),lerp(_effect.startColor.a,_effect.endColor.a,t)};
    s.frame = _effect.firstFrame + static_cast<uint32_t>(std::fmod(std::floor(double(p.age)*_effect.framesPerSecond),double(_effect.frameCount)));
    s.birthId = p.birthId;
    return s;
}
ParticleEffect smoke() {
    ParticleEffect e; e.rate=30; e.gravity={0,0.1f,0}; e.drag=0.5f;
    e.lifetime={2,3}; e.size={0.1f,0.2f}; e.endSizeScale=3;
    e.startColor={0.6f,0.6f,0.6f,0.4f}; e.endColor={0.3f,0.3f,0.3f,0}; return e;
}
ParticleEffect sparks() {
    ParticleEffect e; e.rate=0; e.burst=60; e.looping=false; e.blend=Blend::Additive;
    e.velocityMin={-2,1,0}; e.velocityMax={2,4,0}; e.gravity={0,-6,0};
    e.lifetime={0.3f,1}; e.size={0.02f,0.04f}; e.endSizeScale=0;
    e.startColor={1,0.7f,0.1f,1}; e.endColor={1,0.1f,0,0}; return e;
}
ParticleEffect explosion() {
    auto e = sparks(); e.burst=120; e.shape=Shape::Disk; e.extent={0.1f,0.1f,0};
    e.velocityMin={-3,-3,0}; e.velocityMax={3,3,0}; e.gravity={};
    e.drag=2; e.size={0.05f,0.15f}; e.lifetime={0.3f,0.8f}; return e;
}
} // namespace ayt::particle
