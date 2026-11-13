#include "Scenes/NpcWindowDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
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
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::DrawTitle;
using WindowDrawing::Quad;
using WindowDrawing::SelectFont;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t ShopTitleText = 230;
constexpr std::size_t ShopTaxText = 1623;
constexpr std::size_t BuyHelpText = 231;
constexpr std::size_t SellHelpText = 232;
constexpr std::size_t RepairHelpText = 233;
constexpr std::size_t RepairAllHelpText = 237;
constexpr std::size_t RepairAllPriceText = 239;

// 0x5f474c..0x5f4756: la esquina de la tienda.
constexpr std::int32_t ShopLeft = 0x104;
constexpr std::int32_t ShopTop = 0;
// La grilla de 0x78a7070: 8 × 15 desde la esquina más 15 (0x6b87bc) y 50 (0x6b86b0).
constexpr std::size_t GridColumns = 8;
constexpr std::size_t GridRows = 15;
constexpr double GridLeft = 15.0;
constexpr double GridTop = 50.0;
// 0x5f47d7 y 0x5f4804, 0x5f485c: el título y el impuesto en x + 0x23, y + 8 y y + 0x14.
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 8;
constexpr std::int32_t TaxTop = 0x14;
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
// Los botones del pie: 24 × 24 (0x6b8800) en y + 365 (0x6ba67c) y x + 25, 55, 85 y 115 (0x6b8808, 0x6b8990,
// 0x6ba104 y 0x6ba630), con du = dv = 0,75; la ayuda 12 más arriba.
constexpr double FootTop = 365.0;
constexpr double BuyLeft = 25.0;
constexpr double SellLeft = 55.0;
constexpr double RepairLeft = 85.0;
constexpr double RepairAllLeft = 115.0;
constexpr float ButtonSize = 24.0f;
constexpr float ButtonUv = 0.75f;
constexpr std::int32_t HelpAbove = 12;
// Interface\... de 0x61ae60: comprar (0x11a apretado, 0x11b suelto), vender (0x11c y 0x11d) y reparar (0x11e y 0x11f).
constexpr std::uint16_t BuyPressedSlot = 0x11a;
constexpr std::uint16_t BuySlot = 0x11b;
constexpr std::uint16_t SellPressedSlot = 0x11c;
constexpr std::uint16_t SellSlot = 0x11d;
constexpr std::uint16_t RepairSlot = 0x11e;
constexpr std::uint16_t RepairPressedSlot = 0x11f;
// 0x5f4b33..0x5f4b3d: glColor3f(0,5; 0,7; 1) del botón de reparar todo.
constexpr Render::Color RepairAllColor{0.5f, 0.7f, 1.0f, 1.0f};
// 0x5f4c6a..0x5f4cad: el fondo del precio, 0x10f de 113 × 18 en (x − 60, y + 30) del botón de reparar todo (0x6b86cc
// y 0x6b8858), con du = 0,8828125 y dv = 0,5625.
constexpr std::uint16_t PriceBackSlot = 0x10f;
constexpr double PriceBackLeft = 60.0;
constexpr double PriceBackTop = 30.0;
constexpr float PriceBackWidth = 113.0f;
constexpr float PriceBackHeight = 18.0f;
constexpr float PriceBackDu = 0.8828125f;
constexpr float PriceBackDv = 0.5625f;
// 0x5f4cd2..0x5f4ce6 y 0x5f4d9e..0x5f4e0e: el precio, con el texto en x − 0x23 y el monto en x + 10, 3 más abajo.
constexpr Render::Text::TextColors PriceColors{.text = 0xff96dcffu, .background = 0xff282828u};
constexpr std::int32_t PriceLabelLeft = 0x23;
constexpr std::int32_t PriceAmountLeft = 10;
constexpr std::int32_t PriceTextDown = 3;

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// fild del entero de la esquina, fadd del float de la tabla y fstp.
float Offset(std::int32_t corner, double offset)
{
    return F32(static_cast<double>(corner) + offset);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// fild del mouse (0x7d3682c y 0x7d36828) contra el botón en float: izquierda ≤ x < izquierda + 24, con la suma en la
// x87.
bool MouseOnTheButton(const GameHudContext& context, float left, float top)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(ButtonSize) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(ButtonSize);
}

// Con el mouse encima, SelectObject con la fuente normal y la ayuda (0x554340) en (_ftol(x), _ftol(y) − 12), que deja
// el texto en blanco sobre un fondo transparente. Los colores de antes no cambian nada: 0x554340 pone los suyos.
Core::Expected<void, std::string> DrawTheButtonHelp(GameHudContext& context, float left, float top, std::size_t help,
                                                    std::string_view where)
{
    SelectFont(context, FontKind::Normal);
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    UI::DrawHelp(context.sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, text.Value());
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    return {};
}

