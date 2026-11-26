#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Pathfinding/PathCell.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Models/LogicalAccess.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Levantar, soltar y equipar el ítem del cursor (CL8 hito 4; 06 § El inventario, "El ítem del cursor"): la grilla de
// 8 × 8 con ítems de varios tamaños, el equipo, el piso y MoveHero, sobre un Item.bmd sintético. Los ítems, sus bytes y
// sus lugares son inventados.
namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Network::Xor32Decode;
using Client::Pathfinding::PathCell;
using Client::Protocol::Items::Drop;
using Client::Protocol::Items::ItemMove;
using Client::Scenes::EmptyOwnItemId;
using Client::Scenes::InventoryMoveContext;
using Client::Scenes::InventoryServices;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::ItemTableOf;
using Client::Tests::SameBytes;
using Client::World::ItemState;
namespace Scenes = Client::Scenes;

constexpr int Own = Client::Models::OwnRecordCloseRandoms;
// Los dos rand() del envío en C3: el relleno y el serial de 0x40b0a0.
constexpr int SendRandoms = 2;
constexpr std::int32_t Left = 0x1c2 + 15;
constexpr std::int32_t Top = 200;

// Los tipos del Item.bmd sintético, con su ancho, su alto y su lugar del equipo (+0x22).
constexpr std::uint16_t Sword = 0x0001;    // 1 × 3, la mano derecha
constexpr std::uint16_t Shield = 0x0c01;   // 2 × 2, la izquierda
constexpr std::uint16_t Helm = 0x0e01;     // 2 × 2, la cabeza
constexpr std::uint16_t Potion = 0x1c00;   // 1 × 1, se apila
constexpr std::uint16_t Jewel = 0x1c0d;    // 1 × 1, Jewel of Bless: no se tira
constexpr std::uint16_t Box = 0x1a20;      // 1 × 1, avisa con 1915
constexpr std::uint16_t Bundle = 0x181e;   // 1 × 1, el paquete de Bless (0x402b30 da 2)
constexpr std::uint16_t Wings = 0x1800;    // 3 × 2, las alas
constexpr std::uint16_t Bow = 0x0801;      // 2 × 4, la izquierda: tira flechas
constexpr std::uint16_t Crossbow = 0x0808; // 2 × 4, la derecha: tira virotes
constexpr std::uint16_t Bolts = 0x0807;    // 1 × 1
constexpr std::uint16_t Arrows = 0x080f;   // 1 × 1
// 1 × 1, la derecha: el Dark Raven con 32 tipos por grupo, que 0x5bf03f compara (en el Item.bmd real, vacío).
constexpr std::uint16_t OldDarkRaven = 0x01a5;
constexpr std::uint16_t EntryTicket = 0x1c1c; // 1 × 1, el mapa que mira 0x4379f0.
constexpr std::uint16_t WizardsRing = 0x1a14; // 1 × 1, el primer anillo
constexpr std::uint8_t HeadSlot = 2;
constexpr std::uint8_t WingSlot = 7;
constexpr std::uint8_t NotWorn = 0xff;
constexpr std::uint8_t DarkKnight = 1;
constexpr std::uint8_t Elf = 2;
// Los textos de 0x561840: las flechas que se equipan (con el tipo 1) y las que se acabaron (con el 2).
constexpr std::size_t EquippedArrowsText = 250;
constexpr std::size_t NoArrowsText = 251;

// Lo que piden afuera los pasos: cuántas veces se vistió al héroe, los mensajes y los 24 que salieron.
struct Outside
{
    int dressed = 0;
    int ravens = 0;
    std::vector<std::size_t> messages;
    std::vector<std::int32_t> messageTypes;
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
                                         [this](std::size_t index, std::int32_t type)
                                     {
                                         outside.messages.push_back(index);
                                         outside.messageTypes.push_back(type);
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
    Client::Tables::ItemTable items = ItemTableOf({{Sword, 1, 3, 0},
                                                   {Shield, 2, 2, 1},
                                                   {Helm, 2, 2, HeadSlot},
                                                   {Potion, 1, 1, NotWorn},
                                                   {Jewel, 1, 1, NotWorn},
                                                   {Box, 1, 1, NotWorn},
                                                   {Bundle, 1, 1, NotWorn},
                                                   {Wings, 3, 2, WingSlot},
                                                   {Bow, 2, 4, 1},
                                                   {Crossbow, 2, 4, 0},
                                                   {Bolts, 1, 1, 1},
                                                   {Arrows, 1, 1, 0},
                                                   {OldDarkRaven, 1, 1, 0},
                                                   {WizardsRing, 1, 1, 10},
                                                   {EntryTicket, 1, 1, NotWorn}});
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

// La caja del entrenador (0x723c80), de 8 × 4 en (0x113, 0x6e) con 0x724580 en 3 (0x44ad10 y 0x44aca0).
constexpr std::int32_t TrainerLeft = 0x113;
constexpr std::int32_t TrainerTop = 0x6e;

// 0x5ca250 con el tipo 5 (0x44aca9): con 0x724580 en 3, 0x44acd0 solo deja la joya del caos, las de la mezcla y 0x1a1f;
// la joya entra (marcas 2) y, con el clic, manda 0x449d80(0, 12, 5, fila · 8 + columna) con los dos rand() del envío.
// El arma no entra (marcas 3, sin 0x5c9b40) y no manda nada. En Lorencia y Devias.
TEST_F(CursorItemTest, TheTrainerBoxTakesOnlyItsJewels)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.panels.delegated.trainer = 3;
        runtime.npcWindows.trainerBox = Scenes::EmptyTrainerBox();
        const auto drop = [&](std::int32_t column, std::int32_t row)
        {
            return Scenes::DropOnOtherGrid(Moves(), Scenes::OtherGridDrop{.mouseX = TrainerLeft + column * 20 + 10,
                                                                          .mouseY = TrainerTop + row * 20 + 10,
                                                                          .left = TrainerLeft,
                                                                          .top = TrainerTop,
                                                                          .grid = Scenes::CursorGrid::TrainerBox});
        };

        Put(Jewel, 0, 0);
        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
        runtime.movement.pendingClick = true;
        const CrtRandom sent = Advanced(random, SendRandoms);
        const auto dropped = drop(3, 1);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), sent.State());
        EXPECT_EQ(runtime.npcWindows.trainerBox[11].warning, 2);
        EXPECT_EQ(runtime.itemMoveTarget, 11);
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 0);
        EXPECT_EQ(outside.sent[0].sourceSlot, 12);
        EXPECT_EQ(outside.sent[0].targetFlag, 5);
        EXPECT_EQ(outside.sent[0].targetSlot, 11);

        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        Put(Sword, 2, 0);
        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 2).HasValue());
        runtime.movement.pendingClick = true;
        const CrtRandom refused = random;
        const auto kept = drop(5, 0);
        ASSERT_TRUE(kept.HasValue()) << kept.Error();
        EXPECT_FALSE(kept.Value());
        EXPECT_EQ(random.State(), refused.State());
        EXPECT_EQ(outside.sent.size(), 1u);
        for (const std::size_t cell : {5u, 13u, 21u})
            EXPECT_EQ(runtime.npcWindows.trainerBox[cell].warning, 3) << cell;
    }
}

