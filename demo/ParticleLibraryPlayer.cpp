#include <AYApplication/GameProject.h>
#include <AYApplication/IEngineHost.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <AYParticle/EffectResource.h>
#include <AYParticle/ParticleGeometry.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/ParticleSystem.h>
#include <AYEntity/components/ParticleEffectComponent.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYResource/assetsDefs/ITexture.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYGameLoop.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <cmath>
using namespace ayt;
namespace fs=std::filesystem;
namespace {
void require(bool condition,const std::string& message) { if(!condition) throw std::runtime_error(message); }
// This branch verifies the published executable's scene -> ECS -> resource
// package -> sampled geometry path without needing a visible window.
void verify(const fs::path& content,bool gpu,bool leaves,bool gust) {
    const bool foliage=leaves||gust;
    auto& manager=resource::ResourceManager::instance();
    entity::bootstrapEntityCore();
    auto& world=entity::World::instance(); require(world.initialize(),"World initialization");
    struct Cleanup {
        ~Cleanup() { entity::World::instance().shutdown(); game::SubSystemRegistry::instance().unregisterSubSystem("Renderer"); }
    } cleanup;
    static int window;
    render::RendererSubSystem::setWindowProvider({});
    render::RendererSubSystem::setBootstrapBackend(render::Backend::Noop);
    render::RendererSubSystem::setBootstrapWindow(&window,1280,720);
    auto* renderer=new render::RendererSubSystem();
    game::SubSystemRegistry::instance().registerSubSystem(renderer);
    require(renderer->initialize(),"Verification renderer initialization");
    require(entity::loadScene(world,(content/(gust?"worlds/AutumnGust.scn":leaves?"worlds/FallingLeaves.scn":gpu?"worlds/ParticleLibraryGpu.scn":"worlds/ParticleLibrary.scn")).string()),"Showcase scene load");
    std::vector<entity::Entity*> effects;
    for(auto* entity:world.query<entity::ParticleEffectComponent>()) effects.push_back(entity);
    require(effects.size()==(foliage?1:5),"Expected showcase effects");
    entity::ParticleSimulationSystem simulation; entity::ParticleRenderSystem extraction;
    std::map<std::string,size_t> peak;
    size_t maxBatches=0,drawFrames=0;
    for(int frame=0; frame<360; ++frame) {
        simulation.onUpdate(1.0f/60);
        for(auto* entity:effects) {
            auto* effect=entity->getComponent<entity::ParticleEffectComponent>();
            require(effect->error.empty(),effect->effectPath+": "+effect->error);
            require(effect->runtime!=nullptr,"Effect runtime missing");
            if(gpu||foliage) require((effect->backend==1||effect->backend==2||effect->backend==3)&&!effect->usingGpu&&effect->backendStatus.starts_with("CPU fallback:"),"GPU scene falls back with a reason on Noop");
            peak[effect->effectPath]=std::max(peak[effect->effectPath],effect->runtime->liveParticles());
        }
        render::RenderScene scene; extraction.buildRenderScene(scene);
        size_t batches=0;
        for(const auto& item:scene.items()) if(item.particleBatch && !item.particleBatch->vertices.empty()) ++batches;
        if(batches) ++drawFrames;
        maxBatches=std::max(maxBatches,batches);
    }
    require(maxBatches>=(foliage?2:5) && drawFrames>=(foliage?350:360),"Effects extract renderable batches across six seconds");
    for(auto* entity:effects) {
        auto* effect=entity->getComponent<entity::ParticleEffectComponent>();
        require(peak.at(effect->effectPath)>0,"Effect never emits: "+effect->effectPath);
        const auto logical=resource::resolveAssetPath({},effect->effectPath);
        require(logical==effect->effectPath,"Package effect resolves as a logical path with roots mounted");
        auto value=manager.load<particle::EffectResource>(logical);
        require(value && !manager.hasLoadFailed(logical),"Package particle load");
        for(const auto& emitter:value->asset()->emitters) {
            const auto& texturePath=emitter.effect.texturePath;
            if(texturePath.empty()) {
                require(gust&&emitter.name=="Wind dust","Only wind dust uses the runtime default soft disc");
                continue;
            }
            require(fs::path(texturePath).extension()==".aytex","Release texture representation: "+texturePath);
            auto texture=manager.load<resource::ITexture>(resource::resolveAssetPath({},texturePath));
            require(texture && !manager.hasLoadFailed(texturePath) && texture->getWidth()>0
                    && texture->getMipmapCount()>1 && texture->getMipmapData()!=nullptr,"Package texture + mipmaps: "+texturePath);
        }
        std::cout<<effect->effectPath<<" peak="<<peak.at(effect->effectPath)<<'\n';
    }
    std::cout<<"PASS: packaged scene, effect layers, textures and render geometry; 360 frames, peak batches="<<maxBatches<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        // Resolve beside this executable; packaged playback has no source fallback.
        auto content=fs::absolute(argv[0]).parent_path()/"Content";
        bool verification=false,gpu=false,leaves=false,gust=false; float playSeconds=0;
        for(int index=1; index<argc; ++index) {
            const std::string argument=argv[index];
            if(argument=="--verify") verification=true;
            else if(argument=="--gpu") gpu=true;
            else if(argument=="--leaves") leaves=true;
            else if(argument=="--gust") gust=true;
            else if(argument=="--content" && index+1<argc) content=fs::absolute(argv[++index]);
            else if(argument=="--play-seconds" && index+1<argc) {
                playSeconds=std::stof(argv[++index]);
                require(std::isfinite(playSeconds) && playSeconds>0 && playSeconds<=60,"Playback duration must be 0..60 seconds");
            } else throw std::runtime_error("Usage: AYParticle_LibraryPlayer [--content directory] [--leaves|--gust] [--gpu] [--verify] [--play-seconds N]");
        }
        require(!(leaves&&gust),"Choose either --leaves or --gust");
        const bool foliage=leaves||gust;
        particle::registerEffectResourceLoader(); resource::initializeLoaders();
        resource::setAssetRoot(content.string());
        const auto database=content/"resources.db";
        if(fs::exists(database)) require(resource::ResourceManager::instance().openDatabase(database.string()),"Package database mount");
        else require(!verification,"Verification requires packaged resources.db");
        if(verification) { verify(content,gpu,leaves,gust); return 0; }
        app::GameProject project;
        project.id="particle-library"; project.displayName=gust?"Autumn Gust · Composite + Flipbook":leaves?"Falling Leaves · Composite + Flipbook":"Smoke | Sparks | Explosion | Flame | Rain";
        project.assetRoot=content.string(); project.enablePhysics=false;
        project.width=1280; project.height=720;
        project.startupWorld="showcase";
        project.worlds={{.id="showcase",.scenePath=gust?"worlds/AutumnGust.scn":leaves?"worlds/FallingLeaves.scn":gpu?"worlds/ParticleLibraryGpu.scn":"worlds/ParticleLibrary.scn",.sceneName="Particle Library"}};
        float elapsed=0; size_t frames=0,peak=0,peakGpu=0,peakGpuDraws=0;
        bool systemsReady=false;
        auto& loop=game::GameLoop::instance();
        struct Listener {
            uint64_t id=0;
            ~Listener() { if(id) game::GameLoop::instance().offUpdate(id); }
        } listener;
        if(playSeconds>0) listener.id=loop.onUpdate([&](float dt) {
            elapsed+=dt; ++frames;
            size_t live=0;
            auto* host=app::currentEngineHost();
            auto* scene=host?host->scenes()->current():nullptr;
            if(scene) systemsReady=systemsReady ||
                (scene->world().findSystemByName("ParticleSimulationSystem")
                 && scene->world().findSystemByName("ParticleRenderSystem"));
            if(scene) for(auto* entity:scene->world().query<entity::ParticleEffectComponent>()) {
                auto* effect=entity->getComponent<entity::ParticleEffectComponent>();
                if(effect->runtime) live+=effect->runtime->liveParticles();
                if(gpu&& !effect->error.empty()) throw std::runtime_error(effect->error);
            }
            if(scene) if(auto* simulation=dynamic_cast<entity::ParticleSimulationSystem*>(scene->world().findSystemByName("ParticleSimulationSystem")))
                peakGpu=std::max(peakGpu,size_t(simulation->gpuEmitters));
            if(gpu) if(auto* renderer=render::RendererSubSystem::findRegistered()) {
                size_t gpuDraws=0;
                for(const auto& item:renderer->renderScene().items())
                    if(item.particleBatch&&item.particleBatch->submitGpu&&item.particleBatch->vertices.empty()) ++gpuDraws;
                peakGpuDraws=std::max(peakGpuDraws,gpuDraws);
            }
            peak=std::max(peak,live);
            if(elapsed>=playSeconds) loop.stop();
        });
        app::AppCommandLine commandLine; commandLine.noAudio=true;
        const auto result=app::runGameProject(std::move(project),commandLine);
        if(result==0 && playSeconds>0) {
            require(frames>0 && (gpu?peakGpu>=(foliage?3:7)&&peakGpuDraws>=(foliage?3:7):peak>0) && systemsReady,"Live playback must install particle systems in the Scene World, tick and draw with requested backend");
            std::cout<<"PASS live application playback: frames="<<frames<<", peak CPU particles="<<peak<<", GPU emitters="<<peakGpu<<", GPU draws="<<peakGpuDraws<<'\n';
        }
        return result;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
