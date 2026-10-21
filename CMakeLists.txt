cmake_minimum_required(VERSION 3.24)
set(CMAKE_NINJA_CMCLDEPS_RC OFF)
project(CityBuilder LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This starter uses Direct3D 12 and the Windows shader compiler.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# SDL_ttf 3.2.2, including its pinned FreeType and HarfBuzz dependencies.
# Downloaded into build/_deps on the first configure; no system font SDK needed.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "Use SDL_ttf's bundled font dependencies" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "Enable Unicode text shaping" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "SVG color emoji are not used by the UI" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b
    GIT_SUBMODULES external/freetype external/harfbuzz
    GIT_SUBMODULES_RECURSE OFF)
FetchContent_MakeAvailable(SDL_ttf)

file(GLOB_RECURSE CITYBUILDER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_executable(CityBuilder ${CITYBUILDER_SOURCES})

target_compile_features(CityBuilder PRIVATE cxx_std_20)
target_compile_definitions(CityBuilder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# D3DCompile is supplied by the Windows SDK; no separate shader tools needed.
target_link_libraries(CityBuilder PRIVATE SDL3::SDL3-static SDL3_ttf::SDL3_ttf-static d3dcompiler)
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

option(CITY_BUILD_RENDER_KIT "Build the Cities Unlimited sprite authoring companion" OFF)
if(CITY_BUILD_RENDER_KIT)
    add_subdirectory(render_kit)
endif()

option(CITY_BUILD_TOOLTIP_TESTS "Build tooltip formatting and wrapping checks" OFF)
if(CITY_BUILD_TOOLTIP_TESTS)
    enable_testing()
    add_executable(CityTooltipTextTests tests/tooltip_text_tests.cpp src/ui_tooltip_text.cpp)
    target_compile_features(CityTooltipTextTests PRIVATE cxx_std_20)
    target_include_directories(CityTooltipTextTests PRIVATE src)
    add_test(NAME CityTooltipTextTests COMMAND CityTooltipTextTests)
endif()

option(CITY_BUILD_TOOL_TESTS "Build tool lifecycle and zone selection checks" OFF)
if(CITY_BUILD_TOOL_TESTS)
    enable_testing()
    add_executable(CityToolTests tests/tool_tests.cpp
        src/tool.cpp src/toolcontroller.cpp src/zone_tool.cpp src/world.cpp src/terrain.cpp)
    target_compile_features(CityToolTests PRIVATE cxx_std_20)
    target_include_directories(CityToolTests PRIVATE src)
    add_test(NAME CityToolTests COMMAND CityToolTests)
endif()

option(CITY_BUILD_PICKING_TESTS "Build terrain cursor picking checks" OFF)
if(CITY_BUILD_PICKING_TESTS)
    enable_testing()
    add_executable(CityPickingTests tests/terrain_picking_tests.cpp
        src/terrain_picking.cpp src/camera.cpp src/terrain.cpp src/world.cpp
        src/water.cpp src/terrain_skirt.cpp)
    target_compile_features(CityPickingTests PRIVATE cxx_std_20)
    target_include_directories(CityPickingTests PRIVATE src)
    add_test(NAME CityPickingTests COMMAND CityPickingTests)
endif()

option(CITY_BUILD_MAP_SIZE_TESTS "Build selectable map-size regression checks" OFF)
if(CITY_BUILD_MAP_SIZE_TESTS)
    enable_testing()
    add_executable(CityMapSizeTests tests/map_size_tests.cpp
        src/city.cpp src/terrain.cpp src/world.cpp src/terrain_skirt.cpp src/water.cpp
        src/minimap_map.cpp src/camera.cpp)
    target_compile_features(CityMapSizeTests PRIVATE cxx_std_20)
    target_include_directories(CityMapSizeTests PRIVATE src)
    add_test(NAME CityMapSizeTests COMMAND CityMapSizeTests)
endif()

option(CITY_BUILD_UI_MANAGER_TESTS "Build UI manager input/render lifetime checks" OFF)
if(CITY_BUILD_UI_MANAGER_TESTS)
    enable_testing()
    add_executable(CityUiManagerTests tests/ui_manager_tests.cpp
        src/ui_manager.cpp src/ui_manager_draw.cpp src/ui.cpp src/ui_terrain.cpp src/ui_transport.cpp src/ui_zoning.cpp src/ui_utilities.cpp src/ui_government.cpp src/ui_tool_buttons.cpp src/ui_layout.cpp
        src/ui_manager_windows.cpp src/ui_window.cpp src/ui_window_draw.cpp src/ui_text_input.cpp
        src/ui_button.cpp src/ui_buttons.cpp src/ui_buttons_draw.cpp
        src/ui_minimap.cpp src/ui_minimap_draw.cpp src/ui_text.cpp
        src/pan_overlay.cpp src/minimap_map.cpp src/camera.cpp)
    target_compile_features(CityUiManagerTests PRIVATE cxx_std_20)
    # Device/font doubles only need the public headers, not the SDL_ttf binary.
    target_include_directories(CityUiManagerTests PRIVATE src
        $<TARGET_PROPERTY:SDL3_ttf::SDL3_ttf,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(CityUiManagerTests PRIVATE SDL3::Headers)
    add_test(NAME CityUiManagerTests COMMAND CityUiManagerTests)
endif()

option(CITY_BUILD_UI_TESTS "Build UI visibility and input tests" OFF)
if(CITY_BUILD_UI_TESTS)
    enable_testing()
    add_executable(CityUiVisibilityTests tests/ui_visibility_tests.cpp src/ui.cpp src/ui_terrain.cpp src/ui_transport.cpp src/ui_zoning.cpp src/ui_utilities.cpp src/ui_government.cpp src/ui_tool_buttons.cpp src/ui_layout.cpp
        src/ui_button.cpp src/ui_buttons.cpp src/ui_buttons_draw.cpp src/ui_minimap.cpp src/camera.cpp)
    target_compile_features(CityUiVisibilityTests PRIVATE cxx_std_20)
    target_include_directories(CityUiVisibilityTests PRIVATE src)
    target_link_libraries(CityUiVisibilityTests PRIVATE SDL3::Headers)
    add_test(NAME CityUiVisibilityTests COMMAND CityUiVisibilityTests)
endif()

option(CITY_BUILD_MINIMAP_TESTS "Build minimap map and texture tests" OFF)
if(CITY_BUILD_MINIMAP_TESTS)
    enable_testing()
    add_executable(CityMinimapMapTests tests/minimap_map_tests.cpp src/minimap_map.cpp src/terrain.cpp src/world.cpp)
    target_compile_features(CityMinimapMapTests PRIVATE cxx_std_20)
    target_include_directories(CityMinimapMapTests PRIVATE src)
    add_test(NAME CityMinimapMapTests COMMAND CityMinimapMapTests)

    add_executable(CityMinimapTextureTests tests/minimap_texture_tests.cpp src/texture.cpp
        src/terrain_material.cpp src/ui_minimap.cpp src/ui_minimap_draw.cpp src/ui_layout.cpp src/camera.cpp)
    target_compile_features(CityMinimapTextureTests PRIVATE cxx_std_20)
    target_include_directories(CityMinimapTextureTests PRIVATE src)
    target_link_libraries(CityMinimapTextureTests PRIVATE SDL3::Headers)
    add_test(NAME CityMinimapTextureTests COMMAND CityMinimapTextureTests)
endif()

option(CITY_BUILD_TESTS "Build terrain, camera panning, and texture manager tests" OFF)
option(CITY_BUILD_SKIRT_TESTS "Build the standalone terrain skirt tests" OFF)
if(CITY_BUILD_TESTS OR CITY_BUILD_SKIRT_TESTS)
    enable_testing()
    add_executable(CitySkirtTests tests/skirt_tests.cpp src/terrain.cpp src/world.cpp src/terrain_skirt.cpp)
    target_compile_features(CitySkirtTests PRIVATE cxx_std_20)
    target_include_directories(CitySkirtTests PRIVATE src)
    add_test(NAME CitySkirtTests COMMAND CitySkirtTests)
endif()
option(CITY_BUILD_LAKE_TESTS "Build the standalone lake and water mesh tests" OFF)
if(CITY_BUILD_TESTS OR CITY_BUILD_LAKE_TESTS)
    enable_testing()
    add_executable(CityLakeTests tests/lake_tests.cpp src/terrain.cpp src/world.cpp src/water.cpp)
    target_compile_features(CityLakeTests PRIVATE cxx_std_20)
    target_include_directories(CityLakeTests PRIVATE src)
    add_test(NAME CityLakeTests COMMAND CityLakeTests)
endif()
if(CITY_BUILD_TESTS)
    enable_testing()
    add_executable(CityPanTests tests/pan_tests.cpp src/camera.cpp src/ui_layout.cpp)
    target_compile_features(CityPanTests PRIVATE cxx_std_20)
    target_include_directories(CityPanTests PRIVATE src)
    add_test(NAME CityPanTests COMMAND CityPanTests)

    # Test doubles exercise cache/cleanup behavior without creating a GPU device.
    add_executable(CityTextureTests tests/texture_tests.cpp src/texture.cpp)
    target_compile_features(CityTextureTests PRIVATE cxx_std_20)
    target_include_directories(CityTextureTests PRIVATE src)
    target_link_libraries(CityTextureTests PRIVATE SDL3::Headers)
    add_test(NAME CityTextureTests COMMAND CityTextureTests)

    add_executable(CityTerrainTests tests/terrain_tests.cpp src/terrain.cpp src/world.cpp)
    target_compile_features(CityTerrainTests PRIVATE cxx_std_20)
    target_include_directories(CityTerrainTests PRIVATE src)
    add_test(NAME CityTerrainTests COMMAND CityTerrainTests)
endif()
