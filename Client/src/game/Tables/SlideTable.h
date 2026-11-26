#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Slide.bmd, the texts of the banner that crosses the screen in game (05-formatos-de-datos.md, "Slide.bmd"; in the
// original 0x46b3b0, which the game data load, 0x61ccb0, calls at 0x61ed18). 0xa030 bytes encrypted with XOR3 from
// the first (0x4c1690, 0x6c82e4's key): the timer period in seconds, the banner speed (a float) and five level
// brackets of 0x2008 bytes, each with the bracket's highest level, the text count and 32 texts of 0x100 bytes.
// *Verified* with the repo's Slide.bmd: 60 seconds, 2.5 and the brackets (15, 28), (30, 20), (50, 19), (0, −1) and
// (0, −1).
namespace Client::Tables
{
inline constexpr std::size_t SlideTableSize = 0xa030;
inline constexpr std::size_t SlideGroupCount = 5;
inline constexpr std::size_t SlideGroupSize = 0x2008;
inline constexpr std::size_t SlideTextSize = 0x100;

// The object [0x5763528]'s +0x11b0, +0x11c4 and +0x11d8 list.
struct SlideGroup
{
    // 0x46b680 picks the first with one's own level lower or equal.
    std::int32_t maxLevel = 0;
    // The file's count, which divides 0x46b680's rand() even if negative.
    std::int32_t count = 0;
    // Those 0x46b3b0 reads with that count, in order.
    std::vector<std::string> texts;
};

struct SlideTable
{
    // +0x11ac: timer 0x3eb's period, in seconds.
    std::int32_t periodSeconds = 0;
    // +0x1218: the timer banners' speed.
    float speed = 0.0f;
    std::array<SlideGroup, SlideGroupCount> groups{};
};

// Reads the brackets like 0x46b3b0: with a positive count, texts j = 0..count − 1 from 8 + 0x2008 i + 8 + 0x100 j,
// each up to its first zero (strlen and strncpy). The original checks neither how much fread read nor where a text
// ends: a short file leaves what was on the stack (FileTooShort, Q-C516), and a count over 32 or a text without a
// zero goes on into the next bracket or out of the buffer (InvalidData if past the end). The excess is ignored.
[[nodiscard]] Core::Expected<SlideTable, Data::DataError> LoadSlideTable(std::span<const std::byte> file);
} // namespace Client::Tables
