#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatLog.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The chat log's step (CL8 milestone 3; 06 § El chat): 0x45aa80, its vtable + 0x18, which runs from the UI step
// (0x636e4c) with 0, and what it calls: the mouse (0x479960), the bar, the buttons and the top edge (0x45d760 and
// 0x45d990) and the mouse over the names (0x45dec0). Also the bar the frame's draw computes (0x47a230).
namespace Client::Ui
{
struct ChatLogGlobals
{
    // 0x7d366d4: the left click's edge, which whoever uses it turns off.
    bool& leftClick;
    // 0x7d3685c: the right button down, which 0x45d760 turns off when it uses it.
    bool& rightDown;
    // 0x7d366d8: the wheel in steps of 120.
    std::int32_t& wheel;
    // 0x5762eb4: the window the mouse grabbed (the edge or the bar).
    std::int32_t& capture;
    // 0x5762eb8: the window under the mouse; the UI step sets it back to 0 before the windows.
    std::int32_t& hovered;
    // 0x5762ec0: the list that takes the keyboard arrows.
    std::int32_t& focusedList;
    // 0x775857c.
    std::uint8_t& mouseOnUi;
    // 0x6c7a88 and 0x6c8dd1.
    ChatModes& modes;
};

struct ChatLogStepView
{
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d36874: the left button down.
    bool leftDown = false;
    // 0x6c8e68: the chat field open.
    bool chatOpen = false;
    // The hero's +0x3e2.
    bool heroSafeZone = false;
    // The hero's +0x1d9, up to its '\0'.
    std::string_view heroName;
    // GetFocus() on one of the chat's two EDITs (+0x8c and +0x178 of 0x57634f8).
    bool chatEditFocused = false;
    // GetTextExtentPointA on the texts' DC (0x57634f4), with whatever font it has.
    const Render::Text::ITextMeasure* measure = nullptr;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
};

struct ChatLogStepResult
{
    // 0x41ed10(id, 0, 0): the sounds it asks for, in order.
    std::vector<Audio::SoundId> sounds;
    // 0x4694c0(0, 0, 1, name) on 0x57634f8: the selected line's name for the whisper field (its EDIT at +0xf0, vtable +
    // 0x4c), with the right click.
    std::optional<std::string> whisperTo;
    // What 0x479960 returns: the mouse was over the list, its bar or its buttons.
    bool handled = false;
};

// 0x459f00(x, y, w, h, mode): the mouse in the rectangle; with mode 2, `y` is the bottom edge.
[[nodiscard]] bool MouseInRect(std::int32_t mouseX, std::int32_t mouseY, std::int32_t x, std::int32_t y,
                               std::int32_t width, std::int32_t height, std::int32_t mode);

// An old list's bar: the track's edges (+0x108 and +0x10c), and the thumb's place and height (+0x110 and +0x118).
struct OldListBar
{
    float barTop = 0.0f;
    float barBottom = 0.0f;
    float thumbY = 0.0f;
    float thumbHeight = 0.0f;
};

// 0x47a230 and 0x484490, equal byte for byte except the jumps: the bar of a list with the bottom corner at `y`,
// `height` high, the mode of +0x12c, `rows` lines in view, `count` in the list (the vtable + 0x54) and `scroll` the
// first visible one.
[[nodiscard]] OldListBar PlaceTheListBar(std::int32_t y, std::int32_t height, std::int32_t barMode, std::int32_t rows,
                                         std::int32_t count, std::int32_t scroll);

// 0x47a230, the vtable + 0x70: the track's edges and the size and place of the bar's thumb.
void PlaceTheLogBar(const ChatLogState& log, ChatModes modes, ChatLogFrame& frame);

// 0x45aa80(0), the vtable + 0x18. With the mouse over the list's rectangle (the mode of +0x78 is 2), it marks it as
// the one under the mouse if there is no other or if nobody grabbed it; still (+0x48 at 0), without being the one
// under the mouse or the grabbed one, it does not go on. Then 0x479960.
[[nodiscard]] ChatLogStepResult StepTheChatLog(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals,
                                               const ChatLogStepView& view);
} // namespace Client::Ui
