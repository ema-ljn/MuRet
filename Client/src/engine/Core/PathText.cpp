#include "Core/PathText.h"

#include "Core/Utf8.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Core
{
namespace
{
// Well-formed as PathFromUtf8 in PathText.h describes.
bool IsUtf8(std::string_view text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        if (!ReadUtf8CodePoint(text, offset).has_value())
        {
            return false;
        }
    }
    return true;
}
} // namespace

std::string PathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::optional<std::filesystem::path> PathFromUtf8(std::string_view text)
{
    if (!IsUtf8(text))
    {
        return std::nullopt;
    }
    std::u8string utf8(text.size(), u8'\0');
    std::ranges::transform(text, utf8.begin(), [](char c) { return static_cast<char8_t>(c); });
    return std::filesystem::path(utf8);
}
} // namespace Client::Core
