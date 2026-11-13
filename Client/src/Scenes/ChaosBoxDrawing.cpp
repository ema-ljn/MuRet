#include "Scenes/ChaosBoxDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ChaosMix.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
#include "World/OriginalFtol.h"

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
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::DrawTitle;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5fec15..0x5fec2c: la esquina de la ventana del NPC.
constexpr std::int32_t WindowLeft = 0x104;
constexpr std::int32_t WindowTop = 0;
// 0x5fec31..0x5fec5d: la grilla de 8 × 4 en la esquina más 15 (0x6b87bc) y 110 (0x6ba184).
constexpr double GridLeft = 15.0;
constexpr double GridTop = 110.0;
constexpr std::size_t GridColumns = 8;
constexpr std::size_t GridRows = 4;
// Los renglones, de la esquina (0x554180).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 8;
constexpr std::int32_t TaxTop = 0x14;
constexpr std::int32_t PlainTitleTop = 0xc;
constexpr std::int32_t LineLeft = 0x14;
constexpr std::int32_t NameTop = 0x32;
constexpr std::int32_t RateTop = 0x46;
constexpr std::int32_t PriceTop = 0x5a;
constexpr std::int32_t JewelTop = 0xd2;
constexpr std::int32_t JewelSecondTop = 0xe1;
constexpr std::int32_t WarningTop = 0xf0;
constexpr std::int32_t WarningSecondTop = 0xff;
constexpr std::int32_t WarningThirdTop = 0x10e;
// 0x5ff6f0..0x5ff739: Interface\... 0x124 (y 0x125 apretado) de 44 × 32 (0x6ba730 y 0x6b883c) en la esquina más 75
// (0x6ba62c) y 300 (0x6b8900), con du = 0,6875 y dv = 1; la ayuda 13 más arriba.
constexpr std::uint16_t MixButtonSlot = 0x124;
constexpr std::uint16_t MixButtonDownSlot = 0x125;
constexpr double MixButtonLeft = 75.0;
constexpr double MixButtonTop = 300.0;
constexpr float MixButtonWidth = 44.0f;
constexpr float MixButtonHeight = 32.0f;
constexpr float MixButtonDu = 0.6875f;
constexpr float MixButtonDv = 1.0f;
constexpr std::int32_t HelpAbove = 0xd;

// Los textos de Text.bmd (0x76bd1dc + 300 · n).
constexpr std::size_t PlainTitleText = 583;    // 0x76e7d10, el modo de 2 en adelante
constexpr std::size_t RegularTitleText = 735;  // 0x76f2f30, el modo 0
constexpr std::size_t WeaponTitleText = 736;   // 0x76f305c, el modo 1
constexpr std::size_t TaxText = 1623;          // 0x7733fd0
constexpr std::size_t RateText = 584;          // 0x76e7e3c
constexpr std::size_t MixingText = 591;        // 0x76e8670
constexpr std::size_t PriceText = 1622;        // 0x7733ea4
constexpr std::size_t PotionPriceText = 1636;  // 0x7734f0c, el 0xf y el 0x10
constexpr std::size_t JewelText = 586;         // 0x76e8094
constexpr std::size_t JewelSecondText = 587;   // 0x76e81c0
constexpr std::size_t WarningText = 588;       // 0x76e82ec, en el modo 1
constexpr std::size_t WarningSecondText = 589; // 0x76e8418
constexpr std::size_t WarningThirdText = 590;  // 0x76e8544, también el renglón de las pociones
constexpr std::size_t UpgradeText = 606;       // 0x76e9804, 607 y 608: el 3, el 4, el 0x16 y el 0x17
constexpr std::size_t FailText = 610;          // 0x76e9cb4 y 611: el 5 y el 6
constexpr std::size_t CapeNameText = 1172;     // 0x7712f4c, el 0x18
constexpr std::size_t CreativeText = 664;      // 0x76edbfc, el 0x16 y el 0x17 con el tipo − 10
constexpr std::size_t CreateText = 1412;       // 0x772488c
constexpr std::size_t CapeKindText = 1863;     // 0x7745910 con la capa 3 y 0x7745a3c (1864) con la 4
constexpr std::size_t FirstNameText = 601;     // 0x76bd1dc + 300 · (tipo + 0x259) hasta el 4
constexpr std::size_t LaterNameText = 607;     // y + 0x25f desde el 5

