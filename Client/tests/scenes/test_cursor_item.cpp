#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"

#include "support/ItemFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

// Levantar, soltar y equipar el ítem del cursor (CL8 hito 4; 06 § El inventario, "El ítem del cursor"): la grilla de
// 8 × 8 con ítems de varios tamaños, el equipo, el piso y MoveHero, sobre un Item.bmd sintético. Los ítems, sus bytes y
// sus lugares son inventados.
namespace
{
using Client::Core::Expected;
using Client::Protocol::Inventory::ItemMove;
using Client::Render::Models::CrtRandom;
using Client::Scenes::EmptyOwnItemId;
using Client::Scenes::InventoryMoveContext;
using Client::Scenes::InventoryServices;
using Client::Scenes::WorldState;
using Client::Tests::ItemTableOf;
using Client::World::ItemState;
namespace Scenes = Client::Scenes;

constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
// Los dos rand() del envío en C3: el relleno y el serial de 0x40b0a0.
constexpr int SendRandoms = 2;
constexpr std::int32_t Left = 0x1c2 + 15;
constexpr std::int32_t Top = 200;

// Los tipos del Item.bmd sintético, con su ancho, su alto y su lugar del equipo (+0x22).
constexpr std::uint16_t Sword = 0x0001;  // 1 × 3, la mano derecha
constexpr std::uint16_t Shield = 0x0c01; // 2 × 2, la izquierda
constexpr std::uint16_t Helm = 0x0e01;   // 2 × 2, la cabeza
constexpr std::uint16_t Potion = 0x1c00; // 1 × 1, se apila
constexpr std::uint16_t Jewel = 0x1c0d;  // 1 × 1, Jewel of Bless: no se tira
constexpr std::uint16_t Box = 0x1a20;    // 1 × 1, avisa con 1915
constexpr std::uint16_t Bundle = 0x181e; // 1 × 1, el paquete de Bless (0x402b30 da 2)
constexpr std::uint16_t Wings = 0x1800;  // 3 × 2, las alas
constexpr std::uint8_t HeadSlot = 2;
constexpr std::uint8_t WingSlot = 7;
constexpr std::uint8_t NotWorn = 0xff;
constexpr std::uint8_t DarkKnight = 1;

// Lo que piden afuera los pasos: cuántas veces se vistió al héroe, los mensajes y los 24 que salieron.
struct Outside
{
    int dressed = 0;
    int ravens = 0;
    std::vector<std::size_t> messages;
    std::vector<ItemMove> sent;
};

class CursorItemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().hud.inventoryX = Left - 15;
        world.Runtime().hud.inventoryY = Top - 200;
        services = InventoryServices{.items = &items,
                                     .dressTheHero =
                                         [this]
                                     {
                                         ++outside.dressed;
                                         return Expected<void, std::string>{};
                                     },
                                     .replaceTheRaven =
                                         [this]
                                     {
                                         ++outside.ravens;
                                         return Expected<void, std::string>{};
                                     },
                                     .systemMessage =
                                         [this](std::size_t index)
                                     {
                                         outside.messages.push_back(index);
                                         return Expected<void, std::string>{};
                                     },
                                     .sendItemMove =
                                         [this](const ItemMove& move)
                                     {
                                         outside.sent.push_back(move);
                                         return Expected<void, std::string>{};
                                     }};
    }

    [[nodiscard]] InventoryMoveContext Moves()
    {
        return InventoryMoveContext{.world = world, .random = random, .services = services};
    }

    // El ítem `type` con su huella desde la casilla (column, row), como lo deja 0x5bc740: la copia en cada casilla,
    // con la columna y la fila del origen, y +0x3c en 1 solo en el origen.
    void Put(std::uint16_t type, std::uint8_t column, std::uint8_t row)
    {
        const auto footprint = Scenes::ItemFootprintOf(&items, type, "Put");
        ASSERT_TRUE(footprint.HasValue()) << footprint.Error();
        for (std::int32_t y = 0; y < footprint.Value().height; ++y)
        {
            for (std::int32_t x = 0; x < footprint.Value().width; ++x)
            {
                ItemState& cell = Cell(column + x, row + y);
                cell = Invented(type);
                cell.gridColumn = column;
                cell.gridRow = row;
                cell.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    [[nodiscard]] static ItemState Invented(std::uint16_t type)
    {
        ItemState item;
        item.id = type;
        item.scalar1Low = 0x0b;
        item.durability = 0x2c;
        item.flags1b = 0x00;
        item.flags1c = 0x00;
        return item;
    }

    ItemState& Cell(std::int32_t column, std::int32_t row)
    {
        return world.Inventory().cells.at(static_cast<std::size_t>(row) * Scenes::InventoryColumns +
                                          static_cast<std::size_t>(column));
    }

    // El mouse sobre la casilla (column, row) para un ítem de `width` × `height`: la fórmula de 0x5ca2af centra la
    // huella en el mouse.
    static std::int32_t MouseX(std::int32_t column, std::int32_t width)
    {
        return Left + column * 20 + width * 10;
    }

    static std::int32_t MouseY(std::int32_t row, std::int32_t height)
    {
        return Top + row * 20 + height * 10;
    }

    [[nodiscard]] static CrtRandom Advanced(CrtRandom from, int count)
    {
        from.Discard(static_cast<std::uint32_t>(count));
        return from;
    }

    WorldState world;
    CrtRandom random{0x13579bdu};
    Client::Data::Tables::ItemTable items = ItemTableOf({{Sword, 1, 3, 0},
                                                         {Shield, 2, 2, 1},
                                                         {Helm, 2, 2, HeadSlot},
                                                         {Potion, 1, 1, NotWorn},
                                                         {Jewel, 1, 1, NotWorn},
                                                         {Box, 1, 1, NotWorn},
                                                         {Bundle, 1, 1, NotWorn},
                                                         {Wings, 3, 2, WingSlot}});
    Outside outside;
    InventoryServices services;
};

// 0x5ca2af..0x5ca2fd: _ftol de (mouse − esquina) · 0.05f − tamaño · 0.5f + 0.5f trunca hacia el 0, así que hasta una
// casilla entera a la izquierda o arriba de la grilla todavía da la columna o la fila 0.
TEST_F(CursorItemTest, TheCellUnderTheMouseTruncatesTowardZero)
{
    const Scenes::ItemFootprint one{.width = 1, .height = 1};
    const Scenes::ItemFootprint tall{.width = 1, .height = 3};
    const Scenes::ItemFootprint wide{.width = 2, .height = 2};
    EXPECT_EQ(Scenes::CellUnderTheMouse(Left + 10, Top + 10, Left, Top, one).column, 0);
    const auto cell = Scenes::CellUnderTheMouse(Left + 3 * 20 + 10, Top + 5 * 20 + 10, Left, Top, one);
    EXPECT_EQ(cell.column, 3);
    EXPECT_EQ(cell.row, 5);
    EXPECT_EQ(Scenes::CellUnderTheMouse(MouseX(4, 1), MouseY(2, 3), Left, Top, tall).row, 2);
    EXPECT_EQ(Scenes::CellUnderTheMouse(MouseX(6, 2), MouseY(6, 2), Left, Top, wide).column, 6);
    EXPECT_EQ(Scenes::CellUnderTheMouse(Left - 5, Top, Left, Top, one).column, 0);
    EXPECT_EQ(Scenes::CellUnderTheMouse(Left - 15, Top, Left, Top, one).column, 0);
    EXPECT_EQ(Scenes::CellUnderTheMouse(Left - 25, Top, Left, Top, one).column, -1);
}

// 0x5c9b40: todas las casillas de la huella dentro de la grilla y vacías; si no, el mismo tipo apilable en la casilla
// de arriba a la izquierda.
TEST_F(CursorItemTest, AnItemFitsWhereItsWholeFootprintIsFree)
{
    Put(Sword, 2, 1);
    Put(Shield, 5, 5);
    Put(Potion, 0, 7);
    const std::span<const ItemState> cells(world.Inventory().cells);
    const auto fits = [&](std::uint16_t type, std::int32_t column, std::int32_t row)
    {
        const auto footprint = Scenes::ItemFootprintOf(&items, type, "test");
        return Scenes::ItemFitsInTheGrid(Invented(type), cells, 8, 8, Scenes::GridCell{.column = column, .row = row},
                                         footprint.Value());
    };
    EXPECT_TRUE(fits(Helm, 0, 0));
    EXPECT_FALSE(fits(Helm, 1, 1));
    EXPECT_FALSE(fits(Helm, 7, 0));
    EXPECT_FALSE(fits(Helm, 6, 6));
    EXPECT_TRUE(fits(Helm, 6, 0));
    EXPECT_FALSE(fits(Sword, 3, 6));
    EXPECT_TRUE(fits(Potion, 0, 7));
    EXPECT_FALSE(fits(Potion, 2, 1));
    EXPECT_TRUE(fits(Wings, 5, 0));
    EXPECT_FALSE(fits(Wings, 6, 0));
}

// 0x5c52b1..0x5c5358: levantar desde cualquier casilla de la huella copia la de arriba a la izquierda al cursor, con el
// lugar 12 + fila · 8 + columna, y 0x5bd750 vacía la huella, sin abrir el registro propio. 0x5be150 la vuelve a
// poner, con +0x3c en 1 en el origen, viste al héroe y pide el sonido 0x1d.
TEST_F(CursorItemTest, PickingUpFromTheGridAndReturningItRestoresTheFootprint)
{
    Put(Sword, 2, 1);
    world.Runtime().movement.pendingClick = true;
    world.Runtime().ui.hoveredItem =
        Client::UI::HoveredItem{.place = Client::UI::HoveredItem::Place::InventoryCell, .index = 2 * 8 + 2};
    const CrtRandom before = random;

    const auto picked = Scenes::PickUpFromTheGrid(Moves(), 2 * 8 + 2);

    ASSERT_TRUE(picked.HasValue()) << picked.Error();
    const Scenes::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Sword);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, 12 + 1 * 8 + 2);
    EXPECT_EQ(runtime.cursorGrid, Scenes::CursorGrid::Inventory);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::UI::HoveredItem::Place::None);
    EXPECT_EQ(runtime.hoveredItemCopy.id, EmptyOwnItemId);
    for (const std::int32_t row : {1, 2, 3})
    {
        EXPECT_EQ(Cell(2, row).id, EmptyOwnItemId) << row;
        EXPECT_EQ(Cell(2, row).occupied, 0u) << row;
    }
    EXPECT_EQ(random.State(), before.State());

    const auto returned = Scenes::ReturnTheCursorItem(Moves());

    ASSERT_TRUE(returned.HasValue()) << returned.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);
    for (const std::int32_t row : {1, 2, 3})
    {
        EXPECT_EQ(Cell(2, row).id, Sword) << row;
        EXPECT_EQ(Cell(2, row).occupied, row == 1 ? 1u : 0u) << row;
    }
    EXPECT_EQ(random.State(), before.State());
}

