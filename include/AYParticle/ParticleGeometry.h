#pragma once
#include <AYParticle/Particle.h>

namespace ayt::particle {
/// Tightly packed CPU vertex. UVs start at texture top-left; colors are linear floats.
struct Vertex { float x,y,z,u,v,r,g,b,a; };
struct Geometry { std::vector<Vertex> vertices; std::vector<uint16_t> indices; };
/// One batch is limited to 16383 quads for portable 16-bit indexing.
/// Caller sorts samples first; this function preserves their order.
Geometry buildGeometry(std::span<const ParticleSample> samples, const ParticleEffect& effect,
                       Vec3 right = {1,0,0}, Vec3 up = {0,1,0});
} // namespace ayt::particle
