#include <AYParticle/Particle.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace ayt::particle {
namespace {
uint32_t mix(uint32_t v) { v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; return v^(v>>16); }
}
float propertyRandom(uint32_t seed,uint32_t property,uint64_t cycle,uint64_t birth,SampleScope scope) noexcept {
    uint32_t h=mix(seed^mix(property+0x9e3779b9u));
    if(scope!=SampleScope::EffectPlay) h=mix(h^mix(uint32_t(cycle))^mix(uint32_t(cycle>>32)));
    if(scope==SampleScope::ParticleBirth) h=mix(h^mix(uint32_t(birth))^mix(uint32_t(birth>>32)));
    return float(h>>8)*(1.f/16777216.f);
}
const char* attributeName(FloatAttribute key) noexcept {
    constexpr const char* names[]={"lifetime","size","rotation","angularVelocity","animationFps","animationOffset",
        "sizeMultiplier","opacity","swayAmplitude","swayFrequency","swayPhase","emissionRate","burstCount"};
    const auto i=static_cast<size_t>(key); return i<kFloatAttributeCount?names[i]:"unknown";
}
bool NumberProperty::validate(float minimum,float maximum,bool curves,std::string* error) const {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(unsigned(mode)>5||unsigned(scope)>2) return fail("Invalid numeric source mode/scope");
    const auto valid=[&](float x){return std::isfinite(x)&&x>=minimum&&x<=maximum;};
    if(choices.size()>kMaxPropertyChoices||curve.size()>kMaxLifetimeKeys||upperCurve.size()>kMaxLifetimeKeys)
        return fail("Numeric source exceeds choice/key limits");
    if(!std::isfinite(value)||!std::isfinite(min)||!std::isfinite(max))return fail("Numeric source contains nonfinite stored values");
    for(const auto& c:choices)if(!std::isfinite(c.value)||!std::isfinite(c.weight))return fail("Nonfinite stored choice");
    for(const auto* keys:{&curve,&upperCurve})for(const auto& k:*keys)
        if(!std::isfinite(k.time)||!std::isfinite(k.value))return fail("Nonfinite stored key");
    if(mode==NumberMode::Constant&&!valid(value)) return fail("Constant is outside the property bounds");
    if(mode==NumberMode::Uniform&&(!valid(min)||!valid(max)||min>max)) return fail("Invalid random range");
    if(mode==NumberMode::Weighted) {
        if(choices.empty()) return fail("Weighted source needs choices");
        double total=0;
        for(const auto& c:choices){if(!valid(c.value)||!std::isfinite(c.weight)||c.weight<0)return fail("Invalid weighted choice");total+=c.weight;}
        if(total<=0||total>1000000) return fail("Weighted choices need positive bounded total weight");
    }
    const auto validCurve=[&](const auto& keys){
        if(keys.size()<2||keys.front().time!=0||keys.back().time!=1)return false;
        float last=-1;for(const auto& k:keys){if(!valid(k.value)||!std::isfinite(k.time)||k.time<=last||k.time>1)return false;last=k.time;}return true;
    };
    if(mode==NumberMode::Curve||mode==NumberMode::BetweenCurves) {
        if(!curves||!validCurve(curve)||(mode==NumberMode::BetweenCurves&&!validCurve(upperCurve)))
            return fail("Invalid or unsupported numeric curve");
    }
    if(error)error->clear();return true;
}
float NumberProperty::sample(float time,float random,float legacy) const {
    const float r=std::clamp(random,0.f,std::nextafter(1.f,0.f));
    switch(mode){
        case NumberMode::Constant:return value;
        case NumberMode::Uniform:return std::lerp(min,max,r);
        case NumberMode::Curve:return sampleCurve(curve,time,legacy);
        case NumberMode::BetweenCurves:return std::lerp(sampleCurve(curve,time,legacy),sampleCurve(upperCurve,time,legacy),r);
        case NumberMode::Weighted:{
            double total=0;for(const auto& c:choices)total+=c.weight;const double target=r*total;double sum=0;
            for(const auto& c:choices){sum+=c.weight;if(c.weight>0&&target<sum)return c.value;}
            for(auto c=choices.rbegin();c!=choices.rend();++c)if(c->weight>0)return c->value;
            return legacy;
        }
        default:return legacy;
    }
}
bool BoolProperty::validate(std::string* error) const {
    if(unsigned(mode)>1||unsigned(scope)>2||!std::isfinite(probability)||probability<0||probability>1){if(error)*error="Invalid boolean source/probability";return false;}
    if(error)error->clear();return true;
}
float NumberProperty::sampleWhole(float time,float random,float legacy) const {
    if(mode==NumberMode::Uniform)return std::min(std::floor(max),std::floor(min)+std::floor(std::clamp(random,0.f,std::nextafter(1.f,0.f))*(std::floor(max)-std::floor(min)+1)));
    return std::floor(sample(time,random,legacy));
}
}
