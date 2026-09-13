#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// The recorded answer to "which cooked file is this key?" for a shipped build
// (DevDocs/DESIGN_PACKAGING.md, Phase 20).
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

    // Hostile-manifest caps (DevDocs/PLATFORM_AUDIT.md, "hostile manifests"). A shipped
    // manifest sits on the player's own disk, so load() treats it the way the scene
    // loader and glTF reader already treat their inputs: refuse cleanly, never crash,
    // hang, or exhaust memory. write() enforces MaxEntryCount too, for the same reason
    // in reverse: a cap only load() obeys isn't a cap on the format, it's a cap that
    // silently produces packages the engine itself cannot open. Every real manifest this
    // engine has ever packaged was measured on 2026-09-13 across all eight M4 dogfood
    // games (Asteroids, Breakout, FlappyBird, Pong, TopDownShooter, CombatArena,
    // Minecraft, RtsHandleProbe): longest key 34 chars (Asteroids'
    // "assets/textures/large_enemy_02.png"), most entries 14 (CombatArena) -- these are
    // sanity checks against the caps below, not how the caps were chosen (a real game's
    // asset count should never be the thing bounding a memory-safety limit).

    // Longest resource key load() accepts. Observed real-world maximum: 34 chars.
    // 256 is ~7.5x that margin (about the reach of Windows' own MAX_PATH=260), which
    // comfortably covers a few more nested asset folders than any shipped game uses,
    // while still bounding a single key's memory footprint.
    static constexpr size_t MaxKeyLength = 256;

    // Most entries load() (and write(), see below) will accept in one manifest.
    // Derived memory-bound-first, not from the observed test-game counts: the worst
    // case is MaxEntryCount entries, each MaxKeyLength long, so a hostile manifest costs
    // at most 65536 * (256-byte key + ~40 bytes of std::string/std::map overhead +
    // 8-byte hash) = 19 922 944 bytes, ~19 MB -- bounded and unremarkable for a load the
    // engine must finish before trusting the file. Only after fixing that bound does it
    // matter that it lands ~4681x above the observed real-world maximum of 14 entries (CombatArena,
    // the largest of the eight M4 dogfood games) and comfortably past a commercial-scale
    // asset list -- a sanity check on the number, not the reason for it.
    static constexpr size_t MaxEntryCount = 65536;

    // Longest single line (header or entry) load() will read before refusing the file.
    // Derived, not guessed: a well-formed entry line is at most one MaxKeyLength key,
    // one tab, and 16 hex digits, so nothing legal is ever longer than this. Reading
    // stops at this bound before a '\n' is required, which is what keeps a newline-free
    // multi-megabyte file from being read into one unbounded std::string.
    static constexpr size_t MaxLineLength = MaxKeyLength + 1 + 16;

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
    //
    // Refuses (errorMessage set, nothing written) if entries.size() exceeds
    // MaxEntryCount. The cap is deliberately the SAME constant load() enforces: a format
    // whose writer and reader disagree on a limit isn't a limit, it's a trap that springs
    // on whoever opens the file later -- possibly a player, on a machine where nobody can
    // fix it. Failing here instead means a developer hits it at package time, where they
    // can still act on it.
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