// Levantar de la caja del entrenador (0x44ad10 → 0x5c4110): la copia al cursor desde su esquina (el lugar sin el 12)
// y la huella vacía; 0x5be150 la vuelve con 0x449c80. Soltarla en el inventario manda 0x449d50, 0x449d80(5, lugar, 0,
// destino). En Lorencia y Devias.
TEST_F(CursorItemTest, TheTrainerBoxItemGoesBackOrToTheInventory)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.delegated.trainer = 3;
        auto& box = runtime.npcWindows.trainerBox;
        box = Scenes::EmptyTrainerBox();
        for (const std::size_t cell : {10u, 18u, 26u})
        {
            box[cell] = Invented(Sword);
            box[cell].gridColumn = 2;
            box[cell].gridRow = 1;
            box[cell].occupied = cell == 10 ? 1u : 0u;
        }
        const CrtRandom before = random;
        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromOtherGrid(Moves(), Scenes::CursorGrid::TrainerBox, 18).HasValue());
        EXPECT_EQ(runtime.cursorGrid, Scenes::CursorGrid::TrainerBox);
        EXPECT_EQ(runtime.cursorItem.id, Sword);
        EXPECT_EQ(runtime.slot78a6758, 10);
        for (const std::size_t cell : {10u, 18u, 26u})
            EXPECT_EQ(box[cell].id, EmptyOwnItemId) << cell;

        ASSERT_TRUE(Scenes::ReturnTheCursorItem(Moves()).HasValue());
        EXPECT_EQ(runtime.cursorItemCount, 0);
        for (const std::size_t cell : {10u, 18u, 26u})
        {
            EXPECT_EQ(box[cell].id, Sword) << cell;
            EXPECT_EQ(box[cell].occupied, cell == 10 ? 1u : 0u) << cell;
        }
        EXPECT_EQ(random.State(), before.State());

        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromOtherGrid(Moves(), Scenes::CursorGrid::TrainerBox, 10).HasValue());
        runtime.movement.pendingClick = true;
        const CrtRandom sent = Advanced(random, SendRandoms);
        const auto dropped = Scenes::DropOnTheInventoryGrid(
            Moves(),
            Scenes::GridDrop{
                .mouseX = MouseX(4, 1), .mouseY = MouseY(2, 3), .left = Left, .top = Top, .inventoryOpen = true});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 5);
        EXPECT_EQ(outside.sent[0].sourceSlot, 10);
        EXPECT_EQ(outside.sent[0].targetFlag, 0);
        EXPECT_EQ(outside.sent[0].targetSlot, 12 + 2 * 8 + 4);
    }
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
    const Client::Protocol::Items::ItemData bytes{std::byte{0x01}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0x00},
                                                  std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
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

