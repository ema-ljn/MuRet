#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Data\Local\quest.bmd, the quests of the object [0x710148] (05-formatos-de-datos.md, "quest.bmd"; in the original
// 0x418d10, which the game data load, 0x61ccb0, calls at 0x61eb7c, after Dialog.bmd and before movereq.bmd).
// 0x418d10 zeroes the 200 records from +0xc (34,000 dwords) and reads 0x2a8 bytes 200 times with fread into an
// operator new block, runs XOR3 from its first byte (0x418de0, FC CF AB with 0x6c64b4's key) and copies them in
// place. It does not check how much it read: with a short file the CRT's fread (0x69fecb) leaves in the block what it
// did not read, the previous record already decrypted, and decrypts it again; MuRet does the same. Below 0x2a8 bytes
// that is the uninitialized block (Q-C1127). Without the file it returns 0 without touching the records, and 0x61ccb0
// does not check. *Verified* with the repo's: 136,000 bytes, and only the first four records are non-zero.
namespace Client::Tables
{
inline constexpr std::size_t QuestCount = 200;
inline constexpr std::size_t QuestRecordSize = 0x2a8;
inline constexpr std::size_t QuestTableSize = QuestCount * QuestRecordSize;
inline constexpr std::size_t QuestNameSize = 0x20;
inline constexpr std::size_t QuestEntryCount = 16;
inline constexpr std::size_t QuestConditionCount = 16;
// +7 to +0xb of each entry: one mark per class, indexed by the object's +4 class & 7 (0x418f40 and 0x418fb0). 0x418e20
// does not clamp it: a class 5, 6 or 7 reads the entry's +0xc bytes (its dialogs).
inline constexpr std::size_t QuestClassFlagCount = 5;
inline constexpr std::size_t QuestDialogCount = 4;

// One of the quest's entries (16 of 0x14 bytes from +0x26): the item to bring and each state's dialogs.
struct QuestEntry
{
    // +0: always 1 in the file; nobody reads it (*static*).
    std::uint8_t byte0 = 0;
    // +1: 1 for an item entry (0x4194c0 and 0x419b10 only look at those).
    std::uint8_t kind = 0;
    // +2 and +3: the item's group and index (the type is group · 0x200 + index, 0x4194e6).
    std::uint8_t itemGroup = 0;
    std::uint8_t itemIndex = 0;
    // +4: the level 0x558410 asks for (the item's (+4 >> 3) & 0xf).
    std::uint8_t itemLevel = 0;
    // +5: how many.
    std::uint8_t itemCount = 0;
    // +6: the link to the conditions (each one's +1; 0xff in the condition applies to all).
    std::uint8_t condition = 0;
    // +7 to +0xb: each class's mark (0 no; in the file, 1 or 2).
    std::array<std::uint8_t, QuestClassFlagCount> classes{};
    // +0xc: the Dialog.bmd dialogs (0x418f40 with the slot): 0 before accepting, 1 items missing, 2 has them and 3
    // finished.
    std::array<std::int16_t, QuestDialogCount> dialogs{};

    friend bool operator==(const QuestEntry&, const QuestEntry&) = default;
};

// A condition (16 of 0x14 bytes from +0x168), the one 0x418fb0 checks.
struct QuestCondition
{
    // +0: always 1 in the file; nobody reads it (*static*).
    std::uint8_t byte0 = 0;
    // +1: the entry it applies to (the entry's +6), or 0xff for all.
    std::uint8_t link = 0;
    // +2: the quest that must be finished (state 2), or −1.
    std::int16_t needQuest = 0;
    // +4 and +6: the minimum and maximum level (0 is no cap; both compared unsigned).
    std::uint16_t minLevel = 0;
    std::uint16_t maxLevel = 0;
    // +8 to +0xb: zero in the file; nobody reads it (*static*).
    std::uint32_t dword8 = 0;
    // +0xc: unsigned; 0 asks for none.
    std::uint32_t zen = 0;
    // +0x10: the dialog when it is not met.
    std::int16_t failDialog = 0;
    // +0x12: zero in the file; nobody reads it.
    std::int16_t word12 = 0;

    friend bool operator==(const QuestCondition&, const QuestCondition&) = default;
};

// 0x2a8 bytes.
struct QuestRecord
{
    // +0 and +2: how many entries and conditions (signed: 0x418fb0 compares them with JLE and JL).
    std::int16_t entryCount = 0;
    std::int16_t conditionCount = 0;
    // +4: the type of the NPC that gives it (0xeb, 0xe5 in the file).
    std::uint8_t npcType = 0;
    // +5: raw (NameOf cuts at the first zero).
    std::array<char, QuestNameSize> name{};
    std::array<QuestEntry, QuestEntryCount> entries{};
    std::array<QuestCondition, QuestConditionCount> conditions{};

    friend bool operator==(const QuestRecord&, const QuestRecord&) = default;
};

// Always QuestCount records (on the heap: 136 KB).
using QuestTable = std::vector<QuestRecord>;

// Up to its first zero, or the 32 bytes if it has none (the original would go on into +0x25 and the entries).
[[nodiscard]] std::string_view NameOf(const QuestRecord& quest);

// Decrypts `file`'s first QuestTableSize bytes like 0x418d10, with what is missing as its fread leaves it.
// FileTooShort below one record. The excess is ignored.
[[nodiscard]] Core::Expected<QuestTable, Data::DataError> LoadQuestTable(std::span<const std::byte> file);
} // namespace Client::Tables