// [EBP − 0xac]: el buffer de los renglones. Hasta el precio en texto de 0x5b1130 ([EBP − 0x28]), que se usa en el
// renglón del precio, hay 0x84 bytes; después del precio, hasta lo guardado de la pila, 0xac (*inferido* del marco).
constexpr std::size_t RoomBeforeThePrice = 0x84;
constexpr std::size_t RoomOfTheFrame = 0xac;

// El tipo 0xb muestra el nombre y el color de las alas (0x5ff0d4..0x5ff0db).
constexpr std::int32_t SecondWingsKind = 0xb;
constexpr std::int32_t WingsKind = 7;
constexpr std::int32_t CapeKind = 0x18;
constexpr std::int32_t CapeOfTheFenrirKind = 0x1c;

// 0x6c8e5c y 0x6c8e64 de cada renglón.
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr std::uint32_t LineBackground = 0x80282828u;
constexpr std::uint32_t MixableColor = 0xff30ffffu;
constexpr std::uint32_t NothingColor = 0xff3030ffu;
constexpr std::uint32_t LineColor = 0xffffe6d2u;
constexpr std::uint32_t JewelColor = 0xff1432ffu;
constexpr Render::Text::TextColors HelpColors{.text = 0xffffffffu, .background = LineBackground};

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// Lo que sprintf deja en [EBP − 0xac], si entra en `room` bytes con su cero.
Core::Expected<std::string, std::string> Fits(std::string text, std::size_t room, std::string_view where)
{
    if (text.size() >= room)
        return Core::Unexpected(std::format("{}: el sprintf escribe {} bytes en [EBP − 0xac] y pisa la pila ({} de "
                                            "lugar)",
                                            where, text.size() + 1, room));
    return text;
}

// 0x5fec8d..0x5fed95: el título del modo (0x7d3a114) en la caja de 120 · W / 640; los modos 0 y 1 con el impuesto
// abajo (sprintf del texto 1623 con 0x78aba34).
Core::Expected<void, std::string> DrawTheTitle(GameHudContext& context, const NpcWindowState& npc, std::int32_t x,
                                               std::int32_t y)
{
    if (npc.chaosMixMode != 0 && npc.chaosMixMode != 1)
    {
        auto title = TextOf(context, PlainTitleText, "0x5fece2");
        if (!title.HasValue())
            return Core::Unexpected(title.Error());
        DrawTitle(context, x + TitleLeft, y + PlainTitleTop, title.Value());
        return {};
    }
    auto title = TextOf(context, npc.chaosMixMode == 0 ? RegularTitleText : WeaponTitleText, "0x5fed39");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    auto format = TextOf(context, TaxText, "0x5fed54");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    auto tax = Fits(UI::Sprintf(format.Value(), npc.chaosTaxRate), RoomOfTheFrame, "0x5fed54");
    if (!tax.HasValue())
        return Core::Unexpected(tax.Error());
    DrawTitle(context, x + TitleLeft, y + TaxTop, tax.Value());
    return {};
}

