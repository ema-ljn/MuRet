#pragma once

#include "Core/Expected.h"
#include "Chat/ChatInput.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>

// Data\Macro.txt (0x6c649c), the ten macros of 0x77f1db0 (CL8 milestone 3; 06 § El chat). WinMain reads it once,
// after Text.bmd (0x61ee30 → 0x61c1c0); it is written by the game's exit, the server change and the gate to maps 30
// and 31 (0x61c170 from 0x412b6d, 0x59ad45, 0x624067, 0x624edf and 0x624f1d), which belong to other waves. What the
// player stores in their macros does not go to the log. *Static*.
namespace Client::Chat
{
// 0x77f1db0: each macro in 0x100 bytes with its '\0'.
inline constexpr std::size_t MacroSlotBytes = 0x100;

using Macros = std::array<std::string, MacroCount>;

// 0x61c1c0: fopen("rt") and ten fscanf("%s"), one word per macro in its order. "%s" skips the blank (0x09..0x0d and
// the space, in the "C" locale) and reads up to the next, so a macro with spaces comes back split (Q-C449); at the end
// of the file, the missing ones stay as they were (empty: the read is the first). The CRT's text mode ends the file
// at the first Ctrl-Z (0x1a). "%s" has no width: a word of MacroSlotBytes or more overwrites the next macro or what
// follows 0x77f27b0 (Q-C531); MuRet does not load it and returns the error.
[[nodiscard]] Core::Expected<Macros, std::string> ReadTheMacros(std::span<const std::byte> file);

// 0x61c170: fopen("wt") and ten fprintf("%s\n"), with the text mode's "\r\n". It does not check whether it opened.
[[nodiscard]] std::string WrittenMacros(const Macros& macros);
} // namespace Client::Chat
