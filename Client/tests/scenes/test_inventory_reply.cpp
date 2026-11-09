#include "Scenes/InventoryReply.h"

#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/InventoryList.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El F3 10 (0x4c55a0) y lo que hace con cada entrada (0x5bc740), con el Item.bmd real de la carpeta de datos:
// 06, "El F3 10, el inventario". Los bytes de los ítems son sintéticos; del cable salen solo los cinco
// lugares (04, "El F3 10").
namespace
{
using Client::Protocol::Character::InventoryEntry;
using Client::Protocol::Character::InventoryList;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::ApplyInventory;
using Client::Scenes::EmptyOwnItemId;
using Client::Scenes::WorldEntryResources;
using Client::Tests::AfterDraws;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::size_t PublishedSlot = 42;
// Un cierre del registro propio que lo deja sin accesos, el sufijo de 0x4f2250 y la cola de 0x550d10.
constexpr int OwnRecordClose = 0xf08;
constexpr int StopRandom = 1;
constexpr int TailRandoms = 3;
// Lo que gasta un lugar del equipo con el registro cerrado: su cierre en línea y 0x512a30 adentro.
constexpr int EquipmentEntry = OwnRecordClose + StopRandom + TailRandoms;

// Los tipos de Item.bmd que usan las pruebas, con su ancho y su alto ahí: Kris (1 por 2), Short Sword (1 por
// 3), Small Shield (2 por 2), Guardian Helm (2 por 2), Apple, Jewel of Bless, Gaurdian Angel, Dark Horse, Dark
// Raven y Wizard's Ring (1 por 1) y Horn of Doom (2 por 2).
constexpr std::uint16_t Kris = 0x000;
constexpr std::uint16_t ShortSword = 0x001;
constexpr std::uint16_t SmallShield = 0xc00;
constexpr std::uint16_t GuardianHelm = 0xe0e;
constexpr std::uint16_t Apple = 0x1c00;
constexpr std::uint16_t JewelOfBless = 0x1c0d;
constexpr std::uint16_t GuardianAngel = 0x1a00;
constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
constexpr std::uint16_t WizardsRing = 0x1a14;
constexpr std::uint16_t HornOfDoom = 0x1a25;

constexpr std::uint8_t ChaosCastleMap = 18;

// Los siete bytes de un ítem del F3 10 para `type` (0x54b3a0 al revés): el bit 8 en el 0x80 del byte 3 y
// los bits 9 a 12 arriba del byte 5. El byte 6 y los cuatro de abajo del 5 no los lee nadie.
std::array<std::byte, Client::Protocol::Character::InventoryItemDataSize>
ItemBytes(std::uint16_t type, std::uint8_t byte1 = 0, std::uint8_t durability = 20, std::uint8_t byte3 = 0,
          std::uint8_t byte4 = 0)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{byte1},
            std::byte{durability},
            static_cast<std::byte>(byte3 | ((type >> 1) & 0x80)),
            std::byte{byte4},
            static_cast<std::byte>(((type >> 5) & 0xf0) | 0x0f),
            std::byte{0xa5}};
}

InventoryEntry Entry(std::uint8_t slot, std::uint16_t type, std::uint8_t byte1 = 0, std::uint8_t durability = 20,
                     std::uint8_t byte3 = 0, std::uint8_t byte4 = 0)
{
    return InventoryEntry{.slot = slot, .item = ItemBytes(type, byte1, durability, byte3, byte4)};
}

InventoryList ListOf(std::initializer_list<InventoryEntry> entries)
{
    return InventoryList{.items = std::vector<InventoryEntry>(entries)};
}

