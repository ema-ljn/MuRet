#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The chat log, the new list of 0x576350c (CL8 milestone 3; 06 § El chat): the 0x640-byte object that 0x45b620 builds
// from WinMain (vtable 0x6b8dd0) and that 0x555190 adds to before the old area. Its entries go in a std::deque of
// 0x11c bytes, the newest first (push_front). What its functions read from outside (the log's place, 0x6c7a88, and F2,
// 0x6c8dd1) comes in ChatModes; what the draw and the step write and lasts (the place, the size, the bar, the buttons
// and the blink), in ChatLogFrame, which the game copies in its intermediate draws.
namespace Client::Ui
{
// +0x40: the window's id, from the counter 0x5762eb0 (0x459ef0). *Inferred*: before this one WinMain builds eight
// old windows (0x4674c0), which take 1 to 8.
inline constexpr std::int32_t ChatLogId = 9;
// +4 (strncpy of 0xb) and +0xf (0x100). An 11-byte name is left without '\0' and is read followed by the text.
inline constexpr std::size_t ChatNameBytes = 0xb;
inline constexpr std::size_t ChatTextBytes = 0x100;
// +0x140: 5 palabras de 0x100 bytes.
inline constexpr std::size_t ChatFilterWords = 5;
// 0x45dc40: the sum of the 5 words' lengths has to stay below 31.
inline constexpr std::size_t ChatFilterTotalBytes = 0x1f;
// 0x45bb20 splits into 2 lines 180 wide (0x459c70(text, 2 × 0x100, 0xb4, 2, 0x100, 0, 0)).
inline constexpr int ChatWrapWidth = 0xb4;
// Text.bmd: the notice of the filter set (755) and of the filter removed (756), and the buttons' ones (750, 751, 752).
inline constexpr std::size_t FilterOnText = 755;
inline constexpr std::size_t FilterOffText = 756;
inline constexpr std::size_t FilterTitleText = 754;
inline constexpr std::array<std::size_t, 3> ChatButtonHelpTexts{750, 751, 752};
// 0x41ed10(id, 0, 0): the sounds it asks for.
inline constexpr Audio::SoundId ChatButtonSound = Sound::ButtonClickSound;
inline constexpr Audio::SoundId ChatFilterSound = Sound::WhisperSound;

struct ChatEntry
{
    // +0: the mouse over the name flips its parity (0x45dec0). Nobody reads it.
    std::int32_t blink = 0;
    std::string name;
    std::string text;
    std::int32_t type = 0;
    // +0x114: 0x45bb20's fourth argument, which 0x555190 gives as 0.
    std::int32_t param = 0;
    // +0x118: how many empty adds passed with the log up (0x45bb10). 0x45bfe0 adds it unsigned.
    std::uint32_t age = 0;

