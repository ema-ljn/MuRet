#include "Tooltips/ItemTooltips.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Tooltips/GameTooltips.h"
#include "Items/HudItems.h"
#include "Render/Frame/ITextDrawer.h"
#include "Items/ItemPrice.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Npc/NpcWindowState.h"
#include "Tooltips/PetTooltip.h"
#include "Tooltips/TooltipLines.h"
#include "Tooltips/TooltipWriter.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/ZenText.h"
#include "Ui/PanelRegistry.h"
#include "Math/Ftol.h"
#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Tooltips
{
namespace
{
// 0x5b2aae
constexpr std::uint16_t NoItem = 0xffff;
// 0x5b2b3b..0x5b2b54: the level, (D+4 >> 3) & 0xf.
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// 0x5b2b57..0x5b2cad: the name's color.
constexpr std::array<std::uint16_t, 4> GoldJewels{0x1c0d, 0x1c0e, 0x180f, 0x1c1f};
// 0x402b30: 2 or more with the jewel bundles.
constexpr std::array<std::uint16_t, 2> JewelBundles{0x181e, 0x181f};
constexpr std::array<std::uint16_t, 2> GoldOthers{0x1c10, 0x1c16};
constexpr std::array<std::uint16_t, 4> PurpleItems{0xa0a, 0x13, 0x812, 0x40d};
constexpr std::array<std::uint16_t, 5> GoldQuestItems{0x1c11, 0x1c12, 0x1c13, 0x1a10, 0x1a11};
constexpr std::uint8_t SetKindMask = 3;
constexpr std::uint8_t ExcellentBits = 0x3f;
constexpr std::int32_t GoldLevel = 7;
constexpr std::uint16_t FirstLateWing = 0x1803;
constexpr std::uint16_t LastLateWing = 0x1806;
constexpr std::uint16_t CapeOfLord = 0x1a1e;

// 0x5b2cad..0x5b2f86: what does not look at the shops.
constexpr std::uint16_t Scroll = 0x1c0c;
constexpr std::uint16_t FirstQuestScroll = 0x1c17;
constexpr std::uint16_t LastQuestScroll = 0x1c1a;
constexpr std::uint16_t WarriorRing = 0x1a14;
constexpr std::uint16_t LuckyBox = 0x1c0b;
constexpr std::uint16_t EventTicket = 0x1c14;
constexpr std::int32_t LastFreeTicketLevel = 5;

// 0x5b8080..0x5b8088: with no room above, the box goes below the item, at its height (Item.bmd+0x25) times 20.
constexpr std::size_t HeightByte = 0x25;
constexpr std::int32_t CellPixels = 20;
// 0x5b808b: the centered mode of 0x467060 and the frame.
constexpr std::int32_t CenteredMode = 2;

// 0x5b8150: 238 with the price ("0" with nothing to repair, 0x6c9be8), 620 of the excellent ones, 71 of the
// durability and its box's design height of 480.
constexpr std::size_t RepairPriceText = 238;
// 0x5b2e26 and 0x5b2f19: the buy price with and without the tax (0x7733c4c) and the sell price (0x76c1bb0).
constexpr std::size_t BuyPriceText = 1620;
constexpr std::size_t SellPriceText = 63;
constexpr char FullDurabilityPrice[] = "0";
constexpr std::size_t ExcellentText = 620;
constexpr std::size_t RepairDurabilityText = 71;
constexpr std::uint32_t DesignHeight = 480;

bool IsOneOf(std::uint16_t id, std::span<const std::uint16_t> ids)
{
    for (const std::uint16_t candidate : ids)
    {
        if (id == candidate)
            return true;
    }
    return false;
}

// [EBP−0x1c]
std::int32_t NameColorOf(const Kernel::ItemState& item, std::int32_t level)
{
    const std::uint16_t id = item.id;
    const bool options = item.optionCount > 0;
    const std::int32_t plain = options ? TooltipBlue : TooltipWhite;
    std::int32_t color = plain;
    if (IsOneOf(id, GoldJewels) || IsOneOf(id, JewelBundles) || IsOneOf(id, GoldOthers))
        color = TooltipGold;
    else if (IsOneOf(id, PurpleItems))
        color = TooltipPurple;
    else if (IsOneOf(id, GoldQuestItems))
        color = TooltipGold;
    else if (const auto kind = item.flags1c & SetKindMask; kind == 1 || kind == 2)
        color = TooltipGreenOnRed;
    else if (options && (item.flags1b & ExcellentBits) != 0)
        color = TooltipGreen;
    else if (level >= GoldLevel)
        color = TooltipGold;
    // 0x5b2c7f..0x5b2caa: the wings from 0x1803 to 0x1806 and the cape, with or without everything above.
    if ((id >= FirstLateWing && id <= LastLateWing) || id == CapeOfLord)
        color = level >= GoldLevel ? TooltipGold : plain;
    return color;
}

// 0x5b1200(price, buffer): the price with the shop's tax (0x78aba30, in percent): the signed tax times the unsigned
// price in 64 bits (_allmul), divided by 100 signed (_alldiv), and its low dword added to the price; written like
// 0x5b1130 with its copies of the formats (0x6c9874..0x6c98a0).
std::string PriceWithTax(std::uint32_t price, std::int32_t tax)
{
    const std::int64_t added = static_cast<std::int64_t>(tax) * static_cast<std::int64_t>(price) / 100;
    return Ui::FormatZen(price + static_cast<std::uint32_t>(static_cast<std::uint64_t>(added) & 0xffffffffu));
}

// 0x5b2cad..0x5b3716: the price. With the NPC shop open (0x10000, 1 rand()), except the lucky box and the ticket from
// 1 to 5: from the shop grid (0x78aba27 non-zero, 0x5b2d2e) 1620 with the price of 0x54d0f0 in mode 0 with the tax
// (0x5b1200) and without it; otherwise (0x5b2e46), 63 with mode 1's. Both with the name's color, without bold, and a
// "\n" (0x6c99c8) after. With the own personal shop (0x800) or another's (0x20000), without rand(), the personal shop
// grid's (WriteThePersonalShopPrice, CL9 milestone 7).
Core::Expected<void, std::string> WriteTheShopPrice(Ui::GameHudContext& context, TooltipWriter& lines,
                                                    const Kernel::ItemState& item, std::int32_t level,
                                                    std::int32_t nameColor, std::uint8_t grid)
{
    const std::uint16_t id = item.id;
    if (id == Scroll || (id >= FirstQuestScroll && id <= LastQuestScroll) ||
        (id == WarriorRing && level >= 1 && level <= 2))
        return {};
    Hud::GamePanels panels(context.world, context.random);
    Ui::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(Ui::NpcShopPanel) && id != LuckyBox &&
        !(id == EventTicket && level >= 1 && level <= LastFreeTicketLevel))
    {
        lines.At("0x5b2d23");
        if (grid != 0)
        {
            auto price = Items::ItemPrice(item, *context.items, Items::ShopGridPrice, std::nullopt);
            if (!price.HasValue())
                return Core::Unexpected(std::move(price).Error());
            const std::string plain = Ui::FormatZen(price.Value());
            const std::string taxed = PriceWithTax(price.Value(), context.world.Runtime().npcWindows.taxRate);
            lines.PrintText(BuyPriceText, std::string_view(taxed), std::string_view(plain));
        }
        else
        {
            auto price = Items::ItemPrice(item, *context.items, Items::InventoryPrice, std::nullopt);
            if (!price.HasValue())
                return Core::Unexpected(std::move(price).Error());
            lines.PrintText(SellPriceText, std::string_view(Ui::FormatZen(price.Value())));
        }
        lines.Color(nameColor);
        lines.Next();
        lines.NewLine();
        return {};
    }
    if (!registry.IsOpen(Ui::PersonalShopPanel) && !registry.IsOpen(Ui::OtherShopPanel))
        return {};
    return WriteThePersonalShopPrice(context, lines, item);
}

// 0x5b8160..0x5b821b: the types without a repair tooltip: they exit without touching the lines.
bool IsRepairable(std::int32_t type)
{
    const auto in = [type](std::int32_t first, std::int32_t last) { return type >= first && type <= last; };
    if (in(0x1a00, 0x1a04) || type == 0x1a0a || type == 0x807 || type == 0x80f || type >= 0x1c00 ||
        in(0x1807, 0x1813) || in(0x1a0e, 0x1a13) || type == 0x1a14 || type == 0x1a1d || type == 0x1a05 ||
        type == 0x1a07 || type == 0x1a0b || in(0x1a20, 0x1a25))
        return false;
    return true;
}

// 0x5b8265..0x5b8315: the name's color of 0x5b8150, without the sets' and with the return of 0x1803..0x1806 and of
// everything from 0x1a1e.
std::int32_t RepairNameColorOf(const Kernel::ItemState& item, std::int32_t level)
{
    const std::uint16_t id = item.id;
    const bool options = item.optionCount > 0;
    const std::int32_t plain = options ? TooltipBlue : TooltipWhite;
    std::int32_t color = plain;
    if (id == 0x1c0d || id == 0x1c0e || id == 0x180f || IsOneOf(id, JewelBundles))
        color = TooltipGold;
    else if (IsOneOf(id, PurpleItems))
        color = TooltipPurple;
    else if (id == 0x1c11 || id == 0x1c12 || id == 0x1c13)
        color = TooltipGold;
    else if (options && (item.flags1b & ExcellentBits) != 0)
        color = TooltipGreen;
    else if (level >= GoldLevel)
        color = TooltipGold;
    const auto type = static_cast<std::int16_t>(id);
    if ((type >= FirstLateWing && type <= LastLateWing) || type >= CapeOfLord)
        color = level >= GoldLevel ? TooltipGold : plain;
    return color;
}

// 0x5b8373..0x5b839b: a worn item sets the repair mode to 2 and carries the price of 0x54d0f0 in mode 2 with the format
// of 0x5b13e0, which reads [0x78abb10]. 0x1a04 and 0x1a05, the only ones that read the pets (0x422930, with the A9
// levels), exit at the start of 0x5b8150.
Core::Expected<Items::RepairPrice, std::string> RepairTheWornItem(Ui::GameHudContext& hud,
                                                                  const Kernel::ItemState& item, std::uint16_t maximum)
{
    Kernel::WorldRuntime& runtime = hud.world.Runtime();
    if (!hud.projecting)
        runtime.panels.repairMode = 2;
    const Items::PetPriceLevels pets{.raven = runtime.pets[Kernel::RavenPetStats].level,
                                     .horse = runtime.pets[Kernel::DarkHorsePetStats].level};
    auto price = Items::ItemPrice(item, *hud.items, Items::RepairPriceMode, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    return Items::RepairPriceOf(price.Value(), item.durability, maximum, runtime.field78abb10);
}

// 0x5b8ae1..0x5b8b6d: the repair box, with the height in unsigned integers: ((N − M) · cy + M · cy / 2) · 480 / H.
Core::Expected<void, std::string> DrawTheRepairBox(const ItemTooltipContext& context,
                                                   std::span<const std::byte, Tables::ItemRecordSize> record)
{
    Ui::GameHudContext& hud = context.hud;
    const TooltipLines& lines = hud.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = hud.text.Measure(hud.hud.dcFont, first).height;
    const std::int32_t sum = (lines.next - lines.halves) * cy + lines.halves * cy / 2;
    const auto screenHeight = static_cast<std::uint32_t>(hud.sprites.Screen().height);
    if (screenHeight == 0)
        return Core::Unexpected(std::string("0x5b8b45: la altura de la caja de reparar sin pantalla (DIV por 0)"));
    const std::uint32_t height = static_cast<std::uint32_t>(sum) * DesignHeight / screenHeight;
    auto top = static_cast<std::int32_t>(static_cast<std::uint32_t>(context.y) - height);
    if (top < 0)
        top = context.y + std::to_integer<std::int32_t>(record[HeightByte]) * CellPixels;
    return DrawTheTooltipBox(hud, context.x, top, lines.next, 0, CenteredMode, 1);
}

// 0x5b8020..0x5b8096: the box, above the item with the height that N and M count: (N − M) · cy + M · cy / 2 over ry,
// with cy that of the first letter of line 0 in whatever font the DC has.
Core::Expected<void, std::string> DrawTheItemBox(const ItemTooltipContext& context,
                                                 std::span<const std::byte, Tables::ItemRecordSize> record)
{
    Ui::GameHudContext& hud = context.hud;
    const TooltipLines& lines = hud.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = hud.text.Measure(hud.hud.dcFont, first).height;
    const std::int32_t whole = lines.next - lines.halves;
    const std::int32_t sum = whole * cy + lines.halves * cy / 2;
    const double ry = Render::ScaleY(1.0f, hud.sprites.Screen());
    const std::optional<std::int32_t> height = Math::OriginalFtolLow(static_cast<double>(sum) / ry);
    if (!height)
        return Core::Unexpected(std::format("0x5b806f: el alto del tooltip ({} / {}) no entra en _ftol", sum, ry));
    auto top = static_cast<std::int32_t>(static_cast<std::uint32_t>(context.y) - static_cast<std::uint32_t>(*height));
    if (top < 0)
        top = context.y + std::to_integer<std::int32_t>(record[HeightByte]) * CellPixels;
    return DrawTheTooltipBox(hud, context.x, top, lines.next, 0, CenteredMode, 1);
}
} // namespace

Core::Expected<void, std::string> DrawTheItemTooltip(const ItemTooltipContext& context)
{
    Ui::GameHudContext& hud = context.hud;
    const Kernel::ItemState& item = context.item;
    if (item.id == NoItem)
        return {};
    TooltipLines& tooltip = hud.hud.tooltip;
    tooltip.next = 0;
    tooltip.halves = 0;
    tooltip.ClearColors();
    tooltip.ClearTexts();
    // 0x5b2b0d..0x5b2b17: 0x422990, which with 0x1a04 and 0x1a05 is the whole tooltip.
    auto pet = DrawThePetTooltip(context);
    if (!pet.HasValue())
        return Core::Unexpected(std::move(pet).Error());
    if (pet.Value())
        return {};
    const auto type = static_cast<std::int16_t>(item.id);
    if (hud.items == nullptr)
        return Core::Unexpected(std::string("0x5b2acc: el tooltip del ítem sin Item.bmd"));
    const auto record = type < 0 ? std::nullopt : hud.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x5b2ad2: el tipo {:#x} lee fuera de Item.bmd", item.id));
    TooltipWriter lines(tooltip, hud.texts);
    lines.At("0x5b2b22");
    lines.NewLine();
    const auto level = static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
    const std::int32_t nameColor = NameColorOf(item, level);
    if (auto priced = WriteTheShopPrice(hud, lines, item, level, nameColor, context.grid); !priced.HasValue())
        return priced;
    ItemTooltipFrame frame{
        .context = hud, .lines = lines, .item = item, .record = *record, .level = level, .nameColor = nameColor};
    for (const auto section :
         {WriteTheItemName, WriteTheKindLines, WriteTheStats, WriteTheNotes, WriteTheRequirements, WriteTheOptions})
    {
        if (auto written = section(frame); !written.HasValue())
            return written;
    }
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    return DrawTheItemBox(context, *record);
}

Core::Expected<void, std::string> DrawTheRepairTooltip(const ItemTooltipContext& context)
{
    Ui::GameHudContext& hud = context.hud;
    const Kernel::ItemState& item = context.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    if (!IsRepairable(type))
        return {};
    if (hud.items == nullptr)
        return Core::Unexpected(std::string("0x5b822d: el tooltip de reparar sin Item.bmd"));
    const auto record = type < 0 ? std::nullopt : hud.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x5b822d: el tipo {:#x} lee fuera de Item.bmd", item.id));
    // 0x5b823c..0x5b8258: N, M and the texts; not the colors.
    TooltipLines& tooltip = hud.hud.tooltip;
    tooltip.next = 0;
    tooltip.halves = 0;
    tooltip.ClearTexts();
    const auto level = static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
    const std::int32_t color = RepairNameColorOf(item, level);
    TooltipWriter lines(tooltip, hud.texts);
    lines.At("0x5b8327");
    lines.NewLine();
    // 0x5b835d..0x5b840b: the repair price, with the missing durability.
    const std::uint16_t maximum = Items::MaxDurability(item, *record, level);
    if (item.durability < maximum)
    {
        auto price = RepairTheWornItem(hud, item, maximum);
        if (!price.HasValue())
            return Core::Unexpected(std::move(price).Error());
        lines.PrintText(RepairPriceText, std::string_view(price.Value().text));
    }
    else
    {
        lines.PrintText(RepairPriceText, std::string_view(FullDurabilityPrice));
        if (!hud.projecting)
            hud.world.Runtime().panels.repairMode = 1;
    }
    lines.Color(color);
    lines.Bold(1);
    lines.Next();
    lines.NewLine();
    // 0x5b895a..0x5b8a2b: the name. 0x180b (0x5b8453) and 0x1a0a (0x5b8915) do not get here: they exit at the start.
    lines.At("0x5b895a");
    const std::string name = Items::ItemRecordName(*record);
    const bool plain = (type >= FirstLateWing && type <= LastLateWing) || type >= CapeOfLord;
    if (!plain && (item.flags1b & ExcellentBits) != 0)
    {
        if (level == 0)
            lines.Print("%s %s", lines.Text(ExcellentText), std::string_view(name));
        else
            lines.Print("%s %s +%d", lines.Text(ExcellentText), std::string_view(name), level);
    }
    else if (level == 0)
        lines.Print("%s", std::string_view(name));
    else
        lines.Print("%s +%d", std::string_view(name), level);
    lines.Bold(1);
    lines.Color(color);
    lines.Next();
    lines.NewLine();
    // 0x5b8a6d..0x5b8adc: the durability, in white.
    lines.At("0x5b8a6d");
    lines.Line(TooltipWhite, RepairDurabilityText, static_cast<std::int32_t>(item.durability),
               static_cast<std::int32_t>(maximum));
    lines.NewLine();
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    return DrawTheRepairBox(context, *record);
}
} // namespace Client::Tooltips
