#include <AYParticle/Particle.h>
#include <AYParticle/EffectAsset.h>
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
    test("ground collision chooses authored landing height and emits one impact",[] {
        struct Query final : CollisionQuery2D {
            bool sweep(Vec3,Vec3,uint32_t,CollisionHit2D&) const override { return false; }
            bool ground(Vec3 anchor,float maxHeight,uint32_t mask,CollisionHit2D& hit) const override {
                check(near(anchor.x,2)&&near(anchor.y,3)&&maxHeight==2&&mask==4,"ground query inputs");
                hit.position=anchor;hit.height=.5f;hit.surfaceTag=7;return true;
            }
        } query;
        auto e=burstEffect();e.collision2D.mode=CollisionMode2D::Ground;
        e.collision2D.surfaceMask=4;e.collision2D.spawnHeight=2;e.collision2D.fallSpeed=5;
        Pose pose;pose.position={2,3,0};
        ParticleInstance p(e);p.play();p.update(0,pose,e.capacity,&query);
        check(p.particles().size()==1&&near(p.sample(p.particles()[0]).position.y,5),"height is visual only");
        p.update(.2f,pose,e.capacity,&query);check(p.impacts().empty(),"above roof");
        p.update(.2f,pose,e.capacity,&query);
        check(p.particles().empty()&&p.impacts().size()==1,"ground kills exactly once");
        check(p.impacts()[0].surfaceTag==7&&near(p.impacts()[0].height,.5f),"surface identity retained");
        p.update(.1f,pose,e.capacity,&query);check(p.impacts().empty(),"hit queue is per update");
    },failed);
    test("swept 2D collision bounces then dies at bounce cap",[] {
        struct Query final : CollisionQuery2D {
            bool ground(Vec3,float,uint32_t,CollisionHit2D&) const override { return false; }
            bool sweep(Vec3 from,Vec3 to,uint32_t,CollisionHit2D& hit) const override {
                if(from.x<.5f&&to.x>=.5f){hit.position={.5f,0,0};hit.normal={-1,0,0};hit.fraction=(.5f-from.x)/(to.x-from.x);return true;}
                if(from.x>-.5f&&to.x<=-.5f){hit.position={-.5f,0,0};hit.normal={1,0,0};hit.fraction=(from.x+.5f)/(from.x-to.x);return true;}
                return false;
            }
        } query;
        auto e=burstEffect();e.velocityMin=e.velocityMax={4,0,0};
        e.collision2D.mode=CollisionMode2D::Sweep;e.collision2D.response=CollisionResponse2D::Bounce;
        e.collision2D.restitution=.5f;e.collision2D.maxBounces=1;
        ParticleInstance p(e);p.play();p.update(0,{},e.capacity,&query);
        p.update(.2f,{},e.capacity,&query);
        check(p.particles().size()==1&&p.impacts().size()==1,"first sweep contact");
        check(near(p.particles()[0].velocity.x,-2),"normal restitution reflected");
        p.update(.6f,{},e.capacity,&query);
        check(p.particles().empty()&&p.impacts().size()==1,"second contact kills at cap");
    },failed);
    test("2D collision rejects unsupported modes and invalid values",[] {
        auto e=burstEffect();e.collision2D.mode=CollisionMode2D::Ground;
        e.dimension=Dimension::ThreeD;check(!e.validate(),"3D cannot use 2D collision");
        e.dimension=Dimension::TwoD;e.collision2D.restitution=2;check(!e.validate(),"restitution bounded");
        e.collision2D.restitution=.5f;e.space=Space::Local;check(!e.validate(),"world-space collision only");
    },failed);
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
    test("loop bursts preserve live particles and controls",[] {
        auto e=burstEffect(2); e.looping=true; e.duration=0.5f;
        ParticleInstance p(e); p.play(); p.update(0);
        p.update(0.25f); check(p.particles().size()==2,"no early replay");
        p.update(0.25f); check(p.particles().size()==4,"burst at cycle boundary");
        check(near(p.particles()[0].age,0.5f) && near(p.particles()[2].age,0),"existing particles survive replay");
        check(p.particles()[2].birthId==3,"replay preserves birth sequence");
        p.update(0); check(p.particles().size()==4,"no duplicate boundary");
        p.pause(); p.update(10); check(p.particles().size()==4,"pause freezes cycle");
        p.resume(); p.update(0.5f); check(p.particles().size()==6,"resume retains phase");
        p.stop(); p.update(1); check(p.particles().size()==6,"stop disables later bursts");
        p.play(); p.update(0); check(p.particles().size()==2 && p.particles()[0].birthId==1,"play explicitly restarts");
    },failed);
    test("loop burst birth ages match split updates",[] {
        auto e=burstEffect(2); e.looping=true; e.duration=0.25f;
        e.velocityMin={-1,0,0}; e.velocityMax={1,0,0};
        ParticleInstance a(e),b(e); a.play(); b.play(); a.update(0); b.update(0);
        a.update(1.125f); for (int i=0;i<9;++i) b.update(0.125f);
        check(a.particles().size()==10 && b.particles().size()==10,"all crossed cycles fire");
        for (size_t i=0;i<a.particles().size();++i) {
            check(near(a.particles()[i].age,b.particles()[i].age),"birth ages independent of frame size");
            check(near(a.particles()[i].position.x,b.particles()[i].position.x),"RNG and motion continue across cycles");
        }
    },failed);
    test("loop burst stalls and budgets have no backlog",[] {
        auto e=burstEffect(20); e.looping=true; e.duration=0.5f; e.capacity=4; e.lifetime={0.2f,0.2f};
        ParticleInstance p(e); p.play(); p.update(100.125f);
        check(p.particles().size()==4 && p.droppedBirths()==4016,"bounded latest burst after stall");
        for (const auto& v:p.particles()) check(near(v.age,0.125f),"latest burst age");
        p.update(0.5f,{},2); check(p.particles().size()==2,"replay honors global budget");
        p.update(0); check(p.particles().size()==2,"dropped births do not catch up");
        e.duration=0.000001f; p.setEffect(e); p.play(); p.update(1000000);
        check(p.particles().size()<=4 && p.droppedBirths()>0,"extremely many cycles stay bounded");
    },failed);
    test("short presets and repeating showcase",[] {
        for (auto e:{sparks(),explosion()}) {
            ParticleInstance once(e); once.play(); once.update(1.25f);
            check(once.finished(),"gameplay preset remains one shot");
            once.update(2); check(once.particles().empty(),"one shot does not replay");
            e.looping=true; e.duration=2;
            ParticleInstance preview(e); preview.play(); preview.update(1.25f);
            check(preview.particles().empty() && preview.emitting(),"preview waits for replay");
            preview.update(1); check(preview.particles().size()==e.burst,"visible after opening delay");
            preview.update(2); check(preview.particles().size()==e.burst,"later cycles remain visible");
        }
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
    test("lifetime tracks sample boundaries and alpha precedence",[] {
        auto e=burstEffect(); e.size={2,2}; e.endSizeScale=9;
        e.sizeCurve={{0,0.5f},{0.25f,2},{1,0}};
        e.opacityCurve={{0,0},{0.25f,1},{0.75f,1},{1,0}};
        e.colorGradient={{0,{1,0,0,0.3f}},{0.5f,{0,1,0,0.3f}},{1,{0,0,1,0.3f}}};
        ParticleInstance instance(e); Particle p; p.initialSize=2; p.life=4;
        check(near(instance.sample(p).size,1) && near(instance.sample(p).color.a,0),"birth keys");
        p.age=1; auto s=instance.sample(p);
        check(near(s.size,4) && near(s.color.r,0.5f) && near(s.color.g,0.5f) && near(s.color.a,1),"exact size key, gradient interpolation, absolute alpha override");
        p.age=2; s=instance.sample(p);
        check(near(s.size,8.0f/3) && near(s.color.g,1),"piecewise segment");
        p.age=5; s=instance.sample(p);
        check(near(s.size,0) && near(s.color.b,1) && near(s.color.a,0),"death clamped");
        p.age=-1; check(near(instance.sample(p).size,1),"negative age clamped");
        e.opacityCurve.clear(); instance.setEffect(e); p.age=2;
        check(near(instance.sample(p).color.a,0.3f),"gradient alpha without opacity override");
        e.colorGradient.clear(); e.sizeCurve.clear(); e.startColor={1,0,0,1}; e.endColor={0,0,1,0}; instance.setEffect(e);
        s=instance.sample(p); check(near(s.size,10) && near(s.color.a,0.5f) && near(s.color.r,0.5f),"legacy empty-track fallback");
        check(near(sampleCurve({},0.5f,7),7),"empty helper fallback");
    },failed);
    test("lifetime tracks validate transactionally",[] {
        auto e=burstEffect(); ParticleInstance p(e); p.play(); p.update(0);
        for(const auto& keys:std::vector<std::vector<CurveKey>>{
            {{0,1}},{{0.1f,1},{1,0}},{{0,1},{0.5f,1},{0.5f,2},{1,0}},
            {{0,1},{0.8f,1},{0.2f,2},{1,0}},{{0,-1},{1,0}},
            {{0,1},{1,std::numeric_limits<float>::infinity()}}}) {
            e.sizeCurve=keys; check(!p.setEffect(e),"invalid track rejected");
            check(p.particles().size()==1 && p.effect().sizeCurve.empty(),"running state retained");
        }
        e=burstEffect(); e.opacityCurve={{0,1.1f},{1,0}}; check(!e.validate(),"alpha bounded");
        e=burstEffect(); e.colorGradient={{0,{1,1,1,-0.1f}},{1,{}}}; check(!e.validate(),"gradient alpha bounded");
        e.colorGradient={{0,{2,3,4,1}},{1,{}}}; check(e.validate(),"HDR gradient valid");
        e.sizeCurve.clear(); for(size_t i=0;i<33;++i)e.sizeCurve.push_back({float(i)/32,1});
        check(!e.validate(),"bounded key count");
        e.sizeCurve.pop_back(); e.sizeCurve.back().time=1; check(e.validate(),"32 keys valid");
    },failed);
    test("curved presets share 2D and 3D sampling",[] {
        for(auto e:{smoke(),sparks(),explosion()}) {
            check(!e.sizeCurve.empty() && !e.opacityCurve.empty() && !e.colorGradient.empty(),"preset tracks authored");
            Particle p; p.age=0.3f; p.life=1; p.initialSize=1;
            ParticleInstance two(e); const auto a=two.sample(p);
            e.dimension=Dimension::ThreeD; ParticleInstance three(e); const auto b=three.sample(p);
            check(a.color==b.color && a.size==b.size,"dimension-independent lifetime appearance");
        }
    },failed);
    test("bounded stalled emission",[] {
        auto e=smoke(); e.capacity=16; e.rate=1000000;
        ParticleInstance p(e); p.play(); p.update(100);
        check(p.particles().size()<=16 && p.droppedBirths()>0,"stall work bounded");
    },failed);
    test("composition delay offset and transport",[] {
        auto a=combinedExplosion(); a.emitters[1].offset={2,0,0};
        EffectInstance p(a); p.play(); Pose pose; pose.position={10,0,0}; p.update(0,pose);
        check(p.instances()[0].particles().size()==1 && p.instances()[1].particles().size()==60,"flash and sparks start together");
        check(p.instances()[1].particles()[0].position.x==12,"parent plus emitter offset");
        check(p.instances()[2].particles().empty(),"smoke delayed");
        p.pause(); p.update(1); check(p.time()==0,"composition pause");
        p.resume(); p.update(0.2f,pose); check(!p.instances()[2].particles().empty(),"smoke starts after delay");
        p.stop(true); p.update(10); check(p.finished() && p.liveParticles()==0,"stop cancels all pending emission");
        p.play(); p.stop(); p.update(1); check(p.liveParticles()==0,"stop before delayed start");
    },failed);
    test("composition replay stalls and shared spawn allowance",[] {
        auto a=combinedExplosion(); a.looping=true; a.duration=2;
        EffectInstance p(a); p.play(); p.update(0,{},2);
        check(p.liveParticles()==2,"all children share allowance");
        p.update(100.125f,{},2); check(p.liveParticles()<=2 && near(float(p.time()),100.125f),"stalled cycles bounded");
        check(near(p.instances()[1].particles()[0].age,0.125f),"current cycle age");
        p.stop(true); p.play(); p.update(0); p.update(2.125f);
        check(!p.instances()[1].particles().empty(),"short effects replay after delay");
    },failed);
    test("composition validation bounds and uniqueness",[] {
        auto a=combinedExplosion(); check(a.validate(),"default composite valid");
        a.emitters[1].name=a.emitters[0].name; check(!a.validate(),"duplicate name rejected");
        a=combinedExplosion(); a.emitters[0].delay=a.duration; check(!a.validate(),"delay outside cycle rejected");
        a=combinedExplosion(); for(auto& e:a.emitters) e.effect.capacity=65536;
        check(!a.validate(),"aggregate capacity bounded");
        a.emitters.clear(); check(!a.validate(),"empty composition rejected");
    },failed);
    test("billboard basis and 16-bit batch bounds",[] {
        ParticleSample s; s.size=2; s.position={1,2,3};
        const auto g=buildGeometry(std::span(&s,1),ParticleEffect{}, {0,0,1},{0,1,0});
        check(near(g.vertices[0].x,1) && near(g.vertices[0].z,2),"camera-oriented billboard");
        std::vector<ParticleSample> samples(16384);
        bool rejected=false; try { (void)buildGeometry(samples,ParticleEffect{}); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"batch too large rejected");
    },failed);

    test("typed sources and independent frozen scopes",[] {
        NumberProperty n; n.mode=NumberMode::Uniform; n.min=0;n.max=7;
        check(n.sampleWhole(0,0,0)==0&&n.sampleWhole(0,.999999f,0)==7,"inclusive frame range");
        n.mode=NumberMode::Weighted;n.choices={{9,0},{2,1},{8,3}};
        check(n.sample(0,0,0)==2&&n.sample(0,.9f,0)==8,"weighted ignores zero weights");
        n.mode=NumberMode::BetweenCurves;n.curve={{0,1},{1,3}};n.upperCurve={{0,3},{1,5}};
        check(near(n.sample(.5f,.25f,0),2.5f),"stable blend between curves");
        n.value=std::numeric_limits<float>::quiet_NaN();check(!n.validate(0,10),"dormant nonfinite values rejected");
        const auto r=propertyRandom(1,2,0,1,SampleScope::EffectPlay);
        check(r==propertyRandom(1,2,8,99,SampleScope::EffectPlay),"play scope ignores cycle and birth");
        check(propertyRandom(1,2,8,1,SampleScope::EffectCycle)==propertyRandom(1,2,8,99,SampleScope::EffectCycle),"cycle scope ignores birth");
        auto e=burstEffect();e.burst=12;e.capacity=16;
        e.flipX={BoolMode::Probability,SampleScope::ParticleBirth,false,.5f};
        e.value(FloatAttribute::Size)={NumberMode::Uniform,SampleScope::EffectPlay,0,2,3};
        ParticleInstance a(e);a.play();a.update(0);const auto first=a.particles()[0];
        for(const auto& p:a.particles())check(p.initialSize==first.initialSize,"play size shared");
        a.update(.1f);check(a.particles()[0].flipX==first.flipX,"boolean frozen after birth");
        e.value(FloatAttribute::AnimationFps)={NumberMode::Uniform,SampleScope::ParticleBirth,0,3,9};
        ParticleInstance b(e);b.play();b.update(0);
        check(first.flipX==b.particles()[0].flipX&&first.initialSize==b.particles()[0].initialSize,"unrelated property edits preserve streams");
        b.play();b.update(0);check(b.particles()[0].flipX==first.flipX,"seeded replay");
        e.value(FloatAttribute::EmissionRate)={NumberMode::Constant,SampleScope::ParticleBirth,2};check(!e.validate(),"invalid emission scope rejected");
    },failed);
    test("flipbook modes mirrored cells and actual 3D planes",[] {
        auto e=burstEffect();e.columns=4;e.frameCount=4;e.framesPerSecond=2;
        e.reverseAnimation.value=true;e.flipX.value=true;e.animationPlayback=AnimationPlayback::Once;
        ParticleInstance a(e);a.play();a.update(0);auto p=a.particles()[0];p.age=1;
        auto sample=a.sample(p);check(sample.frame==1&&sample.flipX,"reverse once");
        auto g=buildGeometry(std::span(&sample,1),e);check(g.vertices[0].u>g.vertices[1].u,"mirror only current cell");
        e.reverseAnimation.value=false;e.animationPlayback=AnimationPlayback::PingPong;ParticleInstance b(e);p.reverseAnimation=false;p.age=2;
        check(b.sample(p).frame==2,"ping pong returns after last");
        e.animationOverLifetime=true;e.animationPlayback=AnimationPlayback::Once;ParticleInstance c(e);p.age=p.life;check(c.sample(p).frame==3,"normalized life ends on final frame");
        e.size={2,2};e.dimension=Dimension::ThreeD;e.facing=ParticleFacing::Plane;e.tiltY={1.570796326f,1.570796326f};
        ParticleInstance plane(e);plane.play();plane.update(0);auto card=plane.sample(plane.particles()[0]);
        check(card.oriented&&std::abs(card.right.z)>.99f&&std::abs(card.right.x)<.001f,"plane tilts into Z");
        g=buildGeometry(std::span(&card,1),e);check(std::abs(g.vertices[0].z-g.vertices[1].z)>.9f,"true oriented geometry");
    },failed);
    test("heterogeneous layer gate delay and stable IDs",[] {
        auto a=fallingLeaves();a.looping=false;
        a.emitters[0].kind=EffectLayerKind::SpriteAnimation;a.emitters[0].effect.burst=99;a.emitters[0].effect.rate=99;
        a.emitters[1].enabled={BoolMode::Probability,SampleScope::EffectCycle,true,0};
        EffectInstance instance(a);instance.play();instance.update(0);
        check(instance.instances()[0].particles().size()==1&&instance.instances()[1].particles().empty(),"sprite layer one card and probability zero gate");
        auto seed=layerSeed(a,0);std::swap(a.emitters[0],a.emitters[1]);a.emitters[1].name="Renamed";
        check(layerSeed(a,1)==seed,"stable ID survives reorder and rename");
        a.emitters[0].id=a.emitters[1].id;check(!a.validate(),"duplicate stable IDs rejected");
    },failed);
    return failed ? 1 : 0;
}
