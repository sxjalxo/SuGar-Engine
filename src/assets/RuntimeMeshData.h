#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// CPU-side description of a game-generated mesh, the boundary type for the runtime-mesh
// seam (DevDocs/DESIGN_RUNTIME_MESH.md). Plain math/POD data only — glm is already a Core
// dependency, and no engine type (Mesh/Vertex/VkBuffer) appears here — so it crosses the
// Core→Engine boundary the same way `std::string` + `AssetHandle` do for acquisition.
//
// The engine COPIES this into its own vertex format and uploads it in
// AssetGateway::createMesh; ownership of these vectors stays with the caller, which may
// discard them the instant createMesh returns.
struct RuntimeMeshData {
    std::vector<glm::vec3> positions;  // required
    std::vector<glm::vec3> normals;    // required, same length as positions
    std::vector<glm::vec2> uvs;        // optional; empty => (0,0) per vertex
    std::vector<uint32_t>  indices;    // required; triangle list, each index < positions.size()
};
