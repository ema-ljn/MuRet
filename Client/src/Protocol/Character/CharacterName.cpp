#include "Protocol/Character/CharacterName.h"

#include "Protocol/Character/Opcodes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Protocol::Character
{
std::array<std::byte, NameSize> NameUpToItsEnd(std::span<const std::byte, NameSize> name)
{
    std::array<std::byte, NameSize> upToItsEnd{};
    std::copy(name.begin(), std::ranges::find(name, std::byte{0}), upToItsEnd.begin());
    return upToItsEnd;
}

std::optional<std::array<std::byte, NameSize>> NameFromText(std::string_view text)
{
    if (text.size() > NameSize || text.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }
    std::array<std::byte, NameSize> name{};
    std::ranges::transform(text, name.begin(), [](char value) { return static_cast<std::byte>(value); });
    return name;
}
} // namespace Client::Protocol::Character