// 0x5c007c..0x5c1d8a, en Lorencia, Devias y Noria: el ítem de otra grilla al equipo manda el 24 desde su grilla con su
// lugar sin el 12 y los dos rand() del envío: el casco del almacén a la cabeza (2), la espada de la caja del caos a la
// derecha (3) y el escudo de la caja del entrenador a la izquierda (0x449d50 en 0x5c1d80, el 5). Con la traba del
// almacén, el movimiento espera la contraseña (2, lugar, 0, destino), el teclado se baraja (40 rand()), 0x78abb20 en
// 1 y lo tecleado en 0, sin mandar nada. Las demás grillas usan el 1 y esperan el OK antes de la traba del envío.
TEST_F(CursorItemTest, TheItemOfAnotherGridGoesToTheEquipment)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.delegated.trainer = 3;
        world.Actors()[0].characterClass = DarkKnight;
        Scenes::NpcWindowState& npc = runtime.npcWindows;
        const auto place = [](std::span<ItemState> cells, std::uint16_t type, std::initializer_list<std::size_t> at)
        {
            for (const std::size_t cell : at)
            {
                cells[cell] = Invented(type);
                cells[cell].gridColumn = static_cast<std::uint8_t>(*at.begin() % 8);
                cells[cell].gridRow = static_cast<std::uint8_t>(*at.begin() / 8);
                cells[cell].occupied = cell == *at.begin() ? 1u : 0u;
            }
        };
        const auto pickUp = [&](Scenes::CursorGrid grid, std::size_t cell)
        {
            runtime.ui.field78abb39 = 0;
            runtime.cursorItemCount = 0;
            runtime.movement.pendingClick = true;
            ASSERT_TRUE(Scenes::PickUpFromOtherGrid(Moves(), grid, cell).HasValue());
            EXPECT_EQ(runtime.cursorGrid, grid);
            runtime.movement.pendingClick = true;
        };

        place(npc.grid, Helm, {9, 10, 17, 18});
        pickUp(Scenes::CursorGrid::NpcGrid, 9);
        CrtRandom sent = Advanced(random, SendRandoms);
        auto dropped = Scenes::DropOnTheEquipment(Moves(), HeadSlot);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        EXPECT_EQ(runtime.ui.field78abaac, 1);
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 2);
        EXPECT_EQ(outside.sent[0].sourceSlot, 9);
        EXPECT_EQ(outside.sent[0].item[0], std::byte{0x01});
        EXPECT_EQ(outside.sent[0].item[5], std::byte{0x70});
        EXPECT_EQ(outside.sent[0].targetFlag, 0);
        EXPECT_EQ(outside.sent[0].targetSlot, HeadSlot);

        // Con la traba del almacén, la contraseña: no mira la traba del envío ni manda.
        npc.warehouseLocked = 1;
        npc.field78abb1c = 0;
        npc.typedNumber.fill(0x37);
        runtime.movement.pendingClick = true;
        CrtRandom shuffled = random;
        std::array<std::uint16_t, Scenes::WarehouseKeyCount> keys{};
        Scenes::ShuffleTheKeypad(shuffled, keys);
        dropped = Scenes::DropOnTheEquipment(Moves(), HeadSlot);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), shuffled.State());
        EXPECT_EQ(random.State(), Advanced(sent, 40).State());
        EXPECT_EQ(npc.keypad, keys);
        EXPECT_EQ(npc.pendingMove.slot, 2);
        EXPECT_EQ(npc.pendingMove.second, 9);
        EXPECT_EQ(npc.pendingMove.third, 0);
        EXPECT_EQ(npc.pendingMove.fourth, HeadSlot);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_EQ(npc.typedNumber, (std::array<std::uint8_t, 11>{}));
        EXPECT_EQ(outside.sent.size(), 1u);
        npc.warehouseLocked = 0;
        runtime.ui.field78abb20 = 0;

        place(npc.chaosBox, Sword, {3, 11, 19});
        pickUp(Scenes::CursorGrid::ChaosBox, 11);
        sent = Advanced(random, SendRandoms);
        dropped = Scenes::DropOnTheEquipment(Moves(), 0);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 2u);
        EXPECT_EQ(outside.sent[1].sourceFlag, 3);
        EXPECT_EQ(outside.sent[1].sourceSlot, 3);
        EXPECT_EQ(outside.sent[1].targetFlag, 0);
        EXPECT_EQ(outside.sent[1].targetSlot, 0);

        runtime.npcWindows.trainerBox = Scenes::EmptyTrainerBox();
        place(runtime.npcWindows.trainerBox, Shield, {0, 1, 8, 9});
        pickUp(Scenes::CursorGrid::TrainerBox, 8);
        sent = Advanced(random, SendRandoms);
        dropped = Scenes::DropOnTheEquipment(Moves(), 1);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 3u);
        EXPECT_EQ(outside.sent[2].sourceFlag, 5);
        EXPECT_EQ(outside.sent[2].sourceSlot, 0);
        EXPECT_EQ(outside.sent[2].targetFlag, 0);
        EXPECT_EQ(outside.sent[2].targetSlot, 1);

        runtime.cursorGrid = Scenes::CursorGrid::None;
        runtime.ui.field78abb39 = 0;
        runtime.movement.pendingClick = true;
        const CrtRandom before = random;
        const auto droppedOther = Scenes::DropOnTheEquipment(Moves(), 1);
        ASSERT_TRUE(droppedOther.HasValue()) << droppedOther.Error();
        EXPECT_EQ(random.State(), Advanced(before, SendRandoms).State());
        ASSERT_EQ(outside.sent.size(), 4u);
        EXPECT_EQ(outside.sent.back().sourceFlag, 1);
        EXPECT_EQ(runtime.social.trade.okWait, 150);
    }
}

