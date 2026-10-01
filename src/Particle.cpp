#include <AYParticle/Particle.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ayt::particle {
PlaybackControl::PlaybackControl() noexcept {
    static std::atomic<uint64_t> next{1};
    identity=next.fetch_add(1,std::memory_order_relaxed);
}
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(Color c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a); }
bool range(Range r) { return std::isfinite(r.min) && std::isfinite(r.max) && r.min <= r.max; }
float lerp(float a, float b, float t) { return a+(b-a)*t; }
template<class Key> bool keyTimes(std::span<const Key> keys) {
    if(keys.empty()) return true;
    if(keys.size()<2 || keys.size()>kMaxLifetimeKeys || keys.front().time!=0 || keys.back().time!=1) return false;
    float previous=-1;
    for(const auto& key:keys) {
        if(!std::isfinite(key.time) || key.time<0 || key.time>1 || key.time<=previous) return false;
        previous=key.time;
    }
    return true;
}
template<class Key> size_t segment(std::span<const Key> keys,float t) {
    const auto it=std::upper_bound(keys.begin(),keys.end(),t,[](float v,const Key& k) { return v<k.time; });
    return std::clamp<size_t>(static_cast<size_t>(it-keys.begin()),1,keys.size()-1);
}
}
float sampleCurve(std::span<const CurveKey> keys,float time,float fallback) {
    if(keys.empty()) return fallback;
    const float t=std::isfinite(time)?std::clamp(time,0.0f,1.0f):0;
    const auto i=segment(keys,t); const auto& a=keys[i-1]; const auto& b=keys[i];
    return std::lerp(a.value,b.value,(t-a.time)/(b.time-a.time));
}
Color sampleGradient(std::span<const GradientKey> keys,float time,Color fallback) {
    if(keys.empty()) return fallback;
    const float t=std::isfinite(time)?std::clamp(time,0.0f,1.0f):0;
    const auto i=segment(keys,t); const auto& a=keys[i-1]; const auto& b=keys[i];
    const float u=(t-a.time)/(b.time-a.time);
    return {std::lerp(a.color.r,b.color.r,u),std::lerp(a.color.g,b.color.g,u),std::lerp(a.color.b,b.color.b,u),std::lerp(a.color.a,b.color.a,u)};
}
bool ParticleEffect::validate(std::string* error) const {
    const auto fail = [&](const char* text) { if (error) *error = text; return false; };
    if(static_cast<unsigned>(backend)>2) return fail("Invalid particle backend");
    if (!capacity || capacity > kMaxCapacity || burst > kMaxCapacity) return fail("Invalid particle capacity/burst");
    if (!std::isfinite(rate) || rate < 0 || rate > 1000000 || !std::isfinite(duration) || duration <= 0) return fail("Invalid emission rate/duration");
    if (!finite(extent) || extent.x < 0 || extent.y < 0 || extent.z < 0 || !finite(velocityMin) || !finite(velocityMax) || !finite(gravity)) return fail("Invalid shape/velocity/gravity");
    if (velocityMin.x > velocityMax.x || velocityMin.y > velocityMax.y || velocityMin.z > velocityMax.z) return fail("Reversed velocity range");
    if (!range(lifetime) || lifetime.min <= 0 || !range(size) || size.min < 0 || !range(rotation) || !range(angularVelocity)) return fail("Invalid particle range");
    if (!std::isfinite(drag) || drag < 0 || !std::isfinite(endSizeScale) || endSizeScale < 0 || !finite(startColor) || !finite(endColor)) return fail("Invalid appearance/drag");
    if(!keyTimes<CurveKey>(sizeCurve) || !keyTimes<CurveKey>(opacityCurve) || !keyTimes<GradientKey>(colorGradient))
        return fail("Lifetime tracks need 2..32 strictly ordered keys with endpoints 0 and 1");
    for(const auto& k:sizeCurve) if(!std::isfinite(k.value) || k.value<0) return fail("Size curve values must be finite and nonnegative");
    for(const auto& k:opacityCurve) if(!std::isfinite(k.value) || k.value<0 || k.value>1) return fail("Opacity curve values must be in [0,1]");
    for(const auto& k:colorGradient) if(!finite(k.color) || k.color.r<0 || k.color.g<0 || k.color.b<0 || k.color.a<0 || k.color.a>1)
        return fail("Gradient requires finite nonnegative RGB and alpha in [0,1]");
    if (static_cast<unsigned>(dimension)>1 || static_cast<unsigned>(space)>1 || static_cast<unsigned>(blend)>1 || static_cast<unsigned>(shape)>4) return fail("Invalid particle enum");
    if (dimension == Dimension::TwoD && (shape == Shape::Box || shape == Shape::Sphere)) return fail("3D shape requires 3D mode");
    const uint64_t cells = uint64_t(columns)*rows;
    if (!columns || !rows || cells > 1048576 || !frameCount || uint64_t(firstFrame)+frameCount > cells || !std::isfinite(framesPerSecond) || framesPerSecond < 0) return fail("Invalid atlas grid/range");
    if(unsigned(animationPlayback)>2||unsigned(facing)>1) return fail("Invalid animation/facing mode");
    if(facing==ParticleFacing::Plane&&dimension!=Dimension::ThreeD) return fail("Oriented planes require 3D simulation");
    if(!range(tiltX)||!range(tiltY)||!range(tumbleX)||!range(tumbleY)) return fail("Invalid 3D orientation range");
    for(size_t index=0;index<values.size();++index) {
        const auto key=static_cast<FloatAttribute>(index);
        float lo=-1000000,hi=1000000;
        if(key==FloatAttribute::Lifetime)lo=.000001f;
        if(key==FloatAttribute::Size||key==FloatAttribute::SizeMultiplier||key==FloatAttribute::AnimationFps
            ||key==FloatAttribute::SwayAmplitude||key==FloatAttribute::SwayFrequency||key==FloatAttribute::EmissionRate||key==FloatAttribute::BurstCount)lo=0;
        if(key==FloatAttribute::Opacity){lo=0;hi=1;}
        if(key==FloatAttribute::AnimationOffset){lo=0;hi=float(frameCount-1);}
        if(key==FloatAttribute::BurstCount)hi=float(kMaxCapacity);
        const bool emission=key==FloatAttribute::EmissionRate||key==FloatAttribute::BurstCount;
        std::string detail;
        if(!values[index].validate(lo,hi,!emission,&detail)) {if(error)*error=std::string(attributeName(key))+": "+detail;return false;}
        if(emission&&values[index].mode!=NumberMode::Legacy&&values[index].scope==SampleScope::ParticleBirth)
            return fail("Emission values sample per effect play or composite cycle");
    }
    if(!flipX.validate(error)||!flipY.validate(error)||!reverseAnimation.validate(error))return false;
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
void ParticleInstance::play() { play(_effect.seed,0); }
void ParticleInstance::play(uint32_t instanceSeed,uint64_t effectCycle) {
    _particles.clear(); _elapsed = _fraction = 0;
    _rng = instanceSeed ? instanceSeed : 1; _nextBirth = _dropped = 0;
    _instanceSeed=instanceSeed; _effectCycle=effectCycle; _totalTime=0;
    const auto resolve=[&](FloatAttribute key,float legacy){const auto& source=_effect.value(key);
        const auto r=propertyRandom(_instanceSeed,uint32_t(key),_effectCycle,0,source.scope);
        return key==FloatAttribute::BurstCount?source.sampleWhole(0,r,legacy):source.sample(0,r,legacy);};
    _resolvedRate=resolve(FloatAttribute::EmissionRate,_effect.rate);
    _resolvedBurst=static_cast<uint32_t>(std::floor(resolve(FloatAttribute::BurstCount,float(_effect.burst))));
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
Particle ParticleInstance::spawn(const Pose& pose,double birthTime) {
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
    const float phase=static_cast<float>(std::clamp(std::fmod(std::max(0.,birthTime),double(_effect.duration))/_effect.duration,0.,1.));
    const auto unit=[&](FloatAttribute key){return propertyRandom(_instanceSeed,uint32_t(key),_effectCycle,p.birthId,_effect.value(key).scope);};
    const auto sample=[&](FloatAttribute key,float legacy){return _effect.value(key).sample(phase,unit(key),legacy);};
    p.life=sample(FloatAttribute::Lifetime,p.life); p.initialSize=sample(FloatAttribute::Size,p.initialSize);
    p.rotation=sample(FloatAttribute::Rotation,p.rotation);p.angularVelocity=sample(FloatAttribute::AngularVelocity,p.angularVelocity);
    p.animationFps=sample(FloatAttribute::AnimationFps,_effect.framesPerSecond);
    p.animationOffset=_effect.value(FloatAttribute::AnimationOffset).sampleWhole(phase,unit(FloatAttribute::AnimationOffset),0);
    p.sizeRandom=unit(FloatAttribute::SizeMultiplier);p.opacityRandom=unit(FloatAttribute::Opacity);
    p.swayAmplitude=sample(FloatAttribute::SwayAmplitude,0);p.swayFrequency=sample(FloatAttribute::SwayFrequency,0);p.swayPhase=sample(FloatAttribute::SwayPhase,0);
    const auto boolean=[&](const BoolProperty& source,uint32_t key){return source.sample(propertyRandom(_instanceSeed,key,_effectCycle,p.birthId,source.scope));};
    p.flipX=boolean(_effect.flipX,100);p.flipY=boolean(_effect.flipY,101);p.reverseAnimation=boolean(_effect.reverseAnimation,102);
    if(_effect.facing==ParticleFacing::Plane) {
        p.tiltX=std::lerp(_effect.tiltX.min,_effect.tiltX.max,propertyRandom(_instanceSeed,110,_effectCycle,p.birthId,SampleScope::ParticleBirth));
        p.tiltY=std::lerp(_effect.tiltY.min,_effect.tiltY.max,propertyRandom(_instanceSeed,111,_effectCycle,p.birthId,SampleScope::ParticleBirth));
        p.tumbleX=std::lerp(_effect.tumbleX.min,_effect.tumbleX.max,propertyRandom(_instanceSeed,112,_effectCycle,p.birthId,SampleScope::ParticleBirth));
        p.tumbleY=std::lerp(_effect.tumbleY.min,_effect.tumbleY.max,propertyRandom(_instanceSeed,113,_effectCycle,p.birthId,SampleScope::ParticleBirth));
        p.birthRight=pose.right;p.birthUp=pose.up;p.birthForward=pose.forward;
    }
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
    p.tiltX+=p.tumbleX*dt;p.tiltY+=p.tumbleY*dt;
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
        Particle p = spawn(pose,std::max(0.,_totalTime+dt-age));
        if (age < p.life) { advance(p,age); _particles.push_back(p); --available; }
    };
    const double endTime = _elapsed + dt;
    const double cycles = _effect.looping ? std::floor(endTime/_effect.duration) : 0;
    const double phase = _effect.looping ? std::fmod(endTime,double(_effect.duration)) : endTime;
    if (_resolvedBurst) {
        const double events = cycles + (_burstPending ? 1 : 0);
        const uint64_t count = static_cast<uint64_t>(std::min(events*_resolvedBurst,double(UINT64_MAX/2)));
        const uint64_t attempts = std::min<uint64_t>(count,_effect.capacity);
        // Keep the latest bursts on stalls, without iterating every elapsed cycle.
        // Births at a cycle boundary have age zero, and update(0) never repeats them.
        for (uint64_t i = 0; i < attempts; ++i) {
            const double cyclesBack = double((attempts-1-i)/_resolvedBurst);
            const double age = cyclesBack < cycles ? phase + cyclesBack*_effect.duration : dt;
            birth(static_cast<float>(age));
        }
        _dropped += count-attempts;
    }
    _burstPending = false;
    const double total = _fraction + activeDt*_resolvedRate;
    const uint64_t count = static_cast<uint64_t>(std::min(std::floor(total),double(UINT64_MAX/2)));
    // Bound work on stalls and saturated emitters; no delayed backlog.
    const uint64_t attempts = std::min<uint64_t>(count,_effect.capacity);
    for (uint64_t i = 0; i < attempts; ++i) {
        const double time = _resolvedRate > 0 ? (double(count-attempts+i)+1-_fraction)/_resolvedRate : 0;
        birth(static_cast<float>(std::max(0.0,double(dt)-time)));
    }
    _dropped += count-attempts;
    _fraction = total-std::floor(total); _elapsed = phase;
    _totalTime+=dt;
    if (!_effect.looping && _elapsed >= _effect.duration) _emitting = false;
}
ParticleSample ParticleInstance::sample(const Particle& p, const Pose& pose) const {
    ParticleSample s;
    const float t = std::clamp(p.age/p.life,0.0f,1.0f);
    auto position=p.position;position.x+=p.swayAmplitude*(std::sin(6.283185307f*p.swayFrequency*p.age+p.swayPhase)-std::sin(p.swayPhase));
    s.position = _effect.space == Space::Local ? pose.point(position) : position;
    const float scale=_effect.value(FloatAttribute::SizeMultiplier).sample(t,p.sizeRandom,sampleCurve(_effect.sizeCurve,t,lerp(1,_effect.endSizeScale,t)));
    s.size = p.initialSize*scale; s.rotation = p.rotation;
    s.color = {lerp(_effect.startColor.r,_effect.endColor.r,t),lerp(_effect.startColor.g,_effect.endColor.g,t),lerp(_effect.startColor.b,_effect.endColor.b,t),lerp(_effect.startColor.a,_effect.endColor.a,t)};
    s.color=sampleGradient(_effect.colorGradient,t,s.color);
    s.color.a=sampleCurve(_effect.opacityCurve,t,s.color.a);
    s.color.a=_effect.value(FloatAttribute::Opacity).sample(t,p.opacityRandom,s.color.a);
    const double last=double(_effect.frameCount-1);
    const double steps=std::floor(_effect.animationOverLifetime?double(t)*last:double(p.age)*p.animationFps);
    double frame=(p.reverseAnimation?last-std::floor(p.animationOffset):std::floor(p.animationOffset))+(p.reverseAnimation?-steps:steps);
    if(_effect.animationPlayback==AnimationPlayback::Once)frame=std::clamp(frame,0.,last);
    else if(_effect.animationPlayback==AnimationPlayback::PingPong&&last>0){frame=std::fmod(frame,last*2);if(frame<0)frame+=last*2;frame=frame<=last?frame:last*2-frame;}
    else {frame=std::fmod(frame,double(_effect.frameCount));if(frame<0)frame+=_effect.frameCount;}
    s.frame=_effect.firstFrame+static_cast<uint32_t>(std::floor(frame));s.flipX=p.flipX;s.flipY=p.flipY;
    if(_effect.facing==ParticleFacing::Plane) {
        const auto rotate=[&](Vec3 v){const float cx=std::cos(p.tiltX),sx=std::sin(p.tiltX),cy=std::cos(p.tiltY),sy=std::sin(p.tiltY),cz=std::cos(p.rotation),sz=std::sin(p.rotation);
            const float y=v.y*cx-v.z*sx,z=v.y*sx+v.z*cx,x=v.x*cy+z*sy;
            return Vec3{x*cz-y*sz,x*sz+y*cz,-v.x*sy+z*cy};};
        const auto basis=[&](Vec3 v){return _effect.space==Space::Local?pose.vector(v):p.birthRight*v.x+p.birthUp*v.y+p.birthForward*v.z;};
        s.oriented=true;s.right=basis(rotate({1,0,0}));s.up=basis(rotate({0,1,0}));
    }
    s.birthId = p.birthId;
    return s;
}
ParticleEffect smoke() {
    ParticleEffect e; e.rate=30; e.gravity={0,0.1f,0}; e.drag=0.5f;
    e.lifetime={2,3}; e.size={0.1f,0.2f}; e.endSizeScale=3;
    e.startColor={0.6f,0.6f,0.6f,0.4f}; e.endColor={0.3f,0.3f,0.3f,0};
    e.sizeCurve={{0,1},{0.2f,2},{1,3}};
    e.opacityCurve={{0,0},{0.15f,0.4f},{0.55f,0.3f},{1,0}};
    e.colorGradient={{0,e.startColor},{0.35f,{0.5f,0.5f,0.5f,0.4f}},{1,e.endColor}}; return e;
}
ParticleEffect sparks() {
    ParticleEffect e; e.rate=0; e.burst=60; e.looping=false; e.blend=Blend::Additive;
    e.velocityMin={-2,1,0}; e.velocityMax={2,4,0}; e.gravity={0,-6,0};
    e.lifetime={0.3f,1}; e.size={0.02f,0.04f}; e.endSizeScale=0;
    e.startColor={1,0.7f,0.1f,1}; e.endColor={1,0.1f,0,0};
    e.sizeCurve={{0,1},{0.25f,0.8f},{1,0}};
    e.opacityCurve={{0,1},{0.2f,1},{1,0}};
    e.colorGradient={{0,{1,1,0.7f,1}},{0.2f,e.startColor},{1,e.endColor}}; return e;
}
ParticleEffect explosion() {
    auto e = sparks(); e.burst=120; e.shape=Shape::Disk; e.extent={0.1f,0.1f,0};
    e.velocityMin={-3,-3,0}; e.velocityMax={3,3,0}; e.gravity={};
    e.drag=2; e.size={0.05f,0.15f}; e.lifetime={0.3f,0.8f}; return e;
}
ParticleEffect leaves() {
    ParticleEffect e;e.capacity=512;e.rate=24;e.duration=6;e.looping=true;e.shape=Shape::Rectangle;e.extent={2,.1f,0};
    e.velocityMin={-.12f,-.5f,0};e.velocityMax={.12f,-.25f,0};e.gravity={0,-.08f,0};e.drag=.12f;
    e.lifetime={3,5};e.size={.18f,.35f};e.rotation={-3.14f,3.14f};e.angularVelocity={-.7f,.7f};
    e.columns=4;e.rows=2;e.frameCount=8;e.framesPerSecond=8;
    e.startColor={1,.65f,.25f,1};e.endColor={.8f,.3f,.08f,0};e.opacityCurve={{0,0},{.1f,1},{.8f,1},{1,0}};
    const auto uniform=[&](FloatAttribute key,float lo,float hi){auto& v=e.value(key);v.mode=NumberMode::Uniform;v.min=lo;v.max=hi;};
    uniform(FloatAttribute::AnimationFps,5,11);uniform(FloatAttribute::AnimationOffset,0,7);
    uniform(FloatAttribute::SwayAmplitude,.12f,.45f);uniform(FloatAttribute::SwayFrequency,.4f,.9f);uniform(FloatAttribute::SwayPhase,0,6.283185307f);
    e.flipX.mode=BoolMode::Probability;e.reverseAnimation.mode=BoolMode::Probability;
    return e;
}
} // namespace ayt::particle
