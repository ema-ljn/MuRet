#include "Core/Utf8.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace Client::Core
{
std::optional<char32_t> ReadUtf8CodePoint(std::string_view text, std::size_t& offset)
{
    if (offset >= text.size())
    {
        return std::nullopt;
    }

    constexpr unsigned char ContinuationMask = 0xc0;
    constexpr unsigned char ContinuationBits = 0x80;
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    char32_t codePoint = first;
    if (first >= 0xc2 && first <= 0xdf)
    {
        length = 2;
        codePoint = first & 0x1f;
    }
    else if (first >= 0xe0 && first <= 0xef)
    {
        length = 3;
        codePoint = first & 0x0f;
    }
    else if (first >= 0xf0 && first <= 0xf4)
    {
        length = 4;
        codePoint = first & 0x07;
    }
    else if (first >= 0x80)
    {
        return std::nullopt;
    }

    if (text.size() - offset < length)
    {
        return std::nullopt;
    }
    for (std::size_t index = 1; index < length; ++index)
    {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & ContinuationMask) != ContinuationBits)
        {
            return std::nullopt;
        }
        codePoint = (codePoint << 6) | (next & 0x3f);
    }
    if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000) ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff)
    {
        return std::nullopt;
    }
    offset += length;
    return codePoint;
}
} // namespace Client::Core
