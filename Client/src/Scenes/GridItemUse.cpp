#include "Scenes/GridItemUse.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <bit>
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
using World::ItemState;

constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
constexpr std::int32_t FirstGridSlot = 12;
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// 0x5c7bc6..0x5c7c0a: los libros (0x1e00..0x1fff) y las esferas, en comparaciones con signo de AX. 0x1810..0x1813,
// 0x1814 y 0x1815..0x1818 son tres pruebas seguidas: juntas, 0x1810..0x1818. El 0x180f no entra.
constexpr std::int32_t FirstBook = 0x1e00;
constexpr std::int32_t LastBook = 0x1fff;
constexpr std::int32_t FirstOrb = 0x1807;
constexpr std::int32_t LastFirstOrbs = 0x180e;
constexpr std::int32_t FirstLaterOrb = 0x1810;
constexpr std::int32_t LastOrb = 0x1818;
// 0x5c90ca: el nivel que pide Item.bmd, un WORD en +0x3a del registro.
constexpr std::size_t RequiredLevelOffset = 0x3a;
// 0x5c912b, 0x5c84bb: la dirección del aviso 474 de las consultas en línea.
constexpr std::string_view SkillItemRefusal = "0x5c912b";
constexpr std::string_view Item1a0bRefusal = "0x5c84bb";

// 0x5c7c10..0x5c8447: la fruta. 0x5c8061: desde el nivel 10; 0x5c83ad: la de nivel 4 (el dword +4 entero en 0x20) es
// de la clase 4; los avisos de 0x632e30 (la dirección − 0x76bd1dc, sobre 300: 0x76f3f98, 0x7748ef8 y 0x7748a48) y el
// diálogo de 0x633300(1, 0x178, 0x96).
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::uint16_t FruitLevel = 10;
constexpr std::uint32_t CommandFruitBits = 0x20;
constexpr std::uint8_t CommandFruitClass = 4;
constexpr std::size_t FruitLevelText = 749;
constexpr std::size_t FruitEquipmentText = 1909;
constexpr std::size_t FruitClassText = 1905;
constexpr std::int32_t FruitDialogLines = 1;
constexpr std::int32_t FruitDialogText = 0x178;

// 0x5c8448..0x5c8473: el 0x1a0b de nivel 0 o 1; el de nivel 1, sin el héroe maestro del gremio (+0x368 en 0x80).
constexpr std::uint16_t Item1a0b = 0x1a0b;
constexpr std::uint8_t GuildMaster = 0x80;

// 0x5c67e9, 0x5c6c36 y 0x5c7010: los tres que mandan el C1 .. 91.
constexpr std::int32_t EventTicket1a1d = 0x1a1d;
constexpr std::int32_t EventTicket1c13 = 0x1c13;
constexpr std::int32_t EventTicket1a12 = 0x1a12;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool IsSkillItem(std::int32_t type)
{
    return InRange(type, FirstBook, LastBook) || InRange(type, FirstOrb, LastFirstOrbs) ||
           InRange(type, FirstLaterOrb, LastOrb);
}

// 0x632e30 con el texto `index` de Text.bmd, sin sprintf.
Core::Expected<void, std::string> Notice(const InventoryMoveContext& context, std::size_t index, std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.services.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: falta el texto {} de Text.bmd", where, index));
    WorldRuntime& runtime = context.world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, *text);
    return {};
}

