#include "CrashHandler.h"

#include "BuildInfo.h"

#ifdef _WIN32

// clang-format off
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
// clang-format on

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace {

// Context is held in fixed buffers, never std::string: the handler runs in a process that
// may already be corrupt, so it must not allocate. Setters copy in bounded, the handler
// only reads. char[] is intentional here.
constexpr size_t FieldMax = 260; // MAX_PATH-ish; plenty for a name or a path

char g_dumpDir[FieldMax] = "crashes";
char g_os[FieldMax]      = "unknown";
char g_cpu[FieldMax]     = "unknown";
char g_gpu[FieldMax]     = "none";
char g_scene[FieldMax]   = "none";
char g_package[FieldMax] = "none (editor / source tree)";

void copyField(char* dst, const std::string& value) {
    lstrcpynA(dst, value.c_str(), static_cast<int>(FieldMax));
}

// OS version via RtlGetVersion (GetVersionEx lies without an app manifest).
void captureOs() {
    typedef LONG(WINAPI * RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr) {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtlGetVersion != nullptr) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtlGetVersion(&info) == 0) {
                wsprintfA(g_os, "Windows %lu.%lu build %lu",
                          info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
                return;
            }
        }
    }
    lstrcpynA(g_os, "Windows (version unknown)", FieldMax);
}

// CPU brand string from CPUID leaves 0x80000002..0x80000004.
void captureCpu() {
    int regs[4] = { 0 };
    __cpuid(regs, 0x80000000);
    const unsigned maxExtended = static_cast<unsigned>(regs[0]);
    if (maxExtended < 0x80000004u) {
        return;
    }
    char brand[49] = { 0 };
    for (unsigned leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
        __cpuid(regs, static_cast<int>(leaf));
        memcpy(brand + (leaf - 0x80000002u) * 16, regs, 16);
    }
    // Brand strings are often padded with spaces; trim both ends.
    const char* start = brand;
    while (*start == ' ') {
        ++start;
    }
    char* end = brand + lstrlenA(brand);
    while (end > start && *(end - 1) == ' ') {
        *(--end) = '\0';
    }
    lstrcpynA(g_cpu, start, FieldMax);
}

void writeLine(HANDLE file, const char* text) {
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
}

// wsprintfA into a stack buffer, then WriteFile. No heap, bounded to 1 KiB per line.
void writeFmt(HANDLE file, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    wvsprintfA(buffer, format, args);
    va_end(args);
    writeLine(file, buffer);
}

void writeReportText(const char* path, EXCEPTION_POINTERS* info, const char* dumpName) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);

    writeLine(file, "SuGar Engine crash report\r\n");
    writeLine(file, "=========================\r\n");
    writeFmt(file, "Time      : %04u-%02u-%02u %02u:%02u:%02u\r\n",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    writeFmt(file, "Version   : %s (%s)\r\n", BuildInfo::Version, BuildInfo::BuildConfig);
    writeFmt(file, "Commit    : %s\r\n", BuildInfo::GitCommit);
    writeFmt(file, "OS        : %s\r\n", g_os);
    writeFmt(file, "CPU       : %s\r\n", g_cpu);
    writeFmt(file, "GPU       : %s\r\n", g_gpu);
    writeFmt(file, "Scene     : %s\r\n", g_scene);
    writeFmt(file, "Package   : %s\r\n", g_package);
    writeFmt(file, "Minidump  : %s\r\n", dumpName);
    writeLine(file, "\r\n");

    if (info != nullptr && info->ExceptionRecord != nullptr) {
        writeFmt(file, "Exception : 0x%08lX at 0x%p\r\n",
                 info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    }
    writeLine(file, "\r\nStack trace (best effort -- the .dmp + .pdb is authoritative):\r\n");

    // Symbolized stack walk. DbgHelp is not fully reentrant, but the process is already
    // dying and single-threaded through this filter, so this is the accepted use.
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (info != nullptr && info->ContextRecord != nullptr && SymInitialize(process, nullptr, TRUE)) {
        CONTEXT context = *info->ContextRecord; // StackWalk64 mutates the context
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        alignas(SYMBOL_INFO) unsigned char symbolStorage[sizeof(SYMBOL_INFO) + 256] = { 0 };
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        for (int depth = 0; depth < 64; ++depth) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                             &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                             nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) {
                break;
            }

            DWORD64 displacement = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                DWORD lineDisplacement = 0;
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
                    writeFmt(file, "  #%02d %s  (%s:%lu)\r\n", depth, symbol->Name,
                             line.FileName, line.LineNumber);
                } else {
                    // %I64X, not %llX: wvsprintfA (the heap-free formatter used here)
                    // supports the I64 width but not the ll length modifier.
                    writeFmt(file, "  #%02d %s +0x%I64X\r\n", depth, symbol->Name,
                             static_cast<unsigned __int64>(displacement));
                }
            } else {
                writeFmt(file, "  #%02d 0x%I64X\r\n", depth,
                         static_cast<unsigned __int64>(frame.AddrPC.Offset));
            }
        }
        SymCleanup(process);
    }

    CloseHandle(file);
}

void writeMiniDump(const char* path, EXCEPTION_POINTERS* info) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = info;
    exceptionInfo.ClientPointers = FALSE;

    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithHandleData);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                      info != nullptr ? &exceptionInfo : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    char stem[FieldMax];
    wsprintfA(stem, "%s\\crash_%04u%02u%02u_%02u%02u%02u", g_dumpDir,
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    char dumpPath[FieldMax + 8];
    char textPath[FieldMax + 8];
    wsprintfA(dumpPath, "%s.dmp", stem);
    wsprintfA(textPath, "%s.txt", stem);

    writeMiniDump(dumpPath, info);

    // The .txt records the minidump's base name, not the full path, so the pair reads
    // as one unit wherever the crashes folder is copied.
    const char* dumpBase = dumpPath;
    for (const char* scan = dumpPath; *scan; ++scan) {
        if (*scan == '\\' || *scan == '/') {
            dumpBase = scan + 1;
        }
    }
    writeReportText(textPath, info, dumpBase);

    // Let the OS run its default handler too (WER), so nothing about existing behaviour
    // changes beyond the artifacts we just wrote.
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

namespace CrashHandler {

void install(const std::string& dumpDir) {
    copyField(g_dumpDir, dumpDir);
    CreateDirectoryA(g_dumpDir, nullptr); // ok if it already exists

    captureOs();
    captureCpu();

    // Reserve stack so the filter can still run after a stack-overflow exception, which
    // is exactly the case where the remaining stack would otherwise be too small to act.
    ULONG guaranteed = 64 * 1024;
    SetThreadStackGuarantee(&guaranteed);

    SetUnhandledExceptionFilter(onUnhandledException);
}

void setGpu(const std::string& name) { copyField(g_gpu, name); }
void setScene(const std::string& path) { copyField(g_scene, path); }
void setPackage(const std::string& path) { copyField(g_package, path); }

} // namespace CrashHandler

#else // !_WIN32

namespace CrashHandler {
void install(const std::string&) {}
void setGpu(const std::string&) {}
void setScene(const std::string&) {}
void setPackage(const std::string&) {}
} // namespace CrashHandler

#endif
