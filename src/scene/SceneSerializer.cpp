#include "scene/SceneSerializer.h"
#include "assets/ResourceManager.h"
#include "audio/AudioClip.h"
#include "ecs/Registry.h"
#include "rendering/Material.h"
#include "rendering/Mesh.h"
#include "rendering/Texture.h"
#include "scene/Light.h"
#include "scene/TransformMath.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct JsonValue {
    enum class Type {
        Null,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text(text) {}

    JsonValue parse() {
        JsonValue value = parseValue();
        skipWhitespace();
        if (position != text.size()) {
            throw std::runtime_error("unexpected trailing characters in scene JSON.");
        }
        return value;
    }

private:
    JsonValue parseValue() {
        skipWhitespace();
        if (position >= text.size()) {
            throw std::runtime_error("unexpected end of scene JSON.");
        }

        const char current = text[position];
        if (current == '{') {
            return parseObject();
        }
        if (current == '[') {
            return parseArray();
        }
        if (current == '"') {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = parseString();
            return value;
        }
        if (current == '-' || std::isdigit(static_cast<unsigned char>(current))) {
            JsonValue value;
            value.type = JsonValue::Type::Number;
            value.number = parseNumber();
            return value;
        }
        if (text.compare(position, 4, "null") == 0) {
            position += 4;
            return {};
        }
        // Booleans are stored as numbers (1/0) so the rest of the reader can treat
        // them uniformly via getFloatValue/getBoolValue.
        if (text.compare(position, 4, "true") == 0) {
            position += 4;
            JsonValue value;
            value.type = JsonValue::Type::Number;
            value.number = 1.0;
            return value;
        }
        if (text.compare(position, 5, "false") == 0) {
            position += 5;
            JsonValue value;
            value.type = JsonValue::Type::Number;
            value.number = 0.0;
            return value;
        }

        throw std::runtime_error("unsupported JSON token in scene file.");
    }

    JsonValue parseObject() {
        expect('{');

        JsonValue value;
        value.type = JsonValue::Type::Object;

        skipWhitespace();
        if (match('}')) {
            return value;
        }

        while (true) {
            skipWhitespace();
            const std::string key = parseString();
            skipWhitespace();
            expect(':');
            value.object.emplace(key, parseValue());
            skipWhitespace();

            if (match('}')) {
                break;
            }

            expect(',');
        }

        return value;
    }

    JsonValue parseArray() {
        expect('[');

        JsonValue value;
        value.type = JsonValue::Type::Array;

        skipWhitespace();
        if (match(']')) {
            return value;
        }

        while (true) {
            value.array.push_back(parseValue());
            skipWhitespace();

            if (match(']')) {
                break;
            }

            expect(',');
        }

        return value;
    }

    std::string parseString() {
        expect('"');
        std::string result;

        while (position < text.size()) {
            const char current = text[position++];
            if (current == '"') {
                return result;
            }

            if (current == '\\') {
                if (position >= text.size()) {
                    throw std::runtime_error("unterminated escape sequence in scene JSON.");
                }

                const char escaped = text[position++];
                switch (escaped) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default:
                        throw std::runtime_error("unsupported escape sequence in scene JSON.");
                }
                continue;
            }

            result.push_back(current);
        }

        throw std::runtime_error("unterminated string in scene JSON.");
    }

    double parseNumber() {
        const size_t start = position;

        if (text[position] == '-') {
            position++;
        }

        while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
            position++;
        }

        if (position < text.size() && text[position] == '.') {
            position++;
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
                position++;
            }
        }

        if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
            position++;
            if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
                position++;
            }
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
                position++;
            }
        }

        return std::stod(text.substr(start, position - start));
    }

    void skipWhitespace() {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
            position++;
        }
    }

    bool match(char expected) {
        if (position < text.size() && text[position] == expected) {
            position++;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        skipWhitespace();
        if (position >= text.size() || text[position] != expected) {
            throw std::runtime_error("unexpected scene JSON structure.");
        }
        position++;
    }

    const std::string& text;
    size_t position = 0;
};

const JsonValue& requireObjectField(
    const std::unordered_map<std::string, JsonValue>& object,
    const std::string& key
) {
    const auto value = object.find(key);
    if (value == object.end()) {
        throw std::runtime_error("missing required scene JSON field: " + key);
    }
    return value->second;
}

const JsonValue* findObjectField(
    const std::unordered_map<std::string, JsonValue>& object,
    const std::string& key
) {
    const auto value = object.find(key);
    return value == object.end() ? nullptr : &value->second;
}

const std::unordered_map<std::string, JsonValue>& requireObject(const JsonValue& value, const std::string& name) {
    if (value.type != JsonValue::Type::Object) {
        throw std::runtime_error(name + " must be a JSON object.");
    }
    return value.object;
}

const std::vector<JsonValue>& requireArray(const JsonValue& value, const std::string& name) {
    if (value.type != JsonValue::Type::Array) {
        throw std::runtime_error(name + " must be a JSON array.");
    }
    return value.array;
}

std::string getStringValue(const JsonValue& value, const std::string& name) {
    if (value.type != JsonValue::Type::String) {
        throw std::runtime_error(name + " must be a JSON string.");
    }
    return value.string;
}

float getFloatValue(const JsonValue& value, const std::string& name) {
    if (value.type != JsonValue::Type::Number) {
        throw std::runtime_error(name + " must be a JSON number.");
    }
    return static_cast<float>(value.number);
}

int getIntValue(const JsonValue& value, const std::string& name) {
    if (value.type != JsonValue::Type::Number) {
        throw std::runtime_error(name + " must be a JSON number.");
    }
    return static_cast<int>(value.number);
}

bool getBoolValue(const JsonValue& value, const std::string& name) {
    return getFloatValue(value, name) != 0.0f;
}

glm::vec3 parseVec3(const JsonValue& value, const std::string& name) {
    const auto& array = requireArray(value, name);
    if (array.size() != 3) {
        throw std::runtime_error(name + " must contain exactly three numbers.");
    }

    return {
        getFloatValue(array[0], name + "[0]"),
        getFloatValue(array[1], name + "[1]"),
        getFloatValue(array[2], name + "[2]")
    };
}