// 0x5ca250 con el clic sobre un lugar libre: las marcas 2 en la huella, la traba, los dos rand() del envío y el
// C1 0E 24 con el origen, los bytes 0, +4, +0x1a, +0x1b, +0x1c, el tipo >> 5 & 0xf0 y 0, y el destino.
TEST_F(CursorItemTest, DroppingOnAFreePlaceOfTheGridSendsTheMove)
{
    Put(Sword, 2, 1);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    world.Runtime().movement.pendingClick = true;
    const CrtRandom expected = Advanced(random, SendRandoms);

    const auto dropped = Scenes::DropOnTheInventoryGrid(
        Moves(), Scenes::GridDrop{
                     .mouseX = MouseX(6, 1), .mouseY = MouseY(4, 3), .left = Left, .top = Top, .inventoryOpen = true});

    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_EQ(world.Runtime().ui.field78abb39, 1);
    EXPECT_EQ(world.Runtime().itemMoveTarget, 12 + 4 * 8 + 6);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    const ItemMove& move = outside.sent.front();
    EXPECT_EQ(move.sourceFlag, 0);
    EXPECT_EQ(move.sourceSlot, 12 + 1 * 8 + 2);
    EXPECT_EQ(move.targetFlag, 0);
    EXPECT_EQ(move.targetSlot, 12 + 4 * 8 + 6);
    const Client::Protocol::Inventory::ItemData bytes{std::byte{0x01}, std::byte{0x0b}, std::byte{0x2c},
                                                      std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                                      std::byte{0x00}};
    EXPECT_EQ(move.item, bytes);
    for (const std::int32_t row : {4, 5, 6})
        EXPECT_EQ(Cell(6, row).warning, 2) << row;
}

