cmake_minimum_required(VERSION 3.25)
project(AYParticle VERSION 0.1.0 LANGUAGES CXX)
include(CTest)
# Standalone MSVC/Ninja needs the same UTF-8 /showIncludes repair as the engine.
# Probe both localized and English compilers rather than assuming a UI language.
if(PROJECT_IS_TOP_LEVEL AND MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/particle-prefix.h" "#pragma once\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/particle-prefix.cpp" "#include \"particle-prefix.h\"\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /Zs
        "${CMAKE_CURRENT_BINARY_DIR}/particle-prefix.cpp"
        OUTPUT_VARIABLE _particle_includes ERROR_VARIABLE _particle_include_errors ENCODING UTF-8)
    string(APPEND _particle_includes "${_particle_include_errors}")
    if(_particle_includes MATCHES "注意: 包含文件:  ")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    elseif(_particle_includes MATCHES "Note: including file:  ")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file:  ")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "Note: including file:  ")
    endif()
    add_compile_definitions(AYPARTICLE_STANDALONE_DEPS_PREFIX=1)
endif()
add_library(AYParticle STATIC src/Particle.cpp src/ParticleGeometry.cpp)
add_library(AYParticle::Runtime ALIAS AYParticle)
target_compile_features(AYParticle PUBLIC cxx_std_20)
target_include_directories(AYParticle PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
if(MSVC)
    target_compile_options(AYParticle PRIVATE /W4 /utf-8)
else()
    target_compile_options(AYParticle PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_executable(AYParticle_Tests unittest/ParticleTests.cpp)
    target_link_libraries(AYParticle_Tests PRIVATE AYParticle)
    add_test(NAME AYParticle_Tests COMMAND AYParticle_Tests)
endif()
add_executable(AYParticle_Demo demo/ParticleDemo.cpp)
target_link_libraries(AYParticle_Demo PRIVATE AYParticle)
# Opt-in real GPU regression. A fresh process avoids sticky Noop backend state.
if(WIN32 AND TARGET AYRenderer)
    add_executable(AYParticle_GpuTest EXCLUDE_FROM_ALL unittest/ParticleGpuTest.cpp)
    target_link_libraries(AYParticle_GpuTest PRIVATE AYParticle AYRenderer bgfx::bgfx)
    target_include_directories(AYParticle_GpuTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../AYRenderer/src)
    target_compile_definitions(AYParticle_GpuTest PRIVATE
        AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
        AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
        AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}")
endif()