// Rotations are stored as a quaternion [x, y, z, w] (glTF order). Legacy scenes
// wrote a 3-component Euler vector here; those still load and convert exactly.
glm::quat parseRotation(const JsonValue& value, const std::string& name) {
    const auto& array = requireArray(value, name);
    if (array.size() == 3) {
        return quatFromEulerXYZ(parseVec3(value, name));
    }
    if (array.size() != 4) {
        throw std::runtime_error(name + " must contain three (Euler) or four (quaternion) numbers.");
    }

    return glm::normalize(glm::quat(
        getFloatValue(array[3], name + "[3]"), // w
        getFloatValue(array[0], name + "[0]"), // x
        getFloatValue(array[1], name + "[1]"), // y
        getFloatValue(array[2], name + "[2]")  // z
    ));
}

std::string escapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }

    return escaped;
}

void writeIndent(std::ostream& output, int indentLevel) {
    for (int i = 0; i < indentLevel; i++) {
        output << "  ";
    }
}

void writeVec3(std::ostream& output, const glm::vec3& value) {
    output << "[" << value.x << ", " << value.y << ", " << value.z << "]";
}

// Quaternion as [x, y, z, w] to match glTF's component order.
void writeQuat(std::ostream& output, const glm::quat& value) {
    output << "[" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << "]";
}

AssetHandle loadMeshWithFallback(const std::string& meshKey) {
    if (meshKey.empty()) {
        return INVALID_HANDLE;
    }

    try {
        return ResourceManager::loadMesh(meshKey);
    } catch (...) {
        return ResourceManager::loadMesh("assets/models/textured_cube.obj");
    }
}

// Audio has no sensible default clip, so a missing/undecodable file just yields
// an empty handle (a silent source) rather than substituting something.
AssetHandle loadAudioClipWithFallback(const std::string& clipKey) {
    if (clipKey.empty()) {
        return INVALID_HANDLE;
    }

    try {
        return ResourceManager::loadAudioClip(clipKey);
    } catch (...) {
        return INVALID_HANDLE;
    }
}

AssetHandle loadTextureWithFallback(const std::string& textureKey) {
    if (textureKey.empty()) {
        return ResourceManager::loadTexture(ResourceManager::CheckerboardTextureId);
    }

    try {
        return ResourceManager::loadTexture(textureKey);
    } catch (...) {
        return ResourceManager::loadTexture(ResourceManager::CheckerboardTextureId);
    }
}

