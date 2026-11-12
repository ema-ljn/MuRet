#include "Scenes/ItemTooltips.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/HudItems.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldRuntime.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x5b2aae: el ítem vacío.
constexpr std::uint16_t NoItem = 0xffff;
// 0x5b2b0d: 0x422990, el tooltip del ángel y el sátiro (y la A9), que devuelve 1 con estos dos.
constexpr std::uint16_t FirstPet = 0x1a04;
constexpr std::uint16_t SecondPet = 0x1a05;
// 0x5b2b3b..0x5b2b54: el nivel, (D+4 >> 3) & 0xf.
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// 0x5b2b57..0x5b2cad: el color del nombre.
constexpr std::array<std::uint16_t, 4> GoldJewels{0x1c0d, 0x1c0e, 0x180f, 0x1c1f};
// 0x402b30: 2 o más con los paquetes de joyas.
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

// 0x5b2cad..0x5b2f86: lo que no mira las tiendas.
constexpr std::uint16_t Scroll = 0x1c0c;
constexpr std::uint16_t FirstQuestScroll = 0x1c17;
constexpr std::uint16_t LastQuestScroll = 0x1c1a;
constexpr std::uint16_t WarriorRing = 0x1a14;
constexpr std::uint16_t LuckyBox = 0x1c0b;
constexpr std::uint16_t EventTicket = 0x1c14;
constexpr std::int32_t LastFreeTicketLevel = 5;

// 0x5b8080..0x5b8088: sin lugar arriba, la caja va debajo del ítem, a su alto (Item.bmd+0x25) por 20.
constexpr std::size_t HeightByte = 0x25;
constexpr std::int32_t CellPixels = 20;
// 0x5b808b: el modo centrado de 0x467060 y el marco.
constexpr std::int32_t CenteredMode = 2;

// 0x5b8150: el 238 con el precio ("0" sin nada que reparar, 0x6c9be8), el 620 de las excelentes, el 71 de la
// durabilidad y los 480 de alto de diseño de su caja.
constexpr std::size_t RepairPriceText = 238;
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

// [EBP−0x1c]: el color del nombre.
std::int32_t NameColorOf(const World::ItemState& item, std::int32_t level)
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
    // 0x5b2c7f..0x5b2caa: las alas de 0x1803 a 0x1806 y la capa, con o sin todo lo de arriba.
    if ((id >= FirstLateWing && id <= LastLateWing) || id == CapeOfLord)
        color = level >= GoldLevel ? TooltipGold : plain;
    return color;
}

// 0x5b2cad..0x5b3716: el precio. Con la tienda de NPC abierta (0x10000, 1 rand()), salvo la caja de la suerte y el
// ticket de 1 a 5, va el precio de 0x54d0f0 y 0x5b1200, de la CL9.3. Con la tienda personal propia (0x800) o la
// ajena (0x20000), el de 0x78abaa4, que solo pone en 1 0x5c4110 con la grilla de la tienda personal (0x78ab0b8, la
// CL9.4): MuRet no la tiene, así que queda en 0 y no hay precio.
Core::Expected<void, std::string> CheckTheShops(GameHudContext& context, const World::ItemState& item,
                                                std::int32_t level)
{
    const std::uint16_t id = item.id;
    if (id == Scroll || (id >= FirstQuestScroll && id <= LastQuestScroll) ||
        (id == WarriorRing && level >= 1 && level <= 2))
        return {};
    GamePanels panels(context.world, context.random);
    UI::PanelRegistry& registry = panels.Registry();
    if (registry.IsOpen(UI::NpcShopPanel) && id != LuckyBox &&
        !(id == EventTicket && level >= 1 && level <= LastFreeTicketLevel))
        return Core::Unexpected(std::format("0x5b2d23: el precio del {:#x} en la tienda de NPC (0x54d0f0 y 0x5b1200) "
                                            "es de la CL9.3",
                                            id));
    if (!registry.IsOpen(UI::PersonalShopPanel))
        static_cast<void>(registry.IsOpen(UI::OtherShopPanel));
    return {};
}

// 0x5b8160..0x5b821b: los tipos sin tooltip de reparar: salen sin tocar los renglones.
bool IsRepairable(std::int32_t type)
{
    const auto in = [type](std::int32_t first, std::int32_t last) { return type >= first && type <= last; };
    if (in(0x1a00, 0x1a04) || type == 0x1a0a || type == 0x807 || type == 0x80f || type >= 0x1c00 ||
        in(0x1807, 0x1813) || in(0x1a0e, 0x1a13) || type == 0x1a14 || type == 0x1a1d || type == 0x1a05 ||
        type == 0x1a07 || type == 0x1a0b || in(0x1a20, 0x1a25))
        return false;
    return true;
}

// 0x5b8265..0x5b8315: el color del nombre de 0x5b8150, sin el de los conjuntos y con la vuelta de 0x1803..0x1806 y
// de todo desde 0x1a1e.
std::int32_t RepairNameColorOf(const World::ItemState& item, std::int32_t level)
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

// 0x5b8373..0x5b839b: un ítem gastado pone el modo de reparar en 2 y lleva el precio de 0x54d0f0 en el modo 2 con el
// formato de 0x5b13e0, que lee [0x78abb10]. El 0x1a04 y el 0x1a05, los únicos que leen las mascotas, salen al
// principio de 0x5b8150.
Core::Expected<RepairPrice, std::string> RepairTheWornItem(GameHudContext& hud, const World::ItemState& item,
                                                           std::uint16_t maximum)
{
    WorldRuntime& runtime = hud.world.Runtime();
    if (!hud.projecting)
        runtime.panels.repairMode = 2;
    auto price = ItemPrice(item, *hud.items, RepairPriceMode, std::nullopt);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    return RepairPriceOf(price.Value(), item.durability, maximum, runtime.field78abb10);
}

// 0x5b8ae1..0x5b8b6d: la caja de reparar, con la altura en enteros sin signo: ((N − M) · cy + M · cy / 2) · 480 / H.
Core::Expected<void, std::string> DrawTheRepairBox(const ItemTooltipContext& context,
                                                   std::span<const std::byte, Data::Tables::ItemRecordSize> record)
{
    GameHudContext& hud = context.hud;
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

// 0x5b8020..0x5b8096: la caja, arriba del ítem con el alto que cuentan N y M: (N − M) · cy + M · cy / 2 sobre ry,
// con cy el de la primera letra del renglón 0 en la fuente que tenga el DC.
Core::Expected<void, std::string> DrawTheItemBox(const ItemTooltipContext& context,
                                                 std::span<const std::byte, Data::Tables::ItemRecordSize> record)
{
    GameHudContext& hud = context.hud;
    const TooltipLines& lines = hud.hud.tooltip;
    const std::string_view first = std::string_view(lines.text[0]).substr(0, 1);
    const std::int32_t cy = hud.text.Measure(hud.hud.dcFont, first).height;
    const std::int32_t whole = lines.next - lines.halves;
    const std::int32_t sum = whole * cy + lines.halves * cy / 2;
    const double ry = Render::ScaleY(1.0f, hud.sprites.Screen());
    const std::optional<std::int32_t> height = World::OriginalFtolLow(static_cast<double>(sum) / ry);
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
    GameHudContext& hud = context.hud;
    const World::ItemState& item = context.item;
    if (item.id == NoItem)
        return {};
    TooltipLines& tooltip = hud.hud.tooltip;
    tooltip.next = 0;
    tooltip.halves = 0;
    tooltip.ClearColors();
    tooltip.ClearTexts();
    if (item.id == FirstPet || item.id == SecondPet)
        return Core::Unexpected(std::format("0x4229d4: el tooltip del {:#x} (0x422990) y la A9 que pide son de la CL9 "
                                            "(P-C126)",
                                            item.id));
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
    if (auto checked = CheckTheShops(hud, item, level); !checked.HasValue())
        return checked;
    ItemTooltipFrame frame{.context = hud,
                           .lines = lines,
                           .item = item,
                           .record = *record,
                           .level = level,
                           .nameColor = NameColorOf(item, level)};
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
    GameHudContext& hud = context.hud;
    const World::ItemState& item = context.item;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    if (!IsRepairable(type))
        return {};
    if (hud.items == nullptr)
        return Core::Unexpected(std::string("0x5b822d: el tooltip de reparar sin Item.bmd"));
    const auto record = type < 0 ? std::nullopt : hud.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x5b822d: el tipo {:#x} lee fuera de Item.bmd", item.id));
    // 0x5b823c..0x5b8258: N, M y los textos; los colores no.
    TooltipLines& tooltip = hud.hud.tooltip;
    tooltip.next = 0;
    tooltip.halves = 0;
    tooltip.ClearTexts();
    const auto level = static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
    const std::int32_t color = RepairNameColorOf(item, level);
    TooltipWriter lines(tooltip, hud.texts);
    lines.At("0x5b8327");
    lines.NewLine();
    // 0x5b835d..0x5b840b: el precio de reparar, con la durabilidad que falta.
    const std::uint16_t maximum = MaxDurability(item, *record, level);
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
    // 0x5b895a..0x5b8a2b: el nombre. 0x180b (0x5b8453) y 0x1a0a (0x5b8915) no llegan: salen al principio.
    lines.At("0x5b895a");
    const std::string name = ItemRecordName(*record);
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
    // 0x5b8a6d..0x5b8adc: la durabilidad, en blanco.
    lines.At("0x5b8a6d");
    lines.Line(TooltipWhite, RepairDurabilityText, static_cast<std::int32_t>(item.durability),
               static_cast<std::int32_t>(maximum));
    lines.NewLine();
    if (lines.Failed())
        return Core::Unexpected(lines.Error());
    return DrawTheRepairBox(context, *record);
}

Core::Expected<void, std::string> DrawTheSkillTooltip(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                      std::int32_t skill, std::int32_t flag)
{
    static_cast<void>(context);
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(flag);
    return Core::Unexpected(std::format("0x5b90b0: el tooltip de la skill {} es del hito 5", skill));
}
} // namespace Client::Scenes
