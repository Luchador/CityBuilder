#pragma once

#include "road.hpp"
#include "static_mesh.hpp"

// Applies road-specific bounds, UVs and optional diagonal approach extension
// after the GLB importer has baked the authored node transforms.
StaticMesh LoadRoadMesh(const std::filesystem::path& path, const RoadAsset& asset);
