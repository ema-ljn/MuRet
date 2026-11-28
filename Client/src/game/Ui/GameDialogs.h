#pragma once

#include "Core/Expected.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Ui/Dialogs.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// The game's old boxes (06 § HUD y ventanas, "Los diálogos del juego", CL8 milestone 6): the text and the two buttons
// that 0x632e30 and 0x633300 build, 0x623e70 steps (0x62d756) and 0x62db60 draws. All .bss globals.
namespace Client::Ui
{
// 0x632e30 and 0x633300 split into at most 7 rows (0x632c90(text, 0x7d36a98, 7, 0x26)).
inline constexpr std::size_t DialogRowCount = 7;
// With its '\0'.
inline constexpr std::size_t DialogRowBytes = 0x26;
// 0x7d368b8 and 0x7d368cc: two buttons of 5 dwords (0x62d7e1 stops at 0x7d368e4).
inline constexpr std::size_t DialogButtonCount = 2;
// 0x632e30: the one-button box.
inline constexpr std::int32_t MessageBoxDialog = PanelFriendlyDialog;
// 0x62d7eb
inline constexpr std::int32_t PagedDialog = 0x8c;
// 0x633300: the texts built with their own format (0x63330f, 0x633327 and 0x63340d) or with 0x632c90 one after
// another (0x6333a8).
inline constexpr std::int32_t MoneyTextDialog = 0x99;
// The 0xa3 is the jewel mix question (texts 1809 and 1813 of 0x63340d), with the buttons lower (0x6282f1); the name
// comes from CL8.
inline constexpr std::int32_t ServerTimeDialog = 0xa3;
inline constexpr std::int32_t ItemNameDialog = 0x96;
inline constexpr std::int32_t ItemNameWithLuckDialog = 0xa8;
// 0x4da1b9: the question the F3 40 opens (0x4da070, milestone 11) with text 2020 and +5 at 0, with its yes and no.
inline constexpr std::int32_t NoticeQuestionDialog = 0xaa;
// 0x632f70: the Dialog.bmd dialog with answers (the NPCs' one, CL9 milestone 2).
inline constexpr std::int32_t AnswersDialog = 0x8d;
// 0x7d3691c: the answers of 0x419710 and 0x632f70, 10 rows of 0x26 (0x5f dwords zeroed).
inline constexpr std::size_t DialogAnswerRows = 10;
// 0x419710 and 0x632f70: without answers, "1) " and Text.bmd's text 609 (0x76e9b88).
inline constexpr std::size_t NoAnswersText = 609;

// A button of 0x7d368b8: the state (0 absent; 1 outside; 2 hovered; 0x633300 starts the second at 3), and the
// rectangle, which 0x62d756 shifts (0xd5, 0x3c).
struct DialogButton
{
    std::int32_t state = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct DialogBoxState
{
    // 0x7d36a98: 7 contiguous rows of 0x26 bytes. A long strcpy of 0x633300 goes on in the next row.
    std::array<char, DialogRowCount * DialogRowBytes> rows{};
    // 0x7d368e4: the rows 0x62db60 draws.
    std::int32_t lineCount = 0;
    std::array<DialogButton, DialogButtonCount> buttons{};
    // 0x7d3a0f4, 0x7d3a0f8 and 0x7d3a0f9: the paged dialog's first text, the page (a signed char) and the last one. The
    // opener writes them (CL9).
    std::int32_t pageText = 0;
    std::int8_t page = 0;
    std::int8_t lastPage = 0;
    // 0x7d3a0dc and 0x7d3a0e0: the bless jewel's cell ([0x78a6758] − 12) and the place of the 0x1a25 it was dropped on
    // (0x5d8345 and 0x5d8361, CL9 milestone 5). Dialog 0xa9 reads them (0x629d0e and 0x629e03, CL9.3).
    std::int32_t jewelCell = 0;
    std::int32_t jewelTarget = 0;
    // 0x7d36908..0x7d36918: the third button, built only by the 0xa8 (0x6337c3..0x6337e3) and read by its step
    // (0x6283af) and its draw (0x630ff8). Outside the two's array: between them and it lie 0x7d368e4 and the answers.
    DialogButton thirdButton{};
    // 0x7d3691c: the answers, "%d) %s" one row each. The no-answers one goes with strcpy and can go on in the next.
    std::array<char, DialogAnswerRows * DialogRowBytes> answers{};
    // 0x7d3a0fc: for the 0x8d's list and the quest window's.
    std::int32_t answerCount = 0;
    // 0x6d5e28 (.data, −1): the Dialog.bmd record they show.
    std::int32_t dialogIndex = -1;
};

// Answer `row` as the draw reads it: from its first byte to the first '\0', which can be in the next one. MuRet
// stops at the end of the 10.
[[nodiscard]] std::string_view DialogAnswer(const DialogBoxState& box, std::size_t row);

// What 0x633300's formatted 0xa3 text (0x63340d) reads, from other windows' globals.
struct DialogTextArguments
{
    // 0xa3, the jewel mix: 0x6e5794 (0 combine, 1 split), 0x6c62e0 (the jewel: 0 bless, any other soul), 0x6e579a (the
    // count), 0x6c62dc (the bundle's level, 0x402ae0) and 0x6e579c (the price).
    std::int32_t jewelMode = 0;
    std::uint8_t jewelKind = 0;
    std::uint8_t jewelCount = 0;
    std::int32_t jewelLevel = 0;
    std::int32_t jewelPrice = 0;
};

// Row `row` as the draw reads it: from its first byte to the first '\0', which can be in the next row. With no '\0'
// up to the end of the 7, the original would read on into the .bss: MuRet stops there.
[[nodiscard]] std::string_view DialogRow(const DialogBoxState& box, std::size_t row);

// 0x632e30(text): `text` split with 0x632c90 into the rows, buttons zeroed and the first at (1, 0x47, 0x8c, 0x46,
// 0x15), and the 0x8a open, or waiting if another is (without 0x6236f0's cases).
void OpenMessageBox(DialogBoxState& box, DialogState& dialogs, std::string_view text);

// 0x633300(rows, firstText, id): Text.bmd's texts `firstText` .. `firstText + lineCount − 1` in the rows, one strcpy
// each (the 0x9a splits them with 0x632c90 one after another), the buttons (1, 0x15, 0x5a, 0x46, 0x15) and (3, 0x78,
// 0x5a, 0x46, 0x15), with y = 0x78 in the 0x9a, and the id through 0x6236f0. The 0x96 and the 0xa8
// (0x633644..0x633739) name the dialogs' item (`dialogItem`, 0x78a59c8): the fruit (0x1a0f) of level 0 to 4 with text
// 168, 169, 167, 166 or 1900, any other with nothing, between "( " and " )" in the first row (with the 1901 after it
// in the 0xa8), and below `lineCount` times the text `firstText`; the 0xa8 with its three buttons. Without the item
// they fail. The 0x99 is opened by OpenTheMoneyTextDialog, and fails here. The 0xa3 builds its text with a format
// (0x63340d) from `arguments`: without it, it fails. What would be written past the 7 rows fails, like a missing
// text.
//   - 0xa3: "%s ... %d" (1809 when combining, with the jewel and the count; 1813 when splitting, with the jewel and
//     the level + 1), with the jewel from the 1806 or the 1807, split into at most 2 rows of 0x1e bytes (0x632c90 on
//     the stack, whose rows it strcpys to the first two), and below the price with the 1810 or the 1814. Ignores
//     `lineCount` and `firstText`.
[[nodiscard]] Core::Expected<void, std::string> OpenTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                               const Tables::TextTable* texts, std::int32_t lineCount,
                                                               std::int32_t firstText, std::int32_t id,
                                                               const Kernel::ItemState* dialogItem = nullptr,
                                                               const DialogTextArguments* arguments = nullptr);

// 0x633300(rows, firstText, 0x99) (CL9 milestone 7): the first row is the sprintf of text `firstText` with `zen`,
// what 0x5b1130 writes of the typed zen (0x77f3b6c; the caller builds it with Ui::FormatZen), and the rest, texts
// `firstText + 1` .. `firstText + lineCount − 1` with the same sprintf without arguments (a "%%" comes out as "%");
// the count stays at `lineCount`. The buttons and the id, as in OpenTextDialog.
[[nodiscard]] Core::Expected<void, std::string> OpenTheMoneyTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                                       const Tables::TextTable* texts,
                                                                       std::int32_t lineCount, std::int32_t firstText,
                                                                       std::string_view zen);

