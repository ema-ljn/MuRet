#include "Core/Windows1252.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Core
{
namespace
{
constexpr std::array<char32_t, 32> Extension = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008d, 0x017d, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
    0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
};

void AppendUtf8(std::string& text, char32_t codePoint)
{
    if (codePoint <= 0x7f)
    {
        text.push_back(static_cast<char>(codePoint));
        return;
    }
    if (codePoint <= 0x7ff)
    {
        text.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        return;
    }
    text.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
    text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
}
} // namespace

char32_t Windows1252CodePoint(std::uint8_t byte)
{
    if (byte < 0x80U || byte >= 0xa0U)
    {
        return static_cast<char32_t>(byte);
    }
    return Extension[static_cast<std::size_t>(byte - 0x80U)];
}

std::string DecodeWindows1252(std::string_view bytes)
{
    std::string utf8;
    for (const char byte : bytes)
    {
        const char32_t codePoint = Windows1252CodePoint(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        AppendUtf8(utf8, codePoint);
    }
    return utf8;
}
} // namespace Client::Core
