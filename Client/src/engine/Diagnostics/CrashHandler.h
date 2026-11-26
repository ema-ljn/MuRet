#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

// The original's crash handler (02 § El manejador de caídas): WinMain first calls 0x67f4f0(0x4b9a70, 0), which installs
// the 0x680180 filter; on a crash 0x4b9a70 writes MuError.dmp (0x67f840), logs "Save DumpFile complete - MuError.dmp"
// and returns EXCEPTION_EXECUTE_HANDLER, so the process ends without the system dialog. Differences: Q-C241..Q-C245 of
// 07. After the crash nothing allocates or locks: everything lives in static state built on install, and on Linux only
// async-signal-safe POSIX functions and sysinfo (a bare system call in glibc) run. The exception to D-C1 (no globals):
// a signal handler gets only the signal, so there is a single handler and Install fails if one is installed. The
// resolution is not restored: the original calls ChangeDisplaySettingsA(NULL, 0) (0x4b9b66) because it changes the
// desktop mode for its fullscreen, and MuRet never does (Q-C244).
namespace Client::Diagnostics
{
// The name at 0x6c80dc. Relative to the current folder, like the fopen(..., "wb") of 0x67f840.
inline constexpr std::string_view CrashDumpFileName = "MuError.dmp";

// In characters: paths are copied to a fixed buffer on install, so nothing allocates after the crash.
inline constexpr std::size_t MaxCrashPathLength = 4096;

class ENGINE_API CrashHandler
{
public:
    // An error if one is already installed, the path is too long or the system refuses.
    [[nodiscard]] static Core::Expected<std::unique_ptr<CrashHandler>, std::string>
    Install(const std::filesystem::path& dumpPath);

    // Restores what was there: the previous filter, like 0x67f740, or the previous signal actions and alternate stack.
    ~CrashHandler();

    CrashHandler(const CrashHandler&) = delete;
    CrashHandler& operator=(const CrashHandler&) = delete;
    CrashHandler(CrashHandler&&) = delete;
    CrashHandler& operator=(CrashHandler&&) = delete;

    // Where 0x4b9b3e's line goes once the dump is written: main's FileSink file, opened after install. Without this
    // call no line is written, like the original with the log closed (0x41f9ec). The line is CrashLogLine with the
    // FileSink's line end. false if the path is too long, and then there is no line.
    [[nodiscard]] bool SetLogFile(const std::filesystem::path& logPath);

    [[nodiscard]] static bool IsInstalled();

private:
    CrashHandler() = default;
};
} // namespace Client::Diagnostics
