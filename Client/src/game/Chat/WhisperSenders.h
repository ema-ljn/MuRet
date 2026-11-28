#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Those who whispered while the own level was below 10 (CL8 milestone 3; 06 § El chat, Q-C453): the .bss list at
// 0x77997f8, which 0x554d20 fills on receiving a 02 and 0x554a40 reads on sending one. They are ten consecutive slots
// of 10 bytes, which strcmp and strcpy read and write as texts: a 10-byte name leaves its '\0' in the next slot and is
// read followed by it.
namespace Client::Chat
{
inline constexpr std::size_t WhisperSenderSlots = 10;
inline constexpr std::size_t WhisperSenderSlotBytes = 10;
// 0x555030 empties 110 bytes (0x1b DWORDs and a WORD): the ten slots and the next 10, where the last one's '\0' falls.
inline constexpr std::size_t WhisperSendersBytes = 110;
// 0x554d20(10, name): the level at which it stops noting.
inline constexpr std::uint16_t WhisperSenderLevel = 10;

struct WhisperSenders
{
    std::array<char, WhisperSendersBytes> bytes{};
    // 0x77f3b9c: the slot written next, from 0 to 9.
    std::int32_t next = 0;
};

// The slot `slot` as strcmp reads it: up to its '\0', which can be in the next slot.
[[nodiscard]] std::string_view WhisperSenderAt(const WhisperSenders& senders, std::size_t slot);

// 0x554f89..0x55501f, with the level already read: below 10, if no slot has the name (strcmp), it copies it with
// strcpy into the next one and advances it from 0 to 9. `name` goes up to its '\0'; MuRet cuts it at the bytes left
// from the slot (Q-C512).
void RememberTheWhisperSender(WhisperSenders& senders, std::uint16_t level, std::string_view name);

// 0x555030: the 110 bytes at 0; the next slot stays.
void ClearTheWhisperSenders(WhisperSenders& senders);
} // namespace Client::Chat