// Writes a single entity as a JSON object (indent level 2) WITHOUT a trailing
// comma/newline after the closing brace — the caller adds the separator. Shared
// by scene save and prefab save so component serialization lives in one place.
void writeEntityObject(std::ostream& output, const Registry& registry, Entity entity, int parentIndex) {
    const auto& transform = registry.transforms.get(entity).transform;
    const std::string name = registry.names.has(entity)
        ? registry.names.get(entity).name
        : ("Entity " + std::to_string(entity));

    writeIndent(output, 2);
    output << "{\n";

    writeIndent(output, 3);
    output << "\"name\": \"" << escapeJsonString(name) << "\",\n";
    writeIndent(output, 3);
    output << "\"parent\": " << parentIndex << ",\n";

    writeIndent(output, 3);
    output << "\"transform\": {\n";
    writeIndent(output, 4);
    output << "\"pos\": ";
    writeVec3(output, transform.position);
    output << ",\n";
    writeIndent(output, 4);
    output << "\"rot\": ";
    writeQuat(output, transform.rotation);
    output << ",\n";
    writeIndent(output, 4);
    output << "\"scale\": ";
    writeVec3(output, transform.scale);
    output << "\n";
    writeIndent(output, 3);
    output << "},\n";

    writeIndent(output, 3);
    output << "\"mesh\": \"";
    const auto mesh = registry.meshes.has(entity)
        ? ResourceManager::getMesh(registry.meshes.get(entity).mesh)
        : std::shared_ptr<Mesh>{};
    output << escapeJsonString(mesh ? mesh->getResourceKey() : "");
    output << "\",\n";

    writeIndent(output, 3);
    output << "\"material\": {\n";
    writeIndent(output, 4);
    output << "\"albedo\": \"";
    const auto texture = registry.materials.has(entity)
        ? ResourceManager::getTexture(registry.materials.get(entity).material.albedo)
        : std::shared_ptr<Texture>{};
    output << escapeJsonString(texture ? texture->getResourceKey() : "");
    output << "\",\n";
    writeIndent(output, 4);
    output << "\"metallic\": "
           << (registry.materials.has(entity) ? registry.materials.get(entity).material.metallic : 0.0f)
           << ",\n";
    writeIndent(output, 4);
    output << "\"roughness\": "
           << (registry.materials.has(entity) ? registry.materials.get(entity).material.roughness : 0.5f)
           << ",\n";
    writeIndent(output, 4);
    output << "\"ao\": "
           << (registry.materials.has(entity) ? registry.materials.get(entity).material.ao : 1.0f)
           << "\n";

    const bool hasScript = registry.scripts.has(entity) &&
                           !registry.scripts.get(entity).behavior.empty();
    const bool hasBody = registry.rigidBodies.has(entity);
    const bool hasCollider = registry.colliders.has(entity);
    const bool hasPrefab = registry.prefabInstances.has(entity) &&
                           !registry.prefabInstances.get(entity).prefab.empty();
    const bool hasAudioSource = registry.audioSources.has(entity) &&
                                registry.audioSources.get(entity).clip != INVALID_HANDLE;
    const bool hasAudioListener = registry.audioListeners.has(entity);
    // Runtime UI state (Phase 16A). Authoritative → serialized, so it survives
    // snapshot restore / time travel (RULES.md Rule 21). Only the UIRoot-style
    // entities carry these; everything else omits them.
    const bool hasUIScreen = registry.uiScreens.has(entity);
    const bool hasFocus = registry.focus.has(entity);
    const bool hasTextInput = registry.textInputs.has(entity);

    // Optional components are emitted in a comma-chain; each one needs to know
    // whether any later optional follows it. `tailAfter*` capture that.
    const bool tailAfterMaterial = hasScript || hasBody || hasCollider || hasPrefab ||
                                   hasAudioSource || hasAudioListener || hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterScript = hasBody || hasCollider || hasPrefab || hasAudioSource ||
                                 hasAudioListener || hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterBody = hasCollider || hasPrefab || hasAudioSource || hasAudioListener ||
                               hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterCollider = hasPrefab || hasAudioSource || hasAudioListener || hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterPrefab = hasAudioSource || hasAudioListener || hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterAudioSource = hasAudioListener || hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterAudioListener = hasUIScreen || hasFocus || hasTextInput;
    const bool tailAfterFocus = hasTextInput;
    const bool tailAfterUIScreen = hasFocus || hasTextInput;

    writeIndent(output, 3);
    output << (tailAfterMaterial ? "},\n" : "}\n");

    // Optional: named behavior. `started` is intentionally not written —
    // it always restores to false (behaviors only start in Play mode).
    if (hasScript) {
        writeIndent(output, 3);
        output << "\"script\": \""
               << escapeJsonString(registry.scripts.get(entity).behavior)
               << "\"" << (tailAfterScript ? ",\n" : "\n");
    }

    // Optional: rigid body. `velocity` is written so an authored initial
    // velocity survives; transient force is not part of the body's state.
    if (hasBody) {
        const auto& body = registry.rigidBodies.get(entity);
        writeIndent(output, 3);
        output << "\"rigidbody\": {\n";
        writeIndent(output, 4);
        output << "\"velocity\": ";
        writeVec3(output, body.velocity);
        output << ",\n";
        writeIndent(output, 4);
        output << "\"mass\": " << body.mass << ",\n";
        writeIndent(output, 4);
        output << "\"restitution\": " << body.restitution << ",\n";
        writeIndent(output, 4);
        output << "\"friction\": " << body.friction << ",\n";
        writeIndent(output, 4);
        output << "\"useGravity\": " << (body.useGravity ? "true" : "false") << ",\n";
        writeIndent(output, 4);
        output << "\"isStatic\": " << (body.isStatic ? "true" : "false") << "\n";
        writeIndent(output, 3);
        output << "}" << (tailAfterBody ? ",\n" : "\n");
    }

    // Optional: collider.
    if (hasCollider) {
        const auto& collider = registry.colliders.get(entity);
        writeIndent(output, 3);
        output << "\"collider\": {\n";
        writeIndent(output, 4);
        output << "\"type\": \""
               << (collider.type == ColliderType::Sphere ? "sphere" : "box")
               << "\",\n";
        writeIndent(output, 4);
        output << "\"halfExtents\": ";
        writeVec3(output, collider.halfExtents);
        output << ",\n";
        writeIndent(output, 4);
        output << "\"radius\": " << collider.radius << "\n";
        writeIndent(output, 3);
        output << "}" << (tailAfterCollider ? ",\n" : "\n");
    }

    // Optional: prefab instance link.
    if (hasPrefab) {
        writeIndent(output, 3);
        output << "\"prefab\": \""
               << escapeJsonString(registry.prefabInstances.get(entity).prefab)
               << "\"" << (tailAfterPrefab ? ",\n" : "\n");
    }

    // Optional: audio source. Runtime fields (started/voice) are not written.
    if (hasAudioSource) {
        const auto& source = registry.audioSources.get(entity);
        const auto clip = ResourceManager::getAudioClip(source.clip);
        writeIndent(output, 3);
        output << "\"audiosource\": {\n";
        writeIndent(output, 4);
        output << "\"clip\": \"" << escapeJsonString(clip ? clip->getResourceKey() : "") << "\",\n";
        writeIndent(output, 4);
        output << "\"volume\": " << source.volume << ",\n";
        writeIndent(output, 4);
        output << "\"pitch\": " << source.pitch << ",\n";
        writeIndent(output, 4);
        output << "\"loop\": " << (source.loop ? "true" : "false") << ",\n";
        writeIndent(output, 4);
        output << "\"playOnStart\": " << (source.playOnStart ? "true" : "false") << ",\n";
        writeIndent(output, 4);
        output << "\"spatial\": " << (source.spatial ? "true" : "false") << "\n";
        writeIndent(output, 3);
        output << "}" << (tailAfterAudioSource ? ",\n" : "\n");
    }

    // Optional: audio listener.
    if (hasAudioListener) {
        const auto& listener = registry.audioListeners.get(entity);
        writeIndent(output, 3);
        output << "\"audiolistener\": {\n";
        writeIndent(output, 4);
        output << "\"gain\": " << listener.gain << "\n";
        writeIndent(output, 3);
        output << "}" << (tailAfterAudioListener ? ",\n" : "\n");
    }

    // Optional: runtime UI screen stack (Phase 16A) — a JSON array of screen ids.
    if (hasUIScreen) {
        const auto& screen = registry.uiScreens.get(entity);
        writeIndent(output, 3);
        output << "\"uiscreen\": [";
        for (size_t i = 0; i < screen.screenStack.size(); i++) {
            output << "\"" << escapeJsonString(screen.screenStack[i]) << "\"";
            if (i + 1 < screen.screenStack.size()) {
                output << ", ";
            }
        }
        output << "]" << (tailAfterUIScreen ? ",\n" : "\n");
    }

    // Optional: keyboard/gamepad focus.
    if (hasFocus) {
        writeIndent(output, 3);
        output << "\"focus\": \"" << escapeJsonString(registry.focus.get(entity).focusedElement)
               << "\"" << (tailAfterFocus ? ",\n" : "\n");
    }

    // Optional: in-progress text entry. Authoritative, so it round-trips — a scrub
    // must not lose a half-typed line. The caret blink phase is derived, not stored.
    if (hasTextInput) {
        const auto& text = registry.textInputs.get(entity);
        writeIndent(output, 3);
        output << "\"textinput\": {\n";
        writeIndent(output, 4);
        output << "\"element\": \"" << escapeJsonString(text.element) << "\",\n";
        writeIndent(output, 4);
        output << "\"buffer\": \"" << escapeJsonString(text.buffer) << "\",\n";
        writeIndent(output, 4);
        output << "\"caret\": " << text.caret << "\n";
        writeIndent(output, 3);
        output << "}\n";
    }

    writeIndent(output, 2);
    output << "}";
}