// Un botón de 24 × 24 del pie y su ayuda con el mouse encima.
Core::Expected<void, std::string> DrawTheFootButton(GameHudContext& context, std::uint16_t slot, float left, float top,
                                                    std::size_t help, std::string_view where)
{
    Quad(context.sprites, slot, left, top, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
    if (!MouseOnTheButton(context, left, top))
        return {};
    return DrawTheButtonHelp(context, left, top, help, where);
}
} // namespace

Core::Expected<void, std::string> DrawTheNpcShop(GameHudContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::NpcShopPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    WorldRuntime& runtime = context.world.Runtime();
    const UI::PanelState& state = runtime.panels;
    hud.npcWindowX = ShopLeft;
    hud.npcWindowY = ShopTop;
    const std::int32_t x = hud.npcWindowX;
    const std::int32_t y = hud.npcWindowY;
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    DrawTheGridBoxes(sprites, Offset(x, GridLeft), Offset(y, GridTop), runtime.npcWindows.grid, GridColumns, GridRows);
    sprites.SetColor(White);

    // 0x5f47b0..0x5f4865: el título y el impuesto (sprintf del texto 1623 con 0x78aba30), en cajas de 120 · W / 640.
    SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    auto title = TextOf(context, ShopTitleText, "0x5f4807");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    auto taxFormat = TextOf(context, ShopTaxText, "0x5f4823");
    if (!taxFormat.HasValue())
        return Core::Unexpected(taxFormat.Error());
    const std::string tax = UI::Sprintf(taxFormat.Value(), runtime.npcWindows.taxRate);
    DrawTitle(context, x + TitleLeft, y + TaxTop, tax);

    // 0x5f486a..0x5f4a37: comprar y vender, apretados con el modo de la tienda (0x78abb27) en 0 y en 1.
    const float footTop = Offset(y, FootTop);
    if (auto buy = DrawTheFootButton(context, state.repairCursor == 0 ? BuySlot : BuyPressedSlot, Offset(x, BuyLeft),
                                     footTop, BuyHelpText, "0x5f4930");
        !buy.HasValue())
        return buy;
    if (auto sell = DrawTheFootButton(context, state.repairCursor == 1 ? SellSlot : SellPressedSlot,
                                      Offset(x, SellLeft), footTop, SellHelpText, "0x5f4a16");
        !sell.HasValue())
        return sell;
    // 0x5f4a39..0x5f4a40: sin 0x78abb09 (la tienda no repara), nada más.
    if (state.field78abb09 == 0)
        return {};
    if (auto repair = DrawTheFootButton(context, state.repairMode != 0 ? RepairPressedSlot : RepairSlot,
                                        Offset(x, RepairLeft), footTop, RepairHelpText, "0x5f4b09");
        !repair.HasValue())
        return repair;

    // 0x5f4b2c..0x5f4c57: reparar todo, en celeste; con el mouse encima y el clic pendiente (0x7d366d4), lo gasta y
    // se dibuja apretado.
    const float repairAllLeft = Offset(x, RepairAllLeft);
    sprites.SetColor(RepairAllColor);
    Quad(sprites, RepairSlot, repairAllLeft, footTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
    if (MouseOnTheButton(context, repairAllLeft, footTop))
    {
        bool& click = runtime.movement.pendingClick;
        if (click)
        {
            if (!context.projecting)
                click = false;
            Quad(sprites, RepairPressedSlot, repairAllLeft, footTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
        }
        if (auto help = DrawTheButtonHelp(context, repairAllLeft, footTop, RepairAllHelpText, "0x5f4c36");
            !help.HasValue())
            return help;
    }

    // 0x5f4c59..0x5f4e13: el precio de reparar todo (0x78ababc, sin signo) con comas, en el color de su monto.
    sprites.SetColor(White);
    const float priceLeft = F32(static_cast<double>(repairAllLeft) - PriceBackLeft);
    const float priceTop = F32(static_cast<double>(footTop) + PriceBackTop);
    Quad(sprites, PriceBackSlot, priceLeft, priceTop, PriceBackWidth, PriceBackHeight, PriceBackDu, PriceBackDv);
    const auto price = static_cast<std::uint32_t>(runtime.npcWindows.repairAllPrice);
    const std::string amount = FormatZen(price);
    hud.textColors = PriceColors;
    SelectFont(context, FontKind::Bold);
    const std::int32_t textTop = Truncate(static_cast<double>(priceTop)) + PriceTextDown;
    const std::int32_t textLeft = Truncate(static_cast<double>(priceLeft));
    auto label = TextOf(context, RepairAllPriceText, "0x5f4dae");
    if (!label.HasValue())
        return Core::Unexpected(label.Error());
    DrawText(context, textLeft - PriceLabelLeft, textTop, label.Value());
    hud.textColors.text = ZenColor(price);
    DrawText(context, textLeft + PriceAmountLeft, textTop, amount);
    return {};
}
} // namespace Client::Scenes
