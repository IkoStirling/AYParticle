#include <AYParticle/GpuParticle.h>
#include <bgfx/bgfx.h>
#include "GpuParticleBinaries.h"
#include "GpuShaderBinary.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <limits>

namespace ayt::particle {
namespace {
template<class T> void releaseGpu(T& handle) { if(bgfx::isValid(handle)) bgfx::destroy(handle); handle=BGFX_INVALID_HANDLE; }
void require(bool value,const char* error) { if(!value) throw std::runtime_error(error); }
using Float4=std::array<float,4>;
bgfx::ShaderHandle load(std::span<const uint8_t> bytes,const char* stage,uint64_t hash) {
    require(gpu::validBinary(bytes,stage,hash),"Invalid embedded GPU particle shader");
    const auto handle=bgfx::createShader(bgfx::copy(bytes.data(),static_cast<uint32_t>(bytes.size())));
    require(bgfx::isValid(handle),"GPU particle shader allocation failed"); return handle;
}
}
struct GpuParticleContext::Impl {
    bgfx::ProgramHandle compute=BGFX_INVALID_HANDLE,draw=BGFX_INVALID_HANDLE,diagnostic=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params=BGFX_INVALID_HANDLE,sampler=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sizeKeys=BGFX_INVALID_HANDLE,opacityKeys=BGFX_INVALID_HANDLE,gradientKeys=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sources=BGFX_INVALID_HANDLE,sourceKeys=BGFX_INVALID_HANDLE,choices=BGFX_INVALID_HANDLE,booleans=BGFX_INVALID_HANDLE;
    ~Impl() { releaseGpu(compute); releaseGpu(draw); releaseGpu(diagnostic); releaseGpu(params); releaseGpu(sampler);
        releaseGpu(sizeKeys); releaseGpu(opacityKeys); releaseGpu(gradientKeys);
        releaseGpu(sources);releaseGpu(sourceKeys);releaseGpu(choices);releaseGpu(booleans); }
};
bool GpuParticleContext::supported() noexcept {
    const auto* caps=bgfx::getCaps();
    const auto required=BGFX_CAPS_COMPUTE|BGFX_CAPS_INDEX32;
    return caps && bgfx::getRendererType()==bgfx::RendererType::Direct3D11
        && (caps->supported&required)==required;
}
GpuParticleContext::GpuParticleContext():_impl(std::make_shared<Impl>()) {
    require(supported(),"GPU particles require an initialized D3D11 compute device");
    static_assert(gpu::baked::schema==1);
    _impl->compute=bgfx::createProgram(load(gpu::baked::simulation,"CSH",gpu::baked::simulationHash),true);
    _impl->diagnostic=bgfx::createProgram(load(gpu::baked::diagnostic,"CSH",gpu::baked::diagnosticHash),true);
    const auto vs=load(gpu::baked::vertex,"VSH",gpu::baked::vertexHash);
    bgfx::ShaderHandle fs=BGFX_INVALID_HANDLE;
    try { fs=load(gpu::baked::fragment,"FSH",gpu::baked::fragmentHash); } catch(...) { bgfx::destroy(vs); throw; }
    _impl->draw=bgfx::createProgram(vs,fs,true);
    _impl->params=bgfx::createUniform("u_gp",bgfx::UniformType::Vec4,20);
    _impl->sizeKeys=bgfx::createUniform("u_gpSize",bgfx::UniformType::Vec4,32);
    _impl->opacityKeys=bgfx::createUniform("u_gpOpacity",bgfx::UniformType::Vec4,32);
    _impl->gradientKeys=bgfx::createUniform("u_gpGradient",bgfx::UniformType::Vec4,64);
    static_assert(kFloatAttributeCount==13);
    _impl->sources=bgfx::createUniform("u_gpSource",bgfx::UniformType::Vec4,39);
    _impl->sourceKeys=bgfx::createUniform("u_gpSourceKeys",bgfx::UniformType::Vec4,832);
    _impl->choices=bgfx::createUniform("u_gpChoices",bgfx::UniformType::Vec4,104);
    _impl->booleans=bgfx::createUniform("u_gpBool",bgfx::UniformType::Vec4,3);
    _impl->sampler=bgfx::createUniform("s_particle",bgfx::UniformType::Sampler);
    require(bgfx::isValid(_impl->compute)&&bgfx::isValid(_impl->draw)&&bgfx::isValid(_impl->diagnostic)
        &&bgfx::isValid(_impl->params)&&bgfx::isValid(_impl->sampler)&&bgfx::isValid(_impl->sizeKeys)
        &&bgfx::isValid(_impl->opacityKeys)&&bgfx::isValid(_impl->gradientKeys)&&bgfx::isValid(_impl->sources)
        &&bgfx::isValid(_impl->sourceKeys)&&bgfx::isValid(_impl->choices)&&bgfx::isValid(_impl->booleans),"GPU particle program allocation failed");
}
GpuParticleContext::~GpuParticleContext()=default;
struct GpuEmitter::Impl {
    std::shared_ptr<GpuParticleContext::Impl> context;
    ParticleEffect effect;
    bgfx::DynamicVertexBufferHandle states=BGFX_INVALID_HANDLE,vertices=BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices=BGFX_INVALID_HANDLE;
    bgfx::TextureHandle texture=BGFX_INVALID_HANDLE,diagnostic=BGFX_INVALID_HANDLE,readback=BGFX_INVALID_HANDLE;
    std::array<Float4,20> params{};
    std::array<Float4,32> sizeKeys{},opacityKeys{};
    std::array<Float4,64> gradientKeys{};
    std::array<Float4,39> sources{};
    std::array<Float4,832> sourceKeys{};
    std::array<Float4,104> choices{};
    std::array<Float4,3> booleans{};
    std::array<Float4,576> pixels{};
    uint32_t instanceSeed=1,resolvedBurst=0;
    uint64_t effectCycle=0;
    float resolvedRate=0;
    uint32_t nextSlot=0,ready=0;
    double time=0,fraction=0;
    bool clear=true,playing=false,paused=false,pending=false,burst=true,initialized=false;
    ~Impl() { releaseGpu(states); releaseGpu(vertices); releaseGpu(indices); releaseGpu(texture); releaseGpu(diagnostic); releaseGpu(readback); }
};
bool GpuEmitter::validate(const ParticleEffect& e,std::string* error) {
    if(!e.validate(error)) return false;
    const auto fail=[&](const char* text){ if(error) *error=text; return false; };
    if(e.duration<.001f) return fail("GPU emission duration must be at least 1ms");
    if(e.dimension!=Dimension::TwoD || (e.shape!=Shape::Point && e.shape!=Shape::Rectangle&&e.shape!=Shape::Disk))
        return fail("GPU supports 2D Point/Rectangle/Disk only");
    if(e.framesPerSecond>1000000) return fail("GPU atlas FPS exceeds supported range");
    if(error) error->clear(); return true;
}
GpuEmitter::GpuEmitter(GpuParticleContext& context,const ParticleEffect& effect):_impl(std::make_unique<Impl>()) {
    std::string error; if(!validate(effect,&error)) throw std::invalid_argument(error);
    auto& i=*_impl; i.context=context._impl; i.effect=effect;
    for(size_t key=0;key<effect.sizeCurve.size();++key) i.sizeKeys[key]={effect.sizeCurve[key].time,effect.sizeCurve[key].value,0,0};
    for(size_t key=0;key<effect.opacityCurve.size();++key) i.opacityKeys[key]={effect.opacityCurve[key].time,effect.opacityCurve[key].value,0,0};
    for(size_t key=0;key<effect.colorGradient.size();++key) {
        const auto& k=effect.colorGradient[key]; i.gradientKeys[key*2]={k.time,0,0,0};
        i.gradientKeys[key*2+1]={k.color.r,k.color.g,k.color.b,k.color.a};
    }
    for(size_t key=0;key<kFloatAttributeCount;++key) {
        const auto& source=effect.values[key];
        i.sources[key*3]={float(source.mode),float(source.scope),source.value,source.min};
        i.sources[key*3+1]={source.max,float(source.curve.size()),float(source.upperCurve.size()),float(source.choices.size())};
        for(size_t k=0;k<source.curve.size();++k)i.sourceKeys[key*64+k]={source.curve[k].time,source.curve[k].value,0,0};
        for(size_t k=0;k<source.upperCurve.size();++k)i.sourceKeys[key*64+32+k]={source.upperCurve[k].time,source.upperCurve[k].value,0,0};
        double total=0,sum=0;for(const auto& choice:source.choices)total+=choice.weight;
        for(size_t k=0;k<source.choices.size();++k){sum+=source.choices[k].weight;i.choices[key*8+k]={source.choices[k].value,total>0?float(sum/total):0,0,0};}
    }
    const BoolProperty* flags[]={&effect.flipX,&effect.flipY,&effect.reverseAnimation};
    for(size_t key=0;key<3;++key){const auto& flag=*flags[key];i.booleans[key]={flag.value?1.f:0.f,flag.probability,float(flag.scope),float(flag.mode)};}
    bgfx::VertexLayout stateLayout,vertexLayout;
    stateLayout.begin().add(bgfx::Attrib::Position,4,bgfx::AttribType::Float).end();
    vertexLayout.begin().add(bgfx::Attrib::Position,4,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0,4,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float).end();
    constexpr uint16_t format=BGFX_BUFFER_COMPUTE_FORMAT_32X4|BGFX_BUFFER_COMPUTE_TYPE_FLOAT;
    i.states=bgfx::createDynamicVertexBuffer(effect.capacity*5,stateLayout,format|BGFX_BUFFER_COMPUTE_READ_WRITE);
    i.vertices=bgfx::createDynamicVertexBuffer(effect.capacity*4,vertexLayout,format|BGFX_BUFFER_COMPUTE_READ_WRITE);
    std::vector<uint32_t> indices; indices.reserve(effect.capacity*6);
    for(uint32_t slot=0;slot<effect.capacity;++slot) for(uint32_t corner:{0u,1u,2u,0u,2u,3u}) indices.push_back(slot*4+corner);
    i.indices=bgfx::createIndexBuffer(bgfx::copy(indices.data(),static_cast<uint32_t>(indices.size()*sizeof(uint32_t))),BGFX_BUFFER_INDEX32);
    require(bgfx::isValid(i.states)&&bgfx::isValid(i.vertices)&&bgfx::isValid(i.indices),"GPU particle buffer allocation failed");
    std::array<uint8_t,32*32*4> disc{};
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        const auto offset=(y*32+x)*4; const float dx=(x+.5f-16)/16,dy=(y+.5f-16)/16;
        disc[offset]=disc[offset+1]=disc[offset+2]=255;
        disc[offset+3]=uint8_t(255*std::max(0.0f,1-dx*dx-dy*dy));
    }
    setTexture(32,32,disc.data(),disc.size());
}
GpuEmitter::~GpuEmitter()=default;
void GpuEmitter::play() noexcept { play(_impl->effect.seed,0); }
void GpuEmitter::play(uint32_t instanceSeed,uint64_t effectCycle) noexcept {
    auto& i=*_impl;i.instanceSeed=instanceSeed;i.effectCycle=effectCycle;i.time=i.fraction=0;i.nextSlot=0;i.clear=i.burst=i.playing=true;i.paused=false;
    const auto resolve=[&](FloatAttribute key,float legacy){const auto& source=i.effect.value(key);
        const auto r=propertyRandom(instanceSeed,uint32_t(key),effectCycle,0,source.scope);
        return key==FloatAttribute::BurstCount?source.sampleWhole(0,r,legacy):source.sample(0,r,legacy);};
    i.resolvedRate=resolve(FloatAttribute::EmissionRate,i.effect.rate);
    i.resolvedBurst=uint32_t(std::floor(resolve(FloatAttribute::BurstCount,float(i.effect.burst))));
}
void GpuEmitter::pause() noexcept { _impl->paused=true; }
void GpuEmitter::resume() noexcept { _impl->paused=false; }
void GpuEmitter::stop(bool clear) noexcept { _impl->playing=false; _impl->burst=false; if(clear) _impl->clear=true; }
void GpuEmitter::update(uint16_t view,float dt,Vec3 position) {
    Pose pose; pose.position=position; updatePose(view,dt,pose);
}
void GpuEmitter::updatePose(uint16_t view,float dt,const Pose& pose,uint32_t allowance) {
    const auto position=pose.position;
    const auto finite=[](Vec3 v){ return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); };
    if(!finite(pose.right)||!finite(pose.up)||!finite(pose.forward)) throw std::invalid_argument("GPU pose must be finite");
    if(view>=bgfx::getCaps()->limits.maxViews) throw std::invalid_argument("GPU compute view exceeds device limits");
    if(!std::isfinite(dt)||dt<0||dt>.25f||!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))
        throw std::invalid_argument("GPU update requires finite position and dt in [0,0.25]");
    auto& i=*_impl; const auto& e=i.effect;
    float active=0; uint64_t births=0;
    if(i.playing&&!i.paused) {
        active=e.looping?dt:float(std::min(double(dt),std::max(0.0,double(e.duration)-i.time)));
        const auto oldCycle=uint64_t(i.time/e.duration); i.time+=dt;
        const auto cycles=e.looping?uint64_t(i.time/e.duration)-oldCycle:0;
        const double requested=i.fraction+double(i.resolvedRate)*active;
        births=uint64_t(std::floor(requested)); i.fraction=requested-double(births);
        if(i.burst) { births+=i.resolvedBurst; i.burst=false; }
        births+=cycles*i.resolvedBurst;
        if(!e.looping&&i.time>=e.duration) i.playing=false;
    }
    // Keep the newest bounded births, with no delayed backlog or CPU particle list.
    const uint32_t count=uint32_t(std::min<uint64_t>(births,std::min(e.capacity,allowance)));
    const auto start=uint32_t((i.nextSlot+births-count)%e.capacity);
    i.nextSlot=uint32_t((i.nextSlot+births)%e.capacity);
    auto& p=i.params;
    p[0]={float(e.capacity),i.paused?0:dt,i.clear?1.0f:0.0f,float(i.instanceSeed)};
    p[1]={float(start),float(count),0,float(e.space)};
    p[2]={e.velocityMin.x,e.velocityMin.y,0,0}; p[3]={e.velocityMax.x,e.velocityMax.y,0,0};
    p[4]={e.gravity.x,e.gravity.y,0,e.drag};
    p[5]={position.x,position.y,position.z,float(e.shape)};
    p[6]={e.extent.x,e.extent.y,0,0}; p[7]={e.lifetime.min,e.lifetime.max,e.size.min,e.size.max};
    p[8]={e.rotation.min,e.rotation.max,e.angularVelocity.min,e.angularVelocity.max};
    p[9]={e.startColor.r,e.startColor.g,e.startColor.b,e.startColor.a};
    p[10]={e.endColor.r,e.endColor.g,e.endColor.b,e.endColor.a};
    p[11]={e.endSizeScale,float(e.sizeCurve.size()),float(e.opacityCurve.size()),float(e.colorGradient.size())};
    p[12]={pose.right.x,pose.right.y,pose.right.z,0}; p[13]={pose.up.x,pose.up.y,pose.up.z,0};
    p[14]={float(e.columns),float(e.rows),float(e.firstFrame),float(e.frameCount)};
    p[15]={e.framesPerSecond,float(e.animationPlayback),e.animationOverLifetime?1.f:0.f,0};
    p[16]={float(std::fmod(i.time,double(e.duration))/e.duration),0,0,0};
    p[18]={float(i.instanceSeed&65535),float(i.instanceSeed>>16),float(i.effectCycle&65535),float((i.effectCycle>>16)&65535)};
    p[19]={float((i.effectCycle>>32)&65535),float(i.effectCycle>>48),0,0};
    bgfx::setUniform(i.context->params,p.data(),20);
    bgfx::setUniform(i.context->sizeKeys,i.sizeKeys.data(),32);
    bgfx::setUniform(i.context->opacityKeys,i.opacityKeys.data(),32);
    bgfx::setUniform(i.context->gradientKeys,i.gradientKeys.data(),64);
    bgfx::setUniform(i.context->sources,i.sources.data(),39);bgfx::setUniform(i.context->sourceKeys,i.sourceKeys.data(),832);
    bgfx::setUniform(i.context->choices,i.choices.data(),104);bgfx::setUniform(i.context->booleans,i.booleans.data(),3);
    bgfx::setBuffer(0,i.states,bgfx::Access::ReadWrite); bgfx::setBuffer(1,i.vertices,bgfx::Access::Write);
    bgfx::dispatch(view,i.context->compute,(e.capacity+63)/64);
    i.clear=false; i.initialized=true;
}
void GpuEmitter::draw(uint16_t view) {
    if(view>=bgfx::getCaps()->limits.maxViews) throw std::invalid_argument("GPU draw view exceeds device limits");
    auto& i=*_impl; if(!i.initialized) return;
    bgfx::setVertexBuffer(0,i.vertices); bgfx::setIndexBuffer(i.indices);
    bgfx::setTexture(0,i.context->sampler,i.texture);
    uint64_t state=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_MSAA;
    state|=i.effect.blend==Blend::Additive?BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ZERO,BGFX_STATE_BLEND_ONE)
        :BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_INV_SRC_ALPHA,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state); bgfx::submit(view,i.context->draw);
}
void GpuEmitter::setTexture(uint16_t w,uint16_t h,const uint8_t* pixels,size_t bytes) {
    if(!w||!h||!pixels||bytes!=size_t(w)*h*4||bytes>std::numeric_limits<uint32_t>::max()
        ||w>bgfx::getCaps()->limits.maxTextureSize||h>bgfx::getCaps()->limits.maxTextureSize)
        throw std::invalid_argument("GPU texture requires supported RGBA8 dimensions and exact bytes");
    auto texture=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA8,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(pixels,static_cast<uint32_t>(bytes)));
    require(bgfx::isValid(texture),"GPU texture allocation failed"); releaseGpu(_impl->texture); _impl->texture=texture;
}
void GpuEmitter::capture(uint16_t view) {
    auto& i=*_impl;
    require(i.initialized&&!i.pending&&view<255,"GPU capture requires initialized state, a free slot and view <255");
    const auto required=BGFX_CAPS_TEXTURE_READ_BACK|BGFX_CAPS_TEXTURE_BLIT;
    require((bgfx::getCaps()->supported&required)==required,"GPU diagnostics require readback/blit support");
    if(!bgfx::isValid(i.diagnostic)||!bgfx::isValid(i.readback)) {
        releaseGpu(i.diagnostic); releaseGpu(i.readback);
        i.diagnostic=bgfx::createTexture2D(64,9,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_COMPUTE_WRITE);
        i.readback=bgfx::createTexture2D(64,9,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        require(bgfx::isValid(i.diagnostic)&&bgfx::isValid(i.readback),"GPU diagnostic texture allocation failed");
    }
    bgfx::setUniform(i.context->params,i.params.data(),20);
    bgfx::setBuffer(0,i.states,bgfx::Access::Read);
    bgfx::setBuffer(2,i.vertices,bgfx::Access::Read);
    bgfx::setImage(1,i.diagnostic,0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
    bgfx::dispatch(view,i.context->diagnostic,1);
    bgfx::blit(uint16_t(view+1),i.readback,0,0,i.diagnostic);
    i.ready=bgfx::readTexture(i.readback,i.pixels.data()); i.pending=true;
}
bool GpuEmitter::pollSnapshot(uint32_t frame,GpuParticleSnapshot& result) {
    auto& i=*_impl; if(!i.pending||frame<=i.ready) return false;
    result={};
    for(size_t slot=0;slot<64;++slot) {
        const auto& p=i.pixels[slot]; const auto& v=i.pixels[64+slot]; const auto& data=i.pixels[128+slot];
        result.alive+=uint32_t(i.pixels[192+slot][0]);
        result.slots[slot]={{p[0],p[1],p[2]},{v[0],v[1],v[2]},p[3],v[3],data[0],data[1],data[2],uint64_t(data[3])};
        const auto& color=i.pixels[256+slot]; result.colors[slot]={color[0],color[1],color[2],color[3]};
        result.uv[slot]={i.pixels[320+slot][0],i.pixels[320+slot][1]};
        const auto& vertex=i.pixels[384+slot]; result.firstVertex[slot]={vertex[0],vertex[1],vertex[2]};
        const auto& animation=i.pixels[448+slot];const auto& motion=i.pixels[512+slot];auto& particle=result.slots[slot];
        particle.animationFps=animation[0];particle.animationOffset=animation[1];particle.sizeRandom=animation[3];
        const auto flags=uint32_t(animation[2]);particle.flipX=(flags&1)!=0;particle.flipY=(flags&2)!=0;particle.reverseAnimation=(flags&4)!=0;
        particle.opacityRandom=motion[0];particle.swayAmplitude=motion[1];particle.swayFrequency=motion[2];particle.swayPhase=motion[3];
    }
    i.pending=false; return true;
}
uint32_t GpuEmitter::capacity() const noexcept { return _impl->effect.capacity; }
} // namespace ayt::particle