// Writes a list of entities as a JSON objects array body. Parent indices are
// resolved within the list (entities whose parent is not in the list get -1).
void writeEntitiesAsObjects(std::ostream& output, const Registry& registry, const std::vector<Entity>& orderedEntities) {
    std::unordered_map<Entity, int> entityIndices;
    entityIndices.reserve(orderedEntities.size());
    for (size_t i = 0; i < orderedEntities.size(); i++) {
        entityIndices.emplace(orderedEntities[i], static_cast<int>(i));
    }

    for (size_t i = 0; i < orderedEntities.size(); i++) {
        const Entity entity = orderedEntities[i];
        const auto parentIt = registry.hierarchy.has(entity)
            ? entityIndices.find(registry.hierarchy.get(entity).parent)
            : entityIndices.end();
        const int parentIndex = parentIt == entityIndices.end() ? -1 : parentIt->second;

        writeEntityObject(output, registry, entity, parentIndex);
        output << (i + 1 < orderedEntities.size() ? ",\n" : "\n");
    }
}

// All entities that have a transform, sorted for deterministic output.
std::vector<Entity> collectOrderedEntities(const Registry& registry) {
    std::vector<Entity> orderedEntities;
    orderedEntities.reserve(registry.transforms.getAll().size());
    for (const auto& [entity, transformComponent] : registry.transforms.getAll()) {
        (void)transformComponent;
        orderedEntities.push_back(entity);
    }
    std::sort(orderedEntities.begin(), orderedEntities.end());
    return orderedEntities;
}

// Depth-first collection of an entity and its descendants (root first).
void collectSubtree(const Registry& registry, Entity entity, std::vector<Entity>& out) {
    if (!registry.transforms.has(entity)) {
        return;
    }
    out.push_back(entity);
    if (registry.hierarchy.has(entity)) {
        for (Entity child : registry.hierarchy.get(entity).children) {
            collectSubtree(registry, child, out);
        }
    }
}

void writeSceneJson(std::ostream& output, const Registry& registry, const std::vector<Light>& lights) {
    const std::vector<Entity> orderedEntities = collectOrderedEntities(registry);

    output << "{\n";
    output << "  \"version\": 2,\n";
    output << "  \"objects\": [\n";
    writeEntitiesAsObjects(output, registry, orderedEntities);
    output << "  ],\n";
    output << "  \"lights\": [\n";

    for (size_t i = 0; i < lights.size(); i++) {
        const Light& light = lights[i];
        writeIndent(output, 2);
        output << "{\n";
        writeIndent(output, 3);
        output << "\"pos\": ";
        writeVec3(output, light.position);
        output << ",\n";
        writeIndent(output, 3);
        output << "\"color\": ";
        writeVec3(output, light.color);
        output << "\n";
        writeIndent(output, 2);
        output << (i + 1 < lights.size() ? "},\n" : "}\n");
    }

    output << "  ]\n";
    output << "}\n";
}

struct PendingEntityData {
    std::string name;
    int parentIndex = -1;
    Transform transform{};
    std::string meshKey;
    std::string albedoKey;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    std::string script;
    bool hasBody = false;
    RigidBodyComponent body{};
    bool hasCollider = false;
    ColliderComponent collider{};
    std::string prefab;
    bool hasAudioSource = false;
    AudioSourceComponent audioSource{};
    std::string audioClipKey;
    bool hasAudioListener = false;
    AudioListenerComponent audioListener{};
    bool hasUIScreen = false;
    UIScreenComponent uiScreen{};
    bool hasFocus = false;
    FocusComponent focus{};
    bool hasTextInput = false;
    TextInputComponent textInput{};
};