// Las tablas de los conjuntos sin ningún conjunto: 0x40e1b0 recorre el equipo y no aplica nada.
Client::Data::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// Lo que dura la sesión, con Player.bmd, sin conjuntos y el Item.bmd que pase la prueba.
struct Stage
{
    Stage(std::filesystem::path root, const Client::Data::Tables::ItemTable* itemTable)
        : dataRoot(std::move(root)), cloth{.random = CrtRandom(Seed)}, items(itemTable)
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = dataRoot,
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = items,
                                   .itemSetTables = &sets,
                                   .log = log};
    }

    // El héroe publicado en PublishedSlot, un jugador sin armas en el mapa 0.
    ActorState& PublishedHero()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = PublishedSlot;
        ActorState& hero = world.Actors()[PublishedSlot];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        return hero;
    }

    [[nodiscard]] const ItemState& Cell(std::size_t index) const
    {
        return world.Inventory().cells[index];
    }

    std::filesystem::path dataRoot;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    const Client::Data::Tables::ItemTable* items = nullptr;
    Client::Data::Tables::ItemSetTables sets = NoSets();
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class InventoryReplyTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        const auto path = DataFile("Local\\Item.bmd");
        ASSERT_TRUE(path.has_value());
        const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value());
        auto loaded = Client::Data::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        ASSERT_TRUE(loaded.HasValue());
        m_items.emplace(std::move(loaded).Value().table);
    }

    [[nodiscard]] Stage Fresh() const
    {
        return Stage(m_dataRoot, &*m_items);
    }

    std::optional<Client::Data::Tables::ItemTable> m_items;
};

std::uint16_t ModelOf(std::uint16_t type)
{
    return static_cast<std::uint16_t>(type + Client::Data::Models::FirstItemModel);
}

// 0x54b3a0: el byte 0, el bit 0x80 del byte 3 como bit 8 y los cuatro de arriba del byte 5 como bits 9 a 12.
TEST(InventoryItemTypeTest, TakesByte0TheTopBitOfByte3AndTheTopNibbleOfByte5)
{
    const std::array<std::byte, 7> bytes{std::byte{0x12}, std::byte{0xff}, std::byte{0xff}, std::byte{0x80},
                                         std::byte{0xff}, std::byte{0xa7}, std::byte{0xff}};
    EXPECT_EQ(Client::Scenes::InventoryItemType(bytes), 0x12 | 0x100 | 0x1400);
    const std::array<std::byte, 7> low{std::byte{0x34}, std::byte{0xff}, std::byte{0xff}, std::byte{0x7f},
                                       std::byte{0xff}, std::byte{0x0f}, std::byte{0xff}};
    EXPECT_EQ(Client::Scenes::InventoryItemType(low), 0x34);
    for (const std::uint16_t type : {Kris, SmallShield, GuardianHelm, Apple, JewelOfBless, HornOfDoom})
    {
        EXPECT_EQ(Client::Scenes::InventoryItemType(ItemBytes(type)), type);
    }
}

// 0x5bc75d: el tipo 0x1fff vuelve sin tocar nada, ni el registro ni el azar.
TEST_F(InventoryReplyTest, TheType1fffTouchesNothing)
{
    Stage tested = Fresh();
    const std::array<std::byte, 7> none{std::byte{0xff}, std::byte{0x01}, std::byte{0x02}, std::byte{0x80},
                                        std::byte{0x04}, std::byte{0xf0}, std::byte{0x06}};
    ASSERT_EQ(Client::Scenes::InventoryItemType(none), Client::Scenes::NoInventoryItem);

    for (const int slot : {0, 12, 80})
    {
        ASSERT_TRUE(
            Client::Scenes::PlaceInventoryItem(static_cast<std::uint8_t>(slot), none, tested.Resources()).HasValue());
    }

    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, EmptyOwnItemId);
    EXPECT_EQ(tested.Cell(0).id, 0);
    EXPECT_EQ(tested.world.Inventory().secondCells[4].id, 0);
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c55a0 con la cantidad en 0: la inicialización común igual vacía el equipo y las dos grillas, con un
// cierre del registro. El equipo deja el dword de +4 y la durabilidad (Q-C203), y el héroe no cambia.
TEST_F(InventoryReplyTest, ACountOfZeroStillEmpties)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    hero.hands[0].model = ModelOf(Kris);
    ItemState& worn = tested.world.OwnCharacter().equipment[0];
    worn.id = Kris;
    worn.occupied = 1;
    worn.flags1b = 0x22;
    worn.scalar1Low = 0x18;
    worn.durability = 30;
    for (std::size_t cell = 0; cell < Client::Scenes::InventoryCellCount; ++cell)
    {
        tested.world.Inventory().cells[cell] = ItemState{.id = Apple, .flags1b = 3, .occupied = 1};
    }
    tested.world.Inventory().secondCells.back() = ItemState{.id = Apple, .flags1b = 3, .occupied = 1};

    const auto applied = ApplyInventory(InventoryList{}, tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(applied.Value().sounds.empty());
    EXPECT_EQ(worn.id, EmptyOwnItemId);
    EXPECT_EQ(worn.occupied, 0u);
    EXPECT_EQ(worn.flags1b, 0);
    EXPECT_EQ(worn.scalar1Low, 0x18u);
    EXPECT_EQ(worn.durability, 30);
    for (const ItemState& cell : tested.world.Inventory().cells)
    {
        EXPECT_EQ(cell.id, EmptyOwnItemId);
        EXPECT_EQ(cell.occupied, 0u);
        EXPECT_EQ(cell.flags1b, 0);
    }
    EXPECT_EQ(tested.world.Inventory().secondCells.back().id, EmptyOwnItemId);
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
}

