#include <AYParticle/EffectAssetIO.h>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) { std::cerr<<"Usage: AYParticle_EffectDemo output.ayparticle [combined|smoke|sparks]\n"; return 2; }
    auto asset=ayt::particle::combinedExplosion(); asset.looping=true;
    if(argc==3) {
        const std::string preset=argv[2];
        if(preset=="smoke") { auto e=ayt::particle::smoke(); e.looping=false; e.duration=2.5f; asset.name="Curved Smoke"; asset.emitters={{"Smoke",{},0,e}}; }
        else if(preset=="sparks") { asset.name="Curved Sparks"; asset.emitters={{"Sparks",{},0,ayt::particle::sparks()}}; }
        else if(preset!="combined") { std::cerr<<"Unknown preset\n"; return 2; }
    }
    std::string error;
    if(!ayt::particle::saveEffectAsset(argv[1],asset,&error)) { std::cerr<<error<<'\n'; return 1; }
    std::cout<<"Saved "<<argv[1]<<'\n'; return 0;
}
