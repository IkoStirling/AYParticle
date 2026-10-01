#pragma once
#include <AYParticle/EffectAssetIO.h>
#include <filesystem>
namespace ayt::particle {
/// Offline dependency closure. References must resolve to regular files inside
/// assetRoot; root-relative and owner-relative paths are accepted. An empty
/// root discovers the nearest Assets directory, otherwise uses the owner folder.
struct EffectSource {
    EffectAsset asset;
    std::filesystem::path root;
    std::string logicalPath;
    // One entry per emitter (empty means the renderer's default disc).
    std::vector<std::filesystem::path> textures;
};
bool inspectEffectSource(const std::filesystem::path& file,
                         const std::filesystem::path& assetRoot,
                         EffectSource& output, std::string* error = nullptr);
}