// 0x5bc840..0x5bc886: un lugar del equipo con los bytes del ítem, 0x54bf40 y 0x512a30 sobre el héroe
// publicado. Lo de adentro anida en el acceso en línea: gasta el sufijo de 0x4f2250, la cola de 0x550d10 y
// un cierre.
TEST_F(InventoryReplyTest, AnEquipmentSlotDressesThePublishedHero)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris, 0x18, 25, 0x01, 0x02)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ItemState& worn = tested.world.OwnCharacter().equipment[0];
    EXPECT_EQ(worn.id, Kris);
    EXPECT_EQ(worn.durability, 25);
    EXPECT_EQ(worn.flags1b, 0x01);
    EXPECT_EQ(worn.flags1c, 0x02);
    EXPECT_EQ(worn.occupied, 1u);
    EXPECT_EQ(worn.scalar1Low, 0x18u);
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(hero.hands[0].level, 3);
    EXPECT_EQ(hero.hands[0].flags1b, 0x01);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose + EquipmentEntry).State());
}

// Q-C203: el vaciado común deja la durabilidad. Con un lugar del equipo en el mismo F3 10, el 0x550d10 de
// 0x512a30 lee Item.bmd con el tipo −1 del lugar vaciado (Q-C184), y MuRet corta ahí con el registro cerrado.
TEST_F(InventoryReplyTest, AnEmptiedSlotKeepsItsDurabilityUntilQC184)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    tested.world.OwnCharacter().equipment[5].durability = 10;

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris)}), tested.Resources());

    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Q-C184"), std::string::npos) << applied.Error();
    EXPECT_NE(applied.Error().find("lugar 0"), std::string::npos) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[5].id, EmptyOwnItemId);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[5].durability, 10);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
}

// 0x4c5e78..0x4c5e9f no mira si un lugar se repite: cada entrada corre entera, y la última queda.
TEST_F(InventoryReplyTest, RepeatedSlotsApplyEach)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto applied = ApplyInventory(
        ListOf({Entry(0, Kris), Entry(0, ShortSword), Entry(12, SmallShield), Entry(12, Apple)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, ShortSword);
    EXPECT_EQ(hero.hands[0].model, ModelOf(ShortSword));
    // La manzana pisa solo la casilla de arriba a la izquierda del escudo; las otras tres quedan.
    EXPECT_EQ(tested.Cell(0).id, Apple);
    EXPECT_EQ(tested.Cell(0).occupied, 1u);
    EXPECT_EQ(tested.Cell(1).id, SmallShield);
    EXPECT_EQ(tested.Cell(8).id, SmallShield);
    EXPECT_EQ(tested.Cell(9).id, SmallShield);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, 3 * OwnRecordClose + 2 * EquipmentEntry).State());
}

