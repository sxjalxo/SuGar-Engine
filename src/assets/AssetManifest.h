#pragma once

#include <cstdint>
#include <map>
#include <string>

// The recorded answer to "which cooked file is this key?" for a shipped build
// (docs/DESIGN_PACKAGING.md, Phase 20).
//
// In the editor the cooker hashes the SOURCE to name an artifact. A package has no
// source, so the packager records `resourceKey -> artifact hash` here at package time,
// and the runtime looks it up instead of hashing. Core and headless: it is a sorted
// text map with no format dependencies -- the file packaging and the build pipeline
// enumerate.
//
// This is not a new identity (Rule 21a/21b): the keys are the ones scenes already store
// and the hashes are the ones the cooker already produces. Delete it and re-package from
// source and it comes back byte-for-byte, so it is derived, never authoritative history.
class AssetManifest {
public:
    // Bumped alongside AssetHash::CookerVersion when a manifest that is still readable
    // could name artifacts a newer runtime would cook differently. Written into the
    // header; a mismatch on load is reported, because a manifest from another cooker
    // points at artifacts this build cannot trust.
    static constexpr int FormatVersion = 1;

    // Records key -> artifact hash. The key is normalized (AssetPath) before storing, so
    // any spelling of it resolves later. Overwrites a prior entry for the same key.
    void set(const std::string& resourceKey, uint64_t artifactKey);

    // The artifact hash for a key, or 0 if the manifest does not list it. Zero is "not
    // in this package" -- the caller reports it rather than guessing a filename.
    uint64_t lookup(const std::string& resourceKey) const;

    bool empty() const { return entries.empty(); }
    size_t size() const { return entries.size(); }
    const std::map<std::string, uint64_t>& all() const { return entries; }

    // Deterministic bytes: a header line with the format version, then one
    // "<key>\t<16-hex>" line per entry in sorted key order (std::map iterates sorted),
    // '\n' endings, no timestamps. Two packages of the same inputs write identical files.
    bool write(const std::string& path, std::string& errorMessage) const;

    // Reads a manifest written by write(). Returns false (with errorMessage) if the file
    // is absent, malformed, or from another format version -- a wrong-version manifest
    // is reported, never half-read, because it names artifacts this build may not cook
    // the same way.
    bool load(const std::string& path, std::string& errorMessage);

    // True if a manifest file exists at `path`. The one fact that distinguishes a
    // shipped build from the editor: a manifest beside the executable means "resolve
    // keys through me, do not cook from source."
    static bool existsAt(const std::string& path);

private:
    // Sorted by key: iteration order is the on-disk order, so writes are deterministic
    // without a separate sort step.
    std::map<std::string, uint64_t> entries;
};
