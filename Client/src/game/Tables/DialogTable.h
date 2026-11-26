#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Data\Local\Dialog.bmd (05-formatos-de-datos.md, "Dialog.bmd"; in the original 0x54b2b0, which the game data load,
// 0x61ccb0, calls at 0x61eb69 before quest.bmd). 0x54b2b0 reads 0x400 bytes 200 times with fread into an operator new
// block, runs XOR3 from its first byte (0x5496c0, 0x6c8c74's key) and copies them to 0x7688ac0 + 0x400 i. It does not
// check how much it read: with a short file the CRT's fread (0x69fecb) leaves in the block what it did not read, the
// previous record already decrypted, and decrypts it again; MuRet does the same. Below 0x400 bytes that is the
// uninitialized block (Q-C1127). Without the file it shows a MessageBox and closes the window (WM_DESTROY); MuRet goes
// on without dialogs (P-C170). *Verified* with the repo's: 204,800 bytes, 75 non-zero records, at most 3 answers, and
// actions −1, 0, 1, 2 and 3.
namespace Client::Tables
{
inline constexpr std::size_t DialogCount = 200;
inline constexpr std::size_t DialogRecordSize = 0x400;
inline constexpr std::size_t DialogTableSize = DialogCount * DialogRecordSize;
inline constexpr std::size_t DialogTextSize = 0x12c;
// 0x419710 and 0x632f70 write them in 10 lines of 0x26 (0x7d3691c, 0x17c bytes).
inline constexpr std::size_t DialogAnswerCount = 10;
inline constexpr std::size_t DialogAnswerSize = 0x40;

// +0x158 of each answer: what 0x419bd0 does with it (other values do nothing).
enum class DialogAction : std::int32_t
{
    // Checks the quest's conditions with zen (0x418fb0(record, 1)) and, if met, sends the A2.
    CheckAndAccept = 1,
    // Closes the quest window (0x419900, with C1 03 31).
    Close = 2,
    // Sends the A2 without checking.
    Accept = 3,
};

struct DialogRecord
{
    // +0: the raw text (DialogTextOf cuts at the first zero).
    std::array<char, DialogTextSize> text{};
    // +0x12c: signed (0x419765 exits with JLE).
    std::int32_t answerCount = 0;
    // +0x130: the dialog each answer leads to (negative: none).
    std::array<std::int32_t, DialogAnswerCount> next{};
    // +0x158: DialogAction.
    std::array<std::int32_t, DialogAnswerCount> actions{};
    // +0x180: each answer's raw text.
    std::array<std::array<char, DialogAnswerSize>, DialogAnswerCount> answers{};

    friend bool operator==(const DialogRecord&, const DialogRecord&) = default;
};

// Always DialogCount records (on the heap: 200 KB).
using DialogTable = std::vector<DialogRecord>;

// Up to its first zero, or the 0x12c bytes if it has none (the original would go on into +0x12c).
[[nodiscard]] std::string_view DialogTextOf(const DialogRecord& dialog);
// Up to its first zero, or the 0x40 bytes.
[[nodiscard]] std::string_view DialogAnswerOf(const DialogRecord& dialog, std::size_t answer);

// Decrypts `file`'s first DialogTableSize bytes like 0x54b2b0, with what is missing as its fread leaves it.
// FileTooShort below one record; InvalidData if a count exceeds 10: opening that dialog, 0x419710 and 0x632f70 would
// write their lines past 0x7d3691c's 10 (Q-C1127). The excess is ignored.
[[nodiscard]] Core::Expected<DialogTable, Data::DataError> LoadDialogTable(std::span<const std::byte> file);
} // namespace Client::Tables
