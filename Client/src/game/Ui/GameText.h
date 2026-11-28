#pragma once

#include "Tables/TextTable.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The Text.bmd texts the login UI builds (CL5.5). The original uses them as sprintf formats, like the scene's 456
// (Q-C61); MuRet does not: it replaces each "%s" and each "%d", in order, with the next argument, and leaves the rest
// as it is.
namespace Client::Ui
{
// `format` with its first "%s", "%d" or "%u" replaced by arguments[0], the next by arguments[1], and so on (the %u is
// the HUD's 201, the experience). A mark without an argument stays; an extra argument is dropped; a "%" that is not
// "%s", "%d" or "%u", "%%" included, stays as it is. What comes in from an argument is not read again. "% d", the one
// of the 380's count (0x647ef0), is the "%d" with sprintf's space flag: a number without a sign gets a space in front.
[[nodiscard]] std::string PlaceArguments(std::string_view format, std::span<const std::string_view> arguments);

// None without a table or without that index.
[[nodiscard]] std::optional<std::string_view> TextAt(const Tables::TextTable* texts, std::size_t index);

// 0x632c90(text, rows, maxLines, lineBytes): `text`, up to its first '\0', in rows shorter than `lineBytes` bytes, and
// at most `maxLines` (1 or more). Copies character by character and, if the next one would make the row reach
// `lineBytes`, cuts it: if there was a space in that row less than the smaller of lineBytes / 2 and 10 bytes before
// the one that did not fit, after the space, which stays at the end, and goes on from there; otherwise, right before
// the one that did not fit. What does not fit in the last one is dropped. Always gives at least one row. One byte per
// character, like the original's _mbclen on a single-byte code page (06 § La lista de servidores, *inferred* for the
// lab); with a double-byte one, the original does not split a two-byte character.
[[nodiscard]] std::vector<std::string> SplitIntoLines(std::string_view text, std::size_t maxLines,
                                                      std::size_t lineBytes);
} // namespace Client::Ui
