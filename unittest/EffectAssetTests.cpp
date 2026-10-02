#include <AYParticle/EffectAssetIO.h>
#include <AYParticle/EffectResource.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/ResourceRegistry.h>
#include <AYResource/AssetPath.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace ayt::particle;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    try {
        if(argc==2) {
            auto asset=combinedExplosion(); asset.looping=true;
            check(saveEffectAsset(argv[1],asset),"save example"); return 0;
        }
        auto asset=combinedExplosion(); asset.emitters[1].offset={1,2,3};
        asset.backend=Backend::Gpu; asset.emitters[0].effect.backend=Backend::Auto;
        asset.emitters[1].effect.texturePath="textures/sparks.png";
        asset.emitters[1].effect.angularVelocity={1,2}; asset.emitters[1].effect.seed=UINT32_MAX;
        std::string text,error; check(encodeEffectAsset(asset,text,&error),"encode");
        EffectAsset output; check(decodeEffectAsset(text,output,&error) && output==asset,"roundtrip every field");
        const auto before=output;
        auto badBackend=nlohmann::json::parse(text); badBackend["backend"]=3;
        check(!decodeEffectAsset(badBackend.dump(),output,&error)&&output==before,"invalid composition backend is transactional");
        badBackend=nlohmann::json::parse(text); badBackend["emitters"][0]["effect"]["backend"]=256;
        check(!decodeEffectAsset(badBackend.dump(),output,&error)&&output==before,"invalid emitter backend does not wrap to CPU");
        auto defaults=nlohmann::json::parse(text); defaults.erase("backend");
        for(auto& item:defaults["emitters"]) item["effect"].erase("backend");
        check(decodeEffectAsset(defaults.dump(),output,&error)&&output.backend==Backend::Cpu
            &&output.emitters[0].effect.backend==Backend::Cpu,"legacy assets default to CPU");
        check(decodeEffectAsset(text,output,&error),"restore backend asset");
        check(!decodeEffectAsset(R"({"schemaVersion":4})",output,&error) && output==before,"future schema transactional");
        auto legacy=nlohmann::json::parse(text); legacy["schemaVersion"]=1;
        auto legacyExpected=asset;
        for(size_t i=0;i<legacyExpected.emitters.size();++i) {
            auto& e=legacy["emitters"][i]["effect"];
            e.erase("sizeCurve"); e.erase("opacityCurve"); e.erase("colorGradient");
            auto& f=legacyExpected.emitters[i].effect; f.sizeCurve.clear(); f.opacityCurve.clear(); f.colorGradient.clear();
        }
        check(decodeEffectAsset(legacy.dump(),output,&error) && output==legacyExpected,"schema 1 preserves endpoint ramps");
        check(decodeEffectAsset(text,output,&error),"restore curved asset");
        auto invalidCurve=nlohmann::json::parse(text);
        invalidCurve["emitters"][0]["effect"]["opacityCurve"][0][1]=2;
        check(!decodeEffectAsset(invalidCurve.dump(),output,&error) && output==before,"invalid curve transactional decode");
        auto bad=text; const auto index=bad.find("\"capacity\": 1024"); bad.replace(index,16,"\"capacity\": -1");
        check(!decodeEffectAsset(bad,output,&error) && output==before,"negative unsigned rejected");
        check(!decodeEffectAsset(std::string(1048577,' '),output,&error),"oversized file rejected");

        auto leaves=fallingLeaves(); leaves.emitters[0].kind=EffectLayerKind::SpriteAnimation;
        leaves.emitters[0].effect.value(FloatAttribute::Opacity)={NumberMode::BetweenCurves,SampleScope::ParticleBirth};
        leaves.emitters[0].effect.value(FloatAttribute::Opacity).curve={{0,0},{1,.5f}};
        leaves.emitters[0].effect.value(FloatAttribute::Opacity).upperCurve={{0,.2f},{1,1}};
        std::string variation;check(encodeEffectAsset(leaves,variation,&error),"schema 3 encode");
        check(decodeEffectAsset(variation,output,&error)&&output==leaves,"schema 3 every layer/source roundtrip");
        auto broken=nlohmann::json::parse(variation);broken["emitters"][0]["enabled"]["scope"]=2;
        check(!decodeEffectAsset(broken.dump(),output,&error)&&output==leaves,"bad layer scope transactional");
        auto oldV2=nlohmann::json::parse(text);oldV2["schemaVersion"]=2;oldV2.erase("seed");
        for(auto& layer:oldV2["emitters"]) {layer.erase("id");layer.erase("kind");layer.erase("enabled");layer.erase("delayValue");layer["effect"].erase("extensions");}
        check(decodeEffectAsset(oldV2.dump(),output,&error)&&output==asset,"schema 2 compatibility");
        auto colliding=asset;
        auto& collision=colliding.emitters[1].effect.collision2D;
        collision.mode=CollisionMode2D::Sweep;collision.response=CollisionResponse2D::Bounce;
        collision.surfaceMask=4;collision.restitution=.35f;collision.maxBounces=2;
        collision.impactColor={.2f,.6f,1,.7f};
        std::string collisionText;
        check(encodeEffectAsset(colliding,collisionText,&error),"collision asset encode");
        check(decodeEffectAsset(collisionText,output,&error)&&output==colliding,"2D collision settings roundtrip");
        auto invalidCollision=nlohmann::json::parse(collisionText);
        invalidCollision["emitters"][1]["effect"]["extensions"]["collision2D"]["mode"]=3;
        check(!decodeEffectAsset(invalidCollision.dump(),output,&error)&&output==colliding,"invalid collision remains transactional");
        std::string extras;auto e=leaves.emitters[0].effect;check(encodeParticleExtensions(e,extras),"component extras encode");
        auto e2=e;e2.values={};e2.flipX={};check(decodeParticleExtensions(extras,e2)&&e2==e,"component extras roundtrip");
        ParticleInstance decodedInstance(e2);
        decodedInstance.play();decodedInstance.update(.2f);
        check(!decodedInstance.particles().empty(),"decoded component values construct a live CPU instance");
        const auto collisionExample=std::filesystem::path(__FILE__).parent_path().parent_path()
            /"examples/Collision2D/Assets/effects/RainCollision2D.pfx";
        EffectAsset collisionAsset;
        check(loadEffectAsset(collisionExample.string(),collisionAsset,&error)
            &&collisionAsset.emitters.size()==1
            &&collisionAsset.emitters[0].effect.collision2D.mode==CollisionMode2D::Ground,
            "authored 2D collision example loads");
        const auto dir=std::filesystem::temp_directory_path()/"AYParticle_AssetTests";
        std::filesystem::create_directories(dir); const auto path=(dir/"effect.ayparticle").string();
        check(saveEffectAsset(path,asset,&error),"atomic save"); check(loadEffectAsset(path,output,&error) && output==asset,"disk roundtrip");
        registerEffectResourceLoader(); auto& manager=ayt::resource::ResourceManager::instance();
        auto resource=manager.load<EffectResource>(path);
        check(resource && resource->isLoaded(),"typed resource load");
        check(resource==manager.load<EffectResource>(path),"resource cache");
        check(resource->asset()->emitters[1].effect.texturePath==ayt::resource::resolveAssetPath(path,"textures/sparks.png"),"texture references resolved");
        auto old=resource->asset(); asset.name="Changed"; check(saveEffectAsset(path,asset),"save changed");
        manager.reloadResource(path); auto reloaded=manager.load<EffectResource>(path);
        check(reloaded && reloaded->asset()->name=="Changed" && old->name!="Changed","reload snapshots immutable");
        auto loader=ayt::resource::ResourceRegistry::createLoader("ParticleEffect");
        check(loader && loader->loadFromBinary(text.data(),text.size()),"resource binary payload");
        check(!loader->loadFromBinary(nullptr,1),"invalid binary rejected");
        manager.unloadResource(path); std::filesystem::remove(path);
        check(!manager.load<EffectResource>(path) && manager.hasLoadFailed(path),"missing resource diagnosed");
        check(saveEffectAsset(path,asset),"repair missing resource");
        check(reloadEffectResource(path) && !manager.hasLoadFailed(path),"explicit reload retries failure");
        manager.unloadResource(path); std::filesystem::remove(path);
        std::cout<<"PASS particle asset roundtrip, validation, atomic save, resource cache and reload\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
