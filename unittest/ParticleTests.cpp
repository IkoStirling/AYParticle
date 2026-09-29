#include <AYParticle/Particle.h>
#include <AYParticle/ParticleGeometry.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace ayt::particle;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
bool near(float a,float b,float tolerance=0.0001f) { return std::fabs(a-b)<tolerance; }
ParticleEffect burstEffect(uint32_t count=1) {
    ParticleEffect e; e.burst=count; e.rate=0; e.looping=false;
    e.velocityMin=e.velocityMax={}; e.gravity={}; e.lifetime={10,10}; return e;
}
template<class F> void test(const char* name,F run,int& failed) {
    try { run(); std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
}
}
int main() {
    int failed=0;
    test("fractional rate and birth time",[] {
        auto e=burstEffect(0); e.rate=10; e.duration=2; e.looping=true;
        ParticleInstance p(e); p.play();
        p.update(0.15f); check(p.particles().size()==1,"one birth"); check(near(p.particles()[0].age,0.05f),"birth age");
        p.update(0.15f); check(p.particles().size()==3,"fraction preserved");
    },failed);
    test("pause resume stop drain restart",[] {
        auto e=burstEffect(); ParticleInstance p(e); p.play(); p.update(0.1f);
        p.pause(); p.update(1); check(near(p.particles()[0].age,0.1f),"pause freezes age");
        p.resume(); p.update(0.2f); check(near(p.particles()[0].age,0.3f),"resume preserves");
        p.stop(); check(!p.finished(),"drains live particles"); p.update(10); check(p.finished(),"drained");
        p.play(); p.update(0); check(p.particles().size()==1 && p.particles()[0].birthId==1,"restart burst once");
        p.update(0); check(p.particles().size()==1,"no duplicate burst");
        p.stop(true); check(p.finished(),"clear");
    },failed);
    test("capacity global spawn allowance and saturation",[] {
        auto e=burstEffect(20); e.capacity=4;
        ParticleInstance p(e); p.play(); p.update(0,{},2);
        check(p.particles().size()==2 && p.droppedBirths()==18,"budget hard bound");
        p.update(0); check(p.particles().size()==2,"dropped births never catch up");
        p.play(); p.update(0); check(p.particles().size()==4,"capacity hard bound");
    },failed);
    test("expiry and swap removal",[] {
        auto e=burstEffect(100); e.lifetime={0.1f,0.2f};
        ParticleInstance p(e); p.play(); p.update(0); p.update(0.15f);
        check(!p.particles().empty() && p.particles().size()<100,"partial expiry");
        for (const auto& v:p.particles()) check(v.age<v.life,"only living records");
        p.stop(); p.update(1); check(p.finished(),"full expiry");
    },failed);
    test("fixed seed replay",[] {
        auto e=smoke(); e.burst=10; e.rate=0;
        ParticleInstance a(e),b(e); a.play(); b.play(); a.update(0.1f); b.update(0.1f);
        for(size_t i=0;i<a.particles().size();++i) {
            check(a.particles()[i].velocity==b.particles()[i].velocity,"same seed");
            check(a.particles()[i].initialSize==b.particles()[i].initialSize,"same size");
        }
        const auto first=a.particles()[0].velocity;
        a.play(); a.update(0.1f); check(a.particles()[0].velocity==first,"restart RNG");
    },failed);
    test("analytic drag and gravity split-step",[] {
        auto e=burstEffect(); e.velocityMin=e.velocityMax={1,2,3};
        e.dimension=Dimension::ThreeD; e.gravity={0,-4,1}; e.drag=2;
        ParticleInstance a(e),b(e); a.play(); b.play(); a.update(0); b.update(0);
        a.update(1); for(int i=0;i<100;++i) b.update(0.01f);
        check(near(a.particles()[0].position.y,b.particles()[0].position.y),"position independent of steps");
        check(near(a.particles()[0].velocity.z,b.particles()[0].velocity.z),"velocity independent of steps");
    },failed);
    test("world and local transform lifetime",[] {
        auto e=burstEffect(); Pose origin; origin.position={10,0,0}; origin.right={0,2,0}; origin.up={-2,0,0};
        e.velocityMin=e.velocityMax={1,0,0}; e.space=Space::World;
        ParticleInstance world(e); world.play(); world.update(0,origin); world.update(1);
        check(near(world.particles()[0].position.x,10) && near(world.particles()[0].position.y,2),"world captures transformed velocity");
        e.space=Space::Local; ParticleInstance local(e); local.play(); local.update(0); local.update(1);
        const auto s=local.sample(local.particles()[0],origin);
        check(near(s.position.x,10) && near(s.position.y,2),"local follows new pose");
    },failed);
    test("2D suppresses Z and 3D retains Z",[] {
        auto e=burstEffect(); e.velocityMin=e.velocityMax={0,0,3}; e.gravity={0,0,2};
        ParticleInstance a(e); a.play(); a.update(1); check(a.particles()[0].position.z==0,"2D plane");
        e.dimension=Dimension::ThreeD; ParticleInstance b(e); b.play(); b.update(1); check(near(b.particles()[0].position.z,4),"3D acceleration");
    },failed);
    test("shape bounds",[] {
        for(auto shape:{Shape::Rectangle,Shape::Disk,Shape::Box,Shape::Sphere}) {
            auto e=burstEffect(1000); e.dimension=Dimension::ThreeD; e.shape=shape; e.extent={1,2,3};
            ParticleInstance p(e); p.play(); p.update(0);
            for(const auto& v:p.particles()) {
                check(std::fabs(v.position.x)<=1 && std::fabs(v.position.y)<=2 && std::fabs(v.position.z)<=3,"extent bounds");
                if(shape==Shape::Sphere) check(v.position.x*v.position.x+v.position.y*v.position.y/4+v.position.z*v.position.z/9<=1.00001f,"sphere volume");
            }
        }
    },failed);
    test("nonloop window limits emission",[] {
        auto e=burstEffect(0); e.rate=10; e.duration=0.5f;
        ParticleInstance p(e); p.play(); p.update(1);
        check(p.particles().size()==5 && !p.emitting(),"only active interval emits");
        p.update(1); check(p.particles().size()==5,"no later births");
    },failed);
    test("appearance and top-left atlas",[] {
        auto e=burstEffect(); e.size={2,2}; e.endSizeScale=3;
        e.startColor={1,0,0,1}; e.endColor={0,0,1,0};
        e.columns=2; e.rows=2; e.firstFrame=1; e.frameCount=3; e.framesPerSecond=1;
        ParticleInstance p(e); p.play(); p.update(5);
        const auto s=p.sample(p.particles()[0]);
        check(near(s.size,4) && near(s.color.a,0.5f) && near(s.color.b,0.5f),"linear lifetime appearance");
        check(s.frame==3,"atlas range loops");
        const auto g=buildGeometry(std::span(&s,1),e);
        check(g.vertices.size()==4 && g.indices.size()==6,"quad geometry");
        check(near(g.vertices[2].u,1) && near(g.vertices[2].v,0.5f),"top-right UV top-left row");
    },failed);
    test("invalid authoring and dt fail closed",[] {
        auto e=burstEffect(); ParticleInstance p(e); p.play(); p.update(0);
        e.capacity=0; check(!p.setEffect(e),"invalid capacity"); check(p.particles().size()==1,"transactional effect");
        const auto age=p.particles()[0].age;
        p.update(-1); p.update(std::numeric_limits<float>::quiet_NaN()); p.update(std::numeric_limits<float>::infinity());
        check(p.particles()[0].age==age,"invalid dt ignored");
        e=burstEffect(); e.rows=0; check(!e.validate(),"invalid atlas");
        e=burstEffect(); e.shape=Shape::Sphere; check(!e.validate(),"3D shape needs 3D");
    },failed);
    test("bounded stalled emission",[] {
        auto e=smoke(); e.capacity=16; e.rate=1000000;
        ParticleInstance p(e); p.play(); p.update(100);
        check(p.particles().size()<=16 && p.droppedBirths()>0,"stall work bounded");
    },failed);
    test("billboard basis and 16-bit batch bounds",[] {
        ParticleSample s; s.size=2; s.position={1,2,3};
        const auto g=buildGeometry(std::span(&s,1),ParticleEffect{}, {0,0,1},{0,1,0});
        check(near(g.vertices[0].x,1) && near(g.vertices[0].z,2),"camera-oriented billboard");
        std::vector<ParticleSample> samples(16384);
        bool rejected=false; try { (void)buildGeometry(samples,ParticleEffect{}); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"batch too large rejected");
    },failed);
    return failed ? 1 : 0;
}