// 0x5c8fb9..0x5c991b. El CMP AX, 0x1e12 de 0x5c8fb9 no lo lee nadie: salta a 0x5c8fd4, que pisa las banderas antes
// de mirarlas. 0x5c8fd4..0x5c909c abre el registro propio y 0x5c991b lo cierra, también desde los avisos y las
// salidas: con la cuenta en 0, los 3.848 rand() van después de los del envío.
Core::Expected<void, std::string> UseTheSkillItem(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                  const ItemState& hovered, std::int32_t cell)
{
    WorldState& world = context.world;
    const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    if (context.services.items == nullptr)
        return Core::Unexpected(std::string("0x5c90c1: lee *[0x7758550] (Item.bmd) y no está cargado"));
    const auto record = context.services.items->Record(hovered.id);
    if (!record)
        return Core::Unexpected(std::format("0x5c90ca: el tipo {:#x} lee fuera de Item.bmd", hovered.id));
    const auto required =
        static_cast<std::uint16_t>(std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset]) |
                                   (std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset + 1]) << 8));
    // 0x5c90ab..0x5c90e3: tres JC sobre WORD, sin signo; la energía y la fuerza con lo de +0x30 y +0x2a sumados en 16
    // bits.
    const OwnCharacterRecord& stats = world.OwnCharacter();
    if (stats.level < required)
        return {};
    if (static_cast<std::uint16_t>(stats.word30 + stats.energy) < hovered.word22)
        return {};
    if (static_cast<std::uint16_t>(stats.word2a + stats.strength) < hovered.word1e)
        return {};
    return UseInventoryItemOn(context, panels, InventoryUse{.slot = cell + FirstGridSlot, .where = SkillItemRefusal});
}

// 0x5c7c10..0x5c8447. Los dos accesos al registro propio son en línea: 0x5c7c35..0x5c7e59 lo abre y 0x5c7e59..0x5c8061
// lo cierra, con su rand() por byte en 0x5c8026; 0x5c806e..0x5c8219 lo vuelve a abrir y 0x5c8250..0x5c83a7 lo cierra
// con 0x40b4d0.
Core::Expected<void, std::string> UseTheFruit(const InventoryMoveContext& context, const ItemState& hovered,
                                              std::int32_t cell)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (level < FruitLevel)
        return Notice(context, FruitLevelText, "0x5c842f");
    // 0x5c806b..0x5c8099: la fruta de la casilla de arriba a la izquierda en 0x78a59c8, con la casilla en +9.
    const ItemState& fruit = world.Inventory().cells[static_cast<std::size_t>(cell)];
    runtime.dialogItem = fruit;
    runtime.dialogItemCell = static_cast<std::uint8_t>(cell & 0xff);
    bool unequipped = true;
    std::uint8_t heroClass = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        // 0x5c8219..0x5c8237: los doce lugares del equipo, de a 0x48 desde +0xb6c; 0x5c8245: la clase (+0xb), entera.
        for (const ItemState& worn : world.OwnCharacter().equipment)
        {
            if (worn.id != EmptyOwnItemId)
                unequipped = false;
        }
        heroClass = world.OwnCharacter().characterClass;
    }
    if (!unequipped)
        return Notice(context, FruitEquipmentText, "0x5c8411");
    // 0x5c83ad: el nivel lo mira en EBX, la casilla bajo el mouse (0x5c589e), no en la copia de 0x78a59c8.
    if (hovered.scalar1Low == CommandFruitBits && heroClass != CommandFruitClass)
        return Notice(context, FruitClassText, "0x5c83be");
    // 0x5c83e7..0x5c83f3: el diálogo 0x96 nombra el ítem de 0x78a59c8: corta con la dirección de su texto.
    return UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.services.texts, FruitDialogLines,
                              FruitDialogText, UI::ItemNameDialog);
}