// Sobre otro ítem, la marca 3 y, con el clic, nada; en el mismo lugar, 0x5be150; con el inventario cerrado, el clic
// queda en la grilla sin mandar nada. La traba ya puesta tampoco manda.
TEST_F(CursorItemTest, TheGridRefusesOccupiedPlacesAndTakesItsOwnPlaceBack)
{
    Put(Sword, 2, 1);
    Put(Shield, 5, 5);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    world.Runtime().movement.pendingClick = true;
    const auto drop = [&](std::int32_t column, std::int32_t row, bool open)
    {
        return Scenes::DropOnTheInventoryGrid(Moves(), Scenes::GridDrop{.mouseX = MouseX(column, 1),
                                                                        .mouseY = MouseY(row, 3),
                                                                        .left = Left,
                                                                        .top = Top,
                                                                        .inventoryOpen = open});
    };

    const auto onTheShield = drop(5, 4, true);
    ASSERT_TRUE(onTheShield.HasValue()) << onTheShield.Error();
    EXPECT_FALSE(onTheShield.Value());
    EXPECT_EQ(Cell(5, 5).warning, 3);
    EXPECT_EQ(Cell(5, 4).warning, 3);

    const auto closed = drop(0, 0, false);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_TRUE(closed.Value());
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);

    const auto back = drop(2, 1, true);
    ASSERT_TRUE(back.HasValue()) << back.Error();
    EXPECT_TRUE(back.Value());
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(2, 1).id, Sword);
}

