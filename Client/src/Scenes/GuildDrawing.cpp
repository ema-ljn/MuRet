#include "Scenes/GuildDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/OldWindowDraw.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::Quad;
using WindowDrawing::SelectFont;

// 0x5a764e..0x5a7653.
constexpr std::uint32_t ScoreBackground = 0x80000000u;
// 0x5a768a..0x5a76a7: ancho del mundo · 0,5 (0x6b882c) − 20 (0x6b86d0), en float.
constexpr double HalfOfTheWorld = 0.5;
constexpr double ScoreLeft = 20.0;
// 0x5a76b0..0x5a76d3 y 0x5a7843..0x5a786a: NEG y SBB con el equipo (0x576796e).
constexpr std::uint32_t OrangeText = 0xffff9600u;
constexpr std::uint32_t RedText = 0xff003cffu;
// 0x5a76dd..0x5a7706 y 0x5a78c4..0x5a78ed: 0x621b60(0x22, x, y, 8, 8, 0, 0, 1, 1, 1, 1, 0).
constexpr float MarkSize = 8.0f;
constexpr float OursTop = 4.0f;
constexpr float TheirsTop = 19.0f;
constexpr std::int32_t OursTextTop = 4;
constexpr std::int32_t TheirsTextTop = 0x13;
// 0x5a775a y 0x5a77fb: los textos en _ftol(x) + 0xd y + 0x1e.
constexpr std::int32_t ScoreOffset = 0xd;
constexpr std::int32_t NameOffset = 0x1e;
// 0x5a785f..0x5a78b8: la búsqueda del rival, de la entrada 0 a la 1998 (EDI < 0x78a597d).
constexpr std::size_t RivalSearched = GuildTableSize - 1;

// 0x6576a0: Message_box1 (0xfb) y Message_box2 (0xfc) de 0x61ccb0, de 213 de ancho en x = 214, con las UV (0, 0,
// 0,83203125, 0,625) y glColor4f(1, 1, 1, 0,8); los filos de 5 (0x6b87f8) y las filas de 40 (0x6b8bf4) por cada 40
// de (alto − 10); el resto, (resto · 0,025 (0x6ba1fc)) · 40 de alto con dv = (resto · 0,025) · 0,625 (0x6baad0).
constexpr std::uint16_t BoxRowSlot = 0xfb;
constexpr std::uint16_t BoxEdgeSlot = 0xfc;
constexpr float BoxX = 214.0f;
constexpr float BoxWidth = 213.0f;
constexpr float BoxEdge = 5.0f;
constexpr float BoxRow = 40.0f;
constexpr std::int32_t BoxRowStep = 40;
constexpr std::int32_t BoxMargin = 10;
constexpr float BoxDu = 0.83203125f;
constexpr float BoxDv = 0.625f;
constexpr float BoxRestScale = 0.025f;
constexpr Render::Color BoxColor{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 0.8f};
constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};
// 0x6608c6..0x660968: los colores −1 y 0, los renglones centrados en 0x140 desde y + 0x14, de 15 (0x6b87bc) en 15.
constexpr Render::Text::TextColors DialogColors{.text = 0xffffffffu, .background = 0u};
constexpr std::int32_t DialogCenterX = 0x140;
constexpr std::int32_t FirstLineOffset = 0x14;
constexpr float LineStep = 15.0f;
// 0x65ff12..0x65ff9c: los textos que 0x65feb0 les copia a los botones (vtable + 0x2c, 0x45b170): Aceptar o Sí, No,
// Confirmar y Cancelar.
constexpr std::array<std::size_t, UI::PanelDialogButtonCount> ButtonTexts{228, 229, 1037, 1038};

float F32(double value)
{
    return static_cast<float>(value);
}

template <std::size_t Size> std::string_view NameOf(const std::array<char, Size>& name)
{
    const auto end = std::ranges::find(name, '\0');
    return {name.data(), static_cast<std::size_t>(end - name.begin())};
}

