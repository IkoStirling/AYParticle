#pragma once
#include <AYParticle/EffectAsset.h>
#include <AYResource/IResource.h>
namespace ayt::particle {
/// Immutable asset snapshot loaded/cached by AYResource; hot reload replaces the snapshot.
class EffectResource final : public ayt::resource::IResource {
public:
    bool load(const std::string& path) override;
    bool reload(const std::string& path) override { return load(path); }
    bool unload() override { _asset.reset(); _loaded=false; return true; }
    size_t sizeInBytes() const override;
    std::shared_ptr<const EffectAsset> asset() const noexcept { return _asset; }
    bool decode(const void* data,size_t size);
private:
    std::shared_ptr<const EffectAsset> _asset;
};
/// Register .ayparticle loader before any synchronous/async resource requests.
void registerEffectResourceLoader();
/// Invalidate cached/failed state and synchronously retry this effect only.
bool reloadEffectResource(const std::string& path);
}