// 0x5bee04..0x5bf2a0: el casco va en la cabeza (la marca 2 y, con el clic, el 24 al lugar 2) y no en la mano derecha
// (la marca 3 y, con el clic, 0x5be150). El arma de una mano soltada en la izquierda con la derecha vacía va a la 0.
TEST_F(CursorItemTest, TheEquipmentTakesWhatGoesThere)
{
    world.Actors()[0].characterClass = DarkKnight;
    Put(Helm, 0, 0);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());

    ASSERT_TRUE(Scenes::DropOnTheEquipment(Moves(), 0).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[0].warning, 3);
    ASSERT_TRUE(Scenes::DropOnTheEquipment(Moves(), HeadSlot).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[HeadSlot].warning, 2);
    EXPECT_TRUE(outside.sent.empty());

    world.Runtime().movement.pendingClick = true;
    const CrtRandom expected = Advanced(random, SendRandoms);
    ASSERT_TRUE(Scenes::DropOnTheEquipment(Moves(), HeadSlot).HasValue());
    EXPECT_FALSE(world.Runtime().movement.pendingClick);
    EXPECT_EQ(world.Runtime().ui.field78abaac, 1);
    EXPECT_EQ(world.Runtime().itemMoveTarget, HeadSlot);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12);
    EXPECT_EQ(outside.sent.front().targetSlot, HeadSlot);

    world.Runtime().ui.field78abb39 = 0;
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::DropOnTheEquipment(Moves(), 0).HasValue());
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(0, 0).id, Helm);
    EXPECT_EQ(outside.sent.size(), 1u);

    Put(Sword, 4, 0);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 4).HasValue());
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::DropOnTheEquipment(Moves(), 1).HasValue());
    EXPECT_EQ(world.Runtime().itemMoveTarget, 0);
    ASSERT_EQ(outside.sent.size(), 2u);
    EXPECT_EQ(outside.sent.back().targetSlot, 0);
}

// 0x5c2c65..0x5c2dd0: levantar del equipo deja el lugar en 0xffff (con +0x3c como estaba), viste al héroe y pide el
// sonido. En Icarus, las únicas alas no se sacan.
TEST_F(CursorItemTest, PickingUpFromTheEquipmentEmptiesThePlace)
{
    ItemState& helm = world.OwnCharacter().equipment[HeadSlot];
    helm = Invented(Helm);
    helm.occupied = 1;
    world.Runtime().movement.pendingClick = true;

    ASSERT_TRUE(Scenes::PickUpFromTheEquipment(Moves(), HeadSlot).HasValue());

    const Scenes::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Helm);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, HeadSlot);
    EXPECT_EQ(helm.id, EmptyOwnItemId);
    EXPECT_EQ(helm.occupied, 1u);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);
    EXPECT_FALSE(runtime.movement.pendingClick);

    world.Runtime().cursorItemCount = 0;
    world.Runtime().map = 10;
    world.OwnCharacter().equipment[WingSlot] = Invented(Wings);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheEquipment(Moves(), WingSlot).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[WingSlot].id, Wings);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
}

