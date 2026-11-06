#include "support/HexFloatFixture.h"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <istream>
#include <string>
#include <system_error>

namespace Client::Tests
{
float ReadHexFloat(std::istream& input)
{
    std::string word;
    if (!(input >> word))
        return 0.0f;
    constexpr std::size_t HexDigits = 8;
    std::uint32_t bits = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), bits, 16);
    if (word.size() != HexDigits || error != std::errc{} || end != word.data() + word.size())
    {
        input.setstate(std::ios::failbit);
        return 0.0f;
    }
    return std::bit_cast<float>(bits);
}
} // namespace Client::Tests