// Parses one object entry from the JSON. `sceneVersion` selects modern vs. the
// legacy (v1) material encoding.
PendingEntityData parseEntityObject(const JsonValue& objectValue, int sceneVersion) {
    const auto& objectData = requireObject(objectValue, "object");
    PendingEntityData pendingEntity{};
    pendingEntity.name = getStringValue(requireObjectField(objectData, "name"), "object.name");

    const auto& transformData = requireObject(
        requireObjectField(objectData, "transform"),
        "object.transform"
    );
    pendingEntity.transform.position = parseVec3(requireObjectField(transformData, "pos"), "transform.pos");
    pendingEntity.transform.rotation = parseRotation(requireObjectField(transformData, "rot"), "transform.rot");
    pendingEntity.transform.scale = parseVec3(requireObjectField(transformData, "scale"), "transform.scale");

    pendingEntity.meshKey = getStringValue(requireObjectField(objectData, "mesh"), "object.mesh");

    const auto& materialData = requireObject(
        requireObjectField(objectData, "material"),
        "object.material"
    );
    pendingEntity.albedoKey = getStringValue(
        requireObjectField(materialData, "albedo"),
        "material.albedo"
    );

    if (sceneVersion >= 2) {
        pendingEntity.metallic = getFloatValue(
            requireObjectField(materialData, "metallic"),
            "material.metallic"
        );
        pendingEntity.roughness = getFloatValue(
            requireObjectField(materialData, "roughness"),
            "material.roughness"
        );
        if (const JsonValue* aoValue = findObjectField(materialData, "ao")) {
            pendingEntity.ao = getFloatValue(*aoValue, "material.ao");
        }
    } else {
        float legacyShininess = 32.0f;
        float legacySpecular = 0.5f;

        if (const JsonValue* shininessValue = findObjectField(materialData, "shininess")) {
            legacyShininess = getFloatValue(*shininessValue, "material.shininess");
        }
        if (const JsonValue* specularValue = findObjectField(materialData, "specular")) {
            legacySpecular = getFloatValue(*specularValue, "material.specular");
        }

        const float normalizedShininess = std::clamp((legacyShininess - 2.0f) / 254.0f, 0.0f, 1.0f);
        pendingEntity.metallic = std::clamp(legacySpecular, 0.0f, 1.0f);
        pendingEntity.roughness = 1.0f - normalizedShininess;
        pendingEntity.ao = 1.0f;
    }
    pendingEntity.parentIndex = getIntValue(requireObjectField(objectData, "parent"), "object.parent");

    if (const JsonValue* scriptValue = findObjectField(objectData, "script")) {
        pendingEntity.script = getStringValue(*scriptValue, "object.script");
    }

    if (const JsonValue* bodyValue = findObjectField(objectData, "rigidbody")) {
        const auto& bodyData = requireObject(*bodyValue, "object.rigidbody");
        pendingEntity.hasBody = true;
        if (const JsonValue* v = findObjectField(bodyData, "velocity")) {
            pendingEntity.body.velocity = parseVec3(*v, "rigidbody.velocity");
        }
        if (const JsonValue* v = findObjectField(bodyData, "mass")) {
            pendingEntity.body.mass = getFloatValue(*v, "rigidbody.mass");
        }
        if (const JsonValue* v = findObjectField(bodyData, "restitution")) {
            pendingEntity.body.restitution = getFloatValue(*v, "rigidbody.restitution");
        }
        if (const JsonValue* v = findObjectField(bodyData, "friction")) {
            pendingEntity.body.friction = getFloatValue(*v, "rigidbody.friction");
        }
        if (const JsonValue* v = findObjectField(bodyData, "useGravity")) {
            pendingEntity.body.useGravity = getBoolValue(*v, "rigidbody.useGravity");
        }
        if (const JsonValue* v = findObjectField(bodyData, "isStatic")) {
            pendingEntity.body.isStatic = getBoolValue(*v, "rigidbody.isStatic");
        }
    }

    if (const JsonValue* colliderValue = findObjectField(objectData, "collider")) {
        const auto& colliderData = requireObject(*colliderValue, "object.collider");
        pendingEntity.hasCollider = true;
        if (const JsonValue* v = findObjectField(colliderData, "type")) {
            pendingEntity.collider.type =
                getStringValue(*v, "collider.type") == "sphere" ? ColliderType::Sphere : ColliderType::Box;
        }
        if (const JsonValue* v = findObjectField(colliderData, "halfExtents")) {
            pendingEntity.collider.halfExtents = parseVec3(*v, "collider.halfExtents");
        }
        if (const JsonValue* v = findObjectField(colliderData, "radius")) {
            pendingEntity.collider.radius = getFloatValue(*v, "collider.radius");
        }
    }

    if (const JsonValue* prefabValue = findObjectField(objectData, "prefab")) {
        pendingEntity.prefab = getStringValue(*prefabValue, "object.prefab");
    }

    if (const JsonValue* audioValue = findObjectField(objectData, "audiosource")) {
        const auto& audioData = requireObject(*audioValue, "object.audiosource");
        pendingEntity.hasAudioSource = true;
        if (const JsonValue* v = findObjectField(audioData, "clip")) {
            pendingEntity.audioClipKey = getStringValue(*v, "audiosource.clip");
        }
        if (const JsonValue* v = findObjectField(audioData, "volume")) {
            pendingEntity.audioSource.volume = getFloatValue(*v, "audiosource.volume");
        }
        if (const JsonValue* v = findObjectField(audioData, "pitch")) {
            pendingEntity.audioSource.pitch = getFloatValue(*v, "audiosource.pitch");
        }
        if (const JsonValue* v = findObjectField(audioData, "loop")) {
            pendingEntity.audioSource.loop = getBoolValue(*v, "audiosource.loop");
        }
        if (const JsonValue* v = findObjectField(audioData, "playOnStart")) {
            pendingEntity.audioSource.playOnStart = getBoolValue(*v, "audiosource.playOnStart");
        }
        if (const JsonValue* v = findObjectField(audioData, "spatial")) {
            pendingEntity.audioSource.spatial = getBoolValue(*v, "audiosource.spatial");
        }
    }

    if (const JsonValue* listenerValue = findObjectField(objectData, "audiolistener")) {
        const auto& listenerData = requireObject(*listenerValue, "object.audiolistener");
        pendingEntity.hasAudioListener = true;
        if (const JsonValue* v = findObjectField(listenerData, "gain")) {
            pendingEntity.audioListener.gain = getFloatValue(*v, "audiolistener.gain");
        }
    }

    if (const JsonValue* screenValue = findObjectField(objectData, "uiscreen")) {
        const auto& screenArray = requireArray(*screenValue, "object.uiscreen");
        pendingEntity.hasUIScreen = true;
        pendingEntity.uiScreen.screenStack.reserve(screenArray.size());
        for (const JsonValue& id : screenArray) {
            pendingEntity.uiScreen.screenStack.push_back(getStringValue(id, "uiscreen[]"));
        }
    }

    if (const JsonValue* focusValue = findObjectField(objectData, "focus")) {
        pendingEntity.hasFocus = true;
        pendingEntity.focus.focusedElement = getStringValue(*focusValue, "object.focus");
    }

    if (const JsonValue* textValue = findObjectField(objectData, "textinput")) {
        const auto& textData = requireObject(*textValue, "object.textinput");
        pendingEntity.hasTextInput = true;
        if (const JsonValue* v = findObjectField(textData, "element")) {
            pendingEntity.textInput.element = getStringValue(*v, "textinput.element");
        }
        if (const JsonValue* v = findObjectField(textData, "buffer")) {
            pendingEntity.textInput.buffer = getStringValue(*v, "textinput.buffer");
        }
        if (const JsonValue* v = findObjectField(textData, "caret")) {
            pendingEntity.textInput.caret = getIntValue(*v, "textinput.caret");
        }
    }

    return pendingEntity;
}

