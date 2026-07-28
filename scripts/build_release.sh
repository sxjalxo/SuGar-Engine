#!/usr/bin/env bash
# SuGar Engine -- release build pipeline (Phase 21). POSIX companion to
# build_release.ps1; see that file for the rationale. Windows is the shipping target
# today (Rule 18), so this is provided for parity and CI on other hosts.
#
# Usage:  scripts/build_release.sh [Config] [BuildDir]
set -euo pipefail

CONFIG="${1:-Release}"
BUILD_DIR="${2:-build}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

echo "[pipeline] 1/2 build ($CONFIG)"
# Configure can need a second pass while the vendored FreeType target resolves
# (docs/DEV_ENVIRONMENT.md #1); retry once.
cmake -S . -B "$BUILD_DIR" >/dev/null || cmake -S . -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --config "$CONFIG"

EXE="$BUILD_DIR/$CONFIG/SuGarEngine"
[ -f "$EXE" ] || EXE="$BUILD_DIR/$CONFIG/SuGarEngine.exe"
[ -f "$EXE" ] || { echo "[pipeline] no executable at $EXE"; exit 1; }

echo "[pipeline] 2/2 package + verify"
# SUGAR_PACKAGE cooks, writes build/package, and self-verifies (source-free); it exits
# nonzero if the package does not verify, so this gates the pipeline.
SUGAR_PACKAGE=1 "$EXE"

echo "[pipeline] done -> $BUILD_DIR/package"
