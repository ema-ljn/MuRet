#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

// The original's sprintf and wsprintfA with their arguments as they are, for the Text.bmd texts the original uses as
// formats. Unlike Ui::PlaceArguments it looks at each argument's type: a text is passed by its address (g_GlobalText,
// 0x76bd1dc + index · 300), and a "%d" that takes it prints that address, as the original does with texts 853 and 868
// of the Spanish Data.
namespace Client::Crt
{
// 0x76bd1dc: Text.bmd's first record in memory, 300 bytes each (06 § Los textos).
inline constexpr std::uint32_t TextTableAddress = 0x076bd1dc;
inline constexpr std::uint32_t TextRecordBytes = 300;

// Its index (for the address) and its content.
struct TextArgument
{
    std::size_t index;
    std::string_view text;
};

// Another text by pointer (the hero's name, +0x1d9 of [0x743a170]): its address, if MuRet knows it. A "%d" or "%c"
// that takes it without one fails.
struct StringArgument
{
    std::string_view text;
    std::optional<std::uint32_t> address{};
};

// A number by value (a DWORD on the stack) or a text by pointer.
using FormatArgument = std::variant<std::int32_t, TextArgument, StringArgument>;

enum class FormatFunction : std::uint8_t
{
    // 0x69fd9b, the CRT's sprintf: flags "-+ #0", width and precision. "% d" puts a space before an unsigned number,
    // and with "%s" the space does nothing.
    Sprintf,
    // user32's wsprintfA ([0x6b851c]): only the flags "-", "#" and "0". *Inferred* (P-C290): another character after
    // the flags, width and precision (the ' ' of "% d") is copied without taking an argument, and the 'd' after it
    // comes out as text.
    Wsprintf,
};

// Conversions d, i, u, x, X, c, s and %%. A "%s" with a number (the original reads a bogus pointer), a conversion
// without an argument (it reads the stack) or one MuRet lacks fail with an error at `where`.
[[nodiscard]] ENGINE_API Core::Expected<std::string, std::string>
FormatOriginal(FormatFunction function, std::string_view format, std::span<const FormatArgument> arguments,
               std::string_view where);
} // namespace Client::Crt
