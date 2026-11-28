#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Render::Frame
{
class ITextDrawer;
}

// The old message area and the centered notices (CL8 milestone 3; 06 § El chat). Both tables are in the .bss and all
// three scenes draw them (login, selection and game, 0x6350b0, 0x634680 and 0x6371a0); their clocks run each step,
// after the scene's (0x637cf3 and 0x637cf8, inside 0x637700). What the draw writes and lasts (the rows' widths, the
// blink counters, the DC's font) goes in MessageFrameState, which the game copies in its intermediate draws.
namespace Client::Ui
{
// The message types of 0x555190 and 0x45bb20, by their colors (0x5554e0 and 0x45cbb0) and by who sends them.
inline constexpr std::int32_t WhisperMessage = 0;
inline constexpr std::int32_t SystemMessage = 1;
inline constexpr std::int32_t ErrorMessage = 2;
inline constexpr std::int32_t ChatMessage = 3;
inline constexpr std::int32_t PartyMessage = 4;
inline constexpr std::int32_t GuildMessage = 5;
// The 00's "@@" (0x4c6230): no draw has its case and it comes out with the previous text's colors.
inline constexpr std::int32_t GuildNoticeMessage = 6;

// 0x77d9868: 120 rows of 0x118 bytes, with the name at +0 (11 bytes), the text at +0xb (0x101), the type at +0x10c and
// the width the draw writes at +0x114.
inline constexpr std::size_t MessageAreaRows = 120;
// What fits without overwriting the next field, without the '\0'. The original copies with strcpy, unbounded: a name
// over 10 bytes runs into the text and a text over 0x100 into the type, which are written after. MuRet cuts (Q-C462).
inline constexpr std::size_t MessageNameBytes = 10;
inline constexpr std::size_t MessageAreaTextBytes = 0x100;
// 0x77985c0: 6 notices of 0x108 bytes, the text at +0 (0x104) and the color in byte +0x104.
inline constexpr std::size_t NoticeRows = 6;
inline constexpr std::size_t NoticeTextBytes = 0x103;
// 0x4b8841 (after sending) is the only one that writes it, with the same 0x72 as the .data.
inline constexpr std::int32_t FirstShownRow = 0x72;
// 0x555190 leaves 150 in 0x6c8eb8 and 0x554610 leaves 300 in 0x6c8eb0; the .data holds 200 and 300.
inline constexpr std::int32_t MessageClockSteps = 150;
inline constexpr std::int32_t NoticeClockSteps = 300;

struct MessageAreaRow
{
    std::string name;
    std::string text;
    std::int32_t type = 0;

    friend bool operator==(const MessageAreaRow&, const MessageAreaRow&) = default;
};

struct NoticeRow
{
    std::string text;
    // +0x104: other than 0, the notice shows steady in 0xffc8ff64; at 0, it blinks.
    std::uint8_t highlighted = 0;

    friend bool operator==(const NoticeRow&, const NoticeRow&) = default;
};

struct MessageAreaState
{
    std::array<MessageAreaRow, MessageAreaRows> rows{};
    // 0x77f3b90: reaches 120 and then everything shifts by one.
    std::int32_t count = 0;
    // 0x6c8eb4 (.data 0x72): the first of the 6 that 0x5554e0 draws. Always 0x72: new rows do not show until the count
    // reaches 0x73.
    std::int32_t firstShown = FirstShownRow;
    // 0x6c8eb8 (.data 200): 0x5554b0's clock.
    std::int32_t clock = 200;
    std::array<NoticeRow, NoticeRows> notices{};
    // 0x77f3b88.
    std::int32_t noticeCount = 0;
    // 0x6c8eb0 (.data 300): 0x5547e0's clock.
    std::int32_t noticeClock = NoticeClockSteps;
};

// What the draws write and lasts until the next frame (and the step reads).
struct MessageFrameState
{
    // +0x114 of each row: the pixel width of the text 0x5554e0 drew. The step's right click reads it (0x59ff98).
    // 0x555190's shift does not move it: the one of the row that was there stays.
    std::array<std::int32_t, MessageAreaRows> widths{};
    // 0x77f3b94 and 0x77f3b8c: up by one per draw of the area and of the notices.
    std::int32_t areaBlink = 0;
    std::int32_t noticeBlink = 0;
};

// What the area's draw reads from the game.
struct MessageAreaView
{
    // 0x7d3682c and 0x7d36828, in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x6c8e68
    bool chatOpen = false;
    // The hero's +0x3e2 (0x743a170).
    bool heroSafeZone = false;
    // The hero's +0x1d9, up to its '\0'.
    std::string_view heroName;
    // 0x6c8dd1 (.data 1): F2, the normal chat (type 3) in the areas too.
    bool showChat = true;
    // 0x723c05: with 3 or 4, the rows go down 0x89.
    std::uint8_t crywolfStage = 0;
};

// The text and the font each draw leaves. `dcFont` is the text DC's (0x57634f4): the SelectObject calls choose it and
// the GetTextExtentPointA calls that choose no other use it, like 0x555190.
struct MessageDrawContext
{
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c and 0x6c8e64.
    Render::Text::TextColors& textColors;
    // 0x77f31b8 and 0x77f31bc: the last text's size, in 640 × 480.
    std::int32_t& textWidth;
    std::int32_t& textHeight;
};

// 0x5549a0(text, tail, head, length): cuts `text` at the first space from half minus 2 or at half plus 2, whichever
// comes first; if the text ends before, everything goes to the tail. The tail starts with the space. Advances one
// byte at a time (the original's _mbclen on a single-byte code page).
struct HalvedText
{
    std::string head;
    std::string tail;
};
[[nodiscard]] HalvedText SplitInHalf(std::string_view text);

// 0x555190 after the new list: with the name empty or a type other than 0, 3, 4, 5 and 6, the row goes into the old
// table. Measures "%s: %s" with the DC's font; from 256 pixels it cuts it in two (SplitInHalf): the name with the head
// and 10 spaces with the tail. With the table full it shifts everything one row before each. Leaves the clock at 150.
void AddToTheMessageArea(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                         Render::Text::FontKind dcFont, std::string_view name, std::string_view text,
                         std::int32_t type);

// 0x554610(text, color): selects the bold font in the DC, measures the text and, from 256 pixels, cuts it into two
// notices. With 6 notices it shifts one before each. Leaves the clock at 300.
void AddNotice(MessageAreaState& area, const Render::Text::ITextMeasure& measure, Render::Text::FontKind& dcFont,
               std::string_view text, std::uint8_t highlighted);

// 0x5547e0, per step: with the clock at 0 or less before lowering it, an empty notice (which raises the others) and
// 300.
void StepTheNoticeClock(MessageAreaState& area, const Render::Text::ITextMeasure& measure,
                        Render::Text::FontKind& dcFont);

// 0x5554e0: 6 rows from firstShown at y = 0, 13, ..., 65 (x = 0), with the normal font.
void DrawTheMessageArea(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                        const MessageAreaView& view);

// 0x554810: the 6 notices, centered on 320, from y = 300 (or 20 with the chat log in its window, 0x6c7a88 at 1), with
// the bold font and the normal blend.
void DrawTheNotices(const MessageAreaState& area, MessageFrameState& frame, const MessageDrawContext& context,
                    std::int32_t chatPlacement);
} // namespace Client::Ui
