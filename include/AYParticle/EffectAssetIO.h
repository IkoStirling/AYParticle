#pragma once
#include <AYParticle/EffectAsset.h>
#include <string_view>
namespace ayt::particle {
/// Schema-3 .ayparticle JSON, reading schemas 1/2 with legacy behavior intact.
/// Decode/load validate fully before replacing output, including lifetime tracks.
/// Files are bounded to 1 MiB; saves use AYIO atomic replacement.
bool decodeEffectAsset(std::string_view text, EffectAsset& output, std::string* error = nullptr);
bool encodeEffectAsset(const EffectAsset& asset, std::string& output, std::string* error = nullptr);
bool loadEffectAsset(const std::string& path, EffectAsset& output, std::string* error = nullptr);
bool saveEffectAsset(const std::string& path, const EffectAsset& asset, std::string* error = nullptr);
/// Scene adapter payload for extended properties; ordinary fields remain authoritative.
/// Decode is transactional and validates against the output's existing atlas/dimension.
bool encodeParticleExtensions(const ParticleEffect& effect,std::string& output,std::string* error=nullptr);
bool decodeParticleExtensions(std::string_view text,ParticleEffect& output,std::string* error=nullptr);
}
