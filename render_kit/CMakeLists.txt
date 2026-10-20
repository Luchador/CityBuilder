cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CitiesUnlimitedRenderKit LANGUAGES C CXX)
    option(RENDER_KIT_USE_INSTALLED_SDL "Use an existing SDL3 package" OFF)
    if(RENDER_KIT_USE_INSTALLED_SDL)
        find_package(SDL3 3.4 CONFIG REQUIRED)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory(../vendored/SDL "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)
    endif()
endif()

include(FetchContent)
# Immutable revisions keep imports and editor UI reproducible.
FetchContent_Declare(renderkit_cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 85cd62382dfea638278962690cf515023f33ed00
    SOURCE_SUBDIR render_kit_no_subproject)
FetchContent_MakeAvailable(renderkit_cgltf)
FetchContent_Declare(renderkit_ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG 5955c5c0b042ac2dc6f32955ab206aaec0620cfc
    SOURCE_SUBDIR render_kit_no_subproject)
FetchContent_MakeAvailable(renderkit_ufbx)
add_library(RenderKitUfbx STATIC "${renderkit_ufbx_SOURCE_DIR}/ufbx.c")
target_compile_features(RenderKitUfbx PRIVATE c_std_99)
target_include_directories(RenderKitUfbx SYSTEM PUBLIC "${renderkit_ufbx_SOURCE_DIR}")
find_package(Threads REQUIRED)
FetchContent_Declare(renderkit_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72
    SOURCE_SUBDIR render_kit_no_subproject)
FetchContent_MakeAvailable(renderkit_json)

add_library(RenderKitCore STATIC
    src/scene.cpp src/import_fbx.cpp src/scene_file.cpp src/render.cpp src/export.cpp ../src/camera.cpp)
target_compile_features(RenderKitCore PUBLIC cxx_std_20)
target_include_directories(RenderKitCore PUBLIC src ../src)
target_include_directories(RenderKitCore SYSTEM PRIVATE "${renderkit_cgltf_SOURCE_DIR}")
target_include_directories(RenderKitCore SYSTEM PRIVATE "${renderkit_json_SOURCE_DIR}/single_include")
target_link_libraries(RenderKitCore PUBLIC SDL3::SDL3 Threads::Threads)
target_link_libraries(RenderKitCore PRIVATE RenderKitUfbx)
target_compile_definitions(RenderKitCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

FetchContent_Declare(renderkit_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 6d910d5487d11ca567b61c7824b0c78c569d62f0
    SOURCE_SUBDIR render_kit_no_subproject)
FetchContent_MakeAvailable(renderkit_imgui)
add_library(RenderKitImGui STATIC
    "${renderkit_imgui_SOURCE_DIR}/imgui.cpp"
    "${renderkit_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${renderkit_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${renderkit_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${renderkit_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${renderkit_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(RenderKitImGui SYSTEM PUBLIC
    "${renderkit_imgui_SOURCE_DIR}" "${renderkit_imgui_SOURCE_DIR}/backends")
target_link_libraries(RenderKitImGui PUBLIC SDL3::SDL3)
target_compile_features(RenderKitImGui PUBLIC cxx_std_20)

add_executable(RenderKit src/main.cpp)
target_link_libraries(RenderKit PRIVATE RenderKitCore RenderKitImGui)
target_compile_definitions(RenderKit PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(RenderKit PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET RenderKit POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RenderKit>/render-kit-data"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/SourceSans3-Regular.ttf"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/SourceSans3-Regular.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/LICENSE.md"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/font-license.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${renderkit_imgui_SOURCE_DIR}/LICENSE.txt"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/imgui-license.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${renderkit_cgltf_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/cgltf-license.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${renderkit_ufbx_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/ufbx-license.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${renderkit_json_SOURCE_DIR}/LICENSE.MIT"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/json-license.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../vendored/SDL/LICENSE.txt"
        "$<TARGET_FILE_DIR:RenderKit>/render-kit-data/sdl-license.txt"
    VERBATIM)
if(MSVC)
    target_compile_options(RenderKitCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(RenderKit PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(RenderKitCore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(RenderKit PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(RENDER_KIT_BUILD_TESTS "Build importer, cutout and sprite export regression checks" OFF)
if(RENDER_KIT_BUILD_TESTS)
    enable_testing()
    add_executable(RenderKitTests tests/render_kit_tests.cpp tests/fbx_tests.cpp tests/scene_file_tests.cpp tests/ambient_occlusion_tests.cpp)
    target_include_directories(RenderKitTests SYSTEM PRIVATE "${renderkit_json_SOURCE_DIR}/single_include")
    target_link_libraries(RenderKitTests PRIVATE RenderKitCore)
    target_compile_definitions(RenderKitTests PRIVATE RENDER_KIT_FBX_TEST_DATA="${renderkit_ufbx_SOURCE_DIR}/data")
    if(MSVC)
        target_compile_options(RenderKitTests PRIVATE /utf-8)
    endif()
    add_test(NAME RenderKitTests COMMAND RenderKitTests)
endif()
