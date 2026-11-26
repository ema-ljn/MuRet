#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// A character's name as the selection's requests build it: 0x63d990 (create) and 0x643740 (delete).
namespace Client::Protocol::FrontEnd
{
// Ten bytes, with no terminator when the name fills them (Q-C07).
inline constexpr std::size_t NameSize = 10;
using NameBytes = std::array<std::byte, NameSize>;

// The bytes up to the first zero, then zeros: what followed that zero is not sent.
[[nodiscard]] NameBytes NameUpToItsEnd(std::span<const std::byte, NameSize> name);

// Up to the first zero, or all ten bytes: the %s of notice 0x5e reads the name at +0x1d9, its terminator at +0x1e3.
[[nodiscard]] std::string NameAsText(std::span<const std::byte, NameSize> name);

// std::nullopt for a text that does not fit or holds a zero: 0x63d990 sends a longer name whole and the packet grows,
// MuRet does not send it.
[[nodiscard]] std::optional<NameBytes> NameFromText(std::string_view text);
} // namespace Client::Protocol::FrontEnd
