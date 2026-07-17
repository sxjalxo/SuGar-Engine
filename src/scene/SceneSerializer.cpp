#include "scene/SceneSerializer.h"
#include "assets/ModelImporter.h"
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
#include <cstddef>
#include <fstream>
#include <functional>
#include <ostream>
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

    // Optional components. Each present one contributes an emitter that writes its
    // field *without* a trailing comma or newline; the loop at the bottom owns the
    // separator. So adding a component is one push_back, and comma bookkeeping
    // cannot be forgotten — this control flow is independent of how many optional
    // components exist.
    //
    // This replaced a ladder of `tailAfter*` booleans, each the OR of every optional
    // declared after it. That cost ~10 edits per new component, and a missed term
    // emitted invalid JSON at *runtime* (a failed snapshot parse) rather than a
    // compile error — see docs/DEV_ENVIRONMENT.md on partially-applied edits.
    // Nothing is emitted while this vector is built, so it is safe to assemble here,
    // between the material object's contents and its closing brace.
    std::vector<std::function<void(std::ostream&)>> fields;

    // Optional: named behavior. `started` is intentionally not written —
    // it always restores to false (behaviors only start in Play mode).
    if (registry.scripts.has(entity) && !registry.scripts.get(entity).behavior.empty()) {
        fields.push_back([&](std::ostream& out) {
            writeIndent(out, 3);
            out << "\"script\": \""
                << escapeJsonString(registry.scripts.get(entity).behavior)
                << "\"";
        });
    }

    // Optional: rigid body. `velocity` is written so an authored initial
    // velocity survives; transient force is not part of the body's state.
    if (registry.rigidBodies.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            const auto& body = registry.rigidBodies.get(entity);
            writeIndent(out, 3);
            out << "\"rigidbody\": {\n";
            writeIndent(out, 4);
            out << "\"velocity\": ";
            writeVec3(out, body.velocity);
            out << ",\n";
            writeIndent(out, 4);
            out << "\"mass\": " << body.mass << ",\n";
            writeIndent(out, 4);
            out << "\"restitution\": " << body.restitution << ",\n";
            writeIndent(out, 4);
            out << "\"friction\": " << body.friction << ",\n";
            writeIndent(out, 4);
            out << "\"useGravity\": " << (body.useGravity ? "true" : "false") << ",\n";
            writeIndent(out, 4);
            out << "\"isStatic\": " << (body.isStatic ? "true" : "false") << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: collider.
    if (registry.colliders.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            const auto& collider = registry.colliders.get(entity);
            writeIndent(out, 3);
            out << "\"collider\": {\n";
            writeIndent(out, 4);
            out << "\"type\": \""
                << (collider.type == ColliderType::Sphere ? "sphere" : "box")
                << "\",\n";
            writeIndent(out, 4);
            out << "\"halfExtents\": ";
            writeVec3(out, collider.halfExtents);
            out << ",\n";
            writeIndent(out, 4);
            out << "\"radius\": " << collider.radius << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: prefab instance link.
    if (registry.prefabInstances.has(entity) &&
        !registry.prefabInstances.get(entity).prefab.empty()) {
        fields.push_back([&](std::ostream& out) {
            writeIndent(out, 3);
            out << "\"prefab\": \""
                << escapeJsonString(registry.prefabInstances.get(entity).prefab)
                << "\"";
        });
    }

    // Optional: audio source. Runtime fields (started/voice) are not written.
    if (registry.audioSources.has(entity) &&
        registry.audioSources.get(entity).clip != INVALID_HANDLE) {
        fields.push_back([&](std::ostream& out) {
            const auto& source = registry.audioSources.get(entity);
            const auto clip = ResourceManager::getAudioClip(source.clip);
            writeIndent(out, 3);
            out << "\"audiosource\": {\n";
            writeIndent(out, 4);
            out << "\"clip\": \"" << escapeJsonString(clip ? clip->getResourceKey() : "") << "\",\n";
            writeIndent(out, 4);
            out << "\"volume\": " << source.volume << ",\n";
            writeIndent(out, 4);
            out << "\"pitch\": " << source.pitch << ",\n";
            writeIndent(out, 4);
            out << "\"loop\": " << (source.loop ? "true" : "false") << ",\n";
            writeIndent(out, 4);
            out << "\"playOnStart\": " << (source.playOnStart ? "true" : "false") << ",\n";
            writeIndent(out, 4);
            out << "\"spatial\": " << (source.spatial ? "true" : "false") << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: audio listener.
    if (registry.audioListeners.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            const auto& listener = registry.audioListeners.get(entity);
            writeIndent(out, 3);
            out << "\"audiolistener\": {\n";
            writeIndent(out, 4);
            out << "\"gain\": " << listener.gain << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Runtime UI state (Phase 16A). Authoritative → serialized, so it survives
    // snapshot restore / time travel (RULES.md Rule 21). Only the UIRoot-style
    // entities carry these; everything else omits them.
    //
    // Optional: runtime UI screen stack — a JSON array of screen ids.
    if (registry.uiScreens.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            const auto& screen = registry.uiScreens.get(entity);
            writeIndent(out, 3);
            out << "\"uiscreen\": [";
            for (std::size_t i = 0; i < screen.screenStack.size(); i++) {
                out << "\"" << escapeJsonString(screen.screenStack[i]) << "\"";
                if (i + 1 < screen.screenStack.size()) {
                    out << ", ";
                }
            }
            out << "]";
        });
    }

    // Optional: keyboard/gamepad focus.
    if (registry.focus.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            writeIndent(out, 3);
            out << "\"focus\": \"" << escapeJsonString(registry.focus.get(entity).focusedElement)
                << "\"";
        });
    }

    // Optional: in-progress text entry. Authoritative, so it round-trips — a scrub
    // must not lose a half-typed line. The caret blink phase is derived, not stored.
    if (registry.textInputs.has(entity)) {
        fields.push_back([&](std::ostream& out) {
            const auto& text = registry.textInputs.get(entity);
            writeIndent(out, 3);
            out << "\"textinput\": {\n";
            writeIndent(out, 4);
            out << "\"element\": \"" << escapeJsonString(text.element) << "\",\n";
            writeIndent(out, 4);
            out << "\"buffer\": \"" << escapeJsonString(text.buffer) << "\",\n";
            writeIndent(out, 4);
            out << "\"caret\": " << text.caret << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: animation playback state (Phase 17A). Authoritative → serialized:
    // RULES.md Rule 21's worked example is an animator that hides `currentTime` and
    // jumps after a restore. `time` is the whole point — restore it and the next
    // fixed step re-derives the identical pose, which is why the pose itself is
    // never written. A player with no clip is inert, so it is omitted (the same
    // rule `script` uses for an empty behavior name). See docs/DESIGN_ANIMATION.md.
    if (registry.animations.has(entity) && !registry.animations.get(entity).clip.empty()) {
        fields.push_back([&](std::ostream& out) {
            const auto& player = registry.animations.get(entity);
            writeIndent(out, 3);
            out << "\"animation\": {\n";
            writeIndent(out, 4);
            out << "\"clip\": \"" << escapeJsonString(player.clip) << "\",\n";
            writeIndent(out, 4);
            out << "\"time\": " << player.time << ",\n";
            writeIndent(out, 4);
            out << "\"speed\": " << player.speed << ",\n";
            writeIndent(out, 4);
            out << "\"playing\": " << (player.playing ? "true" : "false") << ",\n";
            writeIndent(out, 4);
            out << "\"loop\": " << (player.loop ? "true" : "false") << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: skinned-mesh binding (Phase 17C). A reference, not state — the name
    // of the skin's bind data. Joint matrices are derived every frame and never
    // written here.
    if (registry.skinnedMeshes.has(entity) && !registry.skinnedMeshes.get(entity).skin.empty()) {
        fields.push_back([&](std::ostream& out) {
            writeIndent(out, 3);
            out << "\"skinnedmesh\": \""
                << escapeJsonString(registry.skinnedMeshes.get(entity).skin)
                << "\"";
        });
    }

    // Optional: animation state machine (Phase 17D). All authoritative — including
    // the transition's progress, which is what makes a scrub land mid-cross-fade in
    // exactly the pose it did the first time.
    if (registry.animationStates.has(entity) && !registry.animationStates.get(entity).graph.empty()) {
        fields.push_back([&](std::ostream& out) {
            const auto& machine = registry.animationStates.get(entity);
            writeIndent(out, 3);
            out << "\"animationstate\": {\n";
            writeIndent(out, 4);
            out << "\"graph\": \"" << escapeJsonString(machine.graph) << "\",\n";
            writeIndent(out, 4);
            out << "\"state\": \"" << escapeJsonString(machine.currentState) << "\",\n";
            writeIndent(out, 4);
            out << "\"phase\": " << machine.statePhase << ",\n";
            writeIndent(out, 4);
            out << "\"target\": \"" << escapeJsonString(machine.transitionTarget) << "\",\n";
            writeIndent(out, 4);
            out << "\"targetPhase\": " << machine.targetPhase << ",\n";
            writeIndent(out, 4);
            out << "\"transitionElapsed\": " << machine.transitionElapsed << ",\n";
            writeIndent(out, 4);
            out << "\"transitionDuration\": " << machine.transitionDuration << "\n";
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Optional: animation parameters (Phase 17D). Gameplay state that the animator
    // reads; std::map keeps the key order stable so the file diffs cleanly.
    if (registry.animationParameters.has(entity) &&
        !registry.animationParameters.get(entity).values.empty()) {
        fields.push_back([&](std::ostream& out) {
            const auto& parameters = registry.animationParameters.get(entity).values;
            writeIndent(out, 3);
            out << "\"animationparams\": {\n";
            size_t index = 0;
            for (const auto& [name, value] : parameters) {
                writeIndent(out, 4);
                out << "\"" << escapeJsonString(name) << "\": " << value
                    << (++index < parameters.size() ? ",\n" : "\n");
            }
            writeIndent(out, 3);
            out << "}";
        });
    }

    // Closes the material object — the last mandatory field, so it takes a comma
    // only when an optional actually follows it.
    writeIndent(output, 3);
    output << (fields.empty() ? "}\n" : "},\n");

    // The one place a comma between optional fields is decided.
    for (std::size_t i = 0; i < fields.size(); i++) {
        fields[i](output);
        output << (i + 1 < fields.size() ? ",\n" : "\n");
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
    bool hasAnimation = false;
    AnimationPlayerComponent animation{};
    bool hasSkinnedMesh = false;
    SkinnedMeshComponent skinnedMesh{};
    bool hasAnimationState = false;
    AnimationStateComponent animationState{};
    bool hasAnimationParams = false;
    AnimationParametersComponent animationParams{};
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

    if (const JsonValue* animationValue = findObjectField(objectData, "animation")) {
        const auto& animationData = requireObject(*animationValue, "object.animation");
        pendingEntity.hasAnimation = true;
        if (const JsonValue* v = findObjectField(animationData, "clip")) {
            pendingEntity.animation.clip = getStringValue(*v, "animation.clip");
        }
        if (const JsonValue* v = findObjectField(animationData, "time")) {
            pendingEntity.animation.time = getFloatValue(*v, "animation.time");
        }
        if (const JsonValue* v = findObjectField(animationData, "speed")) {
            pendingEntity.animation.speed = getFloatValue(*v, "animation.speed");
        }
        if (const JsonValue* v = findObjectField(animationData, "playing")) {
            pendingEntity.animation.playing = getBoolValue(*v, "animation.playing");
        }
        if (const JsonValue* v = findObjectField(animationData, "loop")) {
            pendingEntity.animation.loop = getBoolValue(*v, "animation.loop");
        }
    }

    if (const JsonValue* skinValue = findObjectField(objectData, "skinnedmesh")) {
        pendingEntity.hasSkinnedMesh = true;
        pendingEntity.skinnedMesh.skin = getStringValue(*skinValue, "object.skinnedmesh");
    }

    if (const JsonValue* stateValue = findObjectField(objectData, "animationstate")) {
        const auto& stateData = requireObject(*stateValue, "object.animationstate");
        pendingEntity.hasAnimationState = true;
        auto& machine = pendingEntity.animationState;
        if (const JsonValue* v = findObjectField(stateData, "graph")) {
            machine.graph = getStringValue(*v, "animationstate.graph");
        }
        if (const JsonValue* v = findObjectField(stateData, "state")) {
            machine.currentState = getStringValue(*v, "animationstate.state");
        }
        if (const JsonValue* v = findObjectField(stateData, "phase")) {
            machine.statePhase = getFloatValue(*v, "animationstate.phase");
        }
        if (const JsonValue* v = findObjectField(stateData, "target")) {
            machine.transitionTarget = getStringValue(*v, "animationstate.target");
        }
        if (const JsonValue* v = findObjectField(stateData, "targetPhase")) {
            machine.targetPhase = getFloatValue(*v, "animationstate.targetPhase");
        }
        if (const JsonValue* v = findObjectField(stateData, "transitionElapsed")) {
            machine.transitionElapsed = getFloatValue(*v, "animationstate.transitionElapsed");
        }
        if (const JsonValue* v = findObjectField(stateData, "transitionDuration")) {
            machine.transitionDuration = getFloatValue(*v, "animationstate.transitionDuration");
        }
    }

    if (const JsonValue* paramsValue = findObjectField(objectData, "animationparams")) {
        const auto& paramsData = requireObject(*paramsValue, "object.animationparams");
        pendingEntity.hasAnimationParams = true;
        for (const auto& [name, value] : paramsData) {
            pendingEntity.animationParams.values[name] =
                getFloatValue(value, "animationparams." + name);
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

        // Clips and skins are named, not embedded — so a scene loaded from disk has
        // to put them back in their registries itself. Without this the components
        // survive but resolve to nothing: animation silently stops and skinned
        // meshes render in bind pose.
        if (pendingEntity.hasAnimation) {
            ModelImporter::ensureModelAssets(pendingEntity.animation.clip);
            registry.animations.add(entity, pendingEntity.animation);
        }

        if (pendingEntity.hasSkinnedMesh) {
            ModelImporter::ensureModelAssets(pendingEntity.skinnedMesh.skin);
            registry.skinnedMeshes.add(entity, pendingEntity.skinnedMesh);
        }

        if (pendingEntity.hasAnimationState) {
            registry.animationStates.add(entity, pendingEntity.animationState);
        }

        if (pendingEntity.hasAnimationParams) {
            registry.animationParameters.add(entity, pendingEntity.animationParams);
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

    if (data.hasAnimation) {
        ModelImporter::ensureModelAssets(data.animation.clip);
        if (registry.animations.has(entity)) {
            registry.animations.get(entity) = data.animation;
        } else {
            registry.animations.add(entity, data.animation);
        }
    } else if (registry.animations.has(entity)) {
        registry.animations.remove(entity);
    }

    if (data.hasSkinnedMesh) {
        ModelImporter::ensureModelAssets(data.skinnedMesh.skin);
        if (registry.skinnedMeshes.has(entity)) {
            registry.skinnedMeshes.get(entity) = data.skinnedMesh;
        } else {
            registry.skinnedMeshes.add(entity, data.skinnedMesh);
        }
    } else if (registry.skinnedMeshes.has(entity)) {
        registry.skinnedMeshes.remove(entity);
    }

    if (data.hasAnimationState) {
        if (registry.animationStates.has(entity)) {
            registry.animationStates.get(entity) = data.animationState;
        } else {
            registry.animationStates.add(entity, data.animationState);
        }
    } else if (registry.animationStates.has(entity)) {
        registry.animationStates.remove(entity);
    }

    if (data.hasAnimationParams) {
        if (registry.animationParameters.has(entity)) {
            registry.animationParameters.get(entity) = data.animationParams;
        } else {
            registry.animationParameters.add(entity, data.animationParams);
        }
    } else if (registry.animationParameters.has(entity)) {
        registry.animationParameters.remove(entity);
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
