cmake_minimum_required(VERSION 3.24)
project(CityBuilder LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This starter uses Direct3D 12 and the Windows shader compiler.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

add_executable(CityBuilder src/main.cpp src/terrain.hpp src/shaders.hpp)
target_compile_features(CityBuilder PRIVATE cxx_std_20)
target_compile_definitions(CityBuilder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# D3DCompile is supplied by the Windows SDK; no separate shader tools needed.
target_link_libraries(CityBuilder PRIVATE SDL3::SDL3-static d3dcompiler)
if(MSVC)
    target_compile_options(CityBuilder PRIVATE /W4 /permissive-)
endif()
