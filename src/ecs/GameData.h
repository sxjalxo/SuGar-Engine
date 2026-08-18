#pragma once

#include <map>
#include <string>

// Game-defined per-entity state (M4 Level 3 — see DevDocs/DESIGN_GAME_DATA.md).
//
// A game module links only Core and cannot add a component type to Registry, yet
// Behavior's contract says per-entity state must live in components. Mobs made that
// contradiction unworkable: one shared Behavior instance ticks every mob, so a field on
// it is one value for all of them. This component is the seam — the engine owns the
// storage, serialization, snapshot and inspector; the GAME owns the keys and what they
// mean. The engine never interprets a value.
//
// It is authoritative simulation state by Rule 21b (health, cooldowns, the slot the
// player picked are functions of what happened, not of the present), so it serializes and
// survives snapshot restore like any other component.
//
// Two value types only: number and string. The scene JSON parser folds true/false into
// numbers, so a flag is a number that is 0 or 1 — see the design record.
struct GameValue {
    enum class Type { Number, String };

    Type type = Type::Number;
    double number = 0.0;
    std::string text;

    GameValue() = default;
    explicit GameValue(double value) : type(Type::Number), number(value) {}
    explicit GameValue(std::string value) : type(Type::String), text(std::move(value)) {}
};

struct GameDataComponent {
    // std::map, not unordered_map: the scene file must serialize in a stable order, or
    // two runs of the same state produce different bytes (Rule 10). Same call
    // AssetMeta::settings makes.
    std::map<std::string, GameValue> values;

    bool has(const std::string& key) const { return values.find(key) != values.end(); }

    double getNumber(const std::string& key, double fallback = 0.0) const {
        const auto it = values.find(key);
        return (it != values.end() && it->second.type == GameValue::Type::Number)
                   ? it->second.number
                   : fallback;
    }
    float getFloat(const std::string& key, float fallback = 0.0f) const {
        return static_cast<float>(getNumber(key, static_cast<double>(fallback)));
    }
    int getInt(const std::string& key, int fallback = 0) const {
        return static_cast<int>(getNumber(key, static_cast<double>(fallback)));
    }
    bool getBool(const std::string& key, bool fallback = false) const {
        return getNumber(key, fallback ? 1.0 : 0.0) != 0.0;
    }
    std::string getString(const std::string& key, const std::string& fallback = std::string()) const {
        const auto it = values.find(key);
        return (it != values.end() && it->second.type == GameValue::Type::String) ? it->second.text
                                                                                  : fallback;
    }

    void setNumber(const std::string& key, double value) { values[key] = GameValue(value); }
    void setFloat(const std::string& key, float value) { setNumber(key, static_cast<double>(value)); }
    void setInt(const std::string& key, int value) { setNumber(key, static_cast<double>(value)); }
    void setBool(const std::string& key, bool value) { setNumber(key, value ? 1.0 : 0.0); }
    void setString(const std::string& key, std::string value) {
        values[key] = GameValue(std::move(value));
    }

    void remove(const std::string& key) { values.erase(key); }
};
