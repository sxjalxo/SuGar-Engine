#pragma once

#include <map>
#include <string>

#include "ecs/Entity.h"

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

    // An entity handle a game chose to store — a UI label it owns, a projectile's
    // shooter, the villager the player is trading with.
    //
    // Separate from setInt/getInt on purpose. A handle is not an int: generation bits
    // occupy the TOP of the 32-bit value (Entity.h), so `static_cast<int>(entity)`
    // turns negative as soon as a slot has been reused a few times. It would still
    // round-trip — two's complement, exactly — but the scene file would hold a
    // negative number that reads as corrupt and is one careless `>= 0` check away
    // from a real bug. The double underneath holds every handle exactly: the largest
    // packed value, 4 294 967 295, is six orders of magnitude below 2^53.
    //
    // Values outside the handle range (a hand-edited or hostile scene file, or a -1
    // written by older game code) come back as INVALID_ENTITY rather than as garbage.
    Entity getEntity(const std::string& key, Entity fallback = INVALID_ENTITY) const {
        const double value = getNumber(key, static_cast<double>(fallback));
        if (value < 0.0 || value > 4294967295.0) {
            return INVALID_ENTITY;
        }
        return static_cast<Entity>(value);
    }
    void setEntity(const std::string& key, Entity entity) {
        setNumber(key, static_cast<double>(entity));
    }

    void remove(const std::string& key) { values.erase(key); }
};
