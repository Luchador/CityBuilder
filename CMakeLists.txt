cmake_minimum_required(VERSION 3.24)
project(CityBuilder LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This starter uses Direct3D 12 and the Windows shader compiler.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

add_executable(CityBuilder src/main.cpp src/terrain.hpp src/shaders.hpp src/ui.cpp src/ui.hpp
    src/pan.cpp src/pan.hpp src/pan_overlay.cpp src/pan_overlay.hpp)
target_compile_features(CityBuilder PRIVATE cxx_std_20)
target_compile_definitions(CityBuilder PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
# D3DCompile is supplied by the Windows SDK; no separate shader tools needed.
target_link_libraries(CityBuilder PRIVATE SDL3::SDL3-static d3dcompiler)
if(MSVC)
    target_compile_options(CityBuilder PRIVATE /W4 /permissive-)
endif()

# Keep data beside the executable, independent of the launch working directory.
# This target also runs when only the PNGs change and the EXE needs no relink.
set(CITY_UI_ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/dir_circle.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/omni_arrows.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/dir_arrow.png")
add_custom_target(copy_ui_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:CityBuilder>/assets/ui"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${CITY_UI_ASSETS} "$<TARGET_FILE_DIR:CityBuilder>/assets/ui"
    DEPENDS ${CITY_UI_ASSETS}
    VERBATIM)
add_dependencies(CityBuilder copy_ui_assets)

option(CITY_BUILD_TESTS "Build camera panning tests" OFF)
if(CITY_BUILD_TESTS)
    enable_testing()
    add_executable(CityPanTests tests/pan_tests.cpp src/pan.cpp src/ui.cpp)
    target_compile_features(CityPanTests PRIVATE cxx_std_20)
    target_include_directories(CityPanTests PRIVATE src)
    add_test(NAME CityPanTests COMMAND CityPanTests)
endif()
