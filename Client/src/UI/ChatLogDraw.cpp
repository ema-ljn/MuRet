#include "UI/ChatLogDraw.h"

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextLayout.h"
#include "Render/TextureSlot.h"
#include "Scenes/ITextDrawer.h"
#include "UI/ChatLogWindow.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t RowHeight = 0xd;
// 0x45cbb0: el renglón sobre el borde de abajo, o 0x65 debajo de él en Crywolf, y en Crywolf la lista de x = 0xba
// sube a y = 0x12f.
constexpr std::int32_t RowsBottom = 0x10;
constexpr std::int32_t CrywolfRowsBelow = 0x65;
constexpr std::int32_t CrywolfListX = 0xba;
constexpr std::int32_t CrywolfListY = 0x12f;
constexpr std::int32_t NameX = 10;
// 0x45c460: 40 de alto cada 3 renglones, más 10.
constexpr std::int32_t RowsPerSide = 3;
constexpr std::int32_t SideHeight = 0x28;
constexpr std::int32_t FrameExtra = 10;
// 0x6b8864, 0x6b87f8 y 0x6b8820: el fondo 12 más alto, los lados de 5 de ancho y el último par 22 más arriba.
constexpr float BackgroundExtra = 12.0f;
constexpr float SideWidth = 5.0f;
constexpr float LastSideRise = 22.0f;
// 0x6b8b10, 0x6b86c4 y 0x6b8a44: las flechas, 21 desde el borde derecho, la de arriba 8 debajo del borde de arriba y
// la de abajo, dada vuelta, 4 sobre el de abajo.
constexpr float ArrowInset = 21.0f;
constexpr float ArrowTop = 8.0f;
constexpr float ArrowBottom = 4.0f;
constexpr float ArrowSize = 13.0f;
// 0x6b8878 y 0x6b8818: el cursor, 7 desde el borde derecho menos su ancho y 2 más angosto.
constexpr float TrackInset = 8.0f;
constexpr float ThumbInset = 7.0f;
constexpr float ThumbNarrower = 2.0f;
// 0x6b880c, 0x6b86c4 y 0x6b8a44: el agarre, 28 desde el borde derecho y en (y − h) − 8 + 4.
constexpr float GripInset = 28.0f;
constexpr float GripAbove = 8.0f;
constexpr float GripDown = 4.0f;
constexpr float GripWidth = 22.0f;
constexpr float GripHeight = 12.0f;
// Las UV de 0x45c460: 0x3f550000, 0x3f200000, 0x3f500000, 0x3f300000 y 0x3f400000.
constexpr float FrameDu = 0.83203125f;
constexpr float FrameDv = 0.625f;
constexpr float SideUv = 0.625f;
constexpr float TrackUv = 0.8125f;
constexpr float ThumbUv = 0.6875f;
constexpr float GripDv = 0.75f;
// 0x3f19999a, 0x3dcccccd, 0x3e4ccccd, 0x3f333333 y 0x3f4ccccd.
constexpr float ArrowAlpha = 0.6f;
constexpr float HighlightAlpha = 0.1f;
constexpr float HoverAlpha = 0.2f;
constexpr float Pressed = 0.7f;
constexpr float Darker = 0.8f;
// 0x6b8850: 1/16.
constexpr float Sixteenth = 0.0625f;
// 0x45d420: los botones, desde (295, 417) de a 18, de 16 × 16; su transparencia sube o baja de a 0,2 (0x6b8854).
constexpr float ButtonsX = 295.0f;
constexpr float ButtonsY = 417.0f;
constexpr float ButtonsStep = 18.0f;
constexpr float ButtonSize = 16.0f;
constexpr std::int32_t ButtonPixels = 0x10;
constexpr float ButtonsFade = 0.2f;
// 0x3f700000: el botón del F2 apagado.
constexpr float OffButtonUv = 0.9375f;
// 0x6b87a8 y 0x6b8f00: la ayuda, 10 más arriba y 16 más a la izquierda.
constexpr float HelpRise = 10.0f;
constexpr float HelpLeft = 16.0f;

// _ftol (0x69fd3c): trunca hacia el 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

float F32(std::int32_t value)
{
    return static_cast<float>(value);
}

bool InRect(const ChatLogDrawView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, TopLeftRect);
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado, con el color que haya.
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

// 0x45d230(slot, apretado, x, y, w, h, alfa, al revés): un botón de 16 píxeles por cuadro. Suelto, el quad (dado
// vuelta con la altura negativa); apretado con el botón abajo, más oscuro y corrido un punto; apretado sin el botón,
// con un velo blanco de alfa 0,1 encima. Deja el color en (1, 1, 1, alfa).
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

