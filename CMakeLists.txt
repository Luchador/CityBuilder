cmake_minimum_required(VERSION 3.24)
project(CityBuilder LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This starter uses Direct3D 12 and the Windows shader compiler.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

add_executable(CityBuilder src/main.cpp src/terrain.hpp src/shaders.hpp src/ui.cpp src/ui.hpp
    src/pan.cpp src/pan.hpp src/pan_overlay.cpp src/pan_overlay.hpp
    src/texture.cpp src/texture.hpp)
target_compile_features(CityBuilder PRIVATE cxx_std_20)
target_compile_definitions(CityBuilder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# D3DCompile is supplied by the Windows SDK; no separate shader tools needed.
target_link_libraries(CityBuilder PRIVATE SDL3::SDL3-static d3dcompiler)
if(MSVC)
    target_compile_options(CityBuilder PRIVATE /W4 /permissive-)
endif()

# Keep data beside the executable, independent of the launch working directory.
# Include new assets and subfolders without maintaining a filename list.
# Runs even when only images change and the EXE needs no relink.
add_custom_target(copy_assets
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:CityBuilder>/assets"
    VERBATIM)
add_dependencies(CityBuilder copy_assets)

option(CITY_BUILD_TESTS "Build camera panning and texture manager tests" OFF)
if(CITY_BUILD_TESTS)
    enable_testing()
    add_executable(CityPanTests tests/pan_tests.cpp src/pan.cpp src/ui.cpp)
    target_compile_features(CityPanTests PRIVATE cxx_std_20)
    target_include_directories(CityPanTests PRIVATE src)
    add_test(NAME CityPanTests COMMAND CityPanTests)

    # Test doubles exercise cache/cleanup behavior without creating a GPU device.
    add_executable(CityTextureTests tests/texture_tests.cpp src/texture.cpp)
    target_compile_features(CityTextureTests PRIVATE cxx_std_20)
    target_include_directories(CityTextureTests PRIVATE src)
    target_link_libraries(CityTextureTests PRIVATE SDL3::Headers)
    add_test(NAME CityTextureTests COMMAND CityTextureTests)
endif()
