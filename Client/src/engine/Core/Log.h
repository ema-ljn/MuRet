#pragma once

#include "Core/ILogSink.h"
#include "EngineApi.h"

#include <format>
#include <string_view>

// Not spdlog: four levels, stderr, a file, a test sink and a mutex are a few lines with std::format, and spdlog brings
// fmt. No globals (D-C1): main.cpp creates it and passes it by reference; its sink must outlive it. The Log keeps no
// mutable state, so the mutex lives in each sink that writes to something shared, and a Log writing straight to stderr
// does not mix two lines either. A "\n" inside a message gives two lines, the second without prefix.
namespace Client::Core
{
// A value that is none of the four gets the error prefix. The crash handler uses it too, writing its line without the
// Log (Diagnostics::CrashLogLine).
[[nodiscard]] std::string_view LevelPrefix(LogLevel level);

class ENGINE_API Log
{
public:
    Log(ILogSink& sink, LogLevel minimum);

    // The format is checked at compile time, and a message below the minimum is not formatted. What fails only at
    // format time ({:c} with a number that is not a letter) stays in the Log: it writes "[error] formato de log
    // inválido: " and the format.
    template <typename... Args> void Debug(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Debug, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Info(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Info, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Warning(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Warning, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Error(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Error, format.get(), std::make_format_args(args...));
    }

    // The level chosen at runtime, like an SDL message's priority (SdlLog). The text is not formatted: braces are
    // written as they are.
    void Write(LogLevel level, std::string_view message);

private:
    // The body of the four above, out of the template. `arguments` points to the caller's, so it is used only during
    // the call.
    void WriteFormatted(LogLevel level, std::string_view format, std::format_args arguments);

    ILogSink& m_sink;
    LogLevel m_minimum;
};
} // namespace Client::Core
