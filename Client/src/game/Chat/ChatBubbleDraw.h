#pragma once

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Render
{
class SpriteBatch;
}

// The bubbles' drawing (CL8 milestone 3; 06 § El chat, "Los globos"): the HUD's 12, 0x5a9050, with the measurement
// (0x555840), the projection (0x6205b0) and each one's draw (0x555b00 with TextOut other than 1, 0x553e50 and
// 0x554000). None of this calls rand(). It writes in each entry where it ended up and how big it is (+0x278..+0x284),
// which the step's right click reads (0x557650).
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
class WorldState;
struct GameProjection;
} // namespace Client::Kernel

namespace Client::Chat
{
struct ChatBubble;

// 0x555840(entry) with the normal font in the DC: the width (+0x280) is the largest of the name's plus 3, the guild's
// and the post's, with the lower line if either of the two has time and the upper one if the upper one has; the height
// (+0x284), the font step (0x7758568) times the five measurements that return TRUE, which are all of them (Q-C520).
// With a personal shop's short title (+0x54, 0x5559c2..0x555a62), the width is at least its own plus the text 1104's,
// both in bold, and the height adds a line; without Text.bmd, it stops.
[[nodiscard]] Core::Expected<void, std::string> MeasureTheChatBubble(ChatBubble& bubble,
                                                                     const Render::Text::ITextMeasure& measure,
                                                                     int fontStep, const Tables::TextTable* texts);

struct ChatBubbleDrawContext
{
    Kernel::WorldState& world;
    // 0x6203c0 of this frame's world pass, the one 0x6205b0 reads. Without it, a live bubble stops.
    const Kernel::GameProjection* projection = nullptr;
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    // The SelectObject of 0x5a9050 (the normal font) and those of 0x555b00's shop (the bold and the normal).
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c and 0x6c8e64: the last line's colors stay.
    Render::Text::TextColors& textColors;
    // 0x77f3b66, which 0x555b00 sets, and 0x77f31b8 and 0x77f31bc, the last bubble's width (up to 1280) and height.
    std::uint8_t& clampBoxes;
    std::int32_t& boxWidth;
    std::int32_t& boxHeight;
    // 0x7d3682c and 0x7d36828: the mouse in 640 × 480.
    Ui::Point mouse{};
    // 0x77f3b94: the old area's counter.
    std::int32_t blink = 0;
    // An intermediate MuRet draw does not write the table: the original's step reads what its own last frame left.
    bool projecting = false;
    // Text.bmd, with the text 1104 (0x770df9c) of a shop's title.
    const Tables::TextTable* texts = nullptr;
};

// 0x5a9050: SelectObject of the normal font; each entry with the name or the lower line with time goes 60 above its
// owner's top (+0x138, 0x6b86cc), projected to 640 × 480, is measured, and ends up centered on the x and 0x24 higher;
// then each overlapping pair moves the first one's y above or below the other, depending on which side of its middle
// it is, and each one goes against the edges (the x with W − width and the y with H − height, in pixels, Q-C522) and
// is drawn with 0x555b00: if the owner has a sign (0x451810), the text 1104 and the short title in bold with the colors
// of 0x451af0, 0x451b50 and 0x451bb0 two lines up, and the other lines one higher (Q-C978). A bubble without an owner
// (+0x288, which nobody writes: 0x557430 has no caller, *static*) stops.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatBubbles(ChatBubbleDrawContext& context);
} // namespace Client::Chat
