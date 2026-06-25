#pragma once

#include <string>

class Mesh;
struct GltfModelData;

// Loads glTF 2.0 (.gltf / .glb) data into engine-owned structures. tinygltf is
// used for parsing only and is fully confined to GltfLoader.cpp — no tinygltf
// type appears in this header or anywhere else in the engine.
class GltfLoader {
public:
    // Flatten every mesh/primitive into a single Mesh (model-local space).
    static void loadGltf(const std::string& path, Mesh& mesh);

    // Flatten only the primitives of one glTF mesh (by index) into a Mesh.
    // Backs the "<path>#<meshIndex>" resource key.
    static void loadGltfMesh(const std::string& path, int meshIndex, Mesh& mesh);

    // Parse the scene graph structure (nodes, transforms, mesh refs, materials)
    // into an engine GltfModelData. No geometry is read here.
    static void loadModel(const std::string& path, GltfModelData& outModel);
};