// 0x5ff0c5..0x5ff2ed: el nombre de la mezcla, del tipo (0x7d2dc98) o de EDI (el tipo, 0 si es negativo, 7 con el 0xb).
Core::Expected<std::string, std::string> TheMixName(const GameHudContext& context, const NpcWindowState& npc,
                                                    std::int32_t shown)
{
    const auto text = [&context](std::size_t index) { return TextOf(context, index, "0x5ff2ab"); };
    if (npc.mixKind == CapeKind)
    {
        auto name = text(CapeNameText);
        if (!name.HasValue())
            return Core::Unexpected(name.Error());
        return UI::Sprintf(name.Value());
    }
    if (shown == 0x16 || shown == 0x17)
    {
        auto name = text(CreativeText);
        if (!name.HasValue())
            return Core::Unexpected(name.Error());
        return UI::Sprintf(name.Value(), shown - 10);
    }
    // 0x5ff134..0x5ff21f: "%s %s" (0x6c6350) con el nombre y el texto 1412.
    std::optional<std::size_t> pair;
    switch (shown)
    {
    case 0xf:
        pair = 1413;
        break;
    case 0x10:
        pair = 1414;
        break;
    case 0x11:
        pair = 1415;
        break;
    case 0x19:
        pair = 1921;
        break;
    case 0x1a:
        pair = 1922;
        break;
    case 0x1b:
        pair = 1923;
        break;
    default:
        break;
    }
    if (pair)
    {
        auto first = text(*pair);
        auto create = text(CreateText);
        if (!first.HasValue())
            return Core::Unexpected(first.Error());
        if (!create.HasValue())
            return Core::Unexpected(create.Error());
        return UI::Sprintf("%s %s", first.Value(), create.Value());
    }
    if (shown == CapeOfTheFenrirKind)
    {
        // 0x5ff229..0x5ff280: "%s %s %s" (0x6c648c) con el 1923, el de la capa y el 1412. Con otra capa el buffer
        // queda como estaba; 0x5fd4f0 no da el 0x1c sin la 3 o la 4.
        if (npc.mixCapeKind != 3 && npc.mixCapeKind != 4)
            return Core::Unexpected(
                std::format("0x5ff258: el 0x1c con la capa {} deja el buffer de antes", npc.mixCapeKind));
        auto first = text(1923);
        auto cape = text(CapeKindText + (npc.mixCapeKind == 3 ? 0u : 1u));
        auto create = text(CreateText);
        if (!first.HasValue())
            return Core::Unexpected(first.Error());
        if (!cape.HasValue())
            return Core::Unexpected(cape.Error());
        if (!create.HasValue())
            return Core::Unexpected(create.Error());
        return UI::Sprintf("%s %s %s", first.Value(), cape.Value(), create.Value());
    }
    // 0x5ff282..0x5ff2ab: sprintf del texto del tipo, sin argumentos.
    const auto index = static_cast<std::size_t>(shown <= 4 ? static_cast<std::int32_t>(FirstNameText) + shown
                                                           : static_cast<std::int32_t>(LaterNameText) + shown);
    auto name = text(index);
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    return UI::Sprintf(name.Value());
}

// 0x5ff2f2..0x5ff3e4: la chance, sprintf del texto 584 con el 591 y ESI. Con 0x7d2dc9d y el tipo 2 u 8, en celeste
// con "%s + %d%%" (0x6c9dc0) sobre el mismo buffer, y después el color de los renglones.
Core::Expected<void, std::string> DrawTheRate(GameHudContext& context, const NpcWindowState& npc, std::int32_t shown,
                                              std::int32_t rate, std::int32_t x, std::int32_t y)
{
    auto format = TextOf(context, RateText, "0x5ff336");
    auto mixing = TextOf(context, MixingText, "0x5ff336");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    if (!mixing.HasValue())
        return Core::Unexpected(mixing.Error());
    HudState& hud = context.hud;
    const bool bonus = npc.field7d2dc9d > 0 && (shown == 2 || shown == 8);
    hud.textColors = Render::Text::TextColors{.text = bonus ? MixableColor : LineColor, .background = LineBackground};
    auto line = Fits(UI::Sprintf(format.Value(), mixing.Value(), rate), RoomBeforeThePrice, "0x5ff336");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    if (bonus)
    {
        // 0x5ff35a: sprintf(buffer, "%s + %d%%", buffer, n), que copia el buffer sobre sí mismo.
        line = Fits(UI::Sprintf("%s + %d%%", line.Value(), std::int32_t{npc.field7d2dc9d}), RoomBeforeThePrice,
                    "0x5ff35a");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
    }
    DrawText(context, x + LineLeft, y + RateTop, line.Value());
    if (bonus)
        hud.textColors.text = LineColor;
    return {};
}