// Creates entities from a parsed objects array and wires parent relationships
// (indices are relative to this array). Does NOT reset the registry, so it is
// reused both for full scene loads (after a reset) and prefab instantiation
// (additive). Returns the created entities in object order.
// When `forcedIds` is non-null it must have one id per object; each entity is
// recreated with that exact id (Phase 14B — restoring a destroyed subtree into
// its original ids) instead of a freshly allocated one.
std::vector<Entity> createEntitiesFromObjects(Registry& registry, const std::vector<JsonValue>& objectValues,
                                              int sceneVersion, const std::vector<Entity>* forcedIds = nullptr) {
    std::vector<PendingEntityData> pendingEntities;
    pendingEntities.reserve(objectValues.size());
    for (const JsonValue& objectValue : objectValues) {
        pendingEntities.push_back(parseEntityObject(objectValue, sceneVersion));
    }

    std::vector<Entity> createdEntities;
    createdEntities.reserve(pendingEntities.size());

    for (size_t entityIndex = 0; entityIndex < pendingEntities.size(); entityIndex++) {
        const PendingEntityData& pendingEntity = pendingEntities[entityIndex];
        const Entity entity = forcedIds != nullptr
            ? registry.createEntityWithId((*forcedIds)[entityIndex])
            : registry.createEntity();
        createdEntities.push_back(entity);

        registry.names.add(entity, { pendingEntity.name });
        registry.transforms.add(entity, { pendingEntity.transform });
        registry.hierarchy.add(entity, {});

        const AssetHandle meshHandle = loadMeshWithFallback(pendingEntity.meshKey);
        if (meshHandle != INVALID_HANDLE) {
            registry.meshes.add(entity, { meshHandle });
        }

        Material material{};
        material.albedo = loadTextureWithFallback(pendingEntity.albedoKey);
        material.metallic = pendingEntity.metallic;
        material.roughness = pendingEntity.roughness;
        material.ao = pendingEntity.ao;
        if (material.albedo != INVALID_HANDLE) {
            registry.materials.add(entity, { material });
        }

        if (!pendingEntity.script.empty()) {
            registry.scripts.add(entity, { pendingEntity.script, false });
        }

        if (pendingEntity.hasBody) {
            registry.rigidBodies.add(entity, pendingEntity.body);
        }

        if (pendingEntity.hasCollider) {
            registry.colliders.add(entity, pendingEntity.collider);
        }

        if (!pendingEntity.prefab.empty()) {
            registry.prefabInstances.add(entity, { pendingEntity.prefab });
        }

        if (pendingEntity.hasAudioSource) {
            AudioSourceComponent source = pendingEntity.audioSource;
            source.clip = loadAudioClipWithFallback(pendingEntity.audioClipKey);
            registry.audioSources.add(entity, source);
        }

        if (pendingEntity.hasAudioListener) {
            registry.audioListeners.add(entity, pendingEntity.audioListener);
        }

        if (pendingEntity.hasUIScreen) {
            registry.uiScreens.add(entity, pendingEntity.uiScreen);
        }

        if (pendingEntity.hasFocus) {
            registry.focus.add(entity, pendingEntity.focus);
        }

        if (pendingEntity.hasTextInput) {
            registry.textInputs.add(entity, pendingEntity.textInput);
        }
    }

    for (size_t i = 0; i < createdEntities.size(); i++) {
        const int parentIndex = pendingEntities[i].parentIndex;
        if (parentIndex < 0) {
            continue;
        }

        if (parentIndex >= static_cast<int>(createdEntities.size())) {
            throw std::runtime_error("object parent index is out of range.");
        }

        registry.setParent(createdEntities[i], createdEntities[static_cast<size_t>(parentIndex)]);
    }

    return createdEntities;
}

