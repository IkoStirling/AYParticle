#include <AYParticle/GpuParticle.h>
#include <AYParticle/ParticleGeometry.h>
#include <bgfx/bgfx.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
using namespace ayt::particle;
namespace {
void check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window,message,w,l);
}
// Fail unattended regressions immediately instead of opening a debugger dialog.
struct Callback : bgfx::CallbackI {
    void fatal(const char* file,uint16_t line,bgfx::Fatal::Enum,const char* text) override {
        std::fprintf(stderr,"BGFX fatal %s:%u: %s\n",file,line,text); std::fflush(stderr); std::_Exit(2);
    }
    void traceVargs(const char*,uint16_t,const char*,va_list) override {}
    void profilerBegin(const char*,uint32_t,const char*,uint16_t) override {}
    void profilerBeginLiteral(const char*,uint32_t,const char*,uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t,void*,uint32_t) override { return false; }
    void cacheWrite(uint64_t,const void*,uint32_t) override {}
    void screenShot(const char* path,uint32_t width,uint32_t height,uint32_t pitch,const void* data,uint32_t,bool flip) override {
        BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
        file.bfSize=file.bfOffBits+width*height*4;
        BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=LONG(width); info.biHeight=-LONG(height);
        info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB;
        std::ofstream output(path,std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file),sizeof(file)); output.write(reinterpret_cast<const char*>(&info),sizeof(info));
        for(uint32_t y=0;y<height;++y) output.write(static_cast<const char*>(data)+(flip?height-1-y:y)*pitch,width*4);
        if(!output) std::fprintf(stderr,"Screenshot write failed: %s\n",path);
    }
    void captureBegin(uint32_t,uint32_t,uint32_t,bgfx::TextureFormat::Enum,bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*,uint32_t) override {}
};
struct Device {
    Callback callback;
    HWND window=nullptr;
    bool ready=false;
    Device(bool visible) {
        WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"AYGpuParticles";
        RegisterClassW(&wc);
        window=CreateWindowExW(0,wc.lpszClassName,L"GPU particles: Sparks + Rain (Esc closes)",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,1280,720,nullptr,nullptr,wc.hInstance,nullptr);
        check(window!=nullptr,"window creation");
        bgfx::Init init; init.type=bgfx::RendererType::Direct3D11; init.platformData.nwh=window;
        init.callback=&callback;
        init.profile=true;
        init.resolution.width=1280; init.resolution.height=720; init.resolution.reset=0;
        ready=bgfx::init(init); check(ready,"D3D11 initialization");
        bgfx::setDebug(BGFX_DEBUG_PROFILER);
        if(visible) ShowWindow(window,SW_SHOW);
    }
    ~Device() { if(ready) bgfx::shutdown(); if(window) DestroyWindow(window); }
};
std::array<float,16> identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
void setupView(uint16_t view,uint16_t width,uint16_t height,bgfx::FrameBufferHandle fbo=BGFX_INVALID_HANDLE,bool ortho=false) {
    auto matrix=identity(); if(ortho) { matrix[0]=1.0f/6; matrix[5]=1.0f/3.375f; }
    const auto unit=identity(); bgfx::setViewTransform(view,unit.data(),matrix.data());
    bgfx::setViewFrameBuffer(view,fbo); bgfx::setViewRect(view,0,0,width,height);
    bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0x071020ff); bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::touch(view);
}
GpuParticleSnapshot snapshot(GpuEmitter& emitter) {
    emitter.capture(200); GpuParticleSnapshot result;
    for(int attempt=0;attempt<20;++attempt) if(emitter.pollSnapshot(bgfx::frame(),result)) return result;
    throw std::runtime_error("GPU readback did not complete");
}
struct Target {
    bgfx::TextureHandle color=BGFX_INVALID_HANDLE,readback=BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fbo=BGFX_INVALID_HANDLE;
    Target() {
        color=bgfx::createTexture2D(128,128,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_RT);
        readback=bgfx::createTexture2D(128,128,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        fbo=bgfx::createFrameBuffer(1,&color,false);
        check(bgfx::isValid(fbo)&&bgfx::isValid(readback),"test render targets");
    }
    ~Target() { bgfx::destroy(fbo); bgfx::destroy(color); bgfx::destroy(readback); }
    std::array<float,4> center() {
        std::array<std::array<float,4>,128*128> pixels{};
        bgfx::blit(240,readback,0,0,color); bgfx::frame();
        const auto ready=bgfx::readTexture(readback,pixels.data());
        while(bgfx::frame()<=ready) {}
        return pixels[64*128+64];
    }
};
void verify(GpuParticleContext& context) {
    ParticleEffect e; e.capacity=129; e.burst=5; e.rate=0; e.looping=false;
    e.velocityMin=e.velocityMax={1,0,0}; e.gravity={0,-2,0}; e.lifetime={1,1}; e.size={.4f,.4f};
    e.startColor={1,0,0,.5f}; e.endColor={1,0,0,0};
    GpuEmitter emitter(context,e); Target target;
    emitter.play(); emitter.update(0,0); setupView(10,128,128,target.fbo); emitter.draw(10);
    std::cout<<"GPU validation: birth and direct pixels\n";
    auto state=snapshot(emitter);
    check(state.alive==5,"GPU burst count including partial final workgroup");
    const auto pixel=target.center();
    check(pixel[0]>.2f&&pixel[1]<.08f,"compute-generated vertices produce visible red pixels");
    emitter.update(0,.1f); state=snapshot(emitter);
    check(std::abs(state.slots[0].position.x-.1f)<.0001f&&std::abs(state.slots[0].position.y+.02f)<.0001f
        &&std::abs(state.slots[0].age-.1f)<.0001f,"GPU movement, gravity and age");
    emitter.pause(); emitter.update(0,.1f); const auto frozen=snapshot(emitter);
    check(frozen.slots[0].age==state.slots[0].age&&frozen.slots[0].position==state.slots[0].position,"pause freezes GPU state");
    emitter.resume(); emitter.update(0,.1f); state=snapshot(emitter);
    check(std::abs(state.slots[0].position.x-.2f)<.0001f,"resume advances GPU state");
    emitter.stop(false);
    for(int frame=0;frame<5;++frame) { emitter.update(0,.2f); bgfx::frame(); }
    state=snapshot(emitter); check(state.alive==0,"GPU particles die while emission is stopped");
    emitter.play(); emitter.update(0,0); state=snapshot(emitter);
    check(state.alive==5&&state.slots[0].age==0&&state.slots[0].position.x==0,"restart clears/reseeds GPU state");
    emitter.stop(true); emitter.update(0,0); state=snapshot(emitter); check(state.alive==0,"stop(true) clears GPU state");
    std::string error; auto invalid=e; invalid.dimension=Dimension::ThreeD;
    check(!GpuEmitter::validate(invalid,&error)&&!error.empty(),"unsupported 3D fails explicitly");
    bool rejected=false; try { emitter.update(0,.5f); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"unbounded dt is rejected");
    // Continuous birth ring, including saturation/overwriting living slots.
    auto rain=e; rain.capacity=67; rain.burst=0; rain.rate=400; rain.duration=.1f; rain.looping=true;
    rain.shape=Shape::Rectangle; rain.extent={.8f,.2f,0}; rain.velocityMin=rain.velocityMax={0,-1,0}; rain.gravity={0,0,0};
    GpuEmitter drops(context,rain); drops.play(); drops.update(0,.1f,{0,.5f,0});
    state=snapshot(drops); check(state.alive==40,"continuous GPU emission");
    for(size_t slot=0;slot<40;++slot) check(state.slots[slot].position.x>=-.8f&&state.slots[slot].position.x<=.8f
        &&state.slots[slot].position.y>=.3f&&state.slots[slot].position.y<=.7f,"GPU rectangle half-extent bounds match CPU");
    drops.update(0,.1f); state=snapshot(drops); check(state.alive==67,"ring capacity saturates without overflow");
    auto curved=e; curved.burst=1; curved.velocityMin=curved.velocityMax={0,0,0}; curved.gravity={0,0,0};
    curved.sizeCurve={{0,1},{.5f,2},{1,0}}; curved.opacityCurve={{0,.2f},{.5f,.8f},{1,0}};
    curved.colorGradient={{0,{1,0,0,1}},{.5f,{0,1,0,1}},{1,{0,0,1,1}}};
    curved.columns=4; curved.rows=2; curved.firstFrame=1; curved.frameCount=5; curved.framesPerSecond=8;
    curved.space=Space::Local;
    GpuEmitter animated(context,curved); animated.play(); animated.updatePose(0,0,{}); bgfx::frame();
    Pose moved; moved.position={.3f,.2f,0}; moved.right={0,2,0}; moved.up={-3,0,0};
    animated.updatePose(0,.25f,moved); state=snapshot(animated);
    check(std::abs(state.colors[0].r-.5f)<.001f&&std::abs(state.colors[0].g-.5f)<.001f
        &&std::abs(state.colors[0].a-.5f)<.001f,"GPU interior gradient and absolute opacity keys");
    check(std::abs(state.uv[0][0]-.75f)<.001f&&std::abs(state.uv[0][1]-.5f)<.001f,"GPU atlas frame and top-left UV convention");
    check(state.slots[0].position==Vec3{},"Local GPU state stays relative while emitter moves");
    check(std::abs(state.firstVertex[0].x-1.2f)<.001f&&std::abs(state.firstVertex[0].y+.4f)<.001f,
        "GPU size curve and moving rotated/scaled Local geometry");
    auto disk=curved; disk.space=Space::World; disk.shape=Shape::Disk; disk.extent={.2f,.2f,0}; disk.burst=40;
    GpuEmitter scattered(context,disk); scattered.play(); scattered.updatePose(0,0,moved); state=snapshot(scattered);
    for(size_t slot=0;slot<40;++slot) {
        const auto p=state.slots[slot].position;
        const float x=(p.y-.2f)/2,y=-(p.x-.3f)/3;
        check(x*x+y*y<=.04001f,"GPU disk respects rotated/scaled spawn basis");
    }

    auto varied=e;varied.seed=UINT32_MAX;varied.capacity=16;varied.burst=12;varied.lifetime={2,2};varied.velocityMin=varied.velocityMax={};varied.gravity={};
    varied.columns=4;varied.rows=2;varied.frameCount=8;varied.animationPlayback=AnimationPlayback::PingPong;
    varied.value(FloatAttribute::Size)={NumberMode::Uniform,SampleScope::EffectPlay,0,.25f,.5f};
    varied.value(FloatAttribute::AnimationFps)={NumberMode::Weighted,SampleScope::ParticleBirth};
    varied.value(FloatAttribute::AnimationFps).choices={{3,1},{9,2}};
    varied.value(FloatAttribute::AnimationOffset)={NumberMode::Uniform,SampleScope::ParticleBirth,0,0,7};
    varied.value(FloatAttribute::Opacity)={NumberMode::BetweenCurves,SampleScope::ParticleBirth};
    varied.value(FloatAttribute::Opacity).curve={{0,.1f},{1,.3f}};varied.value(FloatAttribute::Opacity).upperCurve={{0,.6f},{1,1}};
    varied.value(FloatAttribute::SwayAmplitude)={NumberMode::Constant,SampleScope::ParticleBirth,.2f};
    varied.value(FloatAttribute::SwayFrequency)={NumberMode::Constant,SampleScope::ParticleBirth,1};
    varied.flipX={BoolMode::Probability,SampleScope::ParticleBirth,false,.5f};varied.reverseAnimation=varied.flipX;
    GpuEmitter variants(context,varied);ParticleInstance reference(varied);variants.play();reference.play();variants.update(0,0);reference.update(0);auto born=snapshot(variants);
    check(born.alive==12,"typed GPU birth count");
    for(size_t n=0;n<12;++n){const auto& actual=born.slots[n];const auto& expected=reference.particles()[n];
        check(std::abs(actual.initialSize-expected.initialSize)<.001f&&actual.animationFps==expected.animationFps
            &&actual.animationOffset==expected.animationOffset&&actual.flipX==expected.flipX,"CPU GPU hash sources match initial births");}
    variants.update(0,.25f);reference.update(.25f);auto changed=snapshot(variants);
    for(size_t n=0;n<12;++n){const auto& p=changed.slots[n];const auto wanted=reference.sample(reference.particles()[n]);
        const float u=(float(wanted.frame%varied.columns)+(wanted.flipX?1.f:0.f))/varied.columns;
        const float v=(float(wanted.frame/varied.columns)+(wanted.flipY?0.f:1.f))/varied.rows;
        check(std::abs(changed.uv[n][0]-u)<.001f&&std::abs(changed.uv[n][1]-v)<.001f,"GPU ping-pong and mirrored atlas cells match CPU");
        check(p.flipX==born.slots[n].flipX&&p.animationFps==born.slots[n].animationFps,"GPU sources remain frozen");
        check(std::abs(changed.colors[n].a-wanted.color.a)<.001f,"GPU random between lifetime curves");
        check(std::abs((changed.firstVertex[n].x+p.initialSize*.5f)-wanted.position.x)<.001f,"GPU analytic sway matches CPU sampling");}
    variants.play();variants.update(0,0);auto replay=snapshot(variants);
    check(replay.slots[0].flipX==born.slots[0].flipX&&replay.slots[0].animationOffset==born.slots[0].animationOffset,"GPU seeded replay");
    std::cout<<"PASS GPU typed sources, frozen bools, flipbook variation, curve blending and sway\n";
    // Each emitter has independent buffers; 100k live slots total.
    auto stress=rain; stress.capacity=50000; stress.rate=0; stress.burst=50000; stress.size={.002f,.002f};
    stress.looping=false; stress.duration=1;
    GpuEmitter a(context,stress),b(context,stress); a.play(); b.play();
    std::cout<<"GPU validation: 100k slots and timing\n";
    a.update(0,0,{-0.5f,0,0}); b.update(1,0,{0.5f,0,0});
    setupView(10,128,128,target.fbo); a.draw(10); b.draw(10); bgfx::frame();
    auto sa=snapshot(a); auto sb=snapshot(b);
    check(sa.alive==50000&&sb.alive==50000&&std::abs(sb.slots[0].position.x-sa.slots[0].position.x-1)<.001f,"100k independent GPU particles");
    double submitMs=0;
    std::array<double,3> gpuMs{}; std::array<uint32_t,3> gpuSamples{},lastGpuFrame{};
    for(int frame=0;frame<120;++frame) {
        const auto start=std::chrono::steady_clock::now();
        a.update(0,1.0f/240); b.update(1,1.0f/240); a.draw(10); b.draw(10);
        submitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); bgfx::frame();
        const auto* stats=bgfx::getStats();
        if(frame>=20&&stats->gpuTimerFreq>0) for(uint16_t view=0;view<stats->numViews;++view) {
            const auto& timing=stats->viewStats[view];
            const int index=timing.view==0?0:timing.view==1?1:timing.view==10?2:-1;
            if(index>=0&&timing.gpuFrameNum!=lastGpuFrame[index]&&timing.gpuTimeEnd>timing.gpuTimeBegin) {
                gpuMs[index]+=double(timing.gpuTimeEnd-timing.gpuTimeBegin)*1000/stats->gpuTimerFreq;
                ++gpuSamples[index]; lastGpuFrame[index]=timing.gpuFrameNum;
            }
        }
    }
    // CPU reference has equivalent fixed births/endpoints; reports simulate+geometry only.
    ParticleInstance cpu(stress),cpuB(stress); cpu.play(); cpu.update(0); cpuB.play(); cpuB.update(0);
    double cpuMs=0; size_t generated=0;
    for(int frame=0;frame<30;++frame) {
        const auto start=std::chrono::steady_clock::now();
        for(auto* instance:{&cpu,&cpuB}) {
            instance->update(1.0f/240);
            std::vector<ParticleSample> samples; samples.reserve(instance->particles().size());
            for(const auto& p:instance->particles()) samples.push_back(instance->sample(p));
            for(size_t offset=0;offset<samples.size();offset+=16383) {
                const auto count=std::min<size_t>(16383,samples.size()-offset);
                generated+=buildGeometry(std::span<const ParticleSample>(samples).subspan(offset,count),stress).vertices.size();
            }
        }
        cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    check(generated>0,"CPU baseline actually builds geometry");
    std::cout<<"PASS GPU birth/motion/death/pause/restart, rectangle/ring, direct pixels and 100000 live slots\n";
    std::cout<<"GPU host update+draw submission (100k): "<<submitMs/120<<" ms/frame; "
        <<"CPU simulation+sample+geometry (100k, Debug): "<<cpuMs/30<<" ms/frame\n";
    std::cout<<"Timings cover different CPU work scopes; neither is end-to-end GPU frame time.\n";
    if(gpuSamples[0]&&gpuSamples[1]&&gpuSamples[2]) std::cout<<"Profiled GPU views (100k, 128x128): compute="
        <<gpuMs[0]/gpuSamples[0]+gpuMs[1]/gpuSamples[1]<<" ms, draw="<<gpuMs[2]/gpuSamples[2]<<" ms\n";
    else std::cout<<"GPU view timestamps unavailable on this device/run.\n";
}
void show(GpuParticleContext& context,double seconds,uint32_t capacity,const std::string& screenshot) {
    ParticleEffect sparks; sparks.capacity=capacity; sparks.rate=6000; sparks.burst=capacity/4;
    sparks.duration=.8f; sparks.lifetime={.6f,1.3f}; sparks.velocityMin={-2,2,0}; sparks.velocityMax={2,5,0};
    sparks.gravity={0,-7,0}; sparks.size={.02f,.07f}; sparks.startColor={1,.7f,.15f,.9f};
    sparks.endColor={1,.1f,.01f,0}; sparks.endSizeScale=.1f; sparks.blend=Blend::Additive;
    auto rain=sparks; rain.rate=15000; rain.burst=0; rain.duration=1; rain.shape=Shape::Rectangle; rain.extent={5,.2f,0};
    rain.velocityMin={-.4f,-7,0}; rain.velocityMax={-.1f,-4,0}; rain.gravity={0,-.5f,0};
    rain.size={.05f,.1f}; rain.lifetime={.8f,1.4f}; rain.startColor={.4f,.7f,1,.55f}; rain.endColor={.4f,.7f,1,0};
    rain.endSizeScale=1; rain.blend=Blend::Alpha;
    GpuEmitter a(context,sparks),b(context,rain);
    std::array<uint8_t,32*64*4> streak{};
    for(int y=0;y<64;++y) for(int x=0;x<32;++x) {
        const auto index=(y*32+x)*4; streak[index]=streak[index+1]=streak[index+2]=255;
        const float dx=(x+.5f-16)/3.0f,dy=(y+.5f-32)/30.0f;
        streak[index+3]=uint8_t(255*std::max(0.0f,1-dx*dx)*std::max(0.0f,1-dy*dy));
    }
    b.setTexture(32,64,streak.data(),streak.size()); a.play(); b.play();
    setupView(10,1280,720,BGFX_INVALID_HANDLE,true);
    auto start=std::chrono::steady_clock::now(),last=start; uint64_t frames=0; bool captured=false;
    while(true) {
        MSG message; bool quit=false;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT || (message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE)) quit=true;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if(quit) break;
        const auto now=std::chrono::steady_clock::now();
        if(seconds>0&&std::chrono::duration<double>(now-start).count()>=seconds) break;
        const float dt=std::clamp(std::chrono::duration<float>(now-last).count(),0.0f,.1f); last=now;
        a.update(0,dt,{-3,-2.2f,0}); b.update(1,dt,{2.5f,2.8f,0}); a.draw(10); b.draw(10);
        if(!captured&&!screenshot.empty()&&std::chrono::duration<double>(now-start).count()>=1) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE,screenshot.c_str()); captured=true;
        }
        bgfx::frame(); ++frames;
    }
    const auto sa=snapshot(a),sb=snapshot(b);
    std::cout<<"GPU Sparks + Rain: frames="<<frames<<", live="<<sa.alive+sb.alive<<", capacity="<<capacity*2<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        bool test=false,hidden=false,noChildProcesses=false; double seconds=0; uint32_t capacity=50000; std::string screenshot;
        for(int arg=1;arg<argc;++arg) {
            const std::string value=argv[arg];
            if(value=="--verify") test=true;
            else if(value=="--no-child-processes") noChildProcesses=true;
            else if(value=="--hidden") hidden=true;
            else if(value=="--seconds"&&arg+1<argc) seconds=std::stod(argv[++arg]);
            else if(value=="--capacity"&&arg+1<argc) capacity=uint32_t(std::stoul(argv[++arg]));
            else if(value=="--screenshot"&&arg+1<argc) screenshot=argv[++arg];
            else throw std::invalid_argument("Usage: AYParticle_GpuDemo [--verify] [--no-child-processes] [--seconds N] [--capacity 1..65536] [--hidden] [--screenshot file.bmp]");
        }
        check(std::isfinite(seconds)&&seconds>=0&&capacity>0&&capacity<=ParticleEffect::kMaxCapacity,"invalid seconds/capacity");
        if(noChildProcesses) {
            PROCESS_MITIGATION_CHILD_PROCESS_POLICY policy{}; policy.NoChildProcessCreation=1;
            check(SetProcessMitigationPolicy(ProcessChildProcessPolicy,&policy,sizeof(policy))!=FALSE,
                "Windows must prohibit child processes for offline-runtime verification");
            std::cout<<"Child processes prohibited: runtime shaderc cannot execute\n";
        }
        Device device(!test&&!hidden);
        std::cout<<"GPU device ready; loading embedded particle programs\n";
        { GpuParticleContext context; std::cout<<"GPU programs ready\n"; if(test) verify(context); else show(context,seconds,capacity,screenshot); }
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