// 0x5ff3e7..0x5ff497: el precio con el impuesto (0x5b12f0) y el de 0x5b1130; el 0xf y el 0x10 con el texto 1636 y el
// renglón 590 abajo, los demás con el 1622.
Core::Expected<void, std::string> DrawThePrice(GameHudContext& context, const NpcWindowState& npc,
                                               std::string_view priceText, std::int32_t x, std::int32_t y)
{
    const std::string taxed = TaxedPriceText(npc.mixPrice, npc.chaosTaxRate);
    const bool potion = npc.mixKind == 0xf || npc.mixKind == 0x10;
    auto format = TextOf(context, potion ? PotionPriceText : PriceText, potion ? "0x5ff444" : "0x5ff423");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    auto line = Fits(UI::Sprintf(format.Value(), std::string_view(taxed), priceText), RoomBeforeThePrice, "0x5ff423");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    if (potion)
    {
        auto below = TextOf(context, WarningThirdText, "0x5ff46a");
        if (!below.HasValue())
            return Core::Unexpected(below.Error());
        DrawText(context, x + LineLeft, y + WarningThirdTop, below.Value());
    }
    DrawText(context, x + LineLeft, y + PriceTop, line.Value());
    return {};
}

// 0x5ff49a..0x5ff6ed: la joya del caos y los avisos del tipo (la tabla de 0x5ff8c0 con el tipo − 3).
Core::Expected<void, std::string> DrawTheWarnings(GameHudContext& context, const NpcWindowState& npc, std::int32_t x,
                                                  std::int32_t y)
{
    context.hud.textColors = Render::Text::TextColors{.text = JewelColor, .background = LineBackground};
    auto jewel = TextOf(context, JewelText, "0x5ff4bf");
    auto mixing = TextOf(context, MixingText, "0x5ff4bf");
    auto second = TextOf(context, JewelSecondText, "0x5ff50c");
    if (!jewel.HasValue())
        return Core::Unexpected(jewel.Error());
    if (!mixing.HasValue())
        return Core::Unexpected(mixing.Error());
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    auto line = Fits(UI::Sprintf(jewel.Value(), mixing.Value()), RoomOfTheFrame, "0x5ff4bf");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    DrawText(context, x + LineLeft, y + JewelTop, line.Value());
    DrawText(context, x + LineLeft, y + JewelSecondTop, second.Value());
    // Los textos seguidos desde `first`, uno por renglón desde el renglón `row` (0xf0, 0xff y 0x10e).
    const auto lines = [&](std::size_t first, std::size_t row, std::size_t count) -> Core::Expected<void, std::string>
    {
        constexpr std::array<std::int32_t, 3> Tops{WarningTop, WarningSecondTop, WarningThirdTop};
        for (std::size_t i = 0; i < count; ++i)
        {
            auto text = TextOf(context, first + i, "0x5ff555");
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            DrawText(context, x + LineLeft, y + Tops[row + i], text.Value());
        }
        return {};
    };
    switch (npc.mixKind)
    {
    case 3:
    case 4:
    case 0x16:
    case 0x17:
        return lines(UpgradeText, 0, 3);
    case 5:
    case 6:
        return lines(FailText, 0, 2);
    default:
        break;
    }
    // 0x5ff656: los demás, solo en el modo 1.
    if (npc.chaosMixMode != 1)
        return {};
    auto warning = TextOf(context, WarningText, "0x5ff674");
    if (!warning.HasValue())
        return Core::Unexpected(warning.Error());
    auto first = Fits(UI::Sprintf(warning.Value(), mixing.Value()), RoomOfTheFrame, "0x5ff674");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    DrawText(context, x + LineLeft, y + WarningTop, first.Value());
    return lines(WarningSecondText, 1, 2);
}

