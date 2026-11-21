#include "App/CommandLineSystem.h"

#include "Core/Expected.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::App
{
SystemSyntax ThisSystemSyntax()
{
    return {.style = OptionStyle::Gnu, .operands = {}, .helpFooter = {}, .helpWord = {}};
}

// What is not an option is not for MuRet, the launcher's connect /u<host> /p<port> too: here the convention is --name.
Core::Expected<std::optional<ServerTexts>, std::string> ReadSystemArguments(std::span<const std::string> arguments)
{
    const auto option = std::ranges::find_if(arguments, [](std::string_view argument)
                                             { return argument.size() > 1 && argument.front() == '-'; });
    if (option != arguments.end())
    {
        return Core::Unexpected(std::format("unknown option \"{}\"", *option));
    }
    return std::optional<ServerTexts>();
}

// An argument is the file system's bytes, and path keeps them, so a name that is not UTF-8 works too.
std::optional<std::filesystem::path> PathFromArgument(std::string_view argument)
{
    return std::filesystem::path(argument);
}
} // namespace Client::App
