#include <AYParticle/EffectPackaging.h>
#include <AYParticle/EffectResource.h>
#include <AYResource/ImportJob.h>
#include <AYResource/ProjectBuild.h>
#include <AYResource/CookShip.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/AssetPath.h>
#include <AYResource/assetsDefs/ITexture.h>
#include <AYStorage/IStorageDatabase.h>
#include <AYIO/Path.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ayt;
namespace fs=std::filesystem;
void check(bool value,const std::string& message) { if(!value) throw std::runtime_error(message); }
void tga(const fs::path& path,uint8_t red) {
    fs::create_directories(path.parent_path());
    const uint8_t header[]={0,0,2,0,0,0,0,0,0,0,0,0,2,0,2,0,32,0x28};
    std::ofstream stream(path,std::ios::binary); stream.write(reinterpret_cast<const char*>(header),sizeof(header));
    const uint8_t pixel[]={40,80,red,160};
    for(int index=0;index<4;++index) stream.write(reinterpret_cast<const char*>(pixel),4);
}
int main() {
    const auto root=fs::temp_directory_path()/("AYParticle_PackageTests_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(fs::create_directory(root),"unique test workspace");
        const auto assets=root/"Assets";
        fs::create_directories(assets/"effects/nested");
        tga(assets/"textures/shared.tga",120);
        particle::EffectAsset effect; effect.name="Packaged Smoke";
        effect.backend=particle::Backend::Gpu;
        effect.emitters.push_back({"Smoke",{},0,particle::smoke()});
        effect.emitters[0].effect.texturePath="textures/shared.tga";
        const auto owner=assets/"effects/nested/a.pfx";
        check(particle::saveEffectAsset(owner.string(),effect),"save nested effect");
        check(particle::saveEffectAsset((assets/"effects/b.pfx").string(),effect),"save sharing effect");
        particle::EffectSource closure; std::string error;
        check(particle::inspectEffectSource(owner,assets,closure,&error),"inspect: "+error);
        check(closure.logicalPath=="effects/nested/a.pfx" && closure.textures.size()==1,"preserved nested path");
        effect.emitters[0].effect.texturePath="../../textures/shared.tga";
        check(particle::saveEffectAsset(owner.string(),effect),"owner-relative effect");
        check(particle::inspectEffectSource(owner,assets,closure),"owner-relative reference");
        effect.emitters[0].effect.texturePath="textures/missing.tga";
        check(particle::saveEffectAsset(owner.string(),effect),"missing fixture");
        check(!particle::inspectEffectSource(owner,assets,closure,&error) && error.find("Missing")!=std::string::npos,"missing dependency diagnosed");
        tga(root/"outside.tga",180);
        effect.emitters[0].effect.texturePath="../../../outside.tga";
        check(particle::saveEffectAsset(owner.string(),effect),"external fixture");
        check(!particle::inspectEffectSource(owner,assets,closure,&error) && error.find("escapes")!=std::string::npos,"external dependency rejected");
        effect.emitters[0].effect.texturePath="textures/shared.tga";
        check(particle::saveEffectAsset(owner.string(),effect),"restore fixture");

        resource::ProjectBuildProfile profile;
        profile.schemaVersion=1; profile.id="particle-test"; profile.platform="windows";
        profile.architecture="x64"; profile.configuration="Development";
        profile.content.defaultTransform=resource::ProjectAssetTransform::Exclude;
        profile.content.rules={{"effects/**/*.pfx",resource::ProjectAssetTransform::Cook,resource::ProjectAssetStorage::Pak,"particles",true},
            {"textures/**/*.tga",resource::ProjectAssetTransform::Raw,resource::ProjectAssetStorage::Loose,"particles",true}};
        profile.package.output="out/package";
        auto plan=resource::ProjectBuildPlanner::create(profile,root.string());
        check(plan.valid(),"valid particle plan");
        auto build=resource::ProjectBuildExecutor::execute(plan);
        check(build.ok,"first cook: "+build.error);
        check(build.cookedCount==2 && build.cacheHitCount==0,"first cook misses cache");
        build=resource::ProjectBuildExecutor::execute(plan);
        check(build.ok && build.cacheHitCount==2,"shared texture output deduplicated; second build hits cache");
        tga(assets/"textures/shared.tga",220);
        build=resource::ProjectBuildExecutor::execute(plan);
        check(!build.ok && build.error.find("changed after planning")!=std::string::npos,"stale plan rejected before cache reuse");
        auto fresh=resource::ProjectBuildPlanner::create(profile,root.string());
        check(fresh.valid(),"replan texture mutation");
        build=resource::ProjectBuildExecutor::execute(fresh);
        check(build.ok && build.cacheHitCount==0,"texture content invalidates both owners");
        auto excluded=profile; excluded.content.rules[1].transform=resource::ProjectAssetTransform::Exclude;
        check(!resource::ProjectBuildPlanner::create(excluded,root.string()).valid(),"excluded dependency diagnosed");
        fs::create_directories(assets/"worlds");
        {
            std::ofstream stream(assets/"worlds/broken.scn");
            stream<<R"({"entities":[{"components":[{"$type":"ParticleEffectComponent","effectPath":"effects/missing.pfx"}]}]})";
        }
        auto scenes=profile;
        scenes.content.rules.push_back({"worlds/**/*.scn",resource::ProjectAssetTransform::Raw,resource::ProjectAssetStorage::Loose,"",true});
        check(!resource::ProjectBuildPlanner::create(scenes,root.string()).valid(),"scene missing effect preflight");

        particle::registerEffectResourceLoader(); resource::initializeLoaders();
        auto& manager=resource::ResourceManager::instance();
        const auto content=root/"out/package/Content";
        check(manager.openDatabase((content/"resources.db").string()),"mount published DB");
        resource::setAssetRoot(content.string());
        const auto logical=resource::resolveAssetPath({},"effects/nested/a.pfx");
        check(logical=="effects/nested/a.pfx","root mount preserves package logical key");
        // Source and raw texture copies cannot satisfy this load.
        fs::remove_all(assets); fs::remove_all(content/"textures");
        auto value=manager.load<particle::EffectResource>(logical);
        check(value && !manager.hasLoadFailed(logical),"pak-only effect");
        check(value->asset()->backend==particle::Backend::Gpu,"cook and Pak preserve backend preference");
        const auto texturePath=value->asset()->emitters[0].effect.texturePath;
        auto texture=manager.load<resource::ITexture>(resource::resolveAssetPath({},texturePath));
        check(texture && texture->getFormat()==resource::TextureFormat::RGBA8 && texture->getMipmapCount()==2,"lossless release texture + mipmaps");
        const auto pixels=texture->getMipmapData();
        check(pixels && pixels[0]==220 && pixels[3]==160,"latest texture bytes loaded from package");
        check(manager.storage().getDependencies(logical).size()==1,"effect -> texture edge stored once");
        particle::EffectInstance instance(*value->asset()); instance.play(); instance.update(.5f);
        check(instance.liveParticles()>0,"pak-only effect emits");
        manager.unloadAll(); manager.setDatabase(storage::IStorageDatabase::create(":memory:")); resource::setAssetRoot({});

        resource::ImportOptions options; options.sourcePath=owner.string(); options.sourceAssetRoot=assets.string();
        options.outputDir=(root/"cooked").string(); options.cookTextures=true;
        // Recreate source to independently exercise ImportJob + CookShip.
        fs::create_directories(owner.parent_path()); tga(assets/"textures/shared.tga",100);
        check(particle::saveEffectAsset(owner.string(),effect),"standalone import fixture");
        auto imported=resource::importAsset(options);
        check(imported.ok && imported.conversion.resources.size()==2,"standalone importer: "+imported.error);
        check(fs::is_regular_file(imported.depSidecarPath),"standalone importer returns the actual dependency sidecar");
        tga(assets/"textures/shared.tga",230);
        imported=resource::importAsset(options);
        check(imported.ok && !imported.usedCache,"standalone import avoids source-only mtime cache");
        resource::CookShipOptions ship; ship.assetsRoot=options.outputDir; ship.outputDir=(root/"ship").string();
        auto packed=resource::cookShipPackage(ship);
        check(packed.ok && packed.fileCount==2 && packed.dependencyCount==1,"CookShip typed particle closure: "+packed.error);
        // Non-recursive packing must not inspect excluded nested authoring files.
        const auto invalid=fs::path(options.outputDir)/"deep/invalid.pfx";
        fs::create_directories(invalid.parent_path());
        { std::ofstream stream(invalid); stream<<"invalid"; }
        auto nonrecursive=ship; nonrecursive.recursive=false; nonrecursive.outputDir=(root/"nonrecursive").string();
        const auto shallow=resource::cookShipPackage(nonrecursive);
        check(shallow.ok && shallow.fileCount==0,"non-recursive CookShip respects scan boundary");
        fs::remove(invalid);
        // Raw Pak payloads cannot retain owner-relative references: binary
        // resource decoding has no owner directory. Require an explicit cook.
        const auto rawAssets=root/"RawAssets";
        fs::create_directories(rawAssets/"effects"); fs::create_directories(rawAssets/"textures");
        fs::copy_file(fs::path(options.outputDir)/imported.conversion.dependencies[0].to,rawAssets/"textures/ready.aytex");
        effect.emitters[0].effect.texturePath="../textures/ready.aytex";
        check(particle::saveEffectAsset((rawAssets/"effects/raw.pfx").string(),effect),"raw reference fixture");
        auto rawProfile=profile; rawProfile.content.assetRoot="RawAssets";
        rawProfile.content.rules={{"**/*.pfx",resource::ProjectAssetTransform::Raw,resource::ProjectAssetStorage::Pak,"particles",true},
            {"**/*.aytex",resource::ProjectAssetTransform::Raw,resource::ProjectAssetStorage::Pak,"particles",true}};
        check(!resource::ProjectBuildPlanner::create(rawProfile,root.string()).valid(),"raw owner-relative package dependency rejected");
        const auto priorSize=fs::file_size(packed.pakPath);
        fs::remove(fs::path(options.outputDir)/imported.conversion.dependencies[0].to);
        packed=resource::cookShipPackage(ship);
        check(!packed.ok && fs::file_size(root/"ship/content.pak")==priorSize,"broken closure preserves prior package");
        fs::remove_all(root);
        std::cout<<"PASS particle closure, missing/excluded/external references, scene preflight, shared textures, cache invalidation, stale plan, pak-only playback, standalone import and CookShip\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr<<"FAIL "<<exception.what()<<" ("<<root.string()<<")\n"; return 1; }
}