// 0x5bcc1a..0x5bcd05: desde el lugar 12, el ancho y el alto de Item.bmd en casillas de 8 columnas; todas
// con la columna y la fila del origen, ocupada solo la de arriba a la izquierda. 0x54bf40 corre en cada una.
TEST_F(InventoryReplyTest, TheGridTakesTheWidthAndHeightOfItemBmd)
{
    Stage tested = Fresh();

    const auto placed =
        Client::Scenes::PlaceInventoryItem(12 + 9, ItemBytes(SmallShield, 0x10, 40, 0x01, 0x02), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    for (const std::size_t cell : {9u, 10u, 17u, 18u})
    {
        SCOPED_TRACE(cell);
        const ItemState& item = tested.Cell(cell);
        EXPECT_EQ(item.id, SmallShield);
        EXPECT_EQ(item.durability, 40);
        EXPECT_EQ(item.flags1b, 0x01);
        EXPECT_EQ(item.flags1c, 0x02);
        EXPECT_EQ(item.scalar1Low, 0x10u);
        EXPECT_EQ(item.gridColumn, 1);
        EXPECT_EQ(item.gridRow, 1);
        EXPECT_EQ(item.occupied, cell == 9u ? 1u : 0u);
    }
    EXPECT_EQ(tested.Cell(11).id, 0);
    EXPECT_EQ(tested.Cell(25).id, 0);
}

// Q-C201: sin tope por columna, un ítem que pasa de la última sigue en la fila de abajo.
TEST_F(InventoryReplyTest, AnItemPastTheLastColumnContinuesOnTheNextRow)
{
    Stage tested = Fresh();

    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12 + 7, ItemBytes(SmallShield), tested.Resources()).HasValue());

    for (const std::size_t cell : {7u, 8u, 15u, 16u})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(tested.Cell(cell).id, SmallShield);
        EXPECT_EQ(tested.Cell(cell).gridColumn, 7);
        EXPECT_EQ(tested.Cell(cell).gridRow, 0);
    }
}

// Q-C200: sin tope por casilla, el original escribe después de la última, fuera de la grilla. MuRet escribe
// las de adentro, con su 0x54bf40, y corta en la primera de afuera.
TEST_F(InventoryReplyTest, AnItemPastTheLastCellIsAnExplicitFrontier)
{
    Stage tested = Fresh();

    const auto placed = Client::Scenes::PlaceInventoryItem(12 + 63, ItemBytes(SmallShield), tested.Resources());

    ASSERT_FALSE(placed.HasValue());
    EXPECT_NE(placed.Error().find("Q-C200"), std::string::npos) << placed.Error();
    EXPECT_NE(placed.Error().find("0x78a9bb0"), std::string::npos) << placed.Error();
    EXPECT_EQ(tested.Cell(63).id, SmallShield);
    EXPECT_EQ(tested.Cell(63).occupied, 1u);
    // Corta antes del registro: no lo abre ni lo cierra.
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());

    Stage second = Fresh();
    const auto past = Client::Scenes::PlaceInventoryItem(0x4c + 31, ItemBytes(Apple), second.Resources());
    ASSERT_TRUE(past.HasValue()) << past.Error();
    const auto beyond = Client::Scenes::PlaceInventoryItem(0x4c + 32, ItemBytes(Apple), second.Resources());
    ASSERT_FALSE(beyond.HasValue());
    EXPECT_NE(beyond.Error().find("0x78ab0b8"), std::string::npos) << beyond.Error();
}

// 0x5bcf99..0x5bd085: desde el lugar 76, la segunda grilla, con el mismo cálculo.
TEST_F(InventoryReplyTest, TheSecondGridStartsAtSlot76)
{
    Stage tested = Fresh();

    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(0x4c + 11, ItemBytes(GuardianHelm), tested.Resources()).HasValue());

    const auto& second = tested.world.Inventory().secondCells;
    for (const std::size_t cell : {11u, 12u, 19u, 20u})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(second[cell].id, GuardianHelm);
        EXPECT_EQ(second[cell].gridColumn, 3);
        EXPECT_EQ(second[cell].gridRow, 1);
    }
    EXPECT_EQ(tested.Cell(11).id, 0);
}

// 0x5bcd0b: después de una grilla, con [0x78a6758] en menos de 12 abre y cierra el registro (un cierre), y
// con 12 o más no lo toca. Los WORD +0x3e y +0x42 en 0 no llaman a 0x54fe10.
TEST_F(InventoryReplyTest, AfterTheGridTheRecordClosesOnlyWithSlot78a6758BelowTwelve)
{
    Stage closing = Fresh();
    Stage skipping = Fresh();
    skipping.world.Runtime().slot78a6758 = 12;
    constexpr std::uint16_t Untouched = 0x1234;
    closing.world.OwnCharacter().attackSpeed = Untouched;

    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12, ItemBytes(Apple), closing.Resources()).HasValue());
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12, ItemBytes(Apple), skipping.Resources()).HasValue());

    EXPECT_EQ(closing.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
    EXPECT_EQ(skipping.cloth.random.State(), CrtRandom(Seed).State());
    EXPECT_EQ(closing.world.OwnCharacter().attackSpeed, Untouched);
}