// 0x5c1d90: el fallback también cubre la tienda personal; pone la espera ANTES de probar 0x78abb39.
TEST_F(CursorItemTest, TradeAndShopItemsUseTheTradeSourceWhenDroppedOnEquipment)
{
    for (const auto grid : {Scenes::CursorGrid::OwnTrade, Scenes::CursorGrid::PersonalShop})
    {
        for (const std::uint8_t ownOk : std::array<std::uint8_t, 2>{0, 1})
        {
            for (const std::uint8_t locked : std::array<std::uint8_t, 2>{0, 1})
            {
                Scenes::WorldRuntime& runtime = world.Runtime();
                runtime.cursorGrid = grid;
                runtime.cursorItem = Invented(Helm);
                runtime.slot78a6758 = 0x109;
                runtime.cursorItemCount = 1;
                runtime.ui.field78abb39 = locked;
                runtime.movement.pendingClick = true;
                runtime.social.trade.ownOk = ownOk;
                runtime.social.trade.okWait = 7;
                outside.sent.clear();
                services.sendItemMove = [&](const Client::Protocol::Items::ItemMove& move)
                {
                    EXPECT_EQ(runtime.social.trade.okWait, ownOk == 0 ? 150 : 7);
                    outside.sent.push_back(move);
                    return Expected<void, std::string>{};
                };
                const CrtRandom before = random;
                const auto dropped = Scenes::DropOnTheEquipment(Moves(), HeadSlot);
                ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
                EXPECT_EQ(runtime.social.trade.okWait, ownOk == 0 ? 150 : 7);
                EXPECT_EQ(random.State(), Advanced(before, locked == 0 ? SendRandoms : 0).State());
                EXPECT_EQ(runtime.ui.field78abb39, 1);
                if (locked != 0)
                {
                    EXPECT_TRUE(outside.sent.empty());
                    continue;
                }
                ASSERT_EQ(outside.sent.size(), 1u);
                const auto& move = outside.sent.front();
                EXPECT_EQ(move.sourceFlag, 1);
                EXPECT_EQ(move.sourceSlot, 9);
                EXPECT_EQ(move.targetFlag, 0);
                EXPECT_EQ(move.targetSlot, HeadSlot);
                EXPECT_EQ(move.item, (decltype(move.item){std::byte{1}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0},
                                                          std::byte{0}, std::byte{0x70}, std::byte{0}}));
            }
        }
    }
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

// 0x5bf013..0x5bf04a: el 0x1a5 pide de liderazgo el nivel del cuervo ([0x723968]) · 15 + 0xb9, en 16 bits que dan
// la vuelta, contra +0x20 sin agregados (Q-C775).
TEST_F(CursorItemTest, TheOldDarkRavenAsksForTheLeadershipOfTheRavenLevel)
{
    world.Actors()[0].characterClass = DarkKnight;
    Scenes::WorldRuntime& runtime = world.Runtime();
    runtime.cursorItem = Invented(OldDarkRaven);
    runtime.cursorItemCount = 1;
    Scenes::OwnCharacterRecord& own = world.OwnCharacter();
    const auto mark = [&]
    {
        const auto dropped = Scenes::DropOnTheEquipment(Moves(), 0);
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        return own.equipment[0].warning;
    };

    own.leadership = 0xb8;
    EXPECT_EQ(mark(), 3);
    own.leadership = 0xb9;
    EXPECT_EQ(mark(), 2);
    runtime.pets[Client::Scenes::RavenPetStats].level = 2;
    EXPECT_EQ(mark(), 3);
    own.leadership = 0xb9 + 30;
    EXPECT_EQ(mark(), 2);
    own.word36 = 100;
    own.leadership = 0xb9 + 29;
    EXPECT_EQ(mark(), 3);
    // 4.357 · 15 + 0xb9 = 0x10004: queda 4.
    runtime.pets[Client::Scenes::RavenPetStats].level = 4357;
    own.leadership = 4;
    EXPECT_EQ(mark(), 2);
}

// 0x5bf0f6..0x5bf185: with 0x435a00 (0x723c05 in 3 or 4) and the hero's 0x200000 in +0x7c, the horn does not go on the
// pet's slot and the Dark Raven does not go on the left hand. The items and their shapes are invented.
TEST_F(CursorItemTest, TheCrywolfLockNeedsTheStageThreeOrFourAndTheHerosFlag)
{
    constexpr std::uint16_t Horn = 0x1a03;
    constexpr std::uint16_t Raven = 0x1a05;
    constexpr std::size_t PetSlot = 8;
    constexpr std::size_t LeftHand = 1;
    constexpr std::uint32_t LockFlag = 0x200000;
    constexpr std::uint32_t EveryOtherFlag = ~LockFlag;
    constexpr std::int32_t Fits = 2;
    constexpr std::int32_t DoesNotFit = 3;
    items = ItemTableOf({{Horn, 1, 1, std::uint8_t{PetSlot}}, {Raven, 1, 1, std::uint8_t{LeftHand}}});
    world.Actors()[0].characterClass = DarkKnight;
    Scenes::WorldRuntime& runtime = world.Runtime();
    Scenes::OwnCharacterRecord& own = world.OwnCharacter();
    const auto mark = [&](std::uint16_t type, std::size_t slot, std::uint8_t stage, std::uint32_t flags)
    {
        runtime.cursorItem = Invented(type);
        runtime.cursorItemCount = 1;
        runtime.crywolf.stage = stage;
        world.Actors()[0].flags7c = flags;
        const auto dropped = Scenes::DropOnTheEquipment(Moves(), slot);
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        return std::int32_t{own.equipment[slot].warning};
    };

    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{3, 4})
    {
        EXPECT_EQ(mark(Horn, PetSlot, stage, LockFlag), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, LockFlag), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Horn, PetSlot, stage, 0xffffffffu), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Horn, PetSlot, stage, EveryOtherFlag), Fits) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, 0), Fits) << "stage " << int{stage};
    }
    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{0, 1, 2, 5, 6, 0xff})
    {
        EXPECT_EQ(mark(Horn, PetSlot, stage, LockFlag), Fits) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, LockFlag), Fits) << "stage " << int{stage};
    }
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

    // Lo demás sigue a las ventanas (0x5df58b), que piden el registro de los paneles.
    hold(Shield);
    const auto dropped = ground(100, false);
    ASSERT_FALSE(dropped.HasValue());
    EXPECT_TRUE(dropped.Error().starts_with("0x5df59a:")) << dropped.Error();
}