// 0x45c460, la vtable + 0x7c. El marco y la barra solo con el registro en su ventana; el título del filtro, siempre.
void DrawTheFrame(const ChatLogState& log, ChatLogFrame& frame, ChatModes modes, const ChatLogDrawContext& context,
                  const ChatLogDrawView& view)
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

        // Con la barra de antes (la de este dibujo la calcula 0x47a230 después de las flechas).
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

        PlaceTheLogBar(log, modes, frame);
        const double right0 =
            static_cast<double>(x) + static_cast<double>(width) - static_cast<double>(frame.thumbWidth);
        const float trackHeight = F32(static_cast<double>(frame.barBottom) - static_cast<double>(frame.barTop));
        Quad(sprites, ChatTrackSlot, F32(right0 - static_cast<double>(TrackInset)), frame.barTop, frame.thumbWidth,
             trackHeight, 0.0f, 0.0f, TrackUv, TrackUv);
        const float thumbX = F32(right0 - static_cast<double>(ThumbInset));
        const float thumbWidth = F32(static_cast<double>(frame.thumbWidth) - static_cast<double>(ThumbNarrower));
        std::optional<std::array<float, 2>> veil;
        if (VisibleInTheLog(log, modes) < log.rows)
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
    const std::string joined = JoinedChatFilter(log);
    if (!joined.empty())
    {
        // sprintf("%s: %s", Text.bmd 754, las palabras).
        const std::string title = std::string(TextAt(view.texts, FilterTitleText).value_or("")) + ": " + joined;
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

// 0x45cbb0: los colores del nombre por tipo; 0x45cbb0 no tiene caso para 7 o más y quedan los de antes.
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

// Los del texto: el chat normal más claro y sin caso para 6.
void TextColors(std::int32_t type, Render::Text::TextColors& colors)
{
    if (type == 3)
        colors = {.text = 0xffc8dce6u, .background = 0x96000000u};
    else if (type != 6)
        NameColors(type, colors);
}

// Con el registro en su ventana, un fondo negro no se dibuja.
void ClearBlackBackground(ChatModes modes, Render::Text::TextColors& colors)
{
    if (modes.placement == 1 && (colors.background & 0xffffffu) == 0)
        colors.background = 0;
}

// 0x45cbb0, la vtable + 0x80: el renglón `row` (el modo de 0x5763510 + 8 es 0 en este main.exe: sin los códigos de
// color de "%c%c%s"). Devuelve lo mismo que 0x45dec0.
std::int32_t DrawTheRow(const ChatLogState& log, ChatLogFrame& frame, ChatModes modes,
                        const ChatLogDrawContext& context, const ChatLogDrawView& view, std::size_t at,
                        std::int32_t row)
{
    const ChatEntry& entry = log.entries[at];
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
    const bool crywolf = view.crywolfStage == 3 || view.crywolfStage == 4;
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
        const std::string name = NameOf(entry) + ": ";
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

// 0x45d420, por 0x45cba0 (la vtable + 0x84): los botones aparecen y se van de a 0,2 por dibujo; sin el campo del
// chat abierto, el del F2 (apagado, más oscuro y corrido un punto), el del tamaño y el de la transparencia, y la
// ayuda del que tiene el mouse encima.
void DrawTheButtons(const ChatLogState& log, ChatLogFrame& frame, ChatModes modes, const ChatLogDrawContext& context,
                    const ChatLogDrawView& view)
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
        const std::string_view help = TextAt(view.texts, ChatButtonHelpTexts[index]).value_or("");
        // 0x554340 deja el texto en blanco sobre un fondo transparente.
        DrawHelp(sprites, context.text, Truncate(static_cast<double>(xs[index]) - static_cast<double>(HelpLeft)),
                 Truncate(static_cast<double>(ButtonsY) - static_cast<double>(HelpRise)), help);
        context.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    }
    sprites.SetColor(Render::Color{});
    sprites.SetBlend(Render::BlendMode::Opaque);
}
} // namespace

void DrawTheChatLog(const ChatLogState& log, ChatLogFrame& frame, ChatModes modes, const ChatLogDrawContext& context,
                    const ChatLogDrawView& view)
{
    DrawTheFrame(log, frame, modes, context, view);
    std::size_t at = FirstShownInTheLog(log, modes);
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
} // namespace Client::UI
