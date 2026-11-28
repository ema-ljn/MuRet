#include "Npc/NpcWindowDrawing.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Ui/GameHudContext.h"
#include "Hud/GameInventoryHud.h"
#include "Hud/GameWindowDrawing.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/HudPrimitives.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Npc
{
namespace
{
using Ui::WindowDrawing::DrawText;
using Ui::WindowDrawing::DrawTitle;
using Ui::WindowDrawing::Quad;
using Ui::WindowDrawing::SelectFont;
using Render::Text::FontKind;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// The Text.bmd texts (the address − 0x76bd1dc, over 300).
constexpr std::size_t ShopTitleText = 230;
constexpr std::size_t ShopTaxText = 1623;
constexpr std::size_t BuyHelpText = 231;
constexpr std::size_t SellHelpText = 232;
constexpr std::size_t RepairHelpText = 233;
constexpr std::size_t RepairAllHelpText = 237;
constexpr std::size_t RepairAllPriceText = 239;

// 0x5f474c..0x5f4756: the shop's corner.
constexpr std::int32_t ShopLeft = 0x104;
constexpr std::int32_t ShopTop = 0;
// The grid of 0x78a7070: 8 × 15 from the corner plus 15 (0x6b87bc) and 50 (0x6b86b0).
constexpr std::size_t GridColumns = 8;
constexpr std::size_t GridRows = 15;
constexpr double GridLeft = 15.0;
constexpr double GridTop = 50.0;
// 0x5f47d7 and 0x5f4804, 0x5f485c: the title and the tax at x + 0x23, y + 8 and y + 0x14.
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 8;
constexpr std::int32_t TaxTop = 0x14;
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
// The foot buttons: 24 × 24 (0x6b8800) at y + 365 (0x6ba67c) and x + 25, 55, 85 and 115 (0x6b8808, 0x6b8990,
// 0x6ba104 and 0x6ba630), with du = dv = 0.75; the help 12 higher.
constexpr double FootTop = 365.0;
constexpr double BuyLeft = 25.0;
constexpr double SellLeft = 55.0;
constexpr double RepairLeft = 85.0;
constexpr double RepairAllLeft = 115.0;
constexpr float ButtonSize = 24.0f;
constexpr float ButtonUv = 0.75f;
constexpr std::int32_t HelpAbove = 12;
// Interface\... of 0x61ae60: buy (0x11a pressed, 0x11b released), sell (0x11c and 0x11d) and repair (0x11e and 0x11f).
constexpr std::uint16_t BuyPressedSlot = 0x11a;
constexpr std::uint16_t BuySlot = 0x11b;
constexpr std::uint16_t SellPressedSlot = 0x11c;
constexpr std::uint16_t SellSlot = 0x11d;
constexpr std::uint16_t RepairSlot = 0x11e;
constexpr std::uint16_t RepairPressedSlot = 0x11f;
// 0x5f4b33..0x5f4b3d: glColor3f(0.5; 0.7; 1) of the repair-all button.
constexpr Render::Color RepairAllColor{0.5f, 0.7f, 1.0f, 1.0f};
// 0x5f4c6a..0x5f4cad: the price background, 0x10f of 113 × 18 at (x − 60, y + 30) of the repair-all button (0x6b86cc
// and 0x6b8858), with du = 0.8828125 and dv = 0.5625.
constexpr std::uint16_t PriceBackSlot = 0x10f;
constexpr double PriceBackLeft = 60.0;
constexpr double PriceBackTop = 30.0;
constexpr float PriceBackWidth = 113.0f;
constexpr float PriceBackHeight = 18.0f;
constexpr float PriceBackDu = 0.8828125f;
constexpr float PriceBackDv = 0.5625f;
// 0x5f4cd2..0x5f4ce6 and 0x5f4d9e..0x5f4e0e: the price, with the text at x − 0x23 and the amount at x + 10, 3 lower.
constexpr Render::Text::TextColors PriceColors{.text = 0xff96dcffu, .background = 0xff282828u};
constexpr std::int32_t PriceLabelLeft = 0x23;
constexpr std::int32_t PriceAmountLeft = 10;
constexpr std::int32_t PriceTextDown = 3;

// The warehouse (0x5f4e30): the title "%s (%s)" (0x6c9d48) with text 234 and 240, or 241 when locked.
constexpr std::size_t WarehouseTitleText = 234;
constexpr std::size_t UnlockedText = 240;
constexpr std::size_t LockedText = 241;
constexpr std::int32_t WarehouseTitleTop = 0xc;
// 0x5f4eec..0x5f4ef7: glColor3f(1, 1, 0) of the title when locked.
constexpr Render::Color LockedTitleColor{1.0f, 1.0f, 0.0f, 1.0f};
// 0x5f4f84..0x5f4fca: the zen background, 0x10f at (x + 50, y + 352.7f) (0x6b86b0 and 0x6ba688).
constexpr double ZenBackLeft = 50.0;
constexpr float ZenBackTop = 352.7f;
// 0x5f52a7..0x5f5322: text 224 at x − 0x1e and the zen at x + 10, 3 lower.
constexpr std::size_t ZenText = 224;
constexpr std::int32_t ZenLabelLeft = 0x1e;
// 0x5f5327..0x5f5394: text 266 at (_ftol(x + 50), _ftol(y + 374)) (0x6b86b0 and 0x6ba684), in red.
constexpr std::size_t FeeText = 266;
constexpr double FeeLeft = 50.0;
constexpr double FeeTop = 374.0;
constexpr Render::Text::TextColors FeeLabelColors{.text = 0xff0000ffu, .background = 0xff282828u};
// 0x5f546d and 0x5f56cb: the level² times the float at 0x6ba680 and the cost, 0x46 right of the text. The float is
// 0x3d23d70b (0.0400000028…), one more than C++'s 0.04f (0x3d23d70a): with C++'s, 30² would give 35 and not 36.
constexpr float FeePerSquaredLevel = std::bit_cast<float>(0x3d23d70bu);
constexpr std::int32_t FeeAmountLeft = 0x46;
// 0x5f56d5..0x5f5741: the three buttons at x + 0x1a, 0x4c and 0x7e, y + 390 (0x6ba674): deposit and withdraw
// (Money_01 0x110, Money_02 0x111 pressed) and the lock (lock_01 0x126 and lock_02 0x127; lock_03 0x128 and lock_04
// 0x129 locked), with helps 235, 236 and 242, 13 higher.
constexpr std::array<std::int32_t, 3> WarehouseButtonLefts{0x1a, 0x4c, 0x7e};
constexpr double WarehouseButtonTop = 390.0;
constexpr std::array<std::uint16_t, 4> WarehouseButtonSlots{0x110, 0x110, 0x126, 0x128};
constexpr std::array<std::uint16_t, 4> WarehousePressedSlots{0x111, 0x111, 0x127, 0x129};
constexpr std::array<std::size_t, 3> WarehouseHelpTexts{235, 236, 242};
constexpr std::size_t LockButton = 2;
constexpr std::size_t LockedLockButton = 3;
constexpr std::int32_t WarehouseHelpAbove = 13;
constexpr Render::Text::TextColors WarehouseHelpColors{.text = 0xffffffffu, .background = 0xff000000u};

using Math::F32;
using Math::Truncate;

// fild of the corner's integer, fadd of the table's float and fstp.
float Offset(std::int32_t corner, double offset)
{
    return F32(static_cast<double>(corner) + offset);
}

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Models::LogicalAccess OwnAccess(Ui::GameHudContext& context)
{
    return Models::LogicalAccess(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

// 0x5f5457..0x5f55fc: the warehouse fee, _ftol(level² · 0.04f) plus 2 · level when locked; below 1 is 1, from 1000
// it drops to the hundred and from 100 to the ten (C division).
std::int32_t WarehouseFee(std::uint16_t level, bool locked)
{
    // 0x5f5464: IMUL keeps the low 32 bits; FILD reads them as signed.
    const auto squared = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(level) * level);
    std::int32_t fee = Truncate(static_cast<double>(squared) * static_cast<double>(FeePerSquaredLevel));
    fee += locked ? 2 * static_cast<std::int32_t>(level) : 0;
    if (fee < 1)
        return 1;
    if (fee >= 1000)
        return fee / 100 * 100;
    if (fee >= 100)
        return fee / 10 * 10;
    return fee;
}

// fild of the mouse (0x7d3682c and 0x7d36828) against the button in float: left ≤ x < left + 24, with the sum on the
// x87.
bool MouseOnTheButton(const Ui::GameHudContext& context, float left, float top)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(ButtonSize) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(ButtonSize);
}

// With the mouse over it, SelectObject with the normal font and the help (0x554340) at (_ftol(x), _ftol(y) − 12),
// which leaves the text white on a transparent background. The previous colors change nothing: 0x554340 sets its own.
Core::Expected<void, std::string> DrawTheButtonHelp(Ui::GameHudContext& context, float left, float top,
                                                    std::size_t help, std::string_view where)
{
    SelectFont(context, FontKind::Normal);
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    Ui::DrawHelp(context.sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, text.Value());
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    return {};
}

Core::Expected<void, std::string> DrawTheFootButton(Ui::GameHudContext& context, std::uint16_t slot, float left,
                                                    float top, std::size_t help, std::string_view where)
{
    Quad(context.sprites, slot, left, top, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
    if (!MouseOnTheButton(context, left, top))
        return {};
    return DrawTheButtonHelp(context, left, top, help, where);
}
Core::Expected<void, std::string> DrawPanel2000Instructions(Ui::GameHudContext& context)
{
    constexpr std::int32_t Center = 545;
    constexpr std::size_t FirstInstruction = 462;
    constexpr std::size_t InstructionCount = 8;
    for (std::size_t line = 0; line < InstructionCount; ++line)
    {
        auto text = TextOf(context, FirstInstruction + line, "0x5f72d3");
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        Ui::WindowDrawing::DrawCenteredText(context, Center, 50 + 20 * static_cast<std::int32_t>(line), text.Value());
    }
    return {};
}

Core::Expected<void, std::string> DrawPanel2000Buttons(Ui::GameHudContext& context, bool accepted)
{
    auto& sprites = context.sprites;
    auto& hud = context.hud;
    SelectFont(context, FontKind::Normal);
    hud.textColors.text = 0xffd2e6ffu;
    for (const bool acceptButton : {false, true})
    {
        const std::int32_t top = acceptButton ? 320 : 350;
        sprites.SetColor(acceptButton && !accepted ? Render::Color{0.5f, 0.5f, 0.5f, 1.0f} : White);
        Quad(sprites, 0xf0, 485, static_cast<float>(top), 120, 24, 0.83203125f, 1.0f);
        auto text = TextOf(context, acceptButton ? 228 : 229, "0x5f743a");
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        Ui::WindowDrawing::DrawCenteredText(context, 545, top + 5, text.Value());
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheNpcShop(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::NpcShopPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::PanelState& state = runtime.panels;
    runtime.ui.npcWindowX = ShopLeft;
    runtime.ui.npcWindowY = ShopTop;
    const std::int32_t x = runtime.ui.npcWindowX;
    const std::int32_t y = runtime.ui.npcWindowY;
    Ui::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Hud::DrawTheGridBoxes(sprites, Offset(x, GridLeft), Offset(y, GridTop), runtime.npcWindows.grid, GridColumns,
                          GridRows);
    sprites.SetColor(White);

    // 0x5f47b0..0x5f4865: the title and the tax (sprintf of text 1623 with 0x78aba30), in boxes of 120 · W / 640.
    SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    auto title = TextOf(context, ShopTitleText, "0x5f4807");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    auto taxFormat = TextOf(context, ShopTaxText, "0x5f4823");
    if (!taxFormat.HasValue())
        return Core::Unexpected(taxFormat.Error());
    const std::string tax = Crt::Sprintf(taxFormat.Value(), runtime.npcWindows.taxRate);
    DrawTitle(context, x + TitleLeft, y + TaxTop, tax);

    // 0x5f486a..0x5f4a37: buy and sell, pressed with the shop mode (0x78abb27) at 0 and at 1.
    const float footTop = Offset(y, FootTop);
    if (auto buy = DrawTheFootButton(context, state.repairCursor == 0 ? BuySlot : BuyPressedSlot, Offset(x, BuyLeft),
                                     footTop, BuyHelpText, "0x5f4930");
        !buy.HasValue())
        return buy;
    if (auto sell = DrawTheFootButton(context, state.repairCursor == 1 ? SellSlot : SellPressedSlot,
                                      Offset(x, SellLeft), footTop, SellHelpText, "0x5f4a16");
        !sell.HasValue())
        return sell;
    // 0x5f4a39..0x5f4a40: without 0x78abb09 (the shop does not repair), nothing more.
    if (state.field78abb09 == 0)
        return {};
    if (auto repair = DrawTheFootButton(context, state.repairMode != 0 ? RepairPressedSlot : RepairSlot,
                                        Offset(x, RepairLeft), footTop, RepairHelpText, "0x5f4b09");
        !repair.HasValue())
        return repair;

    // 0x5f4b2c..0x5f4c57: repair all, in light blue; with the mouse over it and the click pending (0x7d366d4), it
    // spends the click and is drawn pressed.
    const float repairAllLeft = Offset(x, RepairAllLeft);
    sprites.SetColor(RepairAllColor);
    Quad(sprites, RepairSlot, repairAllLeft, footTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
    if (MouseOnTheButton(context, repairAllLeft, footTop))
    {
        bool& click = runtime.mouse.pendingClick;
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

    // 0x5f4c59..0x5f4e13: the repair-all price (0x78ababc, unsigned) with commas, in its amount's color.
    sprites.SetColor(White);
    const float priceLeft = F32(static_cast<double>(repairAllLeft) - PriceBackLeft);
    const float priceTop = F32(static_cast<double>(footTop) + PriceBackTop);
    Quad(sprites, PriceBackSlot, priceLeft, priceTop, PriceBackWidth, PriceBackHeight, PriceBackDu, PriceBackDv);
    const auto price = static_cast<std::uint32_t>(runtime.npcWindows.repairAllPrice);
    const std::string amount = Ui::FormatZen(price);
    hud.textColors = PriceColors;
    SelectFont(context, FontKind::Bold);
    const std::int32_t textTop = Truncate(static_cast<double>(priceTop)) + PriceTextDown;
    const std::int32_t textLeft = Truncate(static_cast<double>(priceLeft));
    auto label = TextOf(context, RepairAllPriceText, "0x5f4dae");
    if (!label.HasValue())
        return Core::Unexpected(label.Error());
    DrawText(context, textLeft - PriceLabelLeft, textTop, label.Value());
    hud.textColors.text = Ui::ZenColor(price);
    DrawText(context, textLeft + PriceAmountLeft, textTop, amount);
    return {};
}

Core::Expected<void, std::string> DrawTheWarehouse(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::WarehousePanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const NpcWindowState& npc = runtime.npcWindows;
    runtime.ui.npcWindowX = ShopLeft;
    runtime.ui.npcWindowY = ShopTop;
    const std::int32_t x = runtime.ui.npcWindowX;
    const std::int32_t y = runtime.ui.npcWindowY;
    Ui::WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Hud::DrawTheGridBoxes(sprites, Offset(x, GridLeft), Offset(y, GridTop), npc.grid, GridColumns, GridRows);
    sprites.SetColor(White);

    // 0x5f4eba..0x5f4f6b: the title with the bold font, wsprintfA("%s (%s)") of text 234 and 240 or 241, in yellow when
    // locked.
    SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    const bool locked = npc.warehouseLocked != 0;
    if (locked)
        sprites.SetColor(LockedTitleColor);
    auto name = TextOf(context, WarehouseTitleText, "0x5f4f22");
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    auto state = TextOf(context, locked ? LockedText : UnlockedText, "0x5f4f1a");
    if (!state.HasValue())
        return Core::Unexpected(state.Error());
    DrawTitle(context, x + TitleLeft, y + WarehouseTitleTop, std::format("{} ({})", name.Value(), state.Value()));
    sprites.SetColor(White);

    // 0x5f4f84..0x5f5322: the stored zen (+0xed0, read with the own record open) over its background.
    const float zenLeft = Offset(x, ZenBackLeft);
    const float zenTop = Offset(y, static_cast<double>(ZenBackTop));
    Quad(sprites, PriceBackSlot, zenLeft, zenTop, PriceBackWidth, PriceBackHeight, PriceBackDu, PriceBackDv);
    SelectFont(context, FontKind::Bold);
    std::uint32_t zen = 0;
    {
        const auto own = OwnAccess(context);
        zen = world.OwnCharacter().warehouseZen;
    }
    const std::string amount = Ui::FormatZen(zen);
    hud.textColors = PriceColors;
    const std::int32_t zenTextTop = Truncate(static_cast<double>(zenTop)) + PriceTextDown;
    const std::int32_t zenTextLeft = Truncate(static_cast<double>(zenLeft));
    auto zenLabel = TextOf(context, ZenText, "0x5f52bf");
    if (!zenLabel.HasValue())
        return Core::Unexpected(zenLabel.Error());
    DrawText(context, zenTextLeft - ZenLabelLeft, zenTextTop, zenLabel.Value());
    hud.textColors.text = Ui::ZenColor(zen);
    DrawText(context, zenTextLeft + PriceAmountLeft, zenTextTop, amount);

    // 0x5f5327..0x5f56d0: text 266 in red and the warehouse fee (the level, read with the own record open) with commas,
    // in 0xff96dcff.
    hud.textColors = FeeLabelColors;
    const float feeLeft = Offset(x, FeeLeft);
    const float feeTop = Offset(y, FeeTop);
    SelectFont(context, FontKind::Bold);
    const std::int32_t feeTextTop = Truncate(static_cast<double>(feeTop));
    const std::int32_t feeTextLeft = Truncate(static_cast<double>(feeLeft));
    auto feeLabel = TextOf(context, FeeText, "0x5f538a");
    if (!feeLabel.HasValue())
        return Core::Unexpected(feeLabel.Error());
    DrawText(context, feeTextLeft, feeTextTop, feeLabel.Value());
    hud.textColors.text = PriceColors.text;
    std::int32_t fee = 0;
    {
        const auto own = OwnAccess(context);
        fee = WarehouseFee(world.OwnCharacter().level, locked);
    }
    DrawText(context, feeTextLeft + FeeAmountLeft, feeTextTop, Ui::FormatZen(static_cast<std::uint32_t>(fee)));

    // 0x5f56d5..0x5f58cb: the three buttons, with the white on black of their helps.
    hud.textColors = WarehouseHelpColors;
    const float buttonTop = Offset(y, WarehouseButtonTop);
    for (std::size_t button = 0; button < WarehouseButtonLefts.size(); ++button)
    {
        const std::size_t look = button == LockButton && locked ? LockedLockButton : button;
        // fild of the button's x and fiadd of the corner: the sum of two integers, exact.
        const float left = F32(static_cast<double>(WarehouseButtonLefts[button]) + static_cast<double>(x));
        Quad(sprites, WarehouseButtonSlots[look], left, buttonTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
        if (MouseOnTheButton(context, left, buttonTop))
        {
            if (context.leftDown)
                Quad(sprites, WarehousePressedSlots[look], left, buttonTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
            SelectFont(context, FontKind::Normal);
            auto help = TextOf(context, WarehouseHelpTexts[button], "0x5f586a");
            if (!help.HasValue())
                return Core::Unexpected(help.Error());
            Ui::DrawHelp(sprites, context.text, Truncate(static_cast<double>(left)),
                         Truncate(static_cast<double>(buttonTop)) - WarehouseHelpAbove, help.Value());
            hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
        }
        // 0x5f5889..0x5f58c4: the lock, also pressed with the password keypad open (0x78abb20).
        if (button == LockButton && runtime.ui.field78abb20 != 0)
            Quad(sprites, WarehousePressedSlots[look], left, buttonTop, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
    }
    sprites.SetColor(White);
    return {};
}
Core::Expected<void, std::string> DrawThePanel2000(Ui::GameHudContext& context, Ui::PanelRegistry& panels)
{
    if (!panels.IsOpen(Ui::Panel2000))
        return {};
    // 0x5f7180..0x5f74d9: instructions, consent and the two buttons of window 0x2000.
    constexpr std::int32_t Left = 450;
    auto& sprites = context.sprites;
    auto& hud = context.hud;
    hud.inventoryX = Left;
    hud.inventoryY = 0;
    sprites.SetColor(White);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.EnableTexture();
    Ui::WindowDrawing::DrawTheWindowBack(sprites, Left, 0);
    Quad(sprites, 0x117, Left, 225, 190, 10, 0.7421875f, 0.625f);
    hud.textColors = {.text = 0xffd2e6ffu, .background = 0};
    SelectFont(context, FontKind::Bold);
    if (auto written = DrawPanel2000Instructions(context); !written.HasValue())
        return written;
    const bool accepted = context.world.Runtime().panels.field78abb08 != 0;
    Quad(sprites, accepted ? 0x123 : 0x122, Left + 25, 240, 16, 16, 0.75f, 0.75f);
    hud.textColors.text = accepted ? 0xff0096d4u : 0xff67bfdfu;
    auto label = TextOf(context, 447, "0x5f7379");
    if (!label.HasValue())
        return Core::Unexpected(label.Error());
    DrawText(context, Left + 44, 245, label.Value());
    if (auto written = DrawPanel2000Buttons(context, accepted); !written.HasValue())
        return written;
    sprites.SetColor(White);
    return {};
}
} // namespace Client::Npc