// 0x5bcdd9 y 0x5bcdf0: 0x54fe10 una vez por cada WORD, +0x3e y +0x42, que no sea 0, con el registro abierto.
TEST_F(InventoryReplyTest, EachNonZeroWord3eOr42RunsTheSpeeds)
{
    Stage tested = Fresh();
    Client::Scenes::OwnCharacterRecord& own = tested.world.OwnCharacter();
    own.word42 = 1;
    own.agility = 100;
    own.speedAdjustBits = 0x01;

    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12, ItemBytes(Apple), tested.Resources()).HasValue());

    // Clase 0: agilidad / 20 y agilidad / 10, más 20 por el bit 0x01 de +0x3c (0x54fe10).
    EXPECT_EQ(own.attackSpeed, 100 / 20 + 20);
    EXPECT_EQ(own.magicSpeed, 100 / 10 + 20);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
}

// Q-C202: en la grilla 0x54bf40 corre con el registro cerrado, y la familia tardía de las armas con el bit
// 0x10 de B3 lee su nivel, que el último cierre dejó en rand() & 0xff. Abierto, es el nivel.
TEST_F(InventoryReplyTest, ALateWeaponReadsTheLevelOnlyWithTheRecordOpen)
{
    Stage closed = Fresh();
    const auto cut = Client::Scenes::PlaceInventoryItem(12, ItemBytes(Kris, 0, 20, 0x10), closed.Resources());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("Q-C202"), std::string::npos) << cut.Error();
    EXPECT_EQ(closed.Cell(0).id, Kris);
    EXPECT_EQ(closed.Cell(8).id, 0);

    Stage held = Fresh();
    held.world.Runtime().ownRecordDepth = 1;
    held.world.OwnCharacter().level = 200;
    const auto placed = Client::Scenes::PlaceInventoryItem(12, ItemBytes(Kris, 0, 20, 0x10), held.Resources());
    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    const ItemState& top = held.Cell(0);
    std::optional<std::uint8_t> levelOption;
    for (std::uint8_t option = 0; option < top.optionCount; ++option)
    {
        if (top.OptionTag(option) == 0x5d)
        {
            levelOption = top.OptionValue(option);
        }
    }
    EXPECT_EQ(levelOption, std::optional<std::uint8_t>(200 / 20));
    EXPECT_EQ(held.world.Runtime().ownRecordDepth, 1);
}

// 0x54cc5c: el Dark Horse pide 0x4225d0 dentro de 0x54bf40, en la grilla o en el equipo. Corta antes de
// 0x54bf40, con los bytes ya escritos y, en el equipo, el registro cerrado igual.
TEST_F(InventoryReplyTest, TheDarkHorseStopsAt4225d0)
{
    Stage grid = Fresh();
    const auto inGrid = Client::Scenes::PlaceInventoryItem(12, ItemBytes(DarkHorse), grid.Resources());
    ASSERT_FALSE(inGrid.HasValue());
    EXPECT_NE(inGrid.Error().find("0x4225d0"), std::string::npos) << inGrid.Error();
    EXPECT_EQ(grid.Cell(0).id, DarkHorse);

    Stage worn = Fresh();
    worn.PublishedHero();
    const auto inEquipment = Client::Scenes::PlaceInventoryItem(8, ItemBytes(DarkHorse), worn.Resources());
    ASSERT_FALSE(inEquipment.HasValue());
    EXPECT_NE(inEquipment.Error().find("0x4225d0"), std::string::npos) << inEquipment.Error();
    EXPECT_EQ(worn.world.OwnCharacter().equipment[8].id, DarkHorse);
    EXPECT_EQ(worn.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(worn.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
}

// 0x5bc8cc: fuera de los mapas 18 a 23, las mascotas del lugar 8 se crean con 0x443fa0 en la tabla de
// 0x7d335d0, que MuRet no tiene. El héroe ya está vestido, y el registro se cierra igual.
TEST_F(InventoryReplyTest, APetOfSlot8StopsAt443fa0OutsideChaosCastle)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto placed = Client::Scenes::PlaceInventoryItem(8, ItemBytes(GuardianAngel), tested.Resources());

    ASSERT_FALSE(placed.HasValue());
    EXPECT_NE(placed.Error().find("0x443fa0"), std::string::npos) << placed.Error();
    EXPECT_EQ(hero.pet.model, ModelOf(GuardianAngel));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry).State());
}

