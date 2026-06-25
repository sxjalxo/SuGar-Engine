#include "assets/ModelLoader.h"
#include "rendering/Mesh.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ObjVertexKey {
    int positionIndex = -1;
    int normalIndex = -1;
    int texCoordIndex = -1;

    bool operator==(const ObjVertexKey& other) const {
        return positionIndex == other.positionIndex &&
               normalIndex == other.normalIndex &&
               texCoordIndex == other.texCoordIndex;
    }
};

struct ObjVertexKeyHasher {
    size_t operator()(const ObjVertexKey& key) const {
        const auto posHash = std::hash<int>{}(key.positionIndex);
        const auto normalHash = std::hash<int>{}(key.normalIndex);
        const auto texHash = std::hash<int>{}(key.texCoordIndex);
        size_t seed = posHash;
        seed ^= normalHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= texHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

int resolveObjIndex(int index, size_t count) {
    if (index > 0) {
        return index - 1;
    }

    if (index < 0) {
        return static_cast<int>(count) + index;
    }

    return -1;
}

ObjVertexKey parseFaceVertex(
    const std::string& token,
    size_t positionCount,
    size_t texCoordCount,
    size_t normalCount
) {
    std::array<std::string, 3> parts{};
    size_t partIndex = 0;
    size_t start = 0;

    while (start <= token.size() && partIndex < parts.size()) {
        const size_t slash = token.find('/', start);
        if (slash == std::string::npos) {
            parts[partIndex++] = token.substr(start);
            break;
        }

        parts[partIndex++] = token.substr(start, slash - start);
        start = slash + 1;
    }

    ObjVertexKey key{};
    if (!parts[0].empty()) {
        key.positionIndex = resolveObjIndex(std::stoi(parts[0]), positionCount);
    }

    if (!parts[1].empty()) {
        key.texCoordIndex = resolveObjIndex(std::stoi(parts[1]), texCoordCount);
    }

    if (!parts[2].empty()) {
        key.normalIndex = resolveObjIndex(std::stoi(parts[2]), normalCount);
    }

    if (key.positionIndex < 0 || key.positionIndex >= static_cast<int>(positionCount)) {
        throw std::runtime_error("OBJ face references an invalid position index.");
    }

    if (key.normalIndex >= static_cast<int>(normalCount)) {
        throw std::runtime_error("OBJ face references an invalid normal index.");
    }

    if (key.texCoordIndex >= static_cast<int>(texCoordCount)) {
        throw std::runtime_error("OBJ face references an invalid texture coordinate index.");
    }

    return key;
}

uint32_t appendVertex(
    const ObjVertexKey& key,
    const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals,
    const std::vector<Vec2>& texCoords,
    std::vector<Vertex>& vertices,
    std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHasher>& uniqueVertices
) {
    const auto existingVertex = uniqueVertices.find(key);
    if (existingVertex != uniqueVertices.end()) {
        return existingVertex->second;
    }

    const Vec3& position = positions[static_cast<size_t>(key.positionIndex)];

    Vertex vertex{};
    vertex.pos[0] = position.x;
    vertex.pos[1] = position.y;
    vertex.pos[2] = position.z;

    if (key.normalIndex >= 0) {
        const Vec3& normal = normals[static_cast<size_t>(key.normalIndex)];
        vertex.normal[0] = normal.x;
        vertex.normal[1] = normal.y;
        vertex.normal[2] = normal.z;
    } else {
        vertex.normal[0] = 0.0f;
        vertex.normal[1] = 0.0f;
        vertex.normal[2] = 1.0f;
    }

    if (key.texCoordIndex >= 0) {
        const Vec2& uv = texCoords[static_cast<size_t>(key.texCoordIndex)];
        vertex.uv[0] = uv.x;
        vertex.uv[1] = 1.0f - uv.y;
    } else {
        vertex.uv[0] = 0.0f;
        vertex.uv[1] = 0.0f;
    }

    const uint32_t vertexIndex = static_cast<uint32_t>(vertices.size());
    vertices.push_back(vertex);
    uniqueVertices[key] = vertexIndex;

    return vertexIndex;
}
} // namespace

void ModelLoader::loadObj(const std::string& path, Mesh& mesh) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open OBJ model: " + path);
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHasher> uniqueVertices;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream stream(line);
        std::string type;
        stream >> type;

        if (type == "v") {
            Vec3 position{};
            stream >> position.x >> position.y >> position.z;
            positions.push_back(position);
        } else if (type == "vn") {
            Vec3 normal{};
            stream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (type == "vt") {
            Vec2 uv{};
            stream >> uv.x >> uv.y;
            texCoords.push_back(uv);
        } else if (type == "f") {
            std::vector<ObjVertexKey> faceVertices;
            std::string token;

            while (stream >> token) {
                faceVertices.push_back(parseFaceVertex(token, positions.size(), texCoords.size(), normals.size()));
            }

            if (faceVertices.size() < 3) {
                throw std::runtime_error("OBJ face must contain at least 3 vertices.");
            }

            for (size_t i = 1; i + 1 < faceVertices.size(); i++) {
                indices.push_back(appendVertex(faceVertices[0], positions, normals, texCoords, vertices, uniqueVertices));
                indices.push_back(appendVertex(faceVertices[i], positions, normals, texCoords, vertices, uniqueVertices));
                indices.push_back(appendVertex(faceVertices[i + 1], positions, normals, texCoords, vertices, uniqueVertices));
            }
        }
    }

    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("OBJ model did not contain renderable geometry: " + path);
    }

    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
}