// Patches one existing entity's components from parsed snapshot data. Mirrors the
// component semantics of createEntitiesFromObjects (add / update / remove) but
// never recreates the entity, so its id — and any editor state keyed on it —
// survives. Resource-backed components reload only when their key changed, so a
// scrub that only moved transforms doesn't touch the ResourceManager.
void patchEntity(Registry& registry, Entity entity, const PendingEntityData& data) {
    if (registry.names.has(entity)) {
        registry.names.get(entity).name = data.name;
    } else {
        registry.names.add(entity, { data.name });
    }

    // The entity always has a transform (it's how patch candidates are gathered).
    registry.transforms.get(entity).transform = data.transform;

    // Mesh: reload only when the resource key actually changed.
    {
        std::string currentKey;
        if (registry.meshes.has(entity)) {
            if (auto mesh = ResourceManager::getMesh(registry.meshes.get(entity).mesh)) {
                currentKey = mesh->getResourceKey();
            }
        }
        if (data.meshKey != currentKey) {
            if (registry.meshes.has(entity)) {
                if (registry.onReleaseAsset) {
                    registry.onReleaseAsset(registry.meshes.get(entity).mesh);
                }
                registry.meshes.remove(entity);
            }
            const AssetHandle handle = loadMeshWithFallback(data.meshKey);
            if (handle != INVALID_HANDLE) {
                registry.meshes.add(entity, { handle });
            }
        }
    }

    // Material: patched only when the snapshot carries one (non-empty albedo key —
    // loaded entities always do). Floats are cheap to set; the albedo texture is
    // reloaded only when its key changed. An empty key means "no material", which
    // is left untouched to keep the patch free of ResourceManager calls for
    // hand-built (test/headless) entities.
    if (!data.albedoKey.empty()) {
        Material material = registry.materials.has(entity)
            ? registry.materials.get(entity).material : Material{};
        std::string currentKey;
        if (material.albedo != INVALID_HANDLE) {
            if (auto texture = ResourceManager::getTexture(material.albedo)) {
                currentKey = texture->getResourceKey();
            }
        }
        if (data.albedoKey != currentKey) {
            if (material.albedo != INVALID_HANDLE && registry.onReleaseAsset) {
                registry.onReleaseAsset(material.albedo);
            }
            material.albedo = loadTextureWithFallback(data.albedoKey);
        }
        material.metallic = data.metallic;
        material.roughness = data.roughness;
        material.ao = data.ao;
        if (registry.materials.has(entity)) {
            registry.materials.get(entity).material = material;
        } else {
            registry.materials.add(entity, { material });
        }
    }

    if (!data.script.empty()) {
        if (registry.scripts.has(entity)) {
            auto& script = registry.scripts.get(entity);
            script.behavior = data.script;
            script.started = false;
        } else {
            registry.scripts.add(entity, { data.script, false });
        }
    } else if (registry.scripts.has(entity)) {
        registry.scripts.remove(entity);
    }

    if (data.hasBody) {
        if (registry.rigidBodies.has(entity)) {
            registry.rigidBodies.get(entity) = data.body;
        } else {
            registry.rigidBodies.add(entity, data.body);
        }
    } else if (registry.rigidBodies.has(entity)) {
        registry.rigidBodies.remove(entity);
    }

    if (data.hasCollider) {
        if (registry.colliders.has(entity)) {
            registry.colliders.get(entity) = data.collider;
        } else {
            registry.colliders.add(entity, data.collider);
        }
    } else if (registry.colliders.has(entity)) {
        registry.colliders.remove(entity);
    }

    if (!data.prefab.empty()) {
        if (registry.prefabInstances.has(entity)) {
            registry.prefabInstances.get(entity).prefab = data.prefab;
        } else {
            registry.prefabInstances.add(entity, { data.prefab });
        }
    } else if (registry.prefabInstances.has(entity)) {
        registry.prefabInstances.remove(entity);
    }

    if (data.hasAudioSource) {
        AudioSourceComponent source = registry.audioSources.has(entity)
            ? registry.audioSources.get(entity) : AudioSourceComponent{};
        std::string currentKey;
        if (source.clip != INVALID_HANDLE) {
            if (auto clip = ResourceManager::getAudioClip(source.clip)) {
                currentKey = clip->getResourceKey();
            }
        }
        if (data.audioClipKey != currentKey) {
            if (source.clip != INVALID_HANDLE && registry.onReleaseAsset) {
                registry.onReleaseAsset(source.clip);
            }
            source.clip = loadAudioClipWithFallback(data.audioClipKey);
        }
        source.volume = data.audioSource.volume;
        source.pitch = data.audioSource.pitch;
        source.loop = data.audioSource.loop;
        source.playOnStart = data.audioSource.playOnStart;
        source.spatial = data.audioSource.spatial;
        // Runtime latches always restore to a clean slate (matches load path).
        source.started = false;
        source.voice = 0;
        source.oneShotPending = false;
        if (registry.audioSources.has(entity)) {
            registry.audioSources.get(entity) = source;
        } else {
            registry.audioSources.add(entity, source);
        }
    } else if (registry.audioSources.has(entity)) {
        if (registry.onReleaseAsset && registry.audioSources.get(entity).clip != INVALID_HANDLE) {
            registry.onReleaseAsset(registry.audioSources.get(entity).clip);
        }
        registry.audioSources.remove(entity);
    }

    if (data.hasAudioListener) {
        if (registry.audioListeners.has(entity)) {
            registry.audioListeners.get(entity) = data.audioListener;
        } else {
            registry.audioListeners.add(entity, data.audioListener);
        }
    } else if (registry.audioListeners.has(entity)) {
        registry.audioListeners.remove(entity);
    }

    if (data.hasUIScreen) {
        if (registry.uiScreens.has(entity)) {
            registry.uiScreens.get(entity) = data.uiScreen;
        } else {
            registry.uiScreens.add(entity, data.uiScreen);
        }
    } else if (registry.uiScreens.has(entity)) {
        registry.uiScreens.remove(entity);
    }

    if (data.hasFocus) {
        if (registry.focus.has(entity)) {
            registry.focus.get(entity) = data.focus;
        } else {
            registry.focus.add(entity, data.focus);
        }
    } else if (registry.focus.has(entity)) {
        registry.focus.remove(entity);
    }

    if (data.hasTextInput) {
        if (registry.textInputs.has(entity)) {
            registry.textInputs.get(entity) = data.textInput;
        } else {
            registry.textInputs.add(entity, data.textInput);
        }
    } else if (registry.textInputs.has(entity)) {
        registry.textInputs.remove(entity);
    }
}

// In-place restore (Phase 14A). Parses the snapshot, checks it matches the live
// entity set, then patches each entity by serialization order (sorted id) so ids
// are preserved. Returns false without mutating on a structural mismatch.
bool patchSceneFromText(Registry& registry, std::vector<Light>& lights, const std::string& text) {
    try {
        const JsonValue rootValue = JsonParser(text).parse();
        const auto& root = requireObject(rootValue, "scene root");

        int sceneVersion = 1;
        if (const auto version = root.find("version"); version != root.end()) {
            sceneVersion = getIntValue(version->second, "version");
        }
        if (sceneVersion != 1 && sceneVersion != 2) {
            return false;
        }

        const auto& objectValues = requireArray(requireObjectField(root, "objects"), "objects");
        const auto& lightValues = requireArray(requireObjectField(root, "lights"), "lights");

        // Feasibility gate: same entity count as the snapshot. Serialization order
        // is sorted entity id, so a matching count lets the i-th object map to the
        // i-th live entity. A mismatch means a structural change (entity spawned or
        // destroyed since the snapshot) — bail so the caller does a full rebuild.
        // Checked before any mutation, so a false return leaves the registry intact.
        const std::vector<Entity> liveEntities = collectOrderedEntities(registry);
        if (liveEntities.size() != objectValues.size()) {
            return false;
        }

        std::vector<PendingEntityData> pending;
        pending.reserve(objectValues.size());
        for (const JsonValue& objectValue : objectValues) {
            pending.push_back(parseEntityObject(objectValue, sceneVersion));
        }
        // Validate parent indices up front so a malformed snapshot can't leave the
        // registry half-patched.
        for (const PendingEntityData& entry : pending) {
            if (entry.parentIndex >= static_cast<int>(liveEntities.size())) {
                return false;
            }
        }

        std::vector<Light> pendingLights;
        pendingLights.reserve(lightValues.size());
        for (const JsonValue& lightValue : lightValues) {
            const auto& lightData = requireObject(lightValue, "light");
            Light light{};
            light.position = parseVec3(requireObjectField(lightData, "pos"), "light.pos");
            light.color = parseVec3(requireObjectField(lightData, "color"), "light.color");
            pendingLights.push_back(light);
        }

        for (size_t i = 0; i < liveEntities.size(); i++) {
            patchEntity(registry, liveEntities[i], pending[i]);
        }

        // Re-apply hierarchy: detach everything, then wire parents by index. Two
        // passes so no intermediate state trips setParent's cycle guard.
        for (Entity entity : liveEntities) {
            registry.setParent(entity, INVALID_ENTITY);
        }
        for (size_t i = 0; i < liveEntities.size(); i++) {
            if (pending[i].parentIndex >= 0) {
                registry.setParent(liveEntities[i], liveEntities[static_cast<size_t>(pending[i].parentIndex)]);
            }
        }

        lights = std::move(pendingLights);
        return true;
    } catch (...) {
        return false;
    }
}

