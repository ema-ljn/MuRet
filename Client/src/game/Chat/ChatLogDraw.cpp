#include "Chat/ChatLogDraw.h"

#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/Frame/ITextDrawer.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Chat
{
namespace
{
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t RowHeight = 0xd;
// 0x45cbb0: the line above the bottom edge, or 0x65 below it in Crywolf, and in Crywolf the list at x = 0xba goes up
// to y = 0x12f.
constexpr std::int32_t RowsBottom = 0x10;
constexpr std::int32_t CrywolfRowsBelow = 0x65;
constexpr std::int32_t CrywolfListX = 0xba;
constexpr std::int32_t CrywolfListY = 0x12f;
constexpr std::int32_t NameX = 10;
// 0x45c460: 40 high every 3 lines, plus 10.
constexpr std::int32_t RowsPerSide = 3;
constexpr std::int32_t SideHeight = 0x28;
constexpr std::int32_t FrameExtra = 10;
// 0x6b8864, 0x6b87f8 and 0x6b8820: the background 12 taller, the sides 5 wide and the last pair 22 higher.
constexpr float BackgroundExtra = 12.0f;
constexpr float SideWidth = 5.0f;
constexpr float LastSideRise = 22.0f;
// 0x6b8b10, 0x6b86c4 and 0x6b8a44: the arrows, 21 from the right edge, the upper one 8 below the top edge and the
// lower one, flipped, 4 above the bottom one.
constexpr float ArrowInset = 21.0f;
constexpr float ArrowTop = 8.0f;
constexpr float ArrowBottom = 4.0f;
constexpr float ArrowSize = 13.0f;
// 0x6b8878 and 0x6b8818: the thumb, 7 from the right edge minus its width and 2 narrower.
constexpr float TrackInset = 8.0f;
constexpr float ThumbInset = 7.0f;
constexpr float ThumbNarrower = 2.0f;
// 0x6b880c, 0x6b86c4 and 0x6b8a44: the grip, 28 from the right edge and at (y − h) − 8 + 4.
constexpr float GripInset = 28.0f;
constexpr float GripAbove = 8.0f;
constexpr float GripDown = 4.0f;
constexpr float GripWidth = 22.0f;
constexpr float GripHeight = 12.0f;
// 0x45c460's UVs: 0x3f550000, 0x3f200000, 0x3f500000, 0x3f300000 and 0x3f400000.
constexpr float FrameDu = 0.83203125f;
constexpr float FrameDv = 0.625f;
constexpr float SideUv = 0.625f;
constexpr float TrackUv = 0.8125f;
constexpr float ThumbUv = 0.6875f;
constexpr float GripDv = 0.75f;
// 0x3f19999a, 0x3dcccccd, 0x3e4ccccd, 0x3f333333 and 0x3f4ccccd.
constexpr float ArrowAlpha = 0.6f;
constexpr float HighlightAlpha = 0.1f;
constexpr float HoverAlpha = 0.2f;
constexpr float Pressed = 0.7f;
constexpr float Darker = 0.8f;
// 0x6b8850: 1/16.
constexpr float Sixteenth = 0.0625f;
// 0x45d420: the buttons, from (295, 417) every 18, of 16 × 16; their transparency goes up or down by 0.2 (0x6b8854).
constexpr float ButtonsX = 295.0f;
constexpr float ButtonsY = 417.0f;
constexpr float ButtonsStep = 18.0f;
constexpr float ButtonSize = 16.0f;
constexpr std::int32_t ButtonPixels = 0x10;
constexpr float ButtonsFade = 0.2f;
// 0x3f700000: the F2 button turned off.
constexpr float OffButtonUv = 0.9375f;
// 0x6b87a8 and 0x6b8f00: the help, 10 higher and 16 more to the left.
constexpr float HelpRise = 10.0f;
constexpr float HelpLeft = 16.0f;

// _ftol (0x69fd3c): truncates toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

using Math::F32;

float F32(std::int32_t value)
{
    return static_cast<float>(value);
}

bool InRect(const ChatLogDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    return Ui::MouseInRect(view.mouseX, view.mouseY, x, y, width, height, TopLeftRect);
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled, with whatever color is set.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = u,
                                      .v = v,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

void White(Render::SpriteBatch& sprites, float alpha)
{
    sprites.SetColor(Render::Color{1.0f, 1.0f, 1.0f, alpha});
}

// 0x45d230(slot, pressed, x, y, w, h, alpha, flipped): a button of 16 pixels per frame. Released, the quad (flipped
// with the negative height); pressed with the button down, darker and moved by one point; pressed without the button,
// with a white veil of alpha 0.1 on top. It leaves the color at (1, 1, 1, alpha).
void Button(Render::SpriteBatch& sprites, std::uint16_t slot, bool pressed, bool leftDown, float x, float y,
            float width, float height, float alpha, bool flipped)
{
    const float sign = flipped ? -1.0f : 1.0f;
    if (!pressed)
    {
        Quad(sprites, slot, x, y, width, F32(static_cast<double>(sign) * static_cast<double>(height)), 0.0f, 0.0f,
             F32(static_cast<double>(width) * static_cast<double>(Sixteenth)),
             F32(static_cast<double>(height) * static_cast<double>(Sixteenth)));
        return;
    }
    if (leftDown)
    {
        sprites.SetColor(Render::Color{Darker, Darker, Darker, alpha});
        const double inner = static_cast<double>(height) - 1.0;
        float drawn = height;
        float v = 0.0f;
        if (flipped)
        {
            drawn = F32(static_cast<double>(height) * -1.0);
            v = Sixteenth;
        }
        Quad(sprites, slot, F32(static_cast<double>(x) + 1.0), F32(static_cast<double>(y) + 1.0), width, drawn, 0.0f, v,
             F32((static_cast<double>(width) - 1.0) * static_cast<double>(Sixteenth)),
             F32(inner * static_cast<double>(Sixteenth)));
        White(sprites, alpha);
        return;
    }
    const float drawn = F32(static_cast<double>(sign) * static_cast<double>(height));
    Quad(sprites, slot, x, y, width, drawn, 0.0f, 0.0f,
         F32(static_cast<double>(width) * static_cast<double>(Sixteenth)),
         F32(static_cast<double>(height) * static_cast<double>(Sixteenth)));
    White(sprites, HighlightAlpha);
    sprites.ColorQuad(x, y, width, drawn);
    sprites.EnableTexture();
    White(sprites, alpha);
}

// 0x45c460, the vtable + 0x7c. The frame and the bar only with the log in its window; the filter's title, always.
void DrawTheFrame(const Ui::ChatLogState& log, Ui::ChatLogFrame& frame, Ui::ChatModes modes,
                  const ChatLogDrawContext& context, const ChatLogDrawView& view)
{
    Render::SpriteBatch& sprites = context.sprites;
    if (modes.placement == 1)
    {
        const std::int32_t sides = log.rows / RowsPerSide;
        frame.height = sides * SideHeight + FrameExtra;
        const std::int32_t top = frame.y - frame.height;
        const float x = F32(frame.x);
        const float width = F32(frame.width);
        sprites.SetBlend(Render::BlendMode::Alpha);
        sprites.SetColor(Render::Color{0.0f, 0.0f, 0.0f, F32(1.0 - static_cast<double>(log.alpha))});
        sprites.ColorQuad(x, F32(static_cast<double>(frame.y) - static_cast<double>(frame.height)), width,
                          F32(static_cast<double>(frame.height) + static_cast<double>(BackgroundExtra)));
        sprites.SetColor(Render::Color{});
        sprites.SetBlend(Render::BlendMode::Opaque);
        Quad(sprites, ChatFrameSlot, x, F32(top), width, SideWidth, 0.0f, 0.0f, FrameDu, FrameDv);
        const float right = F32(static_cast<double>(x) + static_cast<double>(width) - static_cast<double>(SideWidth));
        std::int32_t sideY = top + 5;
        for (std::int32_t side = 0; side < sides; ++side)
        {
            Quad(sprites, ChatSideSlot, x, F32(sideY), SideWidth, F32(SideHeight), 0.0f, 0.0f, SideUv, SideUv);
            Quad(sprites, ChatSideSlot, right, F32(sideY), SideWidth, F32(SideHeight), 0.0f, 0.0f, SideUv, SideUv);
            sideY += SideHeight;
        }
        const float lastY = F32(static_cast<double>(sideY) - static_cast<double>(LastSideRise));
        Quad(sprites, ChatSideSlot, x, lastY, SideWidth, F32(SideHeight), 0.0f, 0.0f, SideUv, SideUv);
        Quad(sprites, ChatSideSlot, right, lastY, SideWidth, F32(SideHeight), 0.0f, 0.0f, SideUv, SideUv);

        // With the previous bar (this draw's is computed by 0x47a230 after the arrows).
        int hover = 0;
        if (InRect(view, frame.x - 0x15 + frame.width, frame.y - frame.height + 8, 0xd, 0xd))
            hover = 1;
        else if (InRect(view, frame.x - 0x15 + frame.width, frame.y - 0x15, 0xd, 0xd))
            hover = 2;
        else if (InRect(view, frame.x - 0x13 + frame.width, Truncate(frame.thumbY), Truncate(frame.thumbWidth),
                        Truncate(frame.thumbHeight)))
            hover = 3;

        sprites.SetBlend(Render::BlendMode::Alpha);
        White(sprites, ArrowAlpha);
        const float arrowX = F32(static_cast<double>(x) + static_cast<double>(width) - static_cast<double>(ArrowInset));
        Button(sprites, ChatArrowSlot, hover == 1 && log.state != 2, view.leftDown, arrowX,
               F32(static_cast<double>(F32(frame.y)) - static_cast<double>(F32(frame.height)) +
                   static_cast<double>(ArrowTop)),
               ArrowSize, ArrowSize, ArrowAlpha, false);
        Button(sprites, ChatArrowSlot, hover == 2 && log.state != 2, view.leftDown, arrowX,
               F32(static_cast<double>(F32(frame.y)) - static_cast<double>(ArrowBottom)), ArrowSize, ArrowSize,
               ArrowAlpha, true);

        Ui::PlaceTheLogBar(log, modes, frame);
        const double right0 =
            static_cast<double>(x) + static_cast<double>(width) - static_cast<double>(frame.thumbWidth);
        const float trackHeight = F32(static_cast<double>(frame.barBottom) - static_cast<double>(frame.barTop));
        Quad(sprites, ChatTrackSlot, F32(right0 - static_cast<double>(TrackInset)), frame.barTop, frame.thumbWidth,
             trackHeight, 0.0f, 0.0f, TrackUv, TrackUv);
        const float thumbX = F32(right0 - static_cast<double>(ThumbInset));
        const float thumbWidth = F32(static_cast<double>(frame.thumbWidth) - static_cast<double>(ThumbNarrower));
        std::optional<std::array<float, 2>> veil;
        if (Ui::VisibleInTheLog(log, modes) < log.rows)
        {
            Quad(sprites, ChatThumbSlot, thumbX, frame.barTop, thumbWidth, trackHeight, 0.0f, 0.0f, ThumbUv, ThumbUv);
            if (hover == 3)
            {
                const float shade = view.leftDown ? 0.0f : 1.0f;
                sprites.SetColor(Render::Color{shade, shade, shade, HighlightAlpha});
                veil = std::array<float, 2>{frame.barTop, trackHeight};
            }
        }
        else
        {
            Quad(sprites, ChatThumbSlot, thumbX, frame.thumbY, thumbWidth, frame.thumbHeight, 0.0f, 0.0f, ThumbUv,
                 ThumbUv);
            if (hover == 3)
            {
                if (log.state == 2)
                    sprites.SetColor(Render::Color{0.0f, 0.0f, 0.0f, HighlightAlpha});
                else
                    sprites.SetColor(Render::Color{1.0f, 1.0f, 1.0f, HoverAlpha});
                veil = std::array<float, 2>{frame.thumbY, frame.thumbHeight};
            }
        }
        if (veil)
        {
            sprites.ColorQuad(thumbX, (*veil)[0], thumbWidth, (*veil)[1]);
            sprites.EnableTexture();
            White(sprites, ArrowAlpha);
        }
        const float grip = log.state == 1 ? Pressed : 1.0f;
        sprites.SetColor(Render::Color{grip, grip, grip, 1.0f});
        Quad(sprites, ChatGripSlot,
             F32(static_cast<double>(x) + static_cast<double>(width) - static_cast<double>(GripInset)),
             F32(static_cast<double>(F32(frame.y)) - static_cast<double>(F32(frame.height)) -
                 static_cast<double>(GripAbove) + static_cast<double>(GripDown)),
             GripWidth, GripHeight, 0.0f, 0.0f, ThumbUv, GripDv);
        sprites.SetBlend(Render::BlendMode::Opaque);
    }
    const std::string joined = Ui::JoinedChatFilter(log);
    if (!joined.empty())
    {
        // sprintf("%s: %s", Text.bmd 754, the words).
        const std::string title = std::string(Ui::TextAt(view.texts, Ui::FilterTitleText).value_or("")) + ": " + joined;
        context.textColors =
            Render::Text::TextColors{.text = 0xffc8dce6u, .background = modes.placement != 1 ? 0x96000000u : 0u};
        context.dcFont = Render::Text::FontKind::Bold;
        sprites.SetBlend(Render::BlendMode::Alpha);
        sprites.SetColor(Render::Color{});
        context.text.Draw(frame.x + NameX, frame.y, title,
                          Render::Text::TextStyle{.font = context.dcFont, .colors = context.textColors});
        sprites.SetBlend(Render::BlendMode::Opaque);
        context.dcFont = Render::Text::FontKind::Normal;
    }
    ++frame.blink;
}

// 0x45cbb0: the name's colors by type; 0x45cbb0 has no case for 7 or more and the previous ones stay.
void NameColors(std::int32_t type, Render::Text::TextColors& colors)
{
    switch (type)
    {
    case 0:
        colors = {.text = 0xff000000u, .background = 0x9632c8ffu};
        break;
    case 1:
        colors = {.text = 0xffff9664u, .background = 0x96000000u};
        break;
    case 2:
        colors = {.text = 0xff001effu, .background = 0x96000000u};
        break;
    case 3:
        colors = {.text = 0xffefdccdu, .background = 0x96000000u};
        break;
    case 4:
        colors = {.text = 0xff000000u, .background = 0x96ffc800u};
        break;
    case 5:
        colors = {.text = 0xff000000u, .background = 0xc896ff00u};
        break;
    case 6:
        colors = {.text = 0xff000000u, .background = 0xc800c8c8u};
        break;
    default:
        break;
    }
}

// The text's: the normal chat lighter and no case for 6.
void TextColors(std::int32_t type, Render::Text::TextColors& colors)
{
    if (type == 3)
        colors = {.text = 0xffc8dce6u, .background = 0x96000000u};
    else if (type != 6)
        NameColors(type, colors);
}

// With the log in its window, a black background is not drawn.
void ClearBlackBackground(Ui::ChatModes modes, Render::Text::TextColors& colors)
{
    if (modes.placement == 1 && (colors.background & 0xffffffu) == 0)
        colors.background = 0;
}

// 0x45cbb0, the vtable + 0x80: the line `row` (the mode of 0x5763510 + 8 is 0 in this main.exe: without the color
// codes of "%c%c%s"). It returns the same as 0x45dec0.
std::int32_t DrawTheRow(const Ui::ChatLogState& log, Ui::ChatLogFrame& frame, Ui::ChatModes modes,
                        const ChatLogDrawContext& context, const ChatLogDrawView& view, std::size_t at,
                        std::int32_t row)
{
    const Ui::ChatEntry& entry = log.entries[at];
    if (modes.placement == 1 && frame.x == 0)
    {
        ++row;
        if (log.rows <= row)
            return 1;
    }
    if (!modes.showChat && entry.type == 3)
        return 0;
    if (modes.placement == 1)
    {
        if (entry.type == 1 || entry.type == 2)
            return 0;
    }
    else if (modes.placement == 0 && entry.age != 0 && log.scroll == 0)
    {
        const auto age = static_cast<std::int32_t>(entry.age);
        if (log.rows - age - row - 1 < 0)
            return row - log.rows;
        row += age;
    }
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    const bool crywolf = view.crywolfStage == Protocol::Events::CrywolfReadyStage ||
                         view.crywolfStage == Protocol::Events::CrywolfStartStage;
    const auto rowY = [&]
    {
        if (!crywolf)
            return frame.y - row * RowHeight - RowsBottom;
        if (frame.x == CrywolfListX)
            frame.y = CrywolfListY;
        return frame.y - row * RowHeight + CrywolfRowsBelow;
    };
    const Render::Text::TextStyle style{.font = context.dcFont, .colors = {}};
    std::int32_t nameWidth = 0;
    if (!entry.name.empty())
    {
        NameColors(entry.type, context.textColors);
        ClearBlackBackground(modes, context.textColors);
        if (log.selection == log.scroll + 1 + row && frame.blink % 6 < 3)
        {
            const std::uint32_t background = context.textColors.background;
            context.textColors.background = context.textColors.text;
            context.textColors.text = background;
        }
        const std::string name = Ui::NameOf(entry) + ": ";
        const std::int32_t y = rowY();
        context.text.Draw(frame.x + NameX, y, name,
                          Render::Text::TextStyle{.font = style.font, .colors = context.textColors});
        nameWidth = Render::Text::DesignX(context.text.Measure(context.dcFont, name).width, context.sprites.Screen());
    }
    TextColors(entry.type, context.textColors);
    ClearBlackBackground(modes, context.textColors);
    const std::int32_t y = rowY();
    if (!entry.text.empty())
        context.text.Draw(frame.x + NameX + nameWidth, y, entry.text,
                          Render::Text::TextStyle{.font = style.font, .colors = context.textColors});
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x45d420, through 0x45cba0 (the vtable + 0x84): the buttons appear and leave by 0.2 per draw; without the chat field
// open, the F2 one (turned off, darker and moved by one point), the size one and the transparency one, and the help of
// the one under the mouse.
void DrawTheButtons(const Ui::ChatLogState& log, Ui::ChatLogFrame& frame, Ui::ChatModes modes,
                    const ChatLogDrawContext& context, const ChatLogDrawView& view)
{
    if (log.buttonsShown == 1 && frame.buttonsAlpha < 1.0f)
        frame.buttonsAlpha = F32(static_cast<double>(frame.buttonsAlpha) + static_cast<double>(ButtonsFade));
    else if (log.buttonsShown == 0 && frame.buttonsAlpha > 0.0f)
        frame.buttonsAlpha = F32(static_cast<double>(frame.buttonsAlpha) - static_cast<double>(ButtonsFade));
    if (frame.buttonsAlpha <= 0.0f || view.chatOpen)
        return;
    Render::SpriteBatch& sprites = context.sprites;
    const float alpha = frame.buttonsAlpha;
    sprites.SetBlend(Render::BlendMode::Alpha);
    White(sprites, alpha);
    context.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    const float second = F32(static_cast<double>(ButtonsStep) + static_cast<double>(ButtonsX));
    const float third =
        F32(static_cast<double>(ButtonsStep) + static_cast<double>(ButtonsStep) + static_cast<double>(ButtonsX));
    const std::int32_t buttonY = Truncate(static_cast<double>(ButtonsY));
    int hover = 0;
    if (InRect(view, Truncate(static_cast<double>(ButtonsX)), buttonY, ButtonPixels, ButtonPixels))
        hover = 1;
    else if (InRect(view, Truncate(static_cast<double>(second)), buttonY, ButtonPixels, ButtonPixels))
        hover = 2;
    else if (InRect(view, Truncate(static_cast<double>(third)), buttonY, ButtonPixels, ButtonPixels))
        hover = 3;
    if (!modes.showChat)
    {
        sprites.SetColor(Render::Color{Pressed, Pressed, Pressed, alpha});
        Quad(sprites, ChatShowButtonSlot, ButtonsX + 1.0f, ButtonsY + 1.0f, ButtonSize, ButtonSize, 0.0f, 0.0f,
             OffButtonUv, OffButtonUv);
        White(sprites, alpha);
    }
    else
        Button(sprites, ChatShowButtonSlot, hover == 1, view.leftDown, ButtonsX, ButtonsY, ButtonSize, ButtonSize,
               alpha, false);
    Button(sprites, ChatSizeButtonSlot, hover == 2, view.leftDown, second, ButtonsY, ButtonSize, ButtonSize, alpha,
           false);
    Button(sprites, ChatAlphaButtonSlot, hover == 3, view.leftDown, third, ButtonsY, ButtonSize, ButtonSize, alpha,
           false);
    if (hover != 0)
    {
        const std::array<float, 3> xs{ButtonsX, second, third};
        const auto index = static_cast<std::size_t>(hover - 1);
        const std::string_view help = Ui::TextAt(view.texts, Ui::ChatButtonHelpTexts[index]).value_or("");
        // 0x554340 leaves the text white on a transparent background.
        Ui::DrawHelp(sprites, context.text, Truncate(static_cast<double>(xs[index]) - static_cast<double>(HelpLeft)),
                     Truncate(static_cast<double>(ButtonsY) - static_cast<double>(HelpRise)), help);
        context.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    }
    sprites.SetColor(Render::Color{});
    sprites.SetBlend(Render::BlendMode::Opaque);
}
} // namespace

void DrawTheChatLog(const Ui::ChatLogState& log, Ui::ChatLogFrame& frame, Ui::ChatModes modes,
                    const ChatLogDrawContext& context, const ChatLogDrawView& view)
{
    DrawTheFrame(log, frame, modes, context, view);
    std::size_t at = Ui::FirstShownInTheLog(log, modes);
    context.sprites.SetColor(Render::Color{});
    context.dcFont = Render::Text::FontKind::Normal;
    for (std::int32_t row = 0; row < log.rows; ++row)
    {
        if (at >= log.entries.size())
            break;
        const std::int32_t moved = DrawTheRow(log, frame, modes, context, view, at, row);
        if (moved < 0)
            row -= moved;
        else if (moved == 0)
            --row;
        ++at;
    }
    DrawTheButtons(log, frame, modes, context, view);
}
} // namespace Client::Chat