// 0x5a7870..0x5a78b8: strcmp de +0xd de cada entrada con el rival; sin ninguna, EAX queda en 0 (0x5a78ba).
std::int32_t RivalMarkOf(const GuildState& guild)
{
    const std::string_view rival = NameOf(guild.rival);
    for (std::size_t index = 0; index < RivalSearched; ++index)
    {
        if (NameOf(guild.table[index].guildName) == rival)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

// 0x5f16a0(índice, 1) y el quad del slot donde quedó, en (x, top).
Core::Expected<void, std::string> DrawTheMark(GameHudContext& context, const GuildState& guild, std::int32_t index,
                                              float x, float top)
{
    auto slot = context.guildMarks->Upload(guild, index, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    Quad(context.sprites, slot.Value().value, x, top, MarkSize, MarkSize, 1.0f, 1.0f);
    return {};
}

// 0x6576a0(alto, y): la caja desde la y que eligió (un entero, sin redondeo en float).
void DrawTheDialogBox(Render::SpriteBatch& sprites, std::int32_t height, std::int32_t top)
{
    sprites.SetBlendWithoutAlphaTest();
    sprites.SetColor(BoxColor);
    auto y = static_cast<float>(top);
    Quad(sprites, BoxEdgeSlot, BoxX, y, BoxWidth, BoxEdge, BoxDu, BoxDv);
    // 0x657741..0x657764: (alto − 10) / 40 con el redondeo hacia 0 de la división con signo.
    const std::int32_t rest = height - BoxMargin;
    const std::int32_t rows = rest / BoxRowStep;
    y = F32(static_cast<double>(y) + static_cast<double>(BoxEdge));
    for (std::int32_t row = 0; row < rows; ++row)
    {
        Quad(sprites, BoxRowSlot, BoxX, y, BoxWidth, BoxRow, BoxDu, BoxDv);
        y = F32(static_cast<double>(y) + static_cast<double>(BoxRow));
    }
    if (rest > rows * BoxRowStep)
    {
        // 0x6577c5..0x6577e8: el producto queda en la x87 y se guarda en float dos veces; con un resto chico, exacto.
        const double scaled = static_cast<double>(rest - rows * BoxRowStep) * static_cast<double>(BoxRestScale);
        const float last = F32(scaled * static_cast<double>(BoxRow));
        Quad(sprites, BoxRowSlot, BoxX, y, BoxWidth, last, BoxDu, F32(scaled * static_cast<double>(BoxDv)));
        y = F32(static_cast<double>(last) + static_cast<double>(y));
    }
    Quad(sprites, BoxEdgeSlot, BoxX, y, BoxWidth, BoxEdge, BoxDu, BoxDv);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
}

// 0x5542f0(x, y, texto, 0, 1, 0): 0x465950 con la fuente del DC y los colores de 0x6c8e5c; uno vacío no se dibuja. No
// mide (0x77f31b8 es de 0x554180).
void DrawTheLine(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    if (text.empty())
        return;
    context.text.Draw(x, y, text,
                      Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors});
}
} // namespace

Core::Expected<void, std::string> DrawTheWarScore(GameHudContext& context, UI::PanelRegistry& panels)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(BlendMode::Opaque);
    context.hud.textColors.background = ScoreBackground;
    WorldState& world = context.world;
    const GuildState& guild = world.Runtime().guild;
    const auto heroGuild = static_cast<std::int16_t>(world.Actors()[world.Runtime().hero].word1f2);
    // 0x5a7a01: el segundo marcador, con 0x576796f, que prende el F3 23 (0x4d9f00, hito 11): en MuRet, nunca.
    if (guild.warOn == 0 || heroGuild == NoGuildIndex)
        return {};
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x5a76d8: la marca del gremio (0x5f16a0) sin la tabla de las marcas"));
    if (heroGuild < 0 || static_cast<std::size_t>(heroGuild) >= GuildTableSize)
        return Core::Unexpected(std::format("0x5a77c7: el gremio {} del héroe cae fuera de la tabla", heroGuild));
    sprites.SetBlend(BlendMode::Alpha);
    const auto x = static_cast<float>(static_cast<double>(panels.WorldWidth()) * HalfOfTheWorld - ScoreLeft);
    SelectFont(context, FontKind::Bold);
    const bool team = guild.warTeam != 0;
    context.hud.textColors.text = team ? OrangeText : RedText;
    if (auto drawn = DrawTheMark(context, guild, heroGuild, x, OursTop); !drawn.HasValue())
        return drawn;
    const auto left = static_cast<std::int32_t>(x);
    DrawText(context, left + ScoreOffset, OursTextTop, std::format("{}", guild.ourScore));
    DrawText(context, left + NameOffset, OursTextTop,
             NameOf(guild.table[static_cast<std::size_t>(heroGuild)].guildName));
    context.hud.textColors.text = team ? RedText : OrangeText;
    if (auto drawn = DrawTheMark(context, guild, RivalMarkOf(guild), x, TheirsTop); !drawn.HasValue())
        return drawn;
    DrawText(context, left + ScoreOffset, TheirsTextTop, std::format("{}", guild.theirScore));
    // 0x5a79b0: 0x5542f0(…, 0, 1, 0) es el modo 1 de 0x554180 y mide lo mismo que la medida de 0x5a79ba que sigue.
    DrawText(context, left + NameOffset, TheirsTextTop, NameOf(guild.rival));
    return {};
}