// En los mapas 18 a 23 no se crea ninguna mascota, ni el cuervo.
TEST_F(InventoryReplyTest, InChaosCastleNeitherThePetNorTheRavenIsCreated)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    tested.world.Runtime().map = ChaosCastleMap;

    const auto applied = ApplyInventory(ListOf({Entry(8, GuardianAngel), Entry(1, DarkRaven)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[8].id, GuardianAngel);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[1].id, DarkRaven);
}

// 0x5bc9d6..0x5bc9db: con Horn of Doom, el byte +0x1b del ítem va a +0x31b del héroe antes de 0x443fa0.
TEST_F(InventoryReplyTest, HornOfDoomCopiesItsFlags1bToThePetBeforeStopping)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto placed = Client::Scenes::PlaceInventoryItem(8, ItemBytes(HornOfDoom, 0, 20, 0x02), tested.Resources());

    ASSERT_FALSE(placed.HasValue());
    EXPECT_NE(placed.Error().find("0x443fa0"), std::string::npos) << placed.Error();
    EXPECT_EQ(hero.pet.flags1b, 0x02);
}

// 0x5bca62: el Dark Raven del lugar 1 llama a 0x420cf0, que fuera de los mapas 18 a 23 crea el cuervo.
TEST_F(InventoryReplyTest, TheDarkRavenOfSlot1StopsAt420cf0)
{
    Stage tested = Fresh();
    tested.PublishedHero();

    const auto placed = Client::Scenes::PlaceInventoryItem(1, ItemBytes(DarkRaven), tested.Resources());

    ASSERT_FALSE(placed.HasValue());
    EXPECT_NE(placed.Error().find("0x420cf0"), std::string::npos) << placed.Error();
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
}

// 0x5bca6c..0x5bcaa7: el Wizard's Ring de nivel 3 en el 10 o el 11 lleva a 0x414340 y a +0x360 del héroe.
// De otro nivel no.
TEST_F(InventoryReplyTest, ALevelThreeWizardsRingStopsAt414340)
{
    for (const int ring : {10, 11})
    {
        SCOPED_TRACE(ring);
        const auto slot = static_cast<std::uint8_t>(ring);
        Stage tested = Fresh();
        tested.PublishedHero();
        const auto placed = Client::Scenes::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x18), tested.Resources());
        ASSERT_FALSE(placed.HasValue());
        EXPECT_NE(placed.Error().find("0x414340"), std::string::npos) << placed.Error();

        Stage other = Fresh();
        other.PublishedHero();
        const auto levelTwo = Client::Scenes::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x10), other.Resources());
        EXPECT_TRUE(levelTwo.HasValue()) << levelTwo.Error();
    }
}

// Sin Item.bmd, la grilla corta donde lee el ancho (0x5bcc33) y el equipo en 0x54bf40, antes de 0x512a30.
TEST_F(InventoryReplyTest, WithoutItemBmdBothPathsStop)
{
    Stage tested(m_dataRoot, nullptr);
    tested.PublishedHero();

    const auto grid = Client::Scenes::PlaceInventoryItem(12, ItemBytes(Apple), tested.Resources());
    const auto worn = Client::Scenes::PlaceInventoryItem(0, ItemBytes(Kris), tested.Resources());

    ASSERT_FALSE(grid.HasValue());
    EXPECT_NE(grid.Error().find("0x7758550"), std::string::npos) << grid.Error();
    ASSERT_FALSE(worn.HasValue());
    EXPECT_NE(worn.Error().find("0x54bf40"), std::string::npos) << worn.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.Actors()[PublishedSlot].hands[0].model, Client::Scenes::NoItemModel);
}

