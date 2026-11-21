#include "App/CommandLineSystem.h"

#include "Core/Expected.h"

#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::App
{
OptionStyle SystemOptionStyle()
{
    return OptionStyle::Gnu;
}

std::string_view SystemUsage()
{
    return "Usage: Main [OPTIONS]";
}

std::string_view SystemHelpFooter()
{
    return {};
}

// An unknown option is a usage error. The rest is not for MuRet and is ignored, the launcher's connect /u<host>
// /p<port> too, because on Linux the convention is --name (Emanuel, 2026-11-21). So is everything after --.
Core::Expected<Leftovers, std::string> ReadLeftovers(std::span<const std::string> arguments)
{
    for (const std::string& argument : arguments)
    {
        if (argument == "--")
        {
            break;
        }
        if (argument.size() > 1 && argument.front() == '-')
        {
            return Core::Unexpected(std::format("unknown option \"{}\"", argument));
        }
    }
    return Leftovers{};
}
} // namespace Client::App
