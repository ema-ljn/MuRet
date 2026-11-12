#include "UI/MacroFile.h"

#include "Core/Expected.h"
#include "UI/ChatInput.h"

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <utility>

namespace Client::UI
{
namespace
{
// El fin de archivo del modo texto de la CRT (_read).
constexpr std::byte TextModeEnd{0x1a};

// isspace() del locale "C", el que usa "%s".
bool IsBlank(std::byte byte)
{
    const auto value = std::to_integer<unsigned>(byte);
    return value == ' ' || (value >= '\t' && value <= '\r');
}
} // namespace

Core::Expected<Macros, std::string> ReadTheMacros(std::span<const std::byte> file)
{
    std::size_t end = 0;
    while (end < file.size() && file[end] != TextModeEnd)
        ++end;
    Macros macros{};
    std::size_t at = 0;
    for (std::size_t macro = 0; macro < MacroCount; ++macro)
    {
        while (at < end && IsBlank(file[at]))
            ++at;
        if (at == end)
            break;
        std::string word;
        while (at < end && !IsBlank(file[at]))
            word.push_back(static_cast<char>(file[at++]));
        if (word.size() >= MacroSlotBytes)
            return Core::Unexpected(std::format(
                "0x61c1c0: la palabra de la macro {} tiene {} bytes y desborda los 0x100 de su lugar (Q-C531)", macro,
                word.size()));
        macros[macro] = std::move(word);
    }
    return macros;
}

std::string WrittenMacros(const Macros& macros)
{
    std::string written;
    for (const std::string& macro : macros)
    {
        written += macro;
        written += "\r\n";
    }
    return written;
}
} // namespace Client::UI