// 0x633300(n, 0x482, 0x9a) with texts 1154.. already written into Text.bmd's memory by the caller (the 91 with 4,
// 0x4da8c0, CL9 milestone 11): `texts` in place of the n records, split with 0x632c90 one after another, the 0x9a's
// buttons and the 0x9a through 0x6236f0.
[[nodiscard]] Core::Expected<void, std::string> OpenTheChaosCastleDialog(DialogBoxState& box, DialogState& dialogs,
                                                                         std::span<const std::string_view> texts);

// 0x419710(index), what the quest window shows: [0x6d5e28] = index, the Dialog.bmd text split into the 7 rows
// (0x632c90), the answers zeroed and, for each, "%d) %s" (wsprintf with 0x6c64b8) in one row; with none (count 0),
// "1) " and text 609 with strcpy and the count at 1. Then 0x6236f0(0). Leaves the buttons alone. Fails without the
// table, with an index outside the 200 or without text 609.
[[nodiscard]] Core::Expected<void, std::string> ShowTheQuestDialog(DialogBoxState& box, DialogState& dialogs,
                                                                   const Tables::DialogTable* table,
                                                                   const Tables::TextTable* texts, std::int32_t index);

// 0x632f70(index): the same as 0x419710 with the buttons zeroed (0x7d368b8, 10 dwords) and, instead of
// 0x6236f0(0), the 0x8d open, or waiting if another is.
[[nodiscard]] Core::Expected<void, std::string> OpenTheAnswersDialog(DialogBoxState& box, DialogState& dialogs,
                                                                     const Tables::DialogTable* table,
                                                                     const Tables::TextTable* texts,
                                                                     std::int32_t index);