// 0x5df58b..0x5e069d (CL9 hito 5): sin ventanas sobre el mouse, el anillo (el registro propio), IsOpen(0x20), la celda
// del terreno bajo el mouse y el C1 06 23 con la celda y el lugar de donde salió, en C3; después [0x6c82ec] con ese
// lugar y la espera del mouse en 6.
TEST_F(CursorItemTest, TheDropSendsTheCellAndTheSlotByteByByte)
{
    Put(Shield, 2, 1);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 1 * 8 + 2).HasValue());
    ASSERT_EQ(world.Runtime().slot78a6758, 12 + 10);
    std::vector<Drop> sent;
    services.sendDrop = [&sent](const Drop& drop)
    {
        sent.push_back(drop);
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
    Scenes::GamePanels panels(world, random);
    // IsOpen(0x10000) y IsOpen(0x20) de las ventanas, el registro propio, IsOpen(0x20) otra vez y el envío.
    const CrtRandom expected = Advanced(random, 1 + 1 + Own + 1 + SendRandoms);
    world.Runtime().movement.mouseUpdateTime = 3;

    const auto dropped = Scenes::DropOnTheGround(
        Moves(), Scenes::GroundDrop{.mouseX = 100, .inventoryOpen = false, .panels = &panels.Registry()});

    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    const auto packet = Client::Protocol::Items::WriteDrop(sent.front());
    EXPECT_TRUE(SameBytes(Xor32Decode(packet.packet).Bytes(), Bytes({0xc1, 0x06, 0x23, 130, 121, 22})));
    EXPECT_EQ(world.Runtime().floor.pendingDrop, 22);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTime, 0);
    EXPECT_EQ(world.Runtime().movement.mouseUpdateTimeMax, 6);
    // El ítem queda en el cursor hasta el 23.
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_EQ(Cell(2, 1).id, EmptyOwnItemId);
}

// 0x5dfdfc: sin terreno bajo el mouse no sale nada y el ítem sigue en el cursor. 0x5df58b: con el inventario abierto y
// el mouse en él, vuelve a su lugar.
TEST_F(CursorItemTest, WithoutGroundOrOverTheInventoryNothingIsDropped)
{
    Put(Potion, 0, 0);
    world.Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
    int sent = 0;
    services.sendDrop = [&sent](const Drop&)
    {
        ++sent;
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(std::nullopt); };
    Scenes::GamePanels panels(world, random);
    const CrtRandom expected = Advanced(random, 1 + 1 + Own + 1);
    ASSERT_TRUE(
        Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = 100, .panels = &panels.Registry()}).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(sent, 0);
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_EQ(world.Runtime().floor.pendingDrop, -1);

    ASSERT_TRUE(
        Scenes::DropOnTheGround(
            Moves(), Scenes::GroundDrop{.mouseX = Left - 15, .inventoryOpen = true, .panels = &panels.Registry()})
            .HasValue());
    EXPECT_EQ(sent, 0);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(0, 0).id, Potion);
}

// 0x5df651..0x5df671: con el comercio abierto, el mouse desde la x de su ventana (0x78a5a1c, la de su dibujo en 0x104)
// no deja tirar; antes de ella sí, pero 0x5dfda8 vuelve a mirar el comercio (otro rand()). Las dos vuelven el ítem a su
// lugar (0x5be150) sin mandar nada: la de la ventana se ahorra esa consulta.
TEST_F(CursorItemTest, WithTheTradeOpenTheItemGoesBackWithOrWithoutTheWindow)
{
    int sent = 0;
    services.sendDrop = [&sent](const Drop&)
    {
        ++sent;
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
    world.Runtime().panels.trade = 1;
    world.Runtime().hud.tradeX = 0x104;
    Scenes::GamePanels panels(world, random);
    const auto drawsOf = [this](std::int32_t mouseX, Scenes::GamePanels& registry)
    {
        Put(Potion, 0, 0);
        world.Runtime().movement.pendingClick = true;
        EXPECT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
        const CrtRandom start = random;
        const auto dropped =
            Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = mouseX, .panels = &registry.Registry()});
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(Cell(0, 0).id, Potion);
        CrtRandom probe = start;
        for (int count = 0; count < 2 * Own; ++count)
        {
            if (probe.State() == random.State())
                return count;
            static_cast<void>(probe.Next());
        }
        return -1;
    };
    const int over = drawsOf(0x104, panels);
    const int before = drawsOf(0x103, panels);
    EXPECT_EQ(sent, 0);
    EXPECT_GE(over, 1 + 1 + Own);
    EXPECT_EQ(before, over + 1);
}