// 0x5c8e37..0x5c8fb8, desde la grilla del inventario (0x5c8e58). La otra grilla (0x5c8ef8..0x5c8f35, con
// 0x78a5a10 y 0x78a5a14) no la recorre MuRet.
Core::Expected<void, std::string> CopyIntoTheCursor(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                    std::int32_t cell)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.cursorItem = context.world.Inventory().cells[static_cast<std::size_t>(cell)];
    runtime.cursorGrid = CursorGrid::Inventory;
    // 0x5c8e4a..0x5c8e68: el mouse en 0x7d36890 y 0x7d36894, que solo lee 0x5ca250 con 0x7d368a1 (0x5d8ce0): MuRet no
    // lo guarda.
    runtime.ui.field78abb28 = 1;
    if (panels.IsOpen(UI::WarehousePanel))
        return Core::Unexpected(std::string("0x5c8e8f: con el almacén abierto, 0x5c9e10 busca lugar para el ítem en su "
                                            "grilla (0x78a7070, 8 × 15) y, si lo hay, 0x5bd750 lo saca del inventario "
                                            "con el sonido 0x1d: es del hito 6"));
    // 0x5c8eb8..0x5c8ebf: sin el almacén, 0x7d368a1 queda como estaba. Solo lo prenden 0x5c8eb3 y 0x5c8f30, con el
    // almacén abierto, y 0x5d8cf6 lo apaga: en MuRet está en 0, y 0x5c8f9a vacía el cursor (0x12 dwords en 0).
    runtime.cursorItem = ItemState{};
    runtime.cursorItemCount = 0;
    return {};
}
} // namespace

bool IsAnEventTicket(std::int32_t type)
{
    return type == EventTicket1a1d || type == EventTicket1c13 || type == EventTicket1a12;
}

Core::Expected<void, std::string> UseTheEventTicket(UI::PanelRegistry& panels, std::int32_t type)
{
    if (type == EventTicket1a1d)
    {
        // 0x5c67f3..0x5c685f: PUSH 0x10000, 0x40, 0x20, 0x1000 y 0x4000, cada uno con su JNZ a 0x5c9b0b.
        for (const UI::PanelId panel :
             {UI::NpcShopPanel, UI::WarehousePanel, UI::TradePanel, UI::EventNpcPanel, UI::SecondEventNpcPanel})
        {
            if (panels.IsOpen(panel))
                return {};
        }
    }
    const std::uint32_t branch = type == EventTicket1a1d ? 0x5c6865u : type == EventTicket1c13 ? 0x5c6c36u : 0x5c7010u;
    return Core::Unexpected(std::format(
        "{:#x}: usar el {:#x} de la grilla manda el C1 .. 91, la entrada a un evento, del hito 11", branch, type));
}

Core::Expected<void, std::string> UseTheLaterGridItem(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                      std::size_t index)
{
    WorldState& world = context.world;
    const std::span<ItemState> cells(world.Inventory().cells);
    if (index >= cells.size())
        return Core::Unexpected(std::format("0x5c7bc6: la casilla {} está fuera de la grilla", index));
    const ItemState& hovered = cells[index];
    // 0x5c589e..0x5c58b4: la casilla de arriba a la izquierda, en ESI y en [EBP + 0x1c].
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    if (cell >= static_cast<std::int32_t>(cells.size()))
        return Core::Unexpected(std::format("0x5c58aa: el origen {} está fuera de la grilla (Q-C200)", cell));
    const std::int32_t type = std::bit_cast<std::int16_t>(hovered.id);
    if (IsSkillItem(type))
        return UseTheSkillItem(context, panels, hovered, cell);
    // 0x5c7c1a..0x5c7c2f: también el ítem de esa casilla en la grilla del inventario (0x78a9bb0); desde el inventario,
    // es el mismo.
    if (hovered.id == Fruit && cells[static_cast<std::size_t>(cell)].id == Fruit)
        return UseTheFruit(context, hovered, cell);
    if (hovered.id == Item1a0b)
    {
        const std::uint32_t level = (hovered.scalar1Low >> LevelShift) & LevelMask;
        if (level > 1)
            return {};
        if (level == 1 && world.Actors()[world.Runtime().hero].guildStatus == GuildMaster)
            return {};
        return UseInventoryItemOn(context, panels,
                                  InventoryUse{.slot = cell + FirstGridSlot, .where = Item1a0bRefusal});
    }
    return CopyIntoTheCursor(context, panels, cell);
}
} // namespace Client::Scenes
