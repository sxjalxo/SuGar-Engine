#pragma once

#include <string>

// Tiny key/value persistence for game state that must outlive a run -- high scores,
// progress, settings. Deliberately minimal: a game needs "remember this number" long
// before it needs a save system, and scene serialization (the engine's real persistence)
// is the wrong tool for a high score. Lives in Core so game behaviours (which link only
// SuGarCore) can call it directly.
//
// On-disk format: one `key=value` line each, UTF-8, '\n'-terminated. Keys and values are
// opaque strings; a value may not contain a newline. The engine points it at the game's
// folder at boot (SaveData::setPath) and loads it; a game reads/writes keys and calls
// save() when it wants them flushed.
namespace SaveData {

// Where the store is read from / written to. The engine sets this to <gameDir>/save.dat
// (or "save.dat" in the working directory for the editor). Setting a new path clears the
// in-memory store; call load() after to read it.
void setPath(const std::string& path);
const std::string& path();

// Read the file at the current path into memory. Missing file = empty store (not an
// error -- a first run has no save yet). Returns false only on a malformed file.
bool load();

// Flush the in-memory store to the current path. Returns false if the file can't be
// written. Values are written in sorted key order so the bytes are stable run to run.
bool save();

bool has(const std::string& key);
std::string getString(const std::string& key, const std::string& fallback = "");
long long getInt(const std::string& key, long long fallback = 0);

// In-memory writes; call save() to persist. A value containing '\n' is rejected
// (setString returns false) because the line format can't represent it.
bool setString(const std::string& key, const std::string& value);
void setInt(const std::string& key, long long value);

void clear(); // drop the in-memory store (does not touch disk)

} // namespace SaveData