// 0x5df6c8..0x5dfa52: el anillo del mago de nivel 2 pide el nivel 80 (el aviso 923 con el número, del tipo 1) y vuelve;
// el de nivel 3 vuelve sin aviso.
TEST_F(CursorItemTest, TheWizardsRingAsksForItsLevelBeforeBeingDropped)
{
    const auto texts = Client::Tests::MakeTextTable({{923, "hace falta el nivel %d"}});
    services.texts = &texts;
    std::vector<std::string> lines;
    services.chatLine = [&lines](std::string_view line, std::int32_t type)
    {
        EXPECT_EQ(type, 1);
        lines.emplace_back(line);
        return Expected<void, std::string>{};
    };
    world.OwnCharacter().level = 50;
    Scenes::GamePanels panels(world, random);
    for (const std::uint32_t level : {2u, 3u})
    {
        Put(WizardsRing, 0, 0);
        Cell(0, 0).scalar1Low = level << 3u;
        world.Runtime().movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
        ASSERT_TRUE(Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = 100, .panels = &panels.Registry()})
                        .HasValue());
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(Cell(0, 0).id, WizardsRing);
    }
    EXPECT_EQ(lines, std::vector<std::string>{"hace falta el nivel 80"});
}

// 0x5df5cf..0x5df6c4: cada ventana lee la x retenida del dibujo, con comparación signed y sin cortar la UI.
TEST_F(CursorItemTest, NpcWindowsKeepTheDropOnlyOnTheirSideOfTheScreen)
{
    for (const int kind : {0, 1, 2, 3})
    {
        for (const std::int32_t mouseX : {259, 260})
        {
            auto& runtime = world.Runtime();
            runtime.panels.npcShop = kind == 0 ? 1 : 0;
            runtime.panels.warehouse = kind == 1 ? 1 : 0;
            runtime.panels.chaosBox = kind == 2 ? 1 : 0;
            runtime.panels.delegated.trainer = kind == 3 ? 3 : 0;
            runtime.ui.npcWindowX = 260;
            Put(Potion, 0, 0);
            runtime.movement.pendingClick = true;
            ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
            std::vector<Drop> sent;
            services.sendDrop = [&sent](const Drop& drop)
            {
                sent.push_back(drop);
                return Expected<void, std::string>{};
            };
            services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
            Scenes::GamePanels panels(world, random);
            const auto dropped =
                Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = mouseX, .panels = &panels.Registry()});
            ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
            EXPECT_EQ(sent.size(), mouseX < 260 ? 1u : 0u);
            EXPECT_EQ(runtime.cursorItemCount, mouseX < 260 ? 1 : 0);
        }
    }
}

// 0x5df635→0x6689d0: sólo modo 3, marca WORD no negativa y maestro 0x80 retienen a la derecha del NPC.
TEST_F(CursorItemTest, TheSeniorCraftWindowKeepsTheDropWithItsNativeSignedGuildMark)
{
    struct Case
    {
        std::uint16_t mark;
        std::uint8_t guildStatus;
        std::int32_t mode;
        std::int32_t mouseX;
        bool kept;
    };
    constexpr std::array<Case, 7> cases{{{0, 0x80, 3, 260, true},
                                         {0x7fff, 0x80, 3, 260, true},
                                         {0x8000, 0x80, 3, 260, false},
                                         {0xffff, 0x80, 3, 260, false},
                                         {0, 0x7f, 3, 260, false},
                                         {0, 0x80, 2, 260, false},
                                         {0, 0x80, 3, 259, false}}};
    for (const Case& given : cases)
    {
        auto& runtime = world.Runtime();
        runtime.cursorItemCount = 0;
        runtime.ui.field78abb39 = 0;
        runtime.panels.delegated.castleSiegeList = 1;
        runtime.panels.delegated.castleSiegeListMode = given.mode;
        runtime.ui.npcWindowX = 260;
        auto& hero = world.Actors()[runtime.hero];
        hero.word1f2 = given.mark;
        hero.guildStatus = given.guildStatus;
        Put(Potion, 0, 0);
        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
        std::vector<Drop> sent;
        services.sendDrop = [&sent](const Drop& drop)
        {
            sent.push_back(drop);
            return Expected<void, std::string>{};
        };
        services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
        Scenes::GamePanels panels(world, random);
        const auto dropped =
            Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = given.mouseX, .panels = &panels.Registry()});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(sent.size(), given.kept ? 0u : 1u);
        EXPECT_EQ(runtime.cursorItemCount, given.kept ? 0 : 1);
    }
}

