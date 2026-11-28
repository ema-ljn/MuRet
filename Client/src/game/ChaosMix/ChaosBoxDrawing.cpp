#include "ChaosMix/ChaosBoxDrawing.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "ChaosMix/ChaosMix.h"
#include "Hud/GameHud.h"
#include "Hud/GameInventoryHud.h"
#include "Hud/GameWindowDrawing.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Events/EventGlobals.h"
#include "Ui/GameUiState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::ChaosMix
{
namespace
{
using Hud::WindowDrawing::DrawText;
using Hud::WindowDrawing::DrawTitle;
using Render::Text::FontKind;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5fec15..0x5fec2c: the NPC window's corner.
constexpr std::int32_t WindowLeft = 0x104;
constexpr std::int32_t WindowTop = 0;
// 0x5fec31..0x5fec5d: the 8 × 4 grid at the corner plus 15 (0x6b87bc) and 110 (0x6ba184).
constexpr double GridLeft = 15.0;
constexpr double GridTop = 110.0;
constexpr std::size_t GridColumns = 8;
constexpr std::size_t GridRows = 4;
// The lines, from the corner (0x554180).
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
// 0x5ff6f0..0x5ff739: Interface\... 0x124 (and 0x125 pressed) of 44 × 32 (0x6ba730 and 0x6b883c) at the corner plus 75
// (0x6ba62c) and 300 (0x6b8900), with du = 0.6875 and dv = 1; the help 13 higher.
constexpr std::uint16_t MixButtonSlot = 0x124;
constexpr std::uint16_t MixButtonDownSlot = 0x125;
constexpr double MixButtonLeft = 75.0;
constexpr double MixButtonTop = 300.0;
constexpr float MixButtonWidth = 44.0f;
constexpr float MixButtonHeight = 32.0f;
constexpr float MixButtonDu = 0.6875f;
constexpr float MixButtonDv = 1.0f;
constexpr std::int32_t HelpAbove = 0xd;

// The Text.bmd texts (0x76bd1dc + 300 · n).
constexpr std::size_t PlainTitleText = 583;    // 0x76e7d10, mode 2 onward
constexpr std::size_t RegularTitleText = 735;  // 0x76f2f30, mode 0
constexpr std::size_t WeaponTitleText = 736;   // 0x76f305c, mode 1
constexpr std::size_t TaxText = 1623;          // 0x7733fd0
constexpr std::size_t RateText = 584;          // 0x76e7e3c
constexpr std::size_t MixingText = 591;        // 0x76e8670
constexpr std::size_t PriceText = 1622;        // 0x7733ea4
constexpr std::size_t PotionPriceText = 1636;  // 0x7734f0c, the 0xf and the 0x10
constexpr std::size_t JewelText = 586;         // 0x76e8094
constexpr std::size_t JewelSecondText = 587;   // 0x76e81c0
constexpr std::size_t WarningText = 588;       // 0x76e82ec, in mode 1
constexpr std::size_t WarningSecondText = 589; // 0x76e8418
constexpr std::size_t WarningThirdText = 590;  // 0x76e8544, also the potions' line
constexpr std::size_t UpgradeText = 606;       // 0x76e9804, 607 and 608: the 3, the 4, the 0x16 and the 0x17
constexpr std::size_t FailText = 610;          // 0x76e9cb4 and 611: the 5 and the 6
constexpr std::size_t CapeNameText = 1172;     // 0x7712f4c, the 0x18
constexpr std::size_t CreativeText = 664;      // 0x76edbfc, the 0x16 and the 0x17 with the type − 10
constexpr std::size_t CreateText = 1412;       // 0x772488c
constexpr std::size_t CapeKindText = 1863;     // 0x7745910 with cape 3 and 0x7745a3c (1864) with cape 4
constexpr std::size_t FirstNameText = 601;     // 0x76bd1dc + 300 · (type + 0x259) up to 4
constexpr std::size_t LaterNameText = 607;     // and + 0x25f from 5

// [EBP − 0xac]: the lines' buffer. Up to 0x5b1130's price text ([EBP − 0x28]), used in the price line, there are 0x84
// bytes; after the price, up to the saved stack, 0xac (*inferred* from the frame).
constexpr std::size_t RoomBeforeThePrice = 0x84;
constexpr std::size_t RoomOfTheFrame = 0xac;

// Type 0xb shows the wings' name and color (0x5ff0d4..0x5ff0db).
constexpr std::int32_t SecondWingsKind = 0xb;
constexpr std::int32_t WingsKind = 7;
constexpr std::int32_t CapeKind = 0x18;
constexpr std::int32_t CapeOfTheFenrirKind = 0x1c;

// 0x6c8e5c and 0x6c8e64 of each line.
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr std::uint32_t LineBackground = 0x80282828u;
constexpr std::uint32_t MixableColor = 0xff30ffffu;
constexpr std::uint32_t NothingColor = 0xff3030ffu;
constexpr std::uint32_t LineColor = 0xffffe6d2u;
constexpr std::uint32_t JewelColor = 0xff1432ffu;
constexpr Render::Text::TextColors HelpColors{.text = 0xffffffffu, .background = LineBackground};

using Math::F32;
using Math::Truncate;

Core::Expected<std::string_view, std::string> TextOf(const Hud::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// What sprintf leaves in [EBP − 0xac], if it fits in `room` bytes with its zero.
Core::Expected<std::string, std::string> Fits(std::string text, std::size_t room, std::string_view where)
{
    if (text.size() >= room)
        return Core::Unexpected(std::format("{}: el sprintf escribe {} bytes en [EBP − 0xac] y pisa la pila ({} de "
                                            "lugar)",
                                            where, text.size() + 1, room));
    return text;
}

// 0x5fec8d..0x5fed95: the mode's title (0x7d3a114) in the box of 120 · W / 640; modes 0 and 1 with the tax below
// (sprintf of text 1623 with 0x78aba34).
Core::Expected<void, std::string> DrawTheTitle(Hud::GameHudContext& context, const Npc::NpcWindowState& npc,
                                               std::int32_t x, std::int32_t y)
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
    auto tax = Fits(Crt::Sprintf(format.Value(), npc.chaosTaxRate), RoomOfTheFrame, "0x5fed54");
    if (!tax.HasValue())
        return Core::Unexpected(tax.Error());
    DrawTitle(context, x + TitleLeft, y + TaxTop, tax.Value());
    return {};
}

// 0x5ff0c5..0x5ff2ed: the mix's name, from the type (0x7d2dc98) or from EDI (the type, 0 if negative, 7 with the 0xb).
Core::Expected<std::string, std::string> TheMixName(const Hud::GameHudContext& context, const Npc::NpcWindowState& npc,
                                                    std::int32_t shown)
{
    const auto text = [&context](std::size_t index) { return TextOf(context, index, "0x5ff2ab"); };
    if (npc.mixKind == CapeKind)
    {
        auto name = text(CapeNameText);
        if (!name.HasValue())
            return Core::Unexpected(name.Error());
        return Crt::Sprintf(name.Value());
    }
    if (shown == 0x16 || shown == 0x17)
    {
        auto name = text(CreativeText);
        if (!name.HasValue())
            return Core::Unexpected(name.Error());
        return Crt::Sprintf(name.Value(), shown - 10);
    }
    // 0x5ff134..0x5ff21f: "%s %s" (0x6c6350) with the name and text 1412.
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
        return Crt::Sprintf("%s %s", first.Value(), create.Value());
    }
    if (shown == CapeOfTheFenrirKind)
    {
        // 0x5ff229..0x5ff280: "%s %s %s" (0x6c648c) with 1923, the cape's and 1412. With another cape the buffer stays
        // as it was; 0x5fd4f0 does not give the 0x1c without cape 3 or 4.
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
        return Crt::Sprintf("%s %s %s", first.Value(), cape.Value(), create.Value());
    }
    // 0x5ff282..0x5ff2ab: sprintf of the type's text, without arguments.
    const auto index = static_cast<std::size_t>(shown <= 4 ? static_cast<std::int32_t>(FirstNameText) + shown
                                                           : static_cast<std::int32_t>(LaterNameText) + shown);
    auto name = text(index);
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    return Crt::Sprintf(name.Value());
}

// 0x5ff2f2..0x5ff3e4: the chance, sprintf of text 584 with 591 and ESI. With 0x7d2dc9d and type 2 or 8, in light blue
// with "%s + %d%%" (0x6c9dc0) over the same buffer, and then the lines' color.
Core::Expected<void, std::string> DrawTheRate(Hud::GameHudContext& context, const Npc::NpcWindowState& npc,
                                              std::int32_t shown, std::int32_t rate, std::int32_t x, std::int32_t y)
{
    auto format = TextOf(context, RateText, "0x5ff336");
    auto mixing = TextOf(context, MixingText, "0x5ff336");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    if (!mixing.HasValue())
        return Core::Unexpected(mixing.Error());
    Kernel::HudState& hud = context.hud;
    const bool bonus = npc.field7d2dc9d > 0 && (shown == 2 || shown == 8);
    hud.textColors = Render::Text::TextColors{.text = bonus ? MixableColor : LineColor, .background = LineBackground};
    auto line = Fits(Crt::Sprintf(format.Value(), mixing.Value(), rate), RoomBeforeThePrice, "0x5ff336");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    if (bonus)
    {
        // 0x5ff35a: sprintf(buffer, "%s + %d%%", buffer, n), which copies the buffer over itself.
        line = Fits(Crt::Sprintf("%s + %d%%", line.Value(), std::int32_t{npc.field7d2dc9d}), RoomBeforeThePrice,
                    "0x5ff35a");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
    }
    DrawText(context, x + LineLeft, y + RateTop, line.Value());
    if (bonus)
        hud.textColors.text = LineColor;
    return {};
}

// 0x5ff3e7..0x5ff497: the price with tax (0x5b12f0) and that of 0x5b1130; the 0xf and the 0x10 with text 1636 and line
// 590 below, the others with 1622.
Core::Expected<void, std::string> DrawThePrice(Hud::GameHudContext& context, const Npc::NpcWindowState& npc,
                                               std::string_view priceText, std::int32_t x, std::int32_t y)
{
    const std::string taxed = TaxedPriceText(npc.mixPrice, npc.chaosTaxRate);
    const bool potion = npc.mixKind == 0xf || npc.mixKind == 0x10;
    auto format = TextOf(context, potion ? PotionPriceText : PriceText, potion ? "0x5ff444" : "0x5ff423");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    auto line = Fits(Crt::Sprintf(format.Value(), std::string_view(taxed), priceText), RoomBeforeThePrice, "0x5ff423");
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

// 0x5ff49a..0x5ff6ed: the chaos jewel and the type's warnings (the table at 0x5ff8c0 with the type − 3).
Core::Expected<void, std::string> DrawTheWarnings(Hud::GameHudContext& context, const Npc::NpcWindowState& npc,
                                                  std::int32_t x, std::int32_t y)
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
    auto line = Fits(Crt::Sprintf(jewel.Value(), mixing.Value()), RoomOfTheFrame, "0x5ff4bf");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    DrawText(context, x + LineLeft, y + JewelTop, line.Value());
    DrawText(context, x + LineLeft, y + JewelSecondTop, second.Value());
    // The consecutive texts from `first`, one per line from line `row` (0xf0, 0xff and 0x10e).
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
    // 0x5ff656: the others, only in mode 1.
    if (npc.chaosMixMode != 1)
        return {};
    auto warning = TextOf(context, WarningText, "0x5ff674");
    if (!warning.HasValue())
        return Core::Unexpected(warning.Error());
    auto first = Fits(Crt::Sprintf(warning.Value(), mixing.Value()), RoomOfTheFrame, "0x5ff674");
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    DrawText(context, x + LineLeft, y + WarningTop, first.Value());
    return lines(WarningSecondText, 1, 2);
}

// 0x5ff6f0..0x5ff816: the mix button, pressed with the left button down, and its help (text 591) with the mouse over
// it (x87: left ≤ mouse < left + 44, top ≤ mouse < top + 32).
Core::Expected<void, std::string> DrawTheMixButton(Hud::GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const float left = F32(static_cast<double>(x) + MixButtonLeft);
    const float top = F32(static_cast<double>(y) + MixButtonTop);
    Hud::WindowDrawing::Quad(sprites, MixButtonSlot, left, top, MixButtonWidth, MixButtonHeight, MixButtonDu,
                             MixButtonDv);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    const bool inside = static_cast<double>(left) <= mouseX &&
                        mouseX < static_cast<double>(left) + static_cast<double>(MixButtonWidth) &&
                        static_cast<double>(top) <= mouseY &&
                        mouseY < static_cast<double>(top) + static_cast<double>(MixButtonHeight);
    if (!inside)
        return {};
    if (context.leftDown)
        Hud::WindowDrawing::Quad(sprites, MixButtonDownSlot, left, top, MixButtonWidth, MixButtonHeight, MixButtonDu,
                                 MixButtonDv);
    auto help = TextOf(context, MixingText, "0x5ff811");
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    Hud::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = HelpColors;
    Ui::DrawHelp(sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, help.Value());
    // 0x554340 selects the normal font, which stays in the DC, and leaves the text white on a transparent background.
    Hud::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheChaosBox(Hud::GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::ChaosBoxPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Npc::NpcWindowState& npc = runtime.npcWindows;
    runtime.ui.npcWindowX = WindowLeft;
    runtime.ui.npcWindowY = WindowTop;
    const std::int32_t x = runtime.ui.npcWindowX;
    const std::int32_t y = runtime.ui.npcWindowY;
    // 0x5fec2c: 0x5ee220(0x104, 0, 0) and the grid (0x5e3660).
    Hud::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Hud::DrawTheGridBoxes(sprites, F32(static_cast<double>(x) + GridLeft), F32(static_cast<double>(y) + GridTop),
                          npc.chaosBox, GridColumns, GridRows);
    sprites.SetColor(White);
    Hud::WindowDrawing::SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    if (auto title = DrawTheTitle(context, npc, x, y); !title.HasValue())
        return title;
    // 0x5fed98..0x5feda6: the price at 0, and with the mix requested (2 or more) nothing more.
    npc.mixPrice = 0;
    if (npc.chaosMixState >= 2)
        return {};
    const auto& collapse = runtime.events.objectCollapse;
    auto quote = MixQuoteOf(npc, {.glowDetail = runtime.ui.glowDetail,
                                  .map = runtime.map,
                                  .collapseType = collapse.type,
                                  .collapseMap = collapse.map,
                                  .collapseCounter = collapse.counter,
                                  .collapseDelay = collapse.delay});
    if (!quote.HasValue())
        return Core::Unexpected(std::move(quote).Error());
    npc.mixPrice = quote.Value().price;
    // 0x5ff0c5..0x5ff107: EDI and the name's color.
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
} // namespace Client::ChaosMix
