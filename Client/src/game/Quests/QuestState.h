#pragma once

#include "Tables/QuestTable.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The original's quest object, [0x710148] (a static global that 0x41cb60 builds from 0x418ce0; vtable 0x6b89a0).
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las misiones". The window state, +0x21383, lives in
// Ui::DelegatedPanels::quest, because the panel registry queries it as panel 8.
namespace Client::Quests
{
// +0x2134c: the state bytes that the A0 copies (0x418e20 zeroes twelve dwords and a word).
inline constexpr std::size_t QuestStateBytes = 50;
// +0x21383: 1 is the NPC dialog window (0x4198b0; CanOpen opens nothing with it), 2 the list (0x419840).
inline constexpr std::uint8_t QuestDialogWindow = 1;
inline constexpr std::uint8_t QuestListWindow = 2;
// +0x21386: the states of a quest (two bits each) and the 5 of 0x418fb0 when a condition is not met.
inline constexpr std::uint8_t QuestNotStarted = 0;
inline constexpr std::uint8_t QuestInProgress = 1;
inline constexpr std::uint8_t QuestDone = 2;
inline constexpr std::uint8_t QuestAvailable = 3;
inline constexpr std::uint8_t QuestConditionFailed = 5;
// +4 before the first A0: the game's first step (0x63693a) and the arrival at an NPC (0x5676a6) send the A0 with it.
inline constexpr std::uint8_t QuestClassUnknown = 0xff;

struct QuestState
{
    // +4 and +5: the hero's class & 7 and class >> 3, which the A0 (0x418e20) and 0x419600 copy. 0x41cb60 sets +4 to
    // 0xff and so does 0x4c2480 (the return to selection).
    std::uint8_t characterClass = QuestClassUnknown;
    std::uint8_t evolution = 0;
    // +6 to +9: those the 9F writes (0x4ddaa0) with pkt[3] from 0 to 3. Nothing else ported reads them (*static*).
    std::array<std::uint8_t, 4> flags{};
    // +0xc: the 200 records of quest.bmd (0x418d10).
    Tables::QuestTable records = Tables::QuestTable(Tables::QuestCount);
    // +0x2134c: two bits per quest, as the GS sends them, although 0x419560 reads them shifted (Q-C841).
    std::array<std::uint8_t, QuestStateBytes> states{};
    // +0x2137e: the current quest (a byte: 0 − 1 is 255, Q-C842).
    std::uint8_t current = 0;
    // +0x2137f: the highest one that arrived from the server.
    std::uint8_t reached = 0;
    // +0x21380 and +0x21381: 0x419840 and 0x4c2480 set them to 0; 0x41b9e0 reads them for the list's item.
    std::uint8_t byte21380 = 0;
    std::uint8_t byte21381 = 0;
    // +0x21382: 0x41cb60 and 0x4c2480 set it to 0; nothing ported reads it.
    std::uint8_t byte21382 = 0;
    // +0x21384: the Dialog.bmd dialog that the window shows (0x418f40, or that of the failed condition).
    std::int16_t dialog = 0;
    // +0x21386: the state of the current quest (0x419560(−1)), or 5.
    std::uint8_t currentState = 0;
    // +0x21387: padding. 0x419560 reads it with quests 236 to 239, and the A1 and A2 of those quests or an A0 of more
    // than 59 bytes overwrite it (Q-C1126).
    std::uint8_t byte21387 = 0;
    // +0x21388: the zen that the condition requires (0x418fb0).
    std::uint32_t zen = 0;
    // +0x2138c: the list's tab (0x41b170 only handles tab 0).
    std::uint8_t tab = 0;
    // +0x2138d: the A0 sets it to 1 so that 0x419600 copies the hero's class again.
    std::uint8_t refreshClass = 0;
    // +0x21390 and +0x21394: the window's corner (0x41cb60: 0x1c2 and 0; 0x41b5a0 sets them every step).
    std::int32_t x = 0x1c2;
    std::int32_t y = 0;
};
} // namespace Client::Quests