// Q-C204: 0x5bc740 viste al héroe publicado (0x743a170), no al de WorldRuntime::heroSlot. Antes de la
// respuesta del F3 03 es el registro 0 de WinMain, que no es un jugador: 0x512a30 no le hace nada.
TEST_F(InventoryReplyTest, BeforeTheEntryTheEquipmentGoesToTheRecordOnly)
{
    Stage tested = Fresh();
    tested.world.Runtime().heroSlot = 7;
    tested.world.Actors()[7] = Client::Tests::UnarmedPlayer();

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.Actors()[7].hands[0].model, Client::Scenes::NoItemModel);
    EXPECT_EQ(tested.world.Actors()[0].hands[0].model, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, 2 * OwnRecordClose).State());
}

// La respuesta del F3 03 del cable (la celda (133, 127) del mapa 0) y el F3 10 en el mismo segmento, con
// cinco entradas en los lugares del cable (0, 12, 14, 17 y 23; 04, "El F3 10") y bytes sintéticos: en un
// solo Poll la entrada publica al héroe y el F3 10 lo viste (P-C28 a). Lo de un frame de cero pasos espera
// el frame del juego de la T8.
TEST_F(InventoryReplyTest, TheFiveCableSlotsDressTheHeroInTheSamePoll)
{
    Stage tested = Fresh();
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    // La respuesta del F3 03: la cabecera y la celda; el resto en 0 (el mapa 0 y la dirección 0).
    std::vector<std::byte> entry(0x34);
    const std::array head{std::byte{0xC1}, std::byte{0x34}, std::byte{0xF3},
                          std::byte{0x03}, std::byte{133},  std::byte{127}};
    std::ranges::copy(head, entry.begin());
    const std::array cable{Entry(0, Kris, 0, 20), Entry(12, SmallShield), Entry(14, Apple), Entry(17, JewelOfBless),
                           Entry(23, GuardianHelm)};
    std::vector<std::byte> inventory{std::byte{0xC2}, std::byte{0x00},
                                     std::byte{0x00}, std::byte{0xF3},
                                     std::byte{0x10}, std::byte{static_cast<std::uint8_t>(cable.size())}};
    for (const InventoryEntry& item : cable)
    {
        inventory.push_back(std::byte{item.slot});
        inventory.insert(inventory.end(), item.item.begin(), item.item.end());
    }
    inventory[2] = static_cast<std::byte>(inventory.size());
    Client::Network::PacketSerials serials;
    std::vector<std::byte> segment = Client::Tests::FromTheGameServer(entry, serials);
    const std::vector<std::byte> second = Client::Tests::FromTheGameServer(inventory, serials);
    segment.insert(segment.end(), second.begin(), second.end());
    online.socket.reads = {segment};

    static_cast<void>(online.session.Poll());

    ASSERT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    const Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    ASSERT_EQ(runtime.hero, static_cast<std::size_t>(runtime.heroSlot));
    const ActorState& hero = tested.world.Actors()[runtime.hero];
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[1].id, EmptyOwnItemId);
    EXPECT_EQ(tested.Cell(0).id, SmallShield);
    EXPECT_EQ(tested.Cell(9).id, SmallShield);
    EXPECT_EQ(tested.Cell(2).id, Apple);
    EXPECT_EQ(tested.Cell(5).id, JewelOfBless);
    EXPECT_EQ(tested.Cell(11).id, GuardianHelm);
    EXPECT_EQ(tested.Cell(20).id, GuardianHelm);
    EXPECT_EQ(tested.Cell(3).id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ownRecordDepth, 0);
    // La entrada con 0x512a30 entera (WorldEntryTest), y el F3 10 con todo cerrado: el cierre común, el del
    // lugar 0 con lo de 0x512a30 adentro y uno por cada grilla (P-C42).
    constexpr int GrassRandoms = 65536;
    constexpr int HeroRandom = 1;
    constexpr int ConstructionRandoms = 28;
    constexpr int WaitClose = 4;
    constexpr int WindowRandoms = 2;
    constexpr int TheEntry = 4 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom +
                             TailRandoms + WaitClose + WindowRandoms + StopRandom;
    constexpr int TheInventory = OwnRecordClose + EquipmentEntry + 4 * OwnRecordClose;
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, TheEntry + TheInventory).State());
}
} // namespace
