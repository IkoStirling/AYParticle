#include <AYEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/ParticleEmitterComponent.h>

// Invoke after the active World and default Entity/Renderer modules are ready.
void createParticleExamples(ayt::entity::World& world) {
    using namespace ayt;
    int index=0;
    for (auto effect:{particle::smoke(),particle::sparks(),particle::explosion()}) {
        // Showcase replay only; sparks()/explosion() remain one-shot presets.
        if (effect.burst) { effect.looping=true; effect.duration=2; }
        auto* actor=world.createEntity();
        auto* transform=actor->addComponent<entity::Transform>();
        transform->position={float(index++-1),0,0};
        auto* emitter=actor->addComponent<entity::ParticleEmitterComponent>();
        emitter->setEffect(effect);
        emitter->play();
    }
}
