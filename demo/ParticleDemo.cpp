#include <AYParticle/Particle.h>
#include <chrono>
#include <iostream>
int main() {
    using namespace ayt::particle;
    for (auto effect : {smoke(),sparks(),explosion()}) {
        ParticleInstance instance(effect); instance.play();
        for (int i=0;i<60;++i) instance.update(1.0f/60);
        std::cout << "particles after one second: " << instance.particles().size() << '\n';
    }
    ParticleEffect stress=smoke(); stress.capacity=10000; stress.burst=10000; stress.rate=0;
    ParticleInstance instance(stress); instance.play(); instance.update(0);
    const auto start=std::chrono::steady_clock::now();
    for (int i=0;i<120;++i) instance.update(1.0f/120);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout << "10000 particles, CPU average update ms: " << ms/120 << '\n';
}
