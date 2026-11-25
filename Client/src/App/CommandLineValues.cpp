#include "App/CommandLineValues.h"

#include "App/CommandLine.h"
#include "App/CommandLineSystem.h"
#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "Platform/VSyncMode.h"
#include "Core/Expected.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
using Client::App::CommandLine;
using Client::App::OptionTexts;
using Client::App::PacingMode;
using Client::App::ServerAddress;
using Client::App::ServerTexts;
using Client::App::VSyncMode;
using Client::Core::Unexpected;
namespace OptionNames = Client::App::OptionNames;

// A positive integer, with no sign and nothing after it: from_chars already rejects "-1", "+5" and the overflow.
std::optional<std::uint64_t> ParsePositive(std::string_view text)
{
    std::uint64_t value = 0;
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    if (error != std::errc() || end != last || value == 0)
    {
        return std::nullopt;
    }
    return value;
}

// 0, or MinFpsLimit to MaxFpsLimit (IsValidMaxFps), with no sign and nothing after it.
std::optional<int> ParseMaxFps(std::string_view text)
{
    unsigned value = 0;
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    // More than MaxFpsLimit stops before the cast: it need not fit in an int.
    if (error != std::errc() || end != last || value > static_cast<unsigned>(Client::App::MaxFpsLimit) ||
        !Client::App::IsValidMaxFps(static_cast<int>(value)))
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<PacingMode> ParsePacing(std::string_view text)
{
    if (text == "decoupled")
    {
        return PacingMode::Decoupled;
    }
    if (text == "faithful")
    {
        return PacingMode::Faithful;
    }
    return std::nullopt;
}

std::optional<VSyncMode> ParseVSync(std::string_view text)
{
    if (text == "on")
    {
        return VSyncMode::On;
    }
    if (text == "adaptive")
    {
        return VSyncMode::Adaptive;
    }
    if (text == "off")
    {
        return VSyncMode::Off;
    }
    return std::nullopt;
}

// Each Read* below fills what it reads and returns the usage error's line, if any.
template <typename Value>
std::optional<std::string> ReadValue(std::string_view option, const std::optional<std::string>& text,
                                     std::optional<Value> (*parse)(std::string_view), std::string_view expected,
                                     std::optional<Value>& value)
{
    if (!text)
    {
        return std::nullopt;
    }
    value = parse(*text);
    if (!value)
    {
        return std::format("{} needs {}, not \"{}\"", option, expected, *text);
    }
    return std::nullopt;
}

std::optional<std::string> ReadTiming(const OptionTexts& texts, CommandLine& commandLine)
{
    if (auto error = ReadValue(OptionNames::Frames, texts.frames, ParsePositive, "a number greater than 0",
                               commandLine.maxFrames))
    {
        return error;
    }
    if (auto error =
            ReadValue(OptionNames::Pacing, texts.pacing, ParsePacing, "decoupled or faithful", commandLine.pacing))
    {
        return error;
    }
    if (auto error = ReadValue(OptionNames::VSync, texts.vsync, ParseVSync, "on, adaptive or off", commandLine.vsync))
    {
        return error;
    }
    const std::string maxFps =
        std::format("0 or a number from {} to {}", Client::App::MinFpsLimit, Client::App::MaxFpsLimit);
    return ReadValue(OptionNames::MaxFps, texts.maxFps, ParseMaxFps, maxFps, commandLine.maxFps);
}

// Its own message, which does not repeat the bytes that are not UTF-8.
std::optional<std::string> ReadPath(std::string_view option, const std::optional<std::string>& text,
                                    std::optional<std::filesystem::path>& path)
{
    if (!text)
    {
        return std::nullopt;
    }
    path = Client::App::PathFromArgument(*text);
    if (!path)
    {
        return std::format("{} needs a path in UTF-8", option);
    }
    return std::nullopt;
}

std::optional<ServerTexts> OptionServer(const OptionTexts& texts)
{
    if (!texts.host && !texts.port)
    {
        return std::nullopt;
    }
    return ServerTexts{.hostName = std::string(OptionNames::Host),
                       .host = texts.host,
                       .portName = std::string(OptionNames::Port),
                       .port = texts.port};
}

// A half server is a mistake of whoever starts the client: the config does not complete it.
Client::Core::Expected<ServerAddress, std::string> ReadServer(const ServerTexts& texts)
{
    if (!texts.port)
    {
        return Unexpected(std::format("{} needs {}", texts.hostName, texts.portName));
    }
    if (!texts.host)
    {
        return Unexpected(std::format("{} needs {}", texts.portName, texts.hostName));
    }
    auto host = Client::App::ParseHost(*texts.host);
    if (!host.HasValue())
    {
        return Unexpected(std::format("{}: {}", texts.hostName, host.Error()));
    }
    const auto port = Client::App::ParsePort(*texts.port);
    if (!port.HasValue())
    {
        return Unexpected(std::format("{}: {}", texts.portName, port.Error()));
    }
    return ServerAddress{std::move(host).Value(), port.Value()};
}
} // namespace

namespace Client::App
{
std::optional<std::string> ReadOptions(const OptionTexts& texts, CommandLine& commandLine)
{
    commandLine.stats = texts.stats;
    commandLine.autoLogin = texts.autoLogin;
    if (auto error = ReadTiming(texts, commandLine))
    {
        return error;
    }
    if (auto error = ReadPath(OptionNames::Config, texts.configPath, commandLine.configPath))
    {
        return error;
    }
    return ReadPath(OptionNames::Data, texts.dataPath, commandLine.dataPath);
}

std::optional<std::string> ReadConnect(const OptionTexts& texts, const std::optional<ServerTexts>& launcher,
                                       CommandLine& commandLine)
{
    const std::optional<ServerTexts> options = OptionServer(texts);
    if (options && launcher)
    {
        return std::format("{} and {} do not go with {} and {}", launcher->hostName, launcher->portName,
                           options->hostName, options->portName);
    }
    const std::optional<ServerTexts>& server = launcher ? launcher : options;
    if (!server)
    {
        return std::nullopt;
    }
    auto address = ReadServer(*server);
    if (!address.HasValue())
    {
        return address.Error();
    }
    commandLine.connect = std::move(address).Value();
    return std::nullopt;
}
} // namespace Client::App