// 0x5c35ad: la marca 3 si falta un requisito (+0x26 contra la fuerza, Q-C475) y 2 si no, el ítem del cartel y la
// esquina de su ayuda en el medio del lugar.
TEST_F(CursorItemTest, HoveringTheEquipmentMarksWhatTheHeroCannotUse)
{
    ItemState& helm = world.OwnCharacter().equipment[HeadSlot];
    helm = Invented(Helm);
    helm.word26 = 20;
    world.OwnCharacter().strength = 10;

    Scenes::HoverTheEquipment(Moves(), HeadSlot, 300, 46, 40.0f);

    EXPECT_EQ(helm.warning, 3);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::Equipment);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.index, HeadSlot);
    EXPECT_EQ(world.Runtime().hoveredItemCopy.id, Helm);
    EXPECT_EQ(world.Runtime().ui.skillTooltipX, 320);
    EXPECT_EQ(world.Runtime().ui.skillTooltipY, 46);

    world.OwnCharacter().strength = 20;
    Scenes::HoverTheEquipment(Moves(), HeadSlot, 300, 46, 40.0f);
    EXPECT_EQ(helm.warning, 2);
}

// 0x5df449..0x5df58b: lo que no se tira avisa con 269 y vuelve; las cajas, con 1915; los paquetes de joyas, con 269
// salvo con el inventario abierto y el mouse a su derecha. Lo demás corta donde empieza a tirarlo (CL9.3).
TEST_F(CursorItemTest, WhatCannotBeDroppedGoesBackWithItsWarning)
{
    const auto hold = [&](std::uint16_t type)
    {
        Put(type, 0, 0);
        world.Runtime().movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
    };
    const auto ground = [&](std::int32_t mouseX, bool open)
    { return Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = mouseX, .inventoryOpen = open}); };

    hold(Jewel);
    ASSERT_TRUE(ground(100, false).HasValue());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{269});
    EXPECT_EQ(Cell(0, 0).id, Jewel);

    hold(Box);
    ASSERT_TRUE(ground(100, false).HasValue());
    EXPECT_EQ(outside.messages.back(), 1915u);

    hold(Bundle);
    ASSERT_TRUE(ground(Left, true).HasValue());
    EXPECT_EQ(outside.messages.size(), 2u);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    hold(Bundle);
    ASSERT_TRUE(ground(Left - 15, true).HasValue());
    EXPECT_EQ(outside.messages.size(), 3u);
    EXPECT_EQ(outside.messages.back(), 269u);

    hold(Shield);
    const auto dropped = ground(100, false);
    ASSERT_FALSE(dropped.HasValue());
    EXPECT_TRUE(dropped.Error().starts_with("0x5df58b:")) << dropped.Error();
}

// Sin el envío, el 24 corta después de la traba y de sus dos rand(); sin Item.bmd, levantar corta antes de tocar nada.
TEST_F(CursorItemTest, WithoutItsServicesAMoveStopsWithItsAddress)
{
    Put(Sword, 2, 1);
    services.sendItemMove = {};
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    const CrtRandom expected = Advanced(random, SendRandoms);
    const auto sent = Scenes::SendTheCursorItem(Moves(), 30);
    ASSERT_FALSE(sent.HasValue());
    EXPECT_TRUE(sent.Error().starts_with("0x449d80")) << sent.Error();
    EXPECT_EQ(random.State(), expected.State());

    services.items = nullptr;
    world.Runtime().cursorItemCount = 0;
    const auto returned = Scenes::DropOnTheInventoryGrid(
        Moves(), Scenes::GridDrop{.mouseX = Left, .mouseY = Top, .left = Left, .top = Top, .inventoryOpen = true});
    ASSERT_FALSE(returned.HasValue());
    EXPECT_NE(returned.Error().find("0x7758550"), std::string::npos) << returned.Error();
}
} // namespace
