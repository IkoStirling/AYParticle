#include <AYParticle/ParticleGeometry.h>
#include <cmath>
#include <stdexcept>
namespace ayt::particle {
Geometry buildGeometry(std::span<const ParticleSample> samples, const ParticleEffect& effect, Vec3 right, Vec3 up) {
    if (samples.size()>16383 || !effect.validate()) throw std::invalid_argument("Invalid particle batch");
    Geometry g; g.vertices.reserve(samples.size()*4); g.indices.reserve(samples.size()*6);
    for (const auto& s : samples) {
        const float c=std::cos(s.rotation), sn=std::sin(s.rotation);
        const Vec3 r=(s.oriented?s.right:right*c+up*sn)*(s.size*0.5f), u=(s.oriented?s.up:up*c-right*sn)*(s.size*0.5f);
        const uint32_t frame=s.frame%(effect.columns*effect.rows);
        const float u0=float(frame%effect.columns)/effect.columns, v0=float(frame/effect.columns)/effect.rows;
        const float u1=u0+1.0f/effect.columns, v1=v0+1.0f/effect.rows;
        const Vec3 points[]={s.position-r-u,s.position+r-u,s.position+r+u,s.position-r+u};
        const float left=s.flipX?u1:u0,rightUv=s.flipX?u0:u1,top=s.flipY?v1:v0,bottom=s.flipY?v0:v1;
        const float uv[][2]={{left,bottom},{rightUv,bottom},{rightUv,top},{left,top}};
        const auto base=static_cast<uint16_t>(g.vertices.size());
        for (int i=0;i<4;++i) g.vertices.push_back({points[i].x,points[i].y,points[i].z,uv[i][0],uv[i][1],s.color.r,s.color.g,s.color.b,s.color.a});
        for (uint16_t i : std::initializer_list<uint16_t>{0,1,2,0,2,3}) g.indices.push_back(static_cast<uint16_t>(base+i));
    }
    return g;
}
}
