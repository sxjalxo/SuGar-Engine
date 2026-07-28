# SuGar Engine -- release build pipeline (Phase 21).
#
# One command, three consumers of work already built in Phases 19-20:
#   1. build   -- cmake --build (Release)
#   2. package -- SUGAR_PACKAGE cooks reachable assets, writes the manifest, copies the
#                 exe + DLLs (a runnable standalone), and verifies it resolves with no
#                 source. Cooking is device-free, so no GPU or window is needed.
# The pipeline invents no new concept; it orchestrates the headless gates.
#
# Usage:  pwsh scripts/build_release.ps1 [-Config Release] [-BuildDir build]
# Exit code is nonzero if any stage fails, so CI can gate on it.

param(
    [string]$Config = "Release",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

Write-Host "[pipeline] 1/2 build ($Config)"
cmake -S . -B $BuildDir | Out-Null
# Configure can need a second pass while the vendored FreeType target resolves
# (docs/DEV_ENVIRONMENT.md #1); retry once rather than failing a clean build.
if ($LASTEXITCODE -ne 0) { cmake -S . -B $BuildDir | Out-Null }
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { Write-Error "[pipeline] build failed"; exit 1 }

$exe = Join-Path $BuildDir "$Config/SuGarEngine.exe"
if (-not (Test-Path $exe)) { Write-Error "[pipeline] no executable at $exe"; exit 1 }

Write-Host "[pipeline] 2/2 package + verify"
# SUGAR_PACKAGE cooks, writes build/package, and self-verifies (source-free resolution).
# It exits nonzero if the package does not verify, so this line gates the pipeline.
$env:SUGAR_PACKAGE = "1"
& $exe
$packageExit = $LASTEXITCODE
Remove-Item Env:\SUGAR_PACKAGE

if ($packageExit -ne 0) { Write-Error "[pipeline] packaging/verify failed"; exit 1 }

Write-Host "[pipeline] done -> $(Join-Path $BuildDir 'package')"
