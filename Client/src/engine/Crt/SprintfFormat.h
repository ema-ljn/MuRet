#pragma once

#include "EngineApi.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// The original CRT's sprintf (0x69fd9b) with a Text.bmd text as the format, as the tooltips, the set banner and the
// character window use it (Q-C590). MuRet does not hand the text to a CRT: it walks it with _output's state machine
// (0x6a5904, with the class and state table at 0x6bdb14, read from the binary; what each state does is VC6's output.c,
// *inferred*) and puts its own arguments, one by one, in each conversion.
namespace Client::Crt
{
// A 32-bit sprintf stack argument: an integer (what the original pushes, signed or not by who reads it) or a text
// (a %s pointer).
class SprintfArgument
{
public:
    SprintfArgument(std::int32_t value) : m_bits(static_cast<std::uint32_t>(value)) {}
    SprintfArgument(std::uint32_t value) : m_bits(value) {}
    SprintfArgument(std::string_view text) : m_text(text), m_isText(true) {}

    [[nodiscard]] bool IsText() const
    {
        return m_isText;
    }
    [[nodiscard]] std::uint32_t Bits() const
    {
        return m_bits;
    }
    [[nodiscard]] std::string_view Text() const
    {
        return m_text;
    }

private:
    std::uint32_t m_bits = 0;
    std::string_view m_text{};
    bool m_isText = false;
};

// As sprintf writes it: flags '-', '+', ' ', '#' and '0', width (or '*'), precision and sizes 'h', 'l', 'I' and 'L'
// (no effect in 32 bits); types d, i, u, o, x, X, c and s, and "%%"; I64 with integers takes two 32-bit arguments
// (low and high), like Castle Senior's treasure (0x6688bc); a character that breaks a conversion (the ')' of "25%)")
// comes out alone, without what the conversion collected, and a conversion that ends the text writes nothing, like the
// CRT. Where the original would read from the stack what it was not given (a conversion without an argument, a %s
// with an integer, a %d with a text or a floating-point type), MuRet leaves the conversion as written and does not
// use up the argument.
[[nodiscard]] ENGINE_API std::string FormatLikeSprintf(std::string_view format,
                                                       std::span<const SprintfArgument> arguments);

template <typename... Arguments> [[nodiscard]] std::string Sprintf(std::string_view format, Arguments... arguments)
{
    const std::array<SprintfArgument, sizeof...(Arguments) + 1> list{SprintfArgument(arguments)..., SprintfArgument(0)};
    return FormatLikeSprintf(format, std::span<const SprintfArgument>(list.data(), sizeof...(Arguments)));
}
} // namespace Client::Crt
