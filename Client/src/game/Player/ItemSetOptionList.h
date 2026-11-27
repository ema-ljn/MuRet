#pragma once

#include "Core/Expected.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// The set option list of [0x6eed3c] that 0x40e1b0 leaves and that 0x40d690 (equal to 0x40da20, byte for byte but for
// the addresses), 0x40d730 and 0x40d7f0 read, the three thiscall on [0x6eed3c]. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los ocho helpers de los stats propios". A typed projection of
// +0x9348..+0x940b, not the object. 0x40e1b0 and 0x40d010 fill it (Player/OwnItemSets.h), and it lives in
// Player::ItemSetState with the rest of [0x6eed3c]; the helpers get it as a pointer, nullptr while 0x550d10 does not
// pass it.
namespace Client::Player
{
// The 16 three-byte entries of +0x934c end at +0x937c, where their 16 marks start.
inline constexpr std::size_t ItemSetOptionCapacity = 16;

struct ItemSetOptionList
{
    // +0x9348 and +0x9349: the list holds the sum of both.
    std::uint8_t count9348 = 0;
    std::uint8_t count9349 = 0;
    // +0x934a and +0x934b: the last set of each piece kind, from 0x40d010. None of the three reads them.
    std::array<std::uint8_t, 2> setIds{};
    // +0x934c, every 3 bytes: the first byte is the selector minus 0x8c; the second, the hero's class flag that
    // 0x40d010 sets, and 0x40d7f0 asks for it other than 0. 0x40d010 does not write the third (only the 16 bytes that
    // 0x40e1b0 fills at the start of each pass) and none of the three reads it.
    std::array<std::array<std::uint8_t, 3>, ItemSetOptionCapacity> entries{};
    // +0x937c: each entry is used once until 0x550d10 zeroes them (06, its step 2).
    std::array<std::uint8_t, ItemSetOptionCapacity> used{};
    // +0x938c, every 8 bytes: each entry's signed DWORD.
    std::array<std::int32_t, ItemSetOptionCapacity> values{};
    // +0x9390 + 8 · i, the other 4 bytes of each value: 0x40d010 writes the same value there and nothing ported reads
    // them.
    std::array<std::int32_t, ItemSetOptionCapacity> secondValues{};
};

// 0x40d690 (and 0x40da20): adds the values of the unused entries whose first byte is selector − 0x8c, marks them, and
// adds that 32-bit sum's low 16 bits to `word`. With more than 16 entries the original reads the marks as entries: an
// error, touching nothing.
[[nodiscard]] Core::Expected<void, std::string> AddItemSetOptionValues(ItemSetOptionList& list, std::uint16_t& word,
                                                                       std::uint32_t selector);

// 0x40d730: the same sum S, and `word` += (word · S) / 100: the signed 32-bit product, which wraps, and the signed
// integer division, which truncates toward 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemSetOptionPercent(ItemSetOptionList& list, std::uint16_t& word,
                                                                          std::uint32_t selector);

// 0x40d7f0: with the selector's unused entries whose second byte is not 0, the last one's value V and how many there
// are, N; it marks them. `word` += ((base · V) / 100) · N, with an unsigned base, 32-bit products and a signed
// division.
[[nodiscard]] Core::Expected<void, std::string> AddItemSetOptionPercentOf(ItemSetOptionList& list, std::uint16_t& word,
                                                                          std::uint16_t base, std::uint32_t selector);

// 0x40d8c0 (0x5513d0's magic): adds the values of the unused entries whose first byte is above 0x17 and, minus 0x1d,
// equal to `selector`'s low byte, and whose second byte is not 0; it marks them, and adds the 32-bit sum to `value` if
// it is not 0. With more than 16 entries, an error touching nothing, as 0x40d690.
[[nodiscard]] Core::Expected<void, std::string> AddSkillSetOptionValues(ItemSetOptionList& list, std::int32_t& value,
                                                                        std::uint32_t selector);
} // namespace Client::Player
