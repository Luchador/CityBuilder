include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EmbedShaders.cmake")

add_library(CityShaderSources INTERFACE)
target_include_directories(CityShaderSources INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

city_embed_shader_group(CityShaderSources world_shaders.hpp
    VertexShaderSource grid.vert.hlsl
    FragmentShaderSource grid.frag.hlsl
    TerrainVertexShaderSource terrain.vert.hlsl
    TerrainFragmentShaderSource terrain.frag.hlsl
    TerrainSkirtVertexShaderSource terrain_skirt.vert.hlsl
    TerrainSkirtFragmentShaderSource terrain_skirt.frag.hlsl
    WaterVertexShaderSource water.vert.hlsl
    WaterFragmentShaderSource water.frag.hlsl)

city_embed_shader_group(CityShaderSources road_shaders.hpp
    RoadVertexShaderSource road.vert.hlsl
    RoadFragmentShaderSource road.frag.hlsl)

city_embed_shader_group(CityShaderSources ui_shaders.hpp
    UiVertexShaderSource ui/panel.vert.hlsl
    UiFragmentShaderSource ui/panel.frag.hlsl
    TextVertexShaderSource ui/text.vert.hlsl
    TextFragmentShaderSource ui/text.frag.hlsl
    UiImageVertexShaderSource ui/image.vert.hlsl
    UiImageFragmentShaderSource ui/image.frag.hlsl
    UiToolButtonFragmentShaderSource ui/tool_button.frag.hlsl)

# Show editable sources in IDEs without asking MSBuild to compile HLSL itself.
# Runtime compilation still uses the game's existing Shader Model 5.1 pipeline.
file(GLOB_RECURSE CITY_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli")
target_sources(CityShaderSources INTERFACE
    FILE_SET shader_sources TYPE HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES ${CITY_SHADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Shaders" FILES ${CITY_SHADER_FILES})
