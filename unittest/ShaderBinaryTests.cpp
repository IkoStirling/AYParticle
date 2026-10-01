#include "../src/GpuShaderBinary.h"
#include <GpuParticleBinaries.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ayt::particle::gpu;
namespace {
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
void check(std::span<const uint8_t> bytes,std::string_view stage,uint64_t hash) {
    require(validBinary(bytes,stage,hash),"Generated stage failed integrity check");
    require(!validBinary(bytes,stage,hash^1),"Wrong hash accepted");
    require(!validBinary(bytes,stage=="CSH"?"VSH":"CSH",hash),"Wrong shader stage accepted");
    require(!validBinary(bytes.first(7),stage,binaryHash(bytes.first(7))),"Truncated shader accepted");
    std::vector<uint8_t> corrupt(bytes.begin(),bytes.end());
    corrupt.back()^=1;
    require(!validBinary(corrupt,stage,hash),"Corrupt shader payload accepted");
    corrupt[0]='X';
    require(!validBinary(corrupt,stage,binaryHash(corrupt)),"Invalid container magic accepted");
}
}
int main() {
    try {
        static_assert(baked::schema==1);
        check(baked::simulation,"CSH",baked::simulationHash);
        check(baked::diagnostic,"CSH",baked::diagnosticHash);
        check(baked::vertex,"VSH",baked::vertexHash);
        check(baked::fragment,"FSH",baked::fragmentHash);
        require(!validBinary({},"CSH",binaryHash({})),"Empty binary accepted");
        // Published FNV-1a vector checks that producer and consumer share the algorithm.
        constexpr std::array<uint8_t,5> hello={'h','e','l','l','o'};
        require(binaryHash(hello)==0xa430d84680aabd0bull,"FNV-1a hash regression");
        std::cout<<"Four embedded shader stages passed integrity, stage and corruption checks\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