// 0x5ff6f0..0x5ff816: el botón de mezclar, apretado con el botón izquierdo abajo, y su ayuda (texto 591) con el mouse
// encima (x87: izquierda ≤ mouse < izquierda + 44, arriba ≤ mouse < arriba + 32).
Core::Expected<void, std::string> DrawTheMixButton(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const float left = F32(static_cast<double>(x) + MixButtonLeft);
    const float top = F32(static_cast<double>(y) + MixButtonTop);
    WindowDrawing::Quad(sprites, MixButtonSlot, left, top, MixButtonWidth, MixButtonHeight, MixButtonDu, MixButtonDv);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    const bool inside = static_cast<double>(left) <= mouseX &&
                        mouseX < static_cast<double>(left) + static_cast<double>(MixButtonWidth) &&
                        static_cast<double>(top) <= mouseY &&
                        mouseY < static_cast<double>(top) + static_cast<double>(MixButtonHeight);
    if (!inside)
        return {};
    if (context.leftDown)
        WindowDrawing::Quad(sprites, MixButtonDownSlot, left, top, MixButtonWidth, MixButtonHeight, MixButtonDu,
                            MixButtonDv);
    auto help = TextOf(context, MixingText, "0x5ff811");
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = HelpColors;
    UI::DrawHelp(sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, help.Value());
    // 0x554340 selecciona la normal, que queda en el DC, y deja el texto en blanco sobre un fondo transparente.
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheChaosBox(GameHudContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::ChaosBoxPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    NpcWindowState& npc = context.world.Runtime().npcWindows;
    hud.npcWindowX = WindowLeft;
    hud.npcWindowY = WindowTop;
    const std::int32_t x = hud.npcWindowX;
    const std::int32_t y = hud.npcWindowY;
    // 0x5fec2c: 0x5ee220(0x104, 0, 0) y la grilla (0x5e3660).
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    DrawTheGridBoxes(sprites, F32(static_cast<double>(x) + GridLeft), F32(static_cast<double>(y) + GridTop),
                     npc.chaosBox, GridColumns, GridRows);
    sprites.SetColor(White);
    WindowDrawing::SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    if (auto title = DrawTheTitle(context, npc, x, y); !title.HasValue())
        return title;
    // 0x5fed98..0x5feda6: el precio en 0, y con la mezcla pedida (2 o más) nada más.
    npc.mixPrice = 0;
    if (npc.chaosMixState >= 2)
        return {};
    auto quote = MixQuoteOf(npc);
    if (!quote.HasValue())
        return Core::Unexpected(std::move(quote).Error());
    npc.mixPrice = quote.Value().price;
    // 0x5ff0c5..0x5ff107: EDI y el color del nombre.
    std::int32_t shown = npc.mixKind < 0 ? 0 : npc.mixKind;
    std::uint32_t nameColor = shown != 0 ? MixableColor : NothingColor;
    if (npc.mixKind == SecondWingsKind)
    {
        shown = WingsKind;
        nameColor = MixableColor;
    }
    hud.textColors = Render::Text::TextColors{.text = nameColor, .background = LineBackground};
    auto name = TheMixName(context, npc, shown);
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    auto fitted = Fits(std::move(name).Value(), RoomBeforeThePrice, "0x5ff2ab");
    if (!fitted.HasValue())
        return Core::Unexpected(std::move(fitted).Error());
    DrawText(context, x + LineLeft, y + NameTop, fitted.Value());
    if (auto rate = DrawTheRate(context, npc, shown, quote.Value().rate, x, y); !rate.HasValue())
        return rate;
    if (auto price = DrawThePrice(context, npc, quote.Value().priceText, x, y); !price.HasValue())
        return price;
    if (auto warnings = DrawTheWarnings(context, npc, x, y); !warnings.HasValue())
        return warnings;
    return DrawTheMixButton(context, x, y);
}
} // namespace Client::Scenes
