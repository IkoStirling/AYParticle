#include <AYParticle/EffectAsset.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace ayt::particle {
ParticleEffect layerEffect(const EffectEmitter& layer) {
    auto effect=layer.effect;
    if(layer.kind==EffectLayerKind::SpriteAnimation) {
        effect.capacity=1;effect.burst=1;effect.rate=0;effect.looping=false;effect.shape=Shape::Point;
        effect.extent={};effect.velocityMin=effect.velocityMax=effect.gravity={};effect.drag=0;
        effect.value(FloatAttribute::EmissionRate)={};effect.value(FloatAttribute::BurstCount)={};
    }
    return effect;
}
uint32_t layerSeed(const EffectAsset& asset,size_t index) noexcept {
    const auto& layer=asset.emitters[index];uint32_t id=layer.id;
    if(!id){id=2166136261u;for(unsigned char c:layer.name){id^=c;id*=16777619u;}}
    return uint32_t(propertyRandom(asset.seed^layer.effect.seed,id,0,0,SampleScope::EffectPlay)*16777216.f);
}
bool layerEnabled(const EffectAsset& asset,size_t index,uint64_t cycle) noexcept {
    const auto& source=asset.emitters[index].enabled;
    return source.sample(propertyRandom(layerSeed(asset,index),3000,cycle,0,source.scope));
}
float layerDelay(const EffectAsset& asset,size_t index,uint64_t cycle) {
    const auto& layer=asset.emitters[index];const auto& source=layer.delayValue;
    return source.sample(0,propertyRandom(layerSeed(asset,index),3001,cycle,0,source.scope),layer.delay);
}
bool EffectAsset::validate(std::string* error) const {
    if(static_cast<unsigned>(backend)>2) { if(error) *error="Invalid composition backend"; return false; }
    const auto fail=[&](const std::string& message) { if(error) *error=message; return false; };
    if(name.empty() || name.size()>128 || !std::isfinite(duration) || duration<=0)
        return fail("Invalid effect name/duration");
    if(emitters.empty() || emitters.size()>kMaxEmitters) return fail("Effect needs 1..64 emitters");
    uint64_t capacity=0;
    std::unordered_set<std::string> names;
    std::unordered_set<uint32_t> ids;
    for(const auto& e:emitters) {
        if(e.name.empty() || e.name.size()>128 || !names.insert(e.name).second)
            return fail("Emitter names must be unique and nonempty");
        if(!std::isfinite(e.delay) || e.delay<0 || e.delay>=duration
            || !std::isfinite(e.offset.x) || !std::isfinite(e.offset.y) || !std::isfinite(e.offset.z))
            return fail(e.name+": invalid offset/delay");
        std::string detail;
        if(unsigned(e.kind)>1||(e.id&&!ids.insert(e.id).second))return fail("Invalid layer kind or duplicate stable ID");
        if(!e.enabled.validate(&detail)||e.enabled.scope==SampleScope::ParticleBirth)return fail(e.name+": layer enable samples per play/cycle");
        if(!e.delayValue.validate(0,std::nextafter(duration,0.f),false,&detail)
            ||(e.delayValue.mode!=NumberMode::Legacy&&e.delayValue.scope==SampleScope::ParticleBirth))return fail(e.name+": invalid layer delay source");
        if(!e.effect.validate(&detail)) return fail(e.name+": "+detail);
        capacity+=layerEffect(e).capacity;
    }
    if(capacity>kMaxTotalCapacity) return fail("Combined capacity exceeds 100000");
    if(error) error->clear();
    return true;
}
EffectInstance::EffectInstance(const EffectAsset& asset):_asset(asset) {
    std::string error;
    if(!asset.validate(&error)) throw std::invalid_argument(error);
    for(const auto& e:asset.emitters) _instances.emplace_back(layerEffect(e));
    _started.resize(_instances.size(),false);
    _enabled.resize(_instances.size(),true);_delays.resize(_instances.size());
}
void EffectInstance::resetCycle() {
    for(auto& instance:_instances) instance.stop(true);
    std::fill(_started.begin(),_started.end(),false);
    for(size_t i=0;i<_instances.size();++i){_enabled[i]=layerEnabled(_asset,i,_cycle);_delays[i]=layerDelay(_asset,i,_cycle);}
}
void EffectInstance::play() { _cycle=0;resetCycle(); _time=_phase=0; _playing=true; _paused=false; }
void EffectInstance::stop(bool clear) noexcept {
    _playing=false; _paused=false;
    for(auto& instance:_instances) instance.stop(clear);
}
Pose EffectInstance::emitterPose(size_t index,const Pose& parent) const {
    Pose result=parent; result.position=parent.point(_asset.emitters.at(index).offset); return result;
}
void EffectInstance::update(float dt,const Pose& pose,uint32_t spawnBudget) {
    const auto finite=[](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if(_paused || !std::isfinite(dt) || dt<0 || !finite(pose.position) || !finite(pose.right) || !finite(pose.up) || !finite(pose.forward)) return;
    double step=dt;
    if(_playing) {
        _time+=dt;
        const double end=_phase+dt;
        if(_asset.looping && end>=_asset.duration) {
            // Only simulate the current cycle on stalls; replay never creates a backlog.
            _cycle+=static_cast<uint64_t>(std::min(std::floor(end/_asset.duration),double(UINT32_MAX)));
            resetCycle(); _phase=std::fmod(end,double(_asset.duration)); step=_phase;
        } else _phase=end;
    }
    uint32_t remaining=spawnBudget;
    for(size_t i=0;i<_instances.size();++i) {
        if(!_enabled[i])continue;
        auto& instance=_instances[i];
        float active=static_cast<float>(step);
        if(!_started[i]) {
            if(!_playing || _phase<_delays[i]) continue;
            _started[i]=true; instance.play(layerSeed(_asset,i),_cycle);
            active=static_cast<float>(std::min(step,_phase-_delays[i]));
        }
        uint64_t lastBirth=0; for(const auto& p:instance.particles()) lastBirth=std::max(lastBirth,p.birthId);
        instance.update(active,emitterPose(i,pose),remaining);
        uint32_t spawned=0; for(const auto& p:instance.particles()) if(p.birthId>lastBirth) ++spawned;
        remaining-=std::min(remaining,spawned);
    }
}
size_t EffectInstance::liveParticles() const noexcept {
    size_t count=0; for(const auto& p:_instances) count+=p.particles().size(); return count;
}
bool EffectInstance::finished() const noexcept {
    if(_playing && _asset.looping) return false;
    for(size_t i=0;i<_instances.size();++i) {
        if(!_enabled[i])continue;
        if(_playing && !_started[i]) return false;
        if(!_instances[i].finished()) return false;
    }
    return true;
}
EffectAsset combinedExplosion() {
    EffectAsset asset; asset.name="Combined Explosion";
    auto flash=explosion(); flash.burst=1; flash.shape=Shape::Point;
    flash.velocityMin=flash.velocityMax={}; flash.lifetime={0.15f,0.15f};
    flash.size={0.8f,0.8f}; flash.startColor={1,0.9f,0.5f,1};
    flash.sizeCurve={{0,0.2f},{0.15f,1.2f},{1,0}};
    flash.opacityCurve={{0,1},{0.2f,0.8f},{1,0}};
    flash.colorGradient={{0,flash.startColor},{0.4f,{1,0.5f,0.1f,1}},{1,{1,0.1f,0,0}}};
    auto haze=smoke(); haze.looping=false; haze.duration=0.5f; haze.rate=40;
    asset.emitters={{"Flash",{},0,flash},{"Sparks",{},0,sparks()},{"Smoke",{},0.1f,haze}};
    return asset;
}
EffectAsset fallingLeaves() {
    EffectAsset asset;asset.name="Falling Leaves";asset.duration=6;asset.looping=true;asset.backend=Backend::Auto;
    auto main=leaves();auto distant=main;distant.rate=9;distant.size={.08f,.15f};distant.lifetime={4,5.5f};
    asset.emitters={{"Foreground leaves",{0,1.8f,0},0,main},{"Distant leaves",{0,2.4f,0},.4f,distant}};
    asset.emitters[0].id=1;asset.emitters[1].id=2;
    auto& optional=asset.emitters[1].enabled;optional.mode=BoolMode::Probability;optional.scope=SampleScope::EffectCycle;optional.probability=.8f;
    auto& delay=asset.emitters[1].delayValue;delay.mode=NumberMode::Uniform;delay.scope=SampleScope::EffectCycle;delay.min=.1f;delay.max=.6f;
    auto card=main;card.size={.7f,.7f};card.lifetime={4.5f,4.5f};card.angularVelocity={.6f,1.1f};
    card.value(FloatAttribute::SwayAmplitude).min=.05f;card.value(FloatAttribute::SwayAmplitude).max=.1f;
    EffectEmitter hero{"Hero leaf",{-1,0,0},1,card};hero.id=3;hero.kind=EffectLayerKind::SpriteAnimation;
    asset.emitters.push_back(std::move(hero));
    return asset;
}
}
