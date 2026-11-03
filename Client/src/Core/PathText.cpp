#include "Core/PathText.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Core
{
namespace
{
// UTF-8 bien formado, como lo describe PathFromUtf8 en PathText.h.
bool IsUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto first = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        std::uint32_t codePoint = first;
        if (first >= 0xC2 && first <= 0xDF)
        {
            length = 2;
            codePoint = first & 0x1Fu;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            length = 3;
            codePoint = first & 0x0Fu;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            length = 4;
            codePoint = first & 0x07u;
        }
        else if (first >= 0x80)
        {
            return false;
        }
        if (text.size() - i < length)
        {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u)
            {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF)
        {
            return false;
        }
        i += length;
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
