#include "Protocol/FrontEnd/CharacterName.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace Client::Protocol::FrontEnd
{
NameBytes NameUpToItsEnd(std::span<const std::byte, NameSize> name)
{
    NameBytes upToItsEnd{};
    std::copy(name.begin(), std::ranges::find(name, std::byte{0}), upToItsEnd.begin());
    return upToItsEnd;
}

std::string NameAsText(std::span<const std::byte, NameSize> name)
{
    std::string text;
    for (const std::byte value : std::ranges::subrange(name.begin(), std::ranges::find(name, std::byte{0})))
    {
        text.push_back(static_cast<char>(value));
    }
    return text;
}

std::optional<NameBytes> NameFromText(std::string_view text)
{
    if (text.size() > NameSize || text.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }
    NameBytes name{};
    std::ranges::transform(text, name.begin(), [](char value) { return static_cast<std::byte>(value); });
    return name;
}
} // namespace Client::Protocol::FrontEnd
