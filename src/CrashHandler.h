#pragma once

#include <string>

// Minimal crash-report infrastructure -- deliberately NOT an engine subsystem. It exists
// so that when a game under test dies after hours of play, the failure leaves behind
// enough to act on: engine version, git commit, OS/CPU/GPU, the loaded scene and package,
// and a stack trace. On Windows it installs an unhandled-exception filter that writes a
// minidump (.dmp, open in Visual Studio / WinDbg with the matching .pdb) plus a
// human-readable .txt beside it. Everything here is a no-op on non-Windows.
namespace CrashHandler {

// Install the filter. `dumpDir` is created if missing; each crash writes one .dmp + .txt
// pair named by timestamp. Call once, as early in startup as possible.
void install(const std::string& dumpDir);

// Context folded into the report. Cheap, bounded copies -- safe to call whenever the
// state changes (GPU chosen, scene loaded, running packaged vs from source).
void setGpu(const std::string& name);
void setScene(const std::string& path);
void setPackage(const std::string& path);

} // namespace CrashHandler
