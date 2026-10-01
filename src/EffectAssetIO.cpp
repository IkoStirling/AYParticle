#include <AYParticle/EffectAssetIO.h>
#include <AYIO/File.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace ayt::particle {
namespace {
using Json=nlohmann::json;
Json vec(Vec3 v) { return Json::array({v.x,v.y,v.z}); }
Json col(Color c) { return Json::array({c.r,c.g,c.b,c.a}); }
Json ran(Range r) { return Json::array({r.min,r.max}); }
void read(const Json& j,const char* key,Vec3& v) {
    if(!j.contains(key)) return;
    const auto& a=j.at(key); if(!a.is_array() || a.size()!=3) throw std::invalid_argument(std::string(key)+" needs 3 numbers");
    v={a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>()};
}
void read(const Json& j,const char* key,Color& c) {
    if(!j.contains(key)) return;
    const auto& a=j.at(key); if(!a.is_array() || a.size()!=4) throw std::invalid_argument(std::string(key)+" needs 4 numbers");
    c={a.at(0).get<float>(),a.at(1).get<float>(),a.at(2).get<float>(),a.at(3).get<float>()};
}
void read(const Json& j,const char* key,Range& r) {
    if(!j.contains(key)) return;
    const auto& a=j.at(key); if(!a.is_array() || a.size()!=2) throw std::invalid_argument(std::string(key)+" needs 2 numbers");
    r={a.at(0).get<float>(),a.at(1).get<float>()};
}
uint32_t integer(const Json& j,const char* key,uint32_t fallback) {
    if(!j.contains(key)) return fallback;
    const auto& v=j.at(key);
    if(!v.is_number_integer() || v.get<double>()<0 || v.get<double>()>UINT32_MAX)
        throw std::invalid_argument(std::string(key)+" needs an unsigned integer");
    return v.get<uint32_t>();
}
Json numberSource(const NumberProperty& p) {
    Json j={{"mode",int(p.mode)},{"scope",int(p.scope)},{"value",p.value},{"min",p.min},{"max",p.max},
        {"choices",Json::array()},{"curve",Json::array()},{"upperCurve",Json::array()}};
    for(const auto& c:p.choices)j["choices"].push_back(Json::array({c.value,c.weight}));
    for(const auto& k:p.curve)j["curve"].push_back(Json::array({k.time,k.value}));
    for(const auto& k:p.upperCurve)j["upperCurve"].push_back(Json::array({k.time,k.value}));
    return j;
}
NumberProperty readNumberSource(const Json& j) {
    if(!j.is_object())throw std::invalid_argument("Number source must be an object");
    NumberProperty p;const auto mode=integer(j,"mode",0),scope=integer(j,"scope",2);
    if(mode>5||scope>2)throw std::invalid_argument("Invalid number source mode/scope");
    p.mode=static_cast<NumberMode>(mode);p.scope=static_cast<SampleScope>(scope);
    p.value=j.value("value",0.f);p.min=j.value("min",0.f);p.max=j.value("max",0.f);
    const auto list=[&](const char* key,size_t maximum,auto append){
        if(!j.contains(key))return;const auto& items=j.at(key);
        if(!items.is_array()||items.size()>maximum)throw std::invalid_argument("Property list exceeds bounds");
        for(const auto& pair:items){if(!pair.is_array()||pair.size()!=2)throw std::invalid_argument("Property pair needs two numbers");append(pair[0].get<float>(),pair[1].get<float>());}
    };
    list("choices",kMaxPropertyChoices,[&](float v,float w){p.choices.push_back({v,w});});
    list("curve",kMaxLifetimeKeys,[&](float t,float v){p.curve.push_back({t,v});});
    list("upperCurve",kMaxLifetimeKeys,[&](float t,float v){p.upperCurve.push_back({t,v});});
    return p;
}
Json boolSource(const BoolProperty& p){return {{"mode",int(p.mode)},{"scope",int(p.scope)},{"value",p.value},{"probability",p.probability}};}
BoolProperty readBoolSource(const Json& j) {
    if(!j.is_object())throw std::invalid_argument("Boolean source must be an object");
    BoolProperty p;const auto mode=integer(j,"mode",0),scope=integer(j,"scope",2);
    if(mode>1||scope>2)throw std::invalid_argument("Invalid boolean source mode/scope");
    p.mode=static_cast<BoolMode>(mode);p.scope=static_cast<SampleScope>(scope);p.value=j.value("value",false);p.probability=j.value("probability",.5f);return p;
}
Json extensions(const ParticleEffect& e) {
    Json properties=Json::object();
    for(size_t n=0;n<e.values.size();++n)if(!(e.values[n]==NumberProperty{}))properties[attributeName(static_cast<FloatAttribute>(n))]=numberSource(e.values[n]);
    return {{"properties",properties},{"flipX",boolSource(e.flipX)},{"flipY",boolSource(e.flipY)},{"reverseAnimation",boolSource(e.reverseAnimation)},
        {"animationPlayback",int(e.animationPlayback)},{"animationOverLifetime",e.animationOverLifetime},{"facing",int(e.facing)},
        {"tiltX",ran(e.tiltX)},{"tiltY",ran(e.tiltY)},{"tumbleX",ran(e.tumbleX)},{"tumbleY",ran(e.tumbleY)}};
}
void readExtensions(const Json& j,ParticleEffect& e) {
    if(!j.is_object())throw std::invalid_argument("Particle extensions must be an object");
    if(j.contains("properties")) {
        if(!j["properties"].is_object())throw std::invalid_argument("Properties must be a named object");
        for(const auto& [name,source]:j["properties"].items()) {
            size_t n=0;for(;n<kFloatAttributeCount;++n)if(name==attributeName(static_cast<FloatAttribute>(n)))break;
            if(n==kFloatAttributeCount)throw std::invalid_argument("Unknown particle property: "+name);
            e.values[n]=readNumberSource(source);
        }
    }
    if(j.contains("flipX"))e.flipX=readBoolSource(j["flipX"]);
    if(j.contains("flipY"))e.flipY=readBoolSource(j["flipY"]);
    if(j.contains("reverseAnimation"))e.reverseAnimation=readBoolSource(j["reverseAnimation"]);
    const auto playback=integer(j,"animationPlayback",0),facing=integer(j,"facing",0);
    if(playback>2||facing>1)throw std::invalid_argument("Invalid animation/facing mode");
    e.animationPlayback=static_cast<AnimationPlayback>(playback);e.facing=static_cast<ParticleFacing>(facing);
    e.animationOverLifetime=j.value("animationOverLifetime",false);
    read(j,"tiltX",e.tiltX);read(j,"tiltY",e.tiltY);read(j,"tumbleX",e.tumbleX);read(j,"tumbleY",e.tumbleY);
}
Json encode(const ParticleEffect& e) {
    Json j;
#define FIELD(x) j[#x]=e.x
    FIELD(capacity); FIELD(burst); FIELD(seed); FIELD(rate); FIELD(duration); FIELD(looping);
    FIELD(drag); FIELD(endSizeScale); FIELD(texturePath); FIELD(columns); FIELD(rows);
    FIELD(firstFrame); FIELD(frameCount); FIELD(framesPerSecond);
#undef FIELD
    j["dimension"]=int(e.dimension); j["shape"]=int(e.shape); j["space"]=int(e.space); j["blend"]=int(e.blend);
    j["backend"]=int(e.backend);
    j["extent"]=vec(e.extent); j["velocityMin"]=vec(e.velocityMin); j["velocityMax"]=vec(e.velocityMax); j["gravity"]=vec(e.gravity);
    j["lifetime"]=ran(e.lifetime); j["size"]=ran(e.size); j["rotation"]=ran(e.rotation); j["angularVelocity"]=ran(e.angularVelocity);
    j["startColor"]=col(e.startColor); j["endColor"]=col(e.endColor);
    j["sizeCurve"]=Json::array(); j["opacityCurve"]=Json::array(); j["colorGradient"]=Json::array();
    for(const auto& k:e.sizeCurve) j["sizeCurve"].push_back(Json::array({k.time,k.value}));
    for(const auto& k:e.opacityCurve) j["opacityCurve"].push_back(Json::array({k.time,k.value}));
    for(const auto& k:e.colorGradient) j["colorGradient"].push_back({{"time",k.time},{"color",col(k.color)}});
    j["extensions"]=extensions(e);
    return j;
}
ParticleEffect decode(const Json& j) {
    if(!j.is_object()) throw std::invalid_argument("Emitter effect must be an object");
    ParticleEffect e;
#define UINT_FIELD(x) e.x=integer(j,#x,e.x)
    UINT_FIELD(capacity); UINT_FIELD(burst); UINT_FIELD(seed); UINT_FIELD(columns); UINT_FIELD(rows); UINT_FIELD(firstFrame); UINT_FIELD(frameCount);
#undef UINT_FIELD
#define FIELD(x) if(j.contains(#x)) j.at(#x).get_to(e.x)
    FIELD(rate); FIELD(duration); FIELD(looping); FIELD(drag); FIELD(endSizeScale); FIELD(texturePath); FIELD(framesPerSecond);
#undef FIELD
    const auto mode=[&](const char* key,unsigned max,unsigned fallback=0) {
        const auto v=integer(j,key,fallback); if(v>max) throw std::invalid_argument(std::string("Invalid ")+key); return v;
    };
    e.dimension=static_cast<Dimension>(mode("dimension",1)); e.shape=static_cast<Shape>(mode("shape",4));
    e.space=static_cast<Space>(mode("space",1,1)); e.blend=static_cast<Blend>(mode("blend",1));
    e.backend=static_cast<Backend>(mode("backend",2));
    read(j,"extent",e.extent); read(j,"velocityMin",e.velocityMin); read(j,"velocityMax",e.velocityMax); read(j,"gravity",e.gravity);
    read(j,"lifetime",e.lifetime); read(j,"size",e.size); read(j,"rotation",e.rotation); read(j,"angularVelocity",e.angularVelocity);
    read(j,"startColor",e.startColor); read(j,"endColor",e.endColor);
    const auto curve=[&](const char* key,std::vector<CurveKey>& keys) {
        if(!j.contains(key)) return;
        const auto& list=j.at(key);
        if(!list.is_array() || list.size()>kMaxLifetimeKeys) throw std::invalid_argument("Invalid lifetime curve list");
        for(const auto& k:list) {
            if(!k.is_array() || k.size()!=2) throw std::invalid_argument("Curve key needs [time,value]");
            keys.push_back({k.at(0).get<float>(),k.at(1).get<float>()});
        }
    };
    curve("sizeCurve",e.sizeCurve); curve("opacityCurve",e.opacityCurve);
    if(j.contains("colorGradient")) {
        const auto& list=j.at("colorGradient");
        if(!list.is_array() || list.size()>kMaxLifetimeKeys) throw std::invalid_argument("Invalid gradient list");
        for(const auto& k:list) {
            GradientKey key; key.time=k.at("time").get<float>();
            if(!k.contains("color")) throw std::invalid_argument("Gradient key needs color");
            read(k,"color",key.color); e.colorGradient.push_back(key);
        }
    }
    if(j.contains("extensions"))readExtensions(j["extensions"],e);
    return e;
}
bool fail(std::string* error,const std::string& message) { if(error) *error=message; return false; }
}
bool decodeEffectAsset(std::string_view text,EffectAsset& output,std::string* error) {
    if(text.size()>1048576) return fail(error,"Effect exceeds 1 MiB");
    try {
        const auto j=Json::parse(text);
        if(!j.is_object()) return fail(error,"Particle asset must be an object");
        const auto version=integer(j,"schemaVersion",0);
        if(version!=1 && version!=2 && version!=3) return fail(error,"Unsupported particle schema");
        EffectAsset asset; asset.name=j.at("name").get<std::string>();
        const auto backend=integer(j,"backend",0);
        if(backend>2) return fail(error,"Invalid composition backend");
        asset.backend=static_cast<Backend>(backend);
        asset.seed=integer(j,"seed",1);
        asset.duration=j.at("duration").get<float>(); asset.looping=j.at("looping").get<bool>();
        const auto& emitters=j.at("emitters");
        if(!emitters.is_array() || emitters.size()>EffectAsset::kMaxEmitters) return fail(error,"Invalid emitter list");
        for(const auto& item:emitters) {
            EffectEmitter e; e.name=item.at("name").get<std::string>(); e.delay=item.value("delay",0.0f);
            e.id=integer(item,"id",0);const auto kind=integer(item,"kind",0);
            if(kind>1)return fail(error,"Invalid effect layer kind");e.kind=static_cast<EffectLayerKind>(kind);
            if(item.contains("enabled"))e.enabled=readBoolSource(item["enabled"]);
            if(item.contains("delayValue"))e.delayValue=readNumberSource(item["delayValue"]);
            read(item,"offset",e.offset); e.effect=decode(item.at("effect")); asset.emitters.push_back(std::move(e));
        }
        if(!asset.validate(error)) return false;
        output=std::move(asset); if(error) error->clear(); return true;
    } catch(const std::exception& e) { return fail(error,e.what()); }
}
bool encodeEffectAsset(const EffectAsset& asset,std::string& output,std::string* error) {
    if(!asset.validate(error)) return false;
    try {
        Json j={{"schemaVersion",3},{"name",asset.name},{"duration",asset.duration},{"looping",asset.looping},{"seed",asset.seed},{"emitters",Json::array()}};
        j["backend"]=int(asset.backend);
        for(const auto& e:asset.emitters) j["emitters"].push_back({{"name",e.name},{"delay",e.delay},{"offset",vec(e.offset)},
            {"id",e.id},{"kind",int(e.kind)},{"enabled",boolSource(e.enabled)},{"delayValue",numberSource(e.delayValue)},{"effect",encode(e.effect)}});
        auto text=j.dump(2)+'\n';
        if(text.size()>1048576) return fail(error,"Effect exceeds 1 MiB");
        output=std::move(text); if(error) error->clear(); return true;
    } catch(const std::exception& e) { return fail(error,e.what()); }
}
bool loadEffectAsset(const std::string& path,EffectAsset& output,std::string* error) {
    ayt::io::File in(path,ayt::io::File::Mode::BinaryRead); const auto size=in.size();
    if(!in.ok() || size>1048576) return fail(error,"Cannot read particle effect (missing or larger than 1 MiB)");
    std::string text(size,'\0');
    if(in.read(text.data(),size)!=size) return fail(error,"Cannot read particle effect");
    return decodeEffectAsset(text,output,error);
}
bool saveEffectAsset(const std::string& path,const EffectAsset& asset,std::string* error) {
    std::string text; if(!encodeEffectAsset(asset,text,error)) return false;
    if(!ayt::io::File::atomicWrite(path,text.data(),text.size())) return fail(error,"Atomic particle effect save failed");
    return true;
}
bool encodeParticleExtensions(const ParticleEffect& effect,std::string& output,std::string* error) {
    if(!effect.validate(error))return false;
    try{auto text=extensions(effect).dump();if(text.size()>1048576)return fail(error,"Extensions exceed 1 MiB");output=std::move(text);if(error)error->clear();return true;}
    catch(const std::exception& e){return fail(error,e.what());}
}
bool decodeParticleExtensions(std::string_view text,ParticleEffect& output,std::string* error) {
    if(text.empty())return true;
    if(text.size()>1048576)return fail(error,"Extensions exceed 1 MiB");
    try{auto next=output;readExtensions(Json::parse(text),next);if(!next.validate(error))return false;output=std::move(next);if(error)error->clear();return true;}
    catch(const std::exception& e){return fail(error,e.what());}
}
}