bool loadSceneFromText(Registry& registry, std::vector<Light>& lights, const std::string& text) {
    try {
        const JsonValue rootValue = JsonParser(text).parse();
        const auto& root = requireObject(rootValue, "scene root");

        const auto version = root.find("version");
        int sceneVersion = 1;
        if (version != root.end()) {
            sceneVersion = getIntValue(version->second, "version");
        }

        if (sceneVersion != 1 && sceneVersion != 2) {
            throw std::runtime_error("unsupported scene JSON version.");
        }

        const auto& objectValues = requireArray(requireObjectField(root, "objects"), "objects");
        const auto& lightValues = requireArray(requireObjectField(root, "lights"), "lights");

        std::vector<Light> pendingLights;
        pendingLights.reserve(lightValues.size());
        for (const JsonValue& lightValue : lightValues) {
            const auto& lightData = requireObject(lightValue, "light");
            Light light{};
            light.position = parseVec3(requireObjectField(lightData, "pos"), "light.pos");
            light.color = parseVec3(requireObjectField(lightData, "color"), "light.color");
            pendingLights.push_back(light);
        }

        registry.reset();
        lights.clear();

        createEntitiesFromObjects(registry, objectValues, sceneVersion);
        lights = std::move(pendingLights);

        return true;
    } catch (...) {
        registry.reset();
        lights.clear();
        return false;
    }
}
} // namespace

bool SceneSerializer::save(const Registry& registry, const std::vector<Light>& lights, const std::string& path) {
    try {
        std::ofstream output(path);
        if (!output.is_open()) {
            return false;
        }

        writeSceneJson(output, registry, lights);
        return output.good();
    } catch (...) {
        return false;
    }
}

std::string SceneSerializer::saveToString(const Registry& registry, const std::vector<Light>& lights) {
    try {
        std::ostringstream output;
        writeSceneJson(output, registry, lights);
        return output.str();
    } catch (...) {
        return {};
    }
}

bool SceneSerializer::load(Registry& registry, std::vector<Light>& lights, const std::string& path) {
    try {
        std::ifstream input(path);
        if (!input.is_open()) {
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return loadSceneFromText(registry, lights, buffer.str());
    } catch (...) {
        registry.reset();
        lights.clear();
        return false;
    }
}

bool SceneSerializer::loadFromString(Registry& registry, std::vector<Light>& lights, const std::string& text) {
    return loadSceneFromText(registry, lights, text);
}

bool SceneSerializer::patchFromString(Registry& registry, std::vector<Light>& lights, const std::string& text) {
    return patchSceneFromText(registry, lights, text);
}

std::string SceneSerializer::savePrefabToString(const Registry& registry, Entity root) {
    try {
        if (!registry.transforms.has(root)) {
            return {};
        }

        std::vector<Entity> subtree;
        collectSubtree(registry, root, subtree);

        std::ostringstream output;
        output << "{\n";
        output << "  \"version\": 2,\n";
        output << "  \"objects\": [\n";
        writeEntitiesAsObjects(output, registry, subtree);
        output << "  ]\n";
        output << "}\n";
        return output.str();
    } catch (...) {
        return {};
    }
}

bool SceneSerializer::savePrefab(const Registry& registry, Entity root, const std::string& path) {
    const std::string text = savePrefabToString(registry, root);
    if (text.empty()) {
        return false;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

Entity SceneSerializer::instantiateFromString(Registry& registry, const std::string& text,
                                              std::vector<Entity>* outCreated) {
    try {
        const JsonValue rootValue = JsonParser(text).parse();
        const auto& root = requireObject(rootValue, "prefab root");

        int prefabVersion = 2;
        const auto version = root.find("version");
        if (version != root.end()) {
            prefabVersion = getIntValue(version->second, "version");
        }

        const auto& objectValues = requireArray(requireObjectField(root, "objects"), "objects");
        const std::vector<Entity> created = createEntitiesFromObjects(registry, objectValues, prefabVersion);
        if (outCreated != nullptr) {
            *outCreated = created;
        }
        return created.empty() ? INVALID_ENTITY : created.front();
    } catch (...) {
        return INVALID_ENTITY;
    }
}

Entity SceneSerializer::instantiateFromStringWithIds(Registry& registry, const std::string& text,
                                                     const std::vector<Entity>& ids) {
    try {
        const JsonValue rootValue = JsonParser(text).parse();
        const auto& root = requireObject(rootValue, "prefab root");

        int prefabVersion = 2;
        const auto version = root.find("version");
        if (version != root.end()) {
            prefabVersion = getIntValue(version->second, "version");
        }

        const auto& objectValues = requireArray(requireObjectField(root, "objects"), "objects");
        // The id list must line up with the serialized objects (they share order:
        // both are the subtree's DFS order). A mismatch means the caller's stored
        // ids are stale, so refuse rather than assign the wrong ids.
        if (ids.size() != objectValues.size()) {
            return INVALID_ENTITY;
        }

        const std::vector<Entity> created = createEntitiesFromObjects(registry, objectValues, prefabVersion, &ids);
        return created.empty() ? INVALID_ENTITY : created.front();
    } catch (...) {
        return INVALID_ENTITY;
    }
}

void SceneSerializer::collectSubtreeEntities(const Registry& registry, Entity root, std::vector<Entity>& out) {
    collectSubtree(registry, root, out);
}

Entity SceneSerializer::instantiatePrefab(Registry& registry, const std::string& path) {
    try {
        std::ifstream input(path);
        if (!input.is_open()) {
            return INVALID_ENTITY;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return instantiateFromString(registry, buffer.str());
    } catch (...) {
        return INVALID_ENTITY;
    }
}