    friend bool operator==(const ChatEntry&, const ChatEntry&) = default;
};

struct ChatLogState
{
    // The newest first.
    std::deque<ChatEntry> entries;
    // +0x48: 0 still, 1 changing the height (the top edge) and 2 dragging the bar.
    std::int32_t state = 0;
    // +0xc4: the selected line, counting from 1 among the visible ones; 0, none.
    std::int32_t selection = 0;
    // +0xfc.
    std::int32_t maxEntries = 150;
    // +0x100: how many of the visible ones remain below, under the last line.
    std::int32_t scroll = 0;
    // +0x104
    std::int32_t rows = 6;
    // +0x11c: where the mouse grabbed the bar, from its top edge.
    float dragOffset = 0.0f;
    // +0x120 and +0x124: how many steps an arrow or the track has been pressed. With 16, it repeats each step.
    std::int32_t arrowRepeat = 0;
    std::int32_t trackRepeat = 0;
    // +0x12c: 0 from the constructor; with 1, the bar would be the other lists' one (other edges).
    std::int32_t barMode = 0;
    // +0x134: the background's transparency (drawn with 1 − this). The third button and the F3 30 change it.
    float alpha = 0.4f;
    // +0x138: the buttons in view (the mouse passed below).
    std::int32_t buttonsShown = 0;
    // +0x140.
    std::array<std::string, ChatFilterWords> filter{};
};

struct ChatLogFrame
{
    // +0x50 and +0x54: the bottom-left corner. The step sets it on each round (0x637036..0x637056) and a line's draw,
    // in Crywolf, leaves the y at 0x12f (0x45cbb0).
    std::int32_t x = 0xba;
    std::int32_t y = 0x1a4;
    // +0x58 and +0x5c. The frame (0x45c460) leaves the height with the log in its window.
    std::int32_t width = 0x10c;
    std::int32_t height = 0xfa;
    // +0x108, +0x10c, +0x110, +0x114 and +0x118: the bar (0x47a230).
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbWidth = 13.0f;
    float thumbHeight = 0.0f;
    // +0x130: goes up by one per frame draw (0x45c460), also with the log outside its window.
    std::int32_t blink = 0;
    // +0x13c: the buttons' transparency, which goes up and down by 0.2 (0x45d420).
    float buttonsAlpha = 0.0f;
};

// The globals the list reads: 0x6c7a88 (.data 1, the constructor sets it to 0) and 0x6c8dd1 (.data 1, F2).
struct ChatModes
{
    std::int32_t placement = 0;
    bool showChat = true;
};

// The name (+4) as lstrlenA, strcmp and sprintf read it: an 11-byte one has no '\0' and runs on into the text.
[[nodiscard]] std::string NameOf(const ChatEntry& entry);

// 0x45c140: the one not shown. The normal chat (3) without F2; system and error (1 and 2) with the log in its window.
[[nodiscard]] bool IsHiddenInTheLog(const ChatEntry& entry, ChatModes modes);

// 0x45c2e0: the visible ones. It counts the hidden ones only without F2 or with the log in its window.
[[nodiscard]] std::int32_t VisibleInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45c3a0: the first one it draws, after skipping `scroll` of the visible ones. It can be the end.
[[nodiscard]] std::size_t FirstShownInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45c190: the selected one, the `selection`-th of the visible ones. Without it, the end.
[[nodiscard]] std::size_t SelectedInTheLog(const ChatLogState& log, ChatModes modes);

// 0x45bfe0(n), the vtable + 0x34: scrolls `lines` up with n negative. Only with as many as the lines or more
// (unsigned). With the log outside its window, not scrolled and n = −6, it searches by the ages how many fit.
void ScrollTheLog(ChatLogState& log, ChatModes modes, std::int32_t lines);

// 0x479760, the vtable + 0x58: the selection, up to the visible ones.
void SelectInTheLog(ChatLogState& log, ChatModes modes, std::int32_t row);

// 0x47a050, the vtable + 0x6c: with 150 or more, it removes the old ones at the back and sets the selection to 0.
void TrimTheLog(ChatLogState& log, ChatModes modes);

// 0x478d10, the vtable + 0x2c: empties the list, the selection and the scroll.
void ClearTheLog(ChatLogState& log, ChatModes modes);

// 0x459c70(text, lines of 0x100, width, how many, 0x100, indent, 0): splits `text` into up to `lines` lines of `width`
// in 640 × 480, measuring with GetTextExtentPointA on the DC (`font`) and going to 640 with ftol(cx / rx). A line that
// does not fit is searched by halving jumps: it fits if it measures between the width − 4 and the width + 4. It returns
// the lines written (the others, empty) and what the original returns: the used ones, or one more if text is left
// over. The division by a letter's average width (0x459d21) breaks with a text of letters under one point: the error.
struct WrappedText
{
    std::vector<std::string> lines;
    std::int32_t result = 0;
};
[[nodiscard]] Core::Expected<WrappedText, std::string> WrapByWidth(std::string_view text,
                                                                   const Render::Text::ITextMeasure& measure,
                                                                   Render::Text::FontKind font, Core::PixelSize screen,
                                                                   int width, int lines, int indent);

// What 0x45bb20 needs to measure when splitting.
struct ChatMeasure
{
    const Render::Text::ITextMeasure& measure;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    Core::PixelSize screen{};
};

// 0x45bb20(name, text, type, param), the vtable + 0x94:
//   - both empty: with the log outside its window and not scrolled, all the entries raise their age;
//   - the normal chat without F2 does not go in; with F2, if the filter (0x45ddc0) lets neither the name nor the text
//     through, neither, and with the filter set and the notices' sound (0x77f3b78) it asks for the 0x26;
//   - with fewer than 20 bytes between the two, one entry; if not, the text split into 2 of 180 (WrapByWidth), with the
//     name in the first;
//   - then TrimTheLog, the selection at 0, the scroll one more (if it was scrolled and the type is shown), bounded.
// It returns the sound it asks for.
[[nodiscard]] Core::Expected<std::optional<Audio::SoundId>, std::string>
AddToTheLog(ChatLogState& log, ChatModes modes, const ChatMeasure& measure, std::string_view name,
            std::string_view text, std::int32_t type, std::int32_t param, bool noticeSound);

// 0x45ddc0: without a filter, everything passes; with one, `text` has to have one of the words (0x576960).
[[nodiscard]] bool PassesTheChatFilter(const ChatLogState& log, std::string_view text);

// 0x45de20: the words separated by ", ". With all five set, it reads one byte past the object (+0x640): MuRet takes
// it as 0 and does not add the final comma.
[[nodiscard]] std::string JoinedChatFilter(const ChatLogState& log);

// 0x45db50.
void ClearTheChatFilter(ChatLogState& log);

// 0x45dc40(/filter words): the words after the command, up to 5, while the sum of the lengths of all (with the ones
// left from before) does not reach 31: the first that does not fit cuts everything if it is the first, or stays
// empty. It returns the Text.bmd notice that 0x555190("", notice, 1) adds: 755 with a filter set, 756 if one that was
// there was removed, none if not.
[[nodiscard]] std::optional<std::size_t> SetTheChatFilter(ChatLogState& log, std::string_view command);

// 0x45db70(text, n): among the first n, one with the same first 16 bytes of text or with an age.
[[nodiscard]] bool SaidRecently(const ChatLogState& log, std::string_view text, std::int32_t count);

// 0x45d8c0, F4 and the second button: (0, 6) → (1, 3) → (1, 6) → (1, 30) → (0, 6), and then ScrollTheLog(0).
void CycleTheLogSize(ChatLogState& log, ChatModes& modes);
} // namespace Client::Ui