// 0x5dfb3e y 0x4379f0: los dwords de la tabla y el corte de mapas son distintos de la zona segura de 0x437a20.
TEST_F(CursorItemTest, EntryTicketsRespectTheNativeThresholdMapAndSafeZone)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t heroLevel;
        std::uint32_t itemLevel;
        std::int32_t map;
        std::uint8_t safeZone;
        bool sends;
        bool warns;
    };
    constexpr std::array<Case, 9> Cases{{{0, 40, 1, 0, 0, true, false},
                                         {0, 39, 1, 0, 0, false, false},
                                         {0, 65535, 0, 0, 0, false, false},
                                         {3, 350, 0, 0, 0, true, false},
                                         {4, 20, 1, 0, 0, true, false},
                                         {0, 40, 1, 24, 0, false, false},
                                         {0, 40, 1, 36, 0, false, false},
                                         {0, 40, 1, 0, 1, false, true},
                                         {4, 19, 12, 0, 0, false, true}}};
    const auto texts = Client::Tests::MakeTextTable({{1123, "Desde %d"}});
    services.texts = &texts;
    std::vector<std::string> lines;
    services.chatLine = [&lines](std::string_view line, std::int32_t type)
    {
        EXPECT_EQ(type, 2);
        lines.emplace_back(line);
        return Expected<void, std::string>{};
    };
    for (const Case& entry : Cases)
    {
        auto& runtime = world.Runtime();
        runtime.map = entry.map;
        world.Actors()[runtime.hero].characterClass = entry.characterClass;
        world.Actors()[runtime.hero].safeZone = entry.safeZone;
        world.OwnCharacter().level = entry.heroLevel;
        Put(EntryTicket, 0, 0);
        Cell(0, 0).scalar1Low = entry.itemLevel << 3;
        runtime.movement.pendingClick = true;
        ASSERT_TRUE(Scenes::PickUpFromTheGrid(Moves(), 0).HasValue());
        std::vector<Drop> sent;
        services.sendDrop = [&sent](const Drop& drop)
        {
            sent.push_back(drop);
            return Expected<void, std::string>{};
        };
        services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
        Scenes::GamePanels panels(world, random);
        const auto before = random;
        outside.messages.clear();
        lines.clear();
        const auto dropped =
            Scenes::DropOnTheGround(Moves(), Scenes::GroundDrop{.mouseX = 100, .panels = &panels.Registry()});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(sent.size(), entry.sends ? 1u : 0u);
        EXPECT_EQ(runtime.cursorItemCount, entry.sends ? 1 : 0);
        EXPECT_EQ(!outside.messages.empty() || !lines.empty(), entry.warns);
        EXPECT_EQ(random.State(), Advanced(before, 2 + 2 * Own + (entry.sends ? 3 : 0)).State());
        if (entry.safeZone != 0)
        {
            EXPECT_EQ(outside.messages, std::vector<std::size_t>{1238});
        }
        if (entry.characterClass == 4 && entry.heroLevel == 19)
        {
            EXPECT_EQ(lines, std::vector<std::string>{"Desde 20"});
        }
    }
}

// 0x59bd3e..0x59bf56: MoveHero pasa el arma de la izquierda al cursor en un acceso al registro propio, viste al héroe y
// manda el 24 del lugar 1 al 0, salvo con la traba puesta.
TEST_F(CursorItemTest, MoveHeroSendsTheLeftWeaponToTheRight)
{
    ItemState& left = world.OwnCharacter().equipment[1];
    left = Invented(Sword);
    left.occupied = 1;
    const CrtRandom expected = Advanced(random, Own + SendRandoms);

    ASSERT_TRUE(Scenes::MoveTheLeftWeaponToTheRight(Moves()).HasValue());

    const Scenes::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Sword);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(left.id, EmptyOwnItemId);
    EXPECT_EQ(left.scalar1Low, 0u);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.cursorGrid, Scenes::CursorGrid::Inventory);
    EXPECT_EQ(runtime.slot78a6758, 1);
    EXPECT_EQ(runtime.itemMoveTarget, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 1);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);

    left = Invented(Sword);
    const CrtRandom locked = Advanced(random, Own);
    ASSERT_TRUE(Scenes::MoveTheLeftWeaponToTheRight(Moves()).HasValue());
    EXPECT_EQ(random.State(), locked.State());
    EXPECT_EQ(outside.sent.size(), 1u);
}

// 0x5bd750 → 0x561840 → 0x5579e0 (Q-C477): sacar una mano de otra clase cierra el registro dos veces (0x5bd750 y
// 0x5579e0) y avisa con el 251 y el tipo 2.
TEST_F(CursorItemTest, RemovingAHandOfAnotherClassWarnsThatThereAreNoArrows)
{
    world.OwnCharacter().characterClass = DarkKnight;
    world.OwnCharacter().equipment[0] = Invented(Sword);
    const CrtRandom expected = Advanced(random, 2 * Own);

    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 0).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});
    EXPECT_EQ(outside.messageTypes, std::vector<std::int32_t>{2});
    EXPECT_TRUE(outside.sent.empty());
}

// 0x5579e0 recorre la grilla de la columna 7 a la 0 y, en cada una, de abajo arriba: con el arco en la izquierda, las
// flechas de (6, 2) antes que las de (3, 5). Con el registro abierto por quien llama (como 0x561c50), 0x561840 lee las
// manos: la derecha vacía recibe las flechas, con el 24 del lugar 34 al 0 y el 250 con el tipo 1.
TEST_F(CursorItemTest, AnElfWithABowTakesTheArrowsOfTheLastColumnIntoTheRightHand)
{
    world.OwnCharacter().characterClass = Elf;
    world.OwnCharacter().equipment[0] = Invented(Arrows);
    world.OwnCharacter().equipment[1] = Invented(Bow);
    Put(Arrows, 3, 5);
    Put(Arrows, 6, 2);
    Put(Bolts, 7, 7);
    Scenes::WorldRuntime& runtime = world.Runtime();
    const Client::Models::LogicalAccess caller(runtime.ownRecordDepth, random, Client::Models::OwnRecordCloseRandoms);
    const CrtRandom expected = Advanced(random, SendRandoms);

    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 0).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.cursorItem.id, Arrows);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, 12 + 2 * 8 + 6);
    EXPECT_EQ(runtime.itemMoveTarget, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(Cell(6, 2).id, EmptyOwnItemId);
    EXPECT_EQ(Cell(3, 5).id, Arrows);
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12 + 2 * 8 + 6);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{EquippedArrowsText});
    EXPECT_EQ(outside.messageTypes, std::vector<std::int32_t>{1});
    EXPECT_EQ(world.OwnCharacter().equipment[0].id, EmptyOwnItemId);
}