Core::Expected<void, std::string> DrawThePanelDialog(GameHudContext& context, UI::PanelDialogState& dialog)
{
    if (UI::PanelDialogHandle(dialog) == 0)
        return {};
    // 0x660846: +0xc, el dibujo de quien lo abrió, lo escribe solo 0x660090, en 0 (*inferido*): nunca hay.
    if ((dialog.flags & UI::PanelDialogTextField) != 0)
        return Core::Unexpected(
            std::string("0x66097a: el campo de texto del diálogo de los paneles (0x10, la caja 0x10f, "
                        "el EDIT o el chat de 0x553ae0): ningún diálogo de MuRet lo abre (P-C285)"));
    if ((dialog.flags & UI::PanelDialogTimed) != 0)
        return Core::Unexpected(
            std::string("0x660ad6: el reloj del diálogo de los paneles (8, las barras 0x667 y 0x668 "
                        "de 0x61e605): ningún diálogo de MuRet lo abre (P-C285)"));
    for (std::size_t button = 0; button < dialog.buttons.size(); ++button)
    {
        // 0x65feb0 los copia al armarse, después de Text.bmd (0x4b9b9f); MuRet, la primera vez que dibuja.
        if (dialog.buttons[button].label)
            continue;
        const std::optional<std::string_view> label = UI::TextAt(context.texts, ButtonTexts[button]);
        if (!label)
            return Core::Unexpected(
                std::format("0x65ff12: el botón {} del diálogo de los paneles sin el texto {} de Text.bmd", button,
                            ButtonTexts[button]));
        dialog.buttons[button].label = std::string(*label);
    }
    // 0x660856..0x6608a1 y los 0x45a9d0 de los botones: la esquina de la caja y la y de los botones.
    const UI::Point corner = UI::PlaceThePanelDialog(dialog);
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheDialogBox(sprites, dialog.height, corner.y);
    SelectFont(context, FontKind::Normal);
    sprites.SetBlend(BlendMode::Alpha);
    context.hud.textColors = DialogColors;
    auto y = static_cast<float>(corner.y + FirstLineOffset);
    const std::int32_t count = std::min(dialog.lineCount, static_cast<std::int32_t>(dialog.lines.size()));
    for (std::int32_t line = 0; line < count; ++line)
    {
        const std::string& text = dialog.lines[static_cast<std::size_t>(line)];
        // 0x6608f2..0x66093f: GetTextExtentPoint32A con el DC, cx / rx con _ftol y la mitad con signo.
        const std::int32_t width =
            Render::Text::DesignX(context.text.Measure(context.hud.dcFont, text).width, sprites.Screen());
        DrawTheLine(context, DialogCenterX - width / 2, static_cast<std::int32_t>(y), text);
        y = F32(static_cast<double>(y) + static_cast<double>(LineStep));
    }
    const UI::OldWindowDrawContext buttons{
        .sprites = sprites, .text = context.text, .dcFont = context.hud.dcFont, .textColors = context.hud.textColors};
    const UI::OldWindowDrawView view{.mouseX = context.mouse.x, .mouseY = context.mouse.y};
    if ((dialog.flags & UI::PanelDialogOk) != 0)
        UI::DrawTheOldButton(buttons, dialog.buttons[UI::PanelOkButton], view);
    else if ((dialog.flags & UI::PanelDialogYesNo) != 0)
    {
        UI::DrawTheOldButton(buttons, dialog.buttons[UI::PanelOkButton], view);
        UI::DrawTheOldButton(buttons, dialog.buttons[UI::PanelNoButton], view);
    }
    else if ((dialog.flags & UI::PanelDialogConfirm) != 0)
    {
        UI::DrawTheOldButton(buttons, dialog.buttons[UI::PanelConfirmButton], view);
        UI::DrawTheOldButton(buttons, dialog.buttons[UI::PanelCancelButton], view);
    }
    return {};
}
} // namespace Client::Scenes
