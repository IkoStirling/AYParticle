#include <AYRenderer/ParticleDraw.h>
#include <AYParticle/ParticleGeometry.h>
#include "detail/Forward2DOpaquePass.h"
#include "detail/TransparentPass.h"
#include "detail/PassExecContext.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using namespace ayt;
using namespace ayt::render;
using namespace ayt::render::detail;
namespace {
constexpr uint16_t size=32;
using Pixel=std::array<float,4>;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
struct Fixture {
    HWND window=nullptr;
    BGFXAdapter adapter;
    shader::ShaderResourcePool pool;
    std::unordered_map<uint64_t,GpuMesh> meshes;
    std::unordered_map<uint64_t,GpuTexture> textures;
    std::unordered_map<uint64_t,GpuMaterial> materials;
    bgfx::TextureHandle target=BGFX_INVALID_HANDLE,depth=BGFX_INVALID_HANDLE,readback=BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fbo=BGFX_INVALID_HANDLE;
    Fixture() {
        window=CreateWindowExW(0,L"STATIC",L"Particle GPU test",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"hidden test window");
        BGFXInitParams init; init.nativeWindowHandle=window; init.width=init.height=64;
        init.vsync=false; init.msaa=0; init.backend=Backend::Direct3D11;
        require(adapter.initialize(init),"D3D11 initialization");
        require((bgfx::getCaps()->supported&(BGFX_CAPS_TEXTURE_READ_BACK|BGFX_CAPS_TEXTURE_BLIT))
            ==(BGFX_CAPS_TEXTURE_READ_BACK|BGFX_CAPS_TEXTURE_BLIT),"readback support");
        pool.setShadercExecutable(AY_SHADER_SHADERC_HINT);
        pool.setBgfxIncludeDirs({AY_SHADER_BGFX_COMMON_HINT,AY_SHADER_BGFX_SRC_HINT});
        pool.resolvePlatformFromRenderer();
        GpuMaterial material; material.shader=pool.acquire(kParticlePhoskiaSource);
        if (!material.shader.isValid()) for(const auto& e:pool.lastCompileErrors()) std::cerr<<e<<'\n';
        require(material.shader.isValid(),"particle shader compilation");
        material.blendMode=BlendMode::Alpha; material.doubleSided=true;
        material.textures.push_back({"albedoMap",shader::InvalidBinding,{1}});
        materials.emplace(1,std::move(material));
        const uint8_t white[]={255,255,255,255};
        GpuTexture texture; texture.handle=adapter.createTexture2D(1,1,white);
        texture.width=texture.height=1; textures.emplace(1,std::move(texture));
        particle::ParticleSample s; s.size=1;
        auto geom=particle::buildGeometry(std::span(&s,1),particle::ParticleEffect{});
        bgfx::VertexLayout layout; layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float).end();
        GpuMesh mesh; mesh.vertexCount=4; mesh.indexCount=6;
        mesh.vertexBuffer=adapter.createVertexBuffer(geom.vertices.data(),static_cast<uint32_t>(geom.vertices.size()*sizeof(particle::Vertex)),layout);
        mesh.indexBuffer=adapter.createIndexBuffer(geom.indices.data(),12);
        meshes.emplace(1,mesh);
        target=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_RT);
        depth=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT);
        const bgfx::TextureHandle attachments[]={target,depth};
        fbo=bgfx::createFrameBuffer(2,attachments,false);
        readback=bgfx::createTexture2D(size,size,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        require(bgfx::isValid(fbo)&&bgfx::isValid(readback),"offscreen targets");
    }
    ~Fixture() {
        materials.clear(); pool.shutdown();
        for (auto& [id,m]:meshes) { adapter.destroy(m.vertexBuffer); adapter.destroy(m.indexBuffer); }
        for (auto& [id,t]:textures) adapter.destroy(t.handle);
        if(bgfx::isValid(fbo)) adapter.destroy(fbo);
        if(bgfx::isValid(target)) adapter.destroy(target);
        if(bgfx::isValid(depth)) adapter.destroy(depth);
        if(bgfx::isValid(readback)) adapter.destroy(readback);
        adapter.shutdown(); if(window) DestroyWindow(window);
    }
    Pixel draw(bool world,BlendMode blend,float depthClear,float alpha,bool layered=false) {
        materials.at(1).blendMode=blend;
        particle::ParticleSample s; s.position={0,0,0.5f}; s.size=1; s.color={1,0,0,alpha};
        std::array<particle::ParticleSample,2> samples{s,s};
        samples[1].position.z=0.75f; samples[1].color={0,1,0,alpha};
        const auto g=particle::buildGeometry(std::span(samples).first(layered?2:1),particle::ParticleEffect{});
        ParticleDrawData batch; batch.vertices.resize(g.vertices.size()); batch.indices=g.indices;
        std::memcpy(batch.vertices.data(),g.vertices.data(),g.vertices.size()*sizeof(particle::Vertex));
        RenderScene scene; DrawPayload2D payload; DrawItem item;
        item.mesh={1}; item.material={1}; item.particleBatch=&batch; item.shadowFlags=ShadowFlags::None;
        if (!world) item.payload=&payload;
        scene.add(item);
        FrameContext frame; frame.view=frame.projection=math::Float4x4::identity();
        const uint8_t view=world?3:Forward2DOpaquePass::kOverlayViewId;
        adapter.setViewClearRaw(view,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,0,depthClear,0);
        PassExecContext ctx{adapter,pool,scene,meshes,textures,materials,0,0,size,size,frame,view,fbo};
        uint32_t draws=0;
        if(world) { TransparentPass pass; draws=pass.execute(ctx); }
        else { Forward2DOpaquePass pass; draws=pass.execute(ctx); }
        require(draws==1,"one batch submission");
        bgfx::blit(250,readback,0,0,target);
        adapter.endFrame();
        std::array<Pixel,size*size> pixels{};
        const auto ready=bgfx::readTexture(readback,pixels.data());
        while(adapter.gpuFrameCounter()<=ready) adapter.endFrame();
        return pixels[(size/2)*size+size/2];
    }
};
}
int main() {
    try {
        Fixture f;
        auto p=f.draw(false,BlendMode::Alpha,0,0.5f);
        require(p[0]>0.45f && p[0]<0.55f && p[3]>0.45f && p[3]<0.55f,"2D alpha/color and no depth test");
        p=f.draw(false,BlendMode::Additive,0,0.5f);
        require(p[0]>0.45f && p[0]<0.55f && p[3]<0.01f,"2D additive alpha-weighted RGB and preserved coverage");
        p=f.draw(true,BlendMode::Alpha,0,0.5f);
        require(p[0]<0.01f,"3D depth occlusion");
        p=f.draw(true,BlendMode::Alpha,1,0.5f);
        require(p[0]>0.45f && p[0]<0.55f,"3D visible billboard");
        p=f.draw(true,BlendMode::Additive,1,0.5f);
        require(p[0]>0.45f && p[0]<0.55f,"3D additive lifetime fade");
        p=f.draw(true,BlendMode::Alpha,1,0.5f,true);
        require(p[0]>0.20f && p[0]<0.30f && p[1]>0.45f && p[1]<0.55f,"3D particle depth writes stay disabled");
        std::cout<<"PASS real D3D11 particle color, alpha, additive fade and depth occlusion\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
