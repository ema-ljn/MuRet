#include "Core/Ascii.h"

#include <algorithm>
#include <functional>
#include <string_view>

namespace Client::Core
{
char ToLowerAscii(char c)
{
    // Unsigned: with a signed char, bytes >= 0x80 are negative, so they stay outside A to Z either way.
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 'A' || byte > 'Z')
    {
        return c;
    }
    return static_cast<char>(byte - 'A' + 'a');
}

// The standard library has no case-insensitive comparison; ranges::equal projects both sides through ToLowerAscii and
// returns false on different sizes without comparing.
bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, std::ranges::equal_to{}, ToLowerAscii, ToLowerAscii);
}
} // namespace Client::Core
