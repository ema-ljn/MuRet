#pragma once

#include "App/CommandLine.h"
#include "App/CommandLineSystem.h"

#include <optional>
#include <string>
#include <string_view>

// MuRet's rules for the values that CommandLine.cpp collects with CLI11.
namespace Client::App
{
namespace OptionNames
{
inline constexpr std::string_view Host = "--host";
inline constexpr std::string_view Port = "--port";
inline constexpr std::string_view AutoLogin = "--auto-login";
inline constexpr std::string_view Config = "--config";
inline constexpr std::string_view Data = "--data";
inline constexpr std::string_view Frames = "--frames";
inline constexpr std::string_view Pacing = "--pacing";
inline constexpr std::string_view VSync = "--vsync";
inline constexpr std::string_view MaxFps = "--max-fps";
inline constexpr std::string_view Stats = "--stats";
} // namespace OptionNames

// What CLI11 collected, as typed.
struct OptionTexts
{
    std::optional<std::string> host;
    std::optional<std::string> port;
    std::optional<std::string> configPath;
    std::optional<std::string> dataPath;
    std::optional<std::string> frames;
    std::optional<std::string> pacing;
    std::optional<std::string> vsync;
    std::optional<std::string> maxFps;
    bool autoLogin = false;
    bool stats = false;
};

// Both fill commandLine with what they read and return the usage error's line, if any.
std::optional<std::string> ReadOptions(const OptionTexts& texts, CommandLine& commandLine);

// The server of --host and --port, or of the launcher's arguments (ReadSystemArguments), which do not go together.
std::optional<std::string> ReadConnect(const OptionTexts& texts, const std::optional<ServerTexts>& launcher,
                                       CommandLine& commandLine);
} // namespace Client::App
