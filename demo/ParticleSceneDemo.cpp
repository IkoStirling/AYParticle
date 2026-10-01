#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/ParticleEmitterComponent.h>
#include <AYEntity/components/ParticleEffectComponent.h>
#include <iostream>
int main(int argc,char** argv) {
    using namespace ayt;
    if(argc!=2 && argc!=3) { std::cerr<<"Usage: AYParticle_SceneDemo output.ayscene [effects/example.ayparticle]\n"; return 2; }
    entity::bootstrapEntityCore();
    auto& world=entity::World::instance();
    if (!world.initialize()) return 1;
    auto* camera=world.createEntity(); camera->setName("Particle Preview Camera");
    camera->addComponent<entity::Transform>();
    auto* ortho=camera->createComponent<entity::OrthoCameraComponent>();
    ortho->viewSize=6;
    int i=0;
    const char* names[]={"Smoke","Sparks","Explosion"};
    if(argc==3) {
        auto* e=world.createEntity(); e->setName("Combined Particle Effect");
        e->addComponent<entity::Transform>();
        auto* effect=e->addComponent<entity::ParticleEffectComponent>();
        effect->effectPath=argv[2]; effect->layer=3;
    } else for(auto effect:{particle::smoke(),particle::sparks(),particle::explosion()}) {
        // Keep short one-shot presets visible when the scene is opened later.
        if (effect.burst) { effect.looping=true; effect.duration=2; }
        auto* e=world.createEntity(); e->setName(names[i]);
        e->addComponent<entity::Transform>()->position={float(i-1)*2,0,0};
        auto* emitter=e->addComponent<entity::ParticleEmitterComponent>();
        emitter->setEffect(effect); emitter->layer=3; ++i;
    }
    const bool saved=entity::saveScene(world,argv[1]);
    world.shutdown();
    std::cout<<(saved?"Saved ":"Failed ")<<argv[1]<<'\n';
    return saved?0:1;
}
