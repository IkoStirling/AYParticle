#include <AYParticle/EffectPackaging.h>
#include <algorithm>
#include <cctype>
namespace ayt::particle {
namespace fs = std::filesystem;
bool inspectEffectSource(const fs::path& file, const fs::path& assetRoot,
                         EffectSource& output, std::string* error) {
    auto fail = [&](const std::string& message) { if(error) *error=message; return false; };
    try {
        const auto owner=fs::canonical(file);
        auto root=assetRoot;
        if(root.empty()) {
            root=owner.parent_path();
            for(auto dir=root; !dir.empty() && dir!=dir.parent_path(); dir=dir.parent_path()) {
                auto name=dir.filename().string();
                std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){ return char(std::tolower(c)); });
                if(name=="assets") { root=dir; break; }
            }
        }
        root=fs::canonical(root);
        auto relative=[&](const fs::path& path) {
            auto result=path.lexically_relative(root);
            if(result.empty() || result.is_absolute() || *result.begin()=="..")
                throw std::runtime_error("Particle reference escapes asset root: "+path.string());
            return result;
        };
        EffectSource source;
        source.root=root; source.logicalPath=relative(owner).generic_string();
        std::string reason;
        if(!loadEffectAsset(owner.string(),source.asset,&reason)) return fail(reason);
        for(const auto& emitter:source.asset.emitters) {
            if(emitter.effect.texturePath.empty()) { source.textures.emplace_back(); continue; }
            // JSON uses portable separators; tolerate Windows authoring separators.
            auto text=emitter.effect.texturePath;
            std::replace(text.begin(),text.end(),'\\','/');
            fs::path ref(text);
            if(ref.is_absolute() || ref.has_root_name() || text.find(':')!=std::string::npos)
                return fail("Particle texture must be asset-relative: "+text);
            auto prefix=text.substr(0,text.find('/'));
            std::transform(prefix.begin(),prefix.end(),prefix.begin(),[](unsigned char c){ return char(std::tolower(c)); });
            const bool rootRelative=prefix=="textures";
            auto target=rootRelative?root/ref:owner.parent_path()/ref;
            if(!fs::is_regular_file(target)) target=rootRelative?owner.parent_path()/ref:root/ref;
            if(!fs::is_regular_file(target)) return fail("Missing particle texture: "+text+" ("+source.logicalPath+")");
            target=fs::canonical(target);
            relative(target);
            auto extension=target.extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){ return char(std::tolower(c)); });
            if(extension!=".png" && extension!=".jpg" && extension!=".jpeg" && extension!=".bmp"
               && extension!=".tga" && extension!=".dds" && extension!=".aytex")
                return fail("Unsupported particle texture: "+text);
            source.textures.push_back(target);
        }
        output=std::move(source); if(error) error->clear(); return true;
    } catch(const std::exception& exception) { return fail(exception.what()); }
}
}