// Con una ballesta de 0x808 a 0x80e en la derecha, los virotes van a la izquierda (el 1). Con la traba puesta,
// 0x449d80 no manda nada ni saca sus dos rand(), y el aviso sale igual.
TEST_F(CursorItemTest, AnElfWithACrossbowTakesTheBoltsIntoTheLeftHand)
{
    world.OwnCharacter().characterClass = Elf;
    world.OwnCharacter().equipment[0] = Invented(Crossbow);
    world.OwnCharacter().equipment[1] = Invented(Bolts);
    Put(Arrows, 5, 0);
    Put(Bolts, 1, 4);
    Scenes::WorldRuntime& runtime = world.Runtime();
    runtime.ui.field78abb39 = 1;
    const Client::Models::LogicalAccess caller(runtime.ownRecordDepth, random, Client::Models::OwnRecordCloseRandoms);
    const CrtRandom expected = random;

    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 1).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.cursorItem.id, Bolts);
    EXPECT_EQ(runtime.slot78a6758, 12 + 4 * 8 + 1);
    EXPECT_EQ(runtime.itemMoveTarget, 1);
    EXPECT_EQ(Cell(1, 4).id, EmptyOwnItemId);
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{EquippedArrowsText});
}

// Sin lo que busca, el 251; con el cursor ocupado, 0x561840 cierra su acceso y no hace nada más. Con el registro
// cerrado por todos, el cierre de 0x561840 deja las manos en lo que dejó (Q-C202) y corta; sin arco ni ballesta, el
// tipo es la cuenta que dejó la tabla de protección (Q-C772) y corta si hay alguna casilla que pueda serlo.
TEST_F(CursorItemTest, TheArrowsStopWhereTheOriginalReadsWhatMuRetDoesNotHave)
{
    world.OwnCharacter().characterClass = Elf;
    ItemState& left = world.OwnCharacter().equipment[1];
    ItemState& right = world.OwnCharacter().equipment[0];
    left = Invented(Bow);
    right = Invented(Arrows);
    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});

    Put(Arrows, 0, 0);
    right = Invented(Arrows);
    world.Runtime().cursorItemCount = 1;
    CrtRandom expected = Advanced(random, 3 * Own);
    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(outside.messages.size(), 1u);
    EXPECT_EQ(Cell(0, 0).id, Arrows);

    world.Runtime().cursorItemCount = 0;
    right = Invented(Arrows);
    expected = Advanced(random, 3 * Own);
    const auto closed = Scenes::RemoveInventoryItem(Moves(), 0);
    ASSERT_FALSE(closed.HasValue());
    EXPECT_TRUE(closed.Error().starts_with("0x561b13:")) << closed.Error();
    EXPECT_EQ(random.State(), expected.State());

    left = Invented(Shield);
    right = Invented(Sword);
    const auto probe = Scenes::RemoveInventoryItem(Moves(), 0);
    ASSERT_FALSE(probe.HasValue());
    EXPECT_TRUE(probe.Error().starts_with("0x557c62:")) << probe.Error();

    world.Inventory().ClearForInventoryReply();
    right = Invented(Sword);
    ASSERT_TRUE(Scenes::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(outside.messages.size(), 2u);
    EXPECT_EQ(outside.messages.back(), NoArrowsText);
}

// 0x561c50 (el clic de ataque, CL9 hito 3): sin arma de tiro, un acceso y verdadero; con una ballesta y sus virotes
// con durabilidad, dos y verdadero. Sin ellos, 0x561840 dentro del segundo acceso (anidado: sin el corte de Q-C202) y
// falso: el 251 si no hay más en la grilla, o las flechas de la grilla a la derecha vacía del arco, con su 24.
TEST_F(CursorItemTest, TheAttackChecksTheArrowsOfTheRangedWeapon)
{
    world.OwnCharacter().characterClass = Elf;
    ItemState& right = world.OwnCharacter().equipment[0];
    ItemState& left = world.OwnCharacter().equipment[1];
    const auto checked = [this](int draws, bool expected)
    {
        const CrtRandom after = Advanced(random, draws);
        const auto arrows = Scenes::CheckTheArrows(Moves());
        ASSERT_TRUE(arrows.HasValue()) << arrows.Error();
        EXPECT_EQ(arrows.Value(), expected);
        EXPECT_EQ(random.State(), after.State());
    };

    right = Invented(Sword);
    checked(Own, true);
    // 0x811 en la derecha no es ballesta para 0x561c50.
    right = Invented(0x0811);
    checked(Own, true);

    right = Invented(Crossbow);
    left = Invented(Bolts);
    checked(2 * Own, true);
    EXPECT_TRUE(outside.messages.empty());

    left.durability = 0;
    checked(2 * Own, false);
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});

    right = ItemState{};
    right.id = EmptyOwnItemId;
    left = Invented(Bow);
    Put(Arrows, 6, 2);
    checked(2 * Own + SendRandoms, false);
    EXPECT_EQ(outside.messages.back(), EquippedArrowsText);
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12 + 2 * 8 + 6);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);
    EXPECT_EQ(world.Runtime().cursorItem.id, Arrows);
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