// 0x62d756..0x62d7e9: the two buttons. With state 1 or more, the mouse in [x + 0xd5, x + w + 0xd5] × [y + 0x3c, y +
// h + 0x3c] (edges included) sets it to 2 and, with the click (0x7d366d4), clears the click and the button down
// (0x7d36874) and picks it; outside, 1. Returns the last one picked, or −1.
[[nodiscard]] std::int32_t StepTheDialogButtons(DialogBoxState& box, Point mouse, bool& click, bool& leftHeld);

// 0x62d7eb..0x62d8fb: the 0x8c's arrows, with the click and without clearing it. The left one ([0xf9, 0x108] × [0xca,
// 0xde]) goes back a page without going below 0; the right one ([0x179, 0x188] × [0xca, 0xde]) goes forward one
// without passing the last. With either, the rows are those of text pageText + page (0x632c90). Before, 0x620a00(1),
// which draws nothing in the step (*inferred*, 08).
[[nodiscard]] Core::Expected<void, std::string> StepTheDialogPages(DialogBoxState& box, const Tables::TextTable* texts,
                                                                   Point mouse, bool click);

// 0x62d92f, 0x632501 and 0x6325ee: for the boxes' text rows and answer rows.
inline constexpr std::int32_t DialogLineStep = 0x12;
// 0x62d93e and 0x6324a3: the 0x8d does not start its text above y = 0x46.
inline constexpr std::int32_t AnswersDialogTopMinimum = 0x46;
// 0x62d96a..0x62d979 and 0x632573..0x632582: the mouse is over the answers within 0x6a of x = 0x13f.
inline constexpr std::int32_t AnswersCenterX = 0x13f;
inline constexpr std::int32_t AnswersHalfWidth = 0x6a;

// 0x632472..0x6324a6 (and 0x62d91d..0x62d943 in the step): the y of the first text row of the 0x8a, 0x8c and 0x8d,
// ((7 − [0x7d3a0fc] − rows) · 0x12) / 2 + 0x4c (signed division, toward zero); with the 0x8d, no less than 0x46. The
// 0x8d's answers follow below the text, at that y + rows · 0x12.
[[nodiscard]] std::int32_t DialogTextTop(const DialogBoxState& box, std::int32_t id);
} // namespace Client::Ui
