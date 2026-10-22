#pragma once

#include <cstddef>

// Identifies a shared loaded effect, not a specific tool. Road, highway and
// pedestrian tools can all select BuildRoad. Add more IDs before Count.
enum class ToolSuccessSound : std::size_t
{
    None,
    PlaceZone,
    BuildRoad,
    Count
};

// Report the result of applying an edit, not the hover preview. NoChange also
// covers empty/canceled requests and edits whose desired state already exists.
enum class ToolOutcome
{
    NoChange,
    Success,
    PartialSuccess,
    Rejected
};
