#pragma once

#include "Tables/TextTable.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Chat/ChatLog.h"

#include <cstdint>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Render::Frame
{
class ITextDrawer;
}

// The chat log's drawing (CL8 milestone 3; 06 § El chat): 0x479080, its vtable + 0x14, which the HUD calls in the
// game (0x5a7320, step 17). It builds the frame with the bar (0x45c460, only with the log in its window), the lines
// (0x45cbb0) and the buttons (0x45d420), all in 640 × 480.
namespace Client::Chat
{
// The Interface\ slots that 0x61ccb0 loads: the frame's background (Message_box2.jpg), the grip (nis_rsframe), the
// sides (nis_vframe), the thumb (nis_bar), the track (nis_back), the arrows (nis_btnarrow) and the buttons
// (nis_btnchat, nis_btnsize and nis_btnalpha). SwordEff.jpg overwrites the track in the same function (Q-C463).
inline constexpr std::uint16_t ChatFrameSlot = 0xfc;
inline constexpr std::uint16_t ChatGripSlot = 0x63d;
inline constexpr std::uint16_t ChatSideSlot = 0x63e;
inline constexpr std::uint16_t ChatThumbSlot = 0x63f;
inline constexpr std::uint16_t ChatTrackSlot = 0x640;
inline constexpr std::uint16_t ChatArrowSlot = 0x641;
inline constexpr std::uint16_t ChatShowButtonSlot = 0x642;
inline constexpr std::uint16_t ChatSizeButtonSlot = 0x643;
inline constexpr std::uint16_t ChatAlphaButtonSlot = 0x644;

// What the draw uses and leaves: the batch, the texts, the DC's font (0x57634f4) and the colors of 0x465950.
struct ChatLogDrawContext
{
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c and 0x6c8e64.
    Render::Text::TextColors& textColors;
};

struct ChatLogDrawView
{
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d36874: the left button down.
    bool leftDown = false;
    // 0x6c8e68: the chat field open.
    bool chatOpen = false;
    // 0x723c05: with 3 or 4, the lines go down 0x75.
    std::uint8_t crywolfStage = 0;
    // Text.bmd, for the filter's title (754) and the buttons' helps (750, 751 and 752).
    const Tables::TextTable* texts = nullptr;
};

// 0x479080: the frame (and the bar, 0x47a230), from the first visible one up to `rows` lines or the end, and the
// buttons. It writes in `frame` the height, the bar, the blink, the buttons' transparency and, in Crywolf with the
// list at x = 0xba, the y (0x12f).
void DrawTheChatLog(const Ui::ChatLogState& log, Ui::ChatLogFrame& frame, Ui::ChatModes modes,
                    const ChatLogDrawContext& context, const ChatLogDrawView& view);
} // namespace Client::Chat
