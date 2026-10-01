#include <AYParticle/EffectResource.h>
#include <AYParticle/EffectAssetIO.h>
#include <AYResource/ResourceRegistry.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <mutex>
namespace ayt::particle {
namespace {
void resolveTextures(EffectAsset& asset,const std::string& owner) {
    for(auto& e:asset.emitters) if(!e.effect.texturePath.empty())
        e.effect.texturePath=ayt::resource::resolveAssetPath(owner,e.effect.texturePath);
}
class Loader final : public ayt::resource::IResourceLoader {
public:
    bool canLoad(const std::string& path) const override { return path.ends_with(".ayparticle"); }
    const char* getResourceType() const override { return "ParticleEffect"; }
    std::shared_ptr<ayt::resource::IResource> load(const std::string& path) override {
        auto value=std::make_shared<EffectResource>(); return value->load(path)?value:nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadFromBinary(const void* data,size_t size) override {
        auto value=std::make_shared<EffectResource>(); return value->decode(data,size)?value:nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadAsync(const std::string& path,
        std::function<void(std::shared_ptr<ayt::resource::IResource>)> callback) override {
        auto result=load(path); if(callback) callback(result); return result;
    }
};
std::unique_ptr<ayt::resource::IResourceLoader> createLoader() { return std::make_unique<Loader>(); }
}
void registerEffectResourceLoader() {
    static std::once_flag once;
    std::call_once(once,[] {
        ayt::resource::ResourceRegistry::registerExtension(".ayparticle","ParticleEffect");
        ayt::resource::ResourceRegistry::registerLoader("ParticleEffect",&createLoader);
    });
}
bool reloadEffectResource(const std::string& path) {
    registerEffectResourceLoader(); auto& manager=ayt::resource::ResourceManager::instance();
    manager.unloadResource(path); return manager.load<EffectResource>(path)!=nullptr;
}
bool EffectResource::load(const std::string& path) {
    EffectAsset next; if(!loadEffectAsset(path,next)) { _loaded=false; return false; }
    resolveTextures(next,path); _asset=std::make_shared<EffectAsset>(std::move(next));
    _path=path; _type="ParticleEffect"; _loaded=true; return true;
}
bool EffectResource::decode(const void* data,size_t size) {
    if(!data || size>1048576) return false;
    EffectAsset next; if(!decodeEffectAsset(std::string_view(static_cast<const char*>(data),size),next)) return false;
    resolveTextures(next,{}); _asset=std::make_shared<EffectAsset>(std::move(next));
    _type="ParticleEffect"; _loaded=true; return true;
}
size_t EffectResource::sizeInBytes() const {
    return sizeof(*this)+(_asset ? _asset->emitters.size()*sizeof(EffectEmitter) : 0);
}
}