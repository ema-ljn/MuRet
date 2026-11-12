#include "Scenes/ItemTooltips.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/TooltipWriter.h"
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
    return Core::Unexpected(std::format("0x5b8150: el tooltip de reparar el {:#x} es del hito 5", context.item.id));
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
