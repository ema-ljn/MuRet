#pragma once

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "EngineApi.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

// One write and a flush per line, so a crash keeps what was logged. Appends, like 0x41f7a0 (OPEN_ALWAYS, then
// FILE_END): two clients at once write interleaved but whole lines, where the original opens it shared for reading only
// and the second client has no log (Q-C48 of 07). The original XORs it; MuRet writes it in clear, on purpose. A failed
// write warns once with the path and closes the file; the lines go on in stderr through main's TeeSink (AUD-C02). An
// error the system gives only on close is lost: the ofstream swallows it in its destructor.
namespace Client::Core
{
// The original does not empty it: on start 0x41f830 reads up to 0x1ffff bytes and keeps from byte 0x7fff on if it read
// 0x7fff or more, else the last four sessions, then rewrites the file (Q-C48). MuRet logs more per run, and emptying at
// 1 MiB is simpler and keeps several runs. Checked only on open, so a run does not lose its own lines to the cap. A
// client opening the file past the cap empties it, and a running one loses what it had written but not what it writes
// after (AppendsFromIndependentSinksAfterTheCap).
inline constexpr std::uintmax_t MaxLogFileBytes = 1024 * 1024;

class ENGINE_API FileSink final : public ILogSink
{
public:
    // A unique_ptr because the mutex does not move and Expected stores its value by moving it. `noticeOutput` takes the
    // failed-write notice with fwrite, not through the Log; it must outlive the FileSink, which does not close it.
    [[nodiscard]] static Expected<std::unique_ptr<FileSink>, std::string> Open(const std::filesystem::path& path,
                                                                               std::FILE* noticeOutput = stderr);

    void Write(LogLevel level, std::string_view line) override;

private:
    // Private: only Open builds one, after checking that the file opened.
    FileSink(std::ofstream file, std::string failureNotice, std::FILE* noticeOutput);

    std::ofstream m_file;        // closed on the first failure: is_open() tells if it failed
    std::string m_failureNotice; // built on open: writing it needs no new memory
    std::FILE* m_noticeOutput;
    std::mutex m_mutex;
};
} // namespace Client::Core
