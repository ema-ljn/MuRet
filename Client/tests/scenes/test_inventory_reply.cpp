#include "Scenes/InventoryReply.h"

#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/PacketSerials.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/HelperTable.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"
#include "World/ModelEffects.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
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
using Client::Scenes::HelperState;
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

    // En el heap: WorldState pesa unos 350 KB, y tres en la pila pasan el 1 MB de la de Windows.
    [[nodiscard]] std::unique_ptr<Stage> FreshOnTheHeap() const
    {
        return std::make_unique<Stage>(m_dataRoot, &*m_items);
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
    EXPECT_EQ(levelOption, std::optional<std::uint8_t>(static_cast<std::uint8_t>(200 / 20)));
    EXPECT_EQ(held.world.Runtime().ownRecordDepth, 1);
}

// 0x4225d0 desde 0x54cc5c: el Dark Horse agrega la opción 0x96 con 5 + la agilidad / 20 + 2 · el nivel del
// caballo ([0x72397c]). En la grilla, 0x54bf40 corre con el registro cerrado: 0x4225d0 lo abre y lo cierra (3.848
// rand()), y después lo vuelve a abrir y cerrar la cola de la grilla.
TEST_F(InventoryReplyTest, TheDarkHorseInTheGridAddsItsDefenseOption)
{
    Stage grid = Fresh();
    grid.world.OwnCharacter().agility = 205;
    grid.world.Runtime().pets[Client::Scenes::DarkHorsePetStats].level = 3;

    const auto placed = Client::Scenes::PlaceInventoryItem(12, ItemBytes(DarkHorse), grid.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    const ItemState& horse = grid.Cell(0);
    EXPECT_EQ(horse.id, DarkHorse);
    ASSERT_EQ(horse.optionCount, 1);
    EXPECT_EQ(horse.OptionTag(0), 0x96);
    EXPECT_EQ(horse.OptionValue(0), 205 / 20 + 2 * 3 + 5);
    EXPECT_EQ(grid.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(grid.cloth.random.State(), AfterDraws(Seed, 2 * OwnRecordClose).State());
}

// En el equipo, 0x5bc740 ya tiene el registro abierto: 0x4225d0 no lo cierra y no saca rand(). Lo que siga
// después de 0x54bf40 (el ayudante y el efecto del lugar 8) no cambia la opción.
TEST_F(InventoryReplyTest, TheWornDarkHorseGetsItsOptionWithTheRecordAlreadyOpen)
{
    Stage worn = Fresh();
    worn.PublishedHero();
    worn.world.OwnCharacter().agility = 40;

    const auto placed = Client::Scenes::PlaceInventoryItem(8, ItemBytes(DarkHorse), worn.Resources());

    if (!placed.HasValue())
    {
        EXPECT_EQ(placed.Error().find("0x4225d0"), std::string::npos) << placed.Error();
    }
    const ItemState& horse = worn.world.OwnCharacter().equipment[8];
    EXPECT_EQ(horse.id, DarkHorse);
    ASSERT_EQ(horse.optionCount, 1);
    EXPECT_EQ(horse.OptionTag(0), 0x96);
    EXPECT_EQ(horse.OptionValue(0), 40 / 20 + 5);
    EXPECT_EQ(worn.world.Runtime().ownRecordDepth, 0);
}

// 0x4225f2..0x422602: una 0x96 desde la segunda opción recibe el valor nuevo y la cantidad no cambia; una en la
// primera no cuenta y se agrega otra al final (Q-C415). Con el registro abierto, sin rand(); el valor va en un
// byte: (65.535 / 20 = 3.276) + 2 · 200 + 5 = 3.681, que deja 97.
TEST_F(InventoryReplyTest, TheDarkHorseOptionSkipsAnEarlierOneInTheFirstPlace)
{
    Stage tested = Fresh();
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.OwnCharacter().agility = 0xffff;
    tested.world.Runtime().pets[Client::Scenes::DarkHorsePetStats].level = 200;

    ItemState second;
    second.id = DarkHorse;
    second.optionCount = 2;
    second.optionBytes[0] = 0x3e;
    second.optionBytes[1] = 0x96;
    second.optionBytes[Client::World::ItemOptionValueOffset + 1] = 99;
    Client::Scenes::AddTheDarkHorseOption(second, tested.Resources());
    EXPECT_EQ(second.optionCount, 2);
    EXPECT_EQ(second.OptionTag(1), 0x96);
    EXPECT_EQ(second.OptionValue(1), 97);

    ItemState first;
    first.id = DarkHorse;
    first.optionCount = 1;
    first.optionBytes[0] = 0x96;
    first.optionBytes[Client::World::ItemOptionValueOffset] = 99;
    Client::Scenes::AddTheDarkHorseOption(first, tested.Resources());
    EXPECT_EQ(first.optionCount, 2);
    EXPECT_EQ(first.OptionValue(0), 99);
    EXPECT_EQ(first.OptionTag(1), 0x96);
    EXPECT_EQ(first.OptionValue(1), 97);

    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 1);
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());
}

// 0x5bc8cc: fuera de los mapas 18 a 23, el Guardian Angel del lugar 8 se crea con 0x443fa0(0x1c12, héroe + 0x10,
// héroe, 0, 0) en la tabla de 0x7d335d0, con los tres rand() de su lugar (0x443f21..0x443f8c) entre la cola de
// 0x512a30 y el cierre del registro: los rand() 5 a 7 de la semilla (4.116, 25.814 y 28.931) lo corren
// (4.116 % 512 − 256; 25.814 % 512 − 256; 28.931 % 128 + 128). Sin el efecto 0x61e, que no es del 0x1c12.
TEST_F(InventoryReplyTest, AGuardianAngelOfSlot8IsCreatedWithTheSharedRandom)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    hero.position = {1000.0f, 2000.0f, 300.0f};

    const auto placed = Client::Scenes::PlaceInventoryItem(8, ItemBytes(GuardianAngel), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    EXPECT_EQ(hero.pet.model, ModelOf(GuardianAngel));
    const HelperState& angel = tested.world.Helpers()[0];
    EXPECT_TRUE(angel.alive);
    EXPECT_EQ(angel.type, 0x1c12u);
    EXPECT_EQ(angel.rider, PublishedSlot);
    EXPECT_EQ(angel.position, glm::vec3(764.0f, 1958.0f, 431.0f));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry + 3).State());
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

// 0x4c5e4f..0x4c5e61: antes de las entradas, 0x443d40 con el héroe publicado apaga sus ayudantes; los de
// otro jinete quedan.
TEST_F(InventoryReplyTest, TheReplyTurnsOffTheHorseOfThePublishedHero)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    constexpr std::size_t OtherRider = 7;
    auto& helpers = tested.world.Helpers();
    helpers[0] = HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = PublishedSlot};
    helpers[1] = HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = OtherRider};

    const auto applied = ApplyInventory(ListOf({Entry(12, Apple)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_FALSE(helpers[0].alive);
    EXPECT_TRUE(helpers[1].alive);
}

// 0x5bc9d6..0x5bc9db: Horn of Doom copia +0x1b a +0x31b antes de crear el ayudante (el 0x14c con el 2). Fuera de
// la zona segura (+0x3e2 del héroe en 0), 0x5bca46 crea el calco 0x61e/1 sobre el héroe, con él de dueño y sin rand();
// en ella, no.
TEST_F(InventoryReplyTest, HornOfDoomCopiesItsFlags1bAndSummonsOutsideTheSafeZone)
{
    for (const std::uint8_t safeZone : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(safeZone));
        Stage tested = Fresh();
        ActorState& hero = tested.PublishedHero();
        hero.safeZone = safeZone;
        hero.position = {1000.0f, 2000.0f, 300.0f};
        hero.angles = {0.0f, 0.0f, 90.0f};
        hero.light = {0.25f, 0.5f, 0.75f};

        const auto placed =
            Client::Scenes::PlaceInventoryItem(8, ItemBytes(HornOfDoom, 0, 20, 0x02), tested.Resources());

        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        EXPECT_EQ(hero.pet.flags1b, 0x02);
        EXPECT_TRUE(tested.world.Helpers()[0].alive);
        EXPECT_EQ(tested.world.Helpers()[0].type, 0x14cu);
        EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry).State());
        const auto& summon = tested.world.Map().effects.Effects()[0];
        EXPECT_EQ(summon.alive, safeZone == 0);
        if (safeZone == 0)
        {
            EXPECT_EQ(summon.type, Client::World::ColoredDecalEffect);
            EXPECT_EQ(summon.subtype, Client::World::PetSummonSubtype);
            EXPECT_EQ(summon.transform.position, hero.position);
            EXPECT_EQ(summon.transform.angles, hero.angles);
            EXPECT_EQ(summon.light, hero.light);
            EXPECT_EQ(summon.ownerContext, &hero);
            EXPECT_EQ(summon.life, 20);
        }
    }
}

// 0x5bca62: el Dark Raven del lugar 1 llama a 0x420cf0 con el héroe publicado. Suelta el cuervo que tenía y,
// fuera de los mapas 18 a 23, crea otro (0x4167b0) en el centro de su celda, 300 sobre el terreno, con los 12
// rand() de los cierres de sus celdas y del contador. En Chaos Castle solo lo suelta, sin más rand() que los de
// vestir con un escudo en el mismo lugar.
TEST_F(InventoryReplyTest, TheDarkRavenOfSlot1ReplacesTheRavenAt420cf0)
{
    constexpr int RavenRandoms = 3 * Client::Render::Models::SmallLogicalCloseRandoms;
    const auto testedStage = FreshOnTheHeap();
    Stage& tested = *testedStage;
    tested.world.Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    tested.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState& hero = tested.PublishedHero();
    hero.cellX = 133;
    hero.cellY = 127;
    hero.darkSpirit.emplace().state = 4;

    const auto placed = Client::Scenes::PlaceInventoryItem(1, ItemBytes(DarkRaven), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    EXPECT_EQ(hero.hands[1].model, ModelOf(DarkRaven));
    ASSERT_TRUE(hero.darkSpirit.has_value());
    EXPECT_EQ(hero.darkSpirit->state, 0);
    EXPECT_EQ(hero.darkSpirit->position, glm::vec3(13350.0f, 12750.0f, 300.0f));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry + RavenRandoms).State());

    const auto chaosStage = FreshOnTheHeap();
    Stage& chaos = *chaosStage;
    ActorState& rider = chaos.PublishedHero();
    chaos.world.Runtime().map = ChaosCastleMap;
    rider.darkSpirit.emplace();

    const auto inChaos = Client::Scenes::PlaceInventoryItem(1, ItemBytes(DarkRaven), chaos.Resources());

    const auto shieldedStage = FreshOnTheHeap();
    Stage& shielded = *shieldedStage;
    shielded.PublishedHero();
    shielded.world.Runtime().map = ChaosCastleMap;
    const auto withShield = Client::Scenes::PlaceInventoryItem(1, ItemBytes(SmallShield), shielded.Resources());

    ASSERT_TRUE(inChaos.HasValue()) << inChaos.Error();
    ASSERT_TRUE(withShield.HasValue()) << withShield.Error();
    EXPECT_FALSE(rider.darkSpirit.has_value());
    EXPECT_EQ(chaos.cloth.random.State(), shielded.cloth.random.State());
}

// 0x5bca6c..0x5bcaa7: el Wizard's Ring de nivel 3 en el 11, o en el 10 con +0x360 fuera de 1 a 3, suelta la capa
// (0x414340) y deja +0x360 en 4. De otro nivel, o en el 10 con +0x360 de 1 a 3, no lo toca.
TEST_F(InventoryReplyTest, ALevelThreeWizardsRingLeavesTheLayerAtFour)
{
    for (const int ring : {10, 11})
    {
        SCOPED_TRACE(ring);
        const auto slot = static_cast<std::uint8_t>(ring);
        const auto testedStage = FreshOnTheHeap();
        Stage& tested = *testedStage;
        ActorState& hero = tested.PublishedHero();
        hero.layer.emplace();
        hero.byte360 = 5;
        const auto placed = Client::Scenes::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x18), tested.Resources());
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        EXPECT_FALSE(hero.layer.has_value());
        EXPECT_EQ(hero.byte360, 4);

        const auto otherStage = FreshOnTheHeap();
        Stage& other = *otherStage;
        ActorState& plain = other.PublishedHero();
        plain.byte360 = 5;
        const auto levelTwo = Client::Scenes::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x10), other.Resources());
        ASSERT_TRUE(levelTwo.HasValue()) << levelTwo.Error();
        EXPECT_EQ(plain.byte360, 5);
    }

    const auto eventStage = FreshOnTheHeap();
    Stage& event = *eventStage;
    ActorState& bloodCastle = event.PublishedHero();
    bloodCastle.byte360 = 2;
    const auto inTen = Client::Scenes::PlaceInventoryItem(10, ItemBytes(WizardsRing, 0x18), event.Resources());
    ASSERT_TRUE(inTen.HasValue()) << inTen.Error();
    EXPECT_EQ(bloodCastle.byte360, 2);
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

// 0x4d1400, el 24 cifrado con 0: suelta la traba, deja la cuenta del cursor en 0, pone el ítem en el lugar del paquete
// (0x5bc740 con el último argumento en 0) y pide el sonido 0x1d. Con 0xff, 0x5be150 vuelve el ítem del cursor a su
// lugar.
TEST_F(InventoryReplyTest, TheItemMoveResultPlacesTheItemOrPutsItBack)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.ui.field78abb39 = 1;
    runtime.cursorItem.id = Apple;
    runtime.cursorItem.occupied = 1;
    runtime.cursorItemCount = 1;
    const auto moved = Client::Scenes::ApplyItemMoveResult(
        Client::Protocol::Inventory::ItemMoveResult{.result = 0, .slot = 12 + 9, .item = ItemBytes(Apple)},
        tested.Resources());
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(tested.Cell(9).id, Apple);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);

    Stage refused = Fresh();
    refused.PublishedHero();
    Client::Scenes::WorldRuntime& back = refused.world.Runtime();
    back.ui.field78abb39 = 1;
    back.cursorItem.id = Apple;
    back.cursorItem.gridColumn = 1;
    back.cursorItem.occupied = 1;
    back.cursorItemCount = 1;
    back.cursorGrid = Client::Scenes::CursorGrid::Inventory;
    back.slot78a6758 = 12 + 1;
    const auto rejected = Client::Scenes::ApplyItemMoveResult(
        Client::Protocol::Inventory::ItemMoveResult{.result = 0xff, .slot = 0, .item = {}}, refused.Resources());
    ASSERT_TRUE(rejected.HasValue()) << rejected.Error();
    EXPECT_EQ(back.ui.field78abb39, 0);
    EXPECT_EQ(back.cursorItemCount, 0);
    EXPECT_EQ(refused.Cell(1).id, Apple);
    EXPECT_EQ(refused.Cell(1).occupied, 1u);
}

// 0x4c5ec0 (28): 0x5bd750 con el lugar y, con pkt[4], el fin de la espera; desde el 0x4c, la tienda personal corta.
// 0x4d3cc0 (2A): la durabilidad del lugar del equipo, con su acceso al registro, o de toda la huella de la grilla.
TEST_F(InventoryReplyTest, TheDeleteAndTheDurabilityTouchTheirPlace)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12 + 2, ItemBytes(ShortSword), tested.Resources()).HasValue());
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12 + 6, ItemBytes(Apple), tested.Resources()).HasValue());
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();

    const auto worn = Client::Scenes::ApplyDurability(
        Client::Protocol::Inventory::Durability{.slot = 12 + 2, .durability = 7, .endsTheWait = 0}, tested.Resources());
    ASSERT_TRUE(worn.HasValue()) << worn.Error();
    for (const std::size_t cell : {2u, 10u, 18u})
        EXPECT_EQ(tested.Cell(cell).durability, 7) << cell;
    EXPECT_NE(tested.Cell(6).durability, 7);

    tested.world.OwnCharacter().equipment[3].id = GuardianHelm;
    const CrtRandom before = tested.cloth.random;
    runtime.ui.field5767954 = 10;
    const auto helm = Client::Scenes::ApplyDurability(
        Client::Protocol::Inventory::Durability{.slot = 3, .durability = 9, .endsTheWait = 1}, tested.Resources());
    ASSERT_TRUE(helm.HasValue()) << helm.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].durability, 9);
    EXPECT_EQ(runtime.ui.field5767954, 0);
    CrtRandom expected = before;
    expected.Discard(OwnRecordClose);
    EXPECT_EQ(tested.cloth.random.State(), expected.State());

    runtime.ui.field5767954 = 10;
    const auto deleted = Client::Scenes::ApplyItemDelete(
        Client::Protocol::Inventory::ItemDelete{.slot = 12 + 6, .endsTheWait = 1}, tested.Resources());
    ASSERT_TRUE(deleted.HasValue()) << deleted.Error();
    EXPECT_EQ(tested.Cell(6).id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ui.field5767954, 0);

    const auto shop = Client::Scenes::ApplyItemDelete(
        Client::Protocol::Inventory::ItemDelete{.slot = 0x4c, .endsTheWait = 0}, tested.Resources());
    ASSERT_FALSE(shop.HasValue());
    EXPECT_TRUE(shop.Error().starts_with("0x4c5ece:")) << shop.Error();
}

// 0x4d3510 (26): cada uno con el registro propio abierto, 3.848 rand() al cerrarlo; 0xfe la vida máxima, 0xff la vida,
// 0xfd el fin de la espera y otro lugar la poción: baja 1 y en 0 deja la casilla vacía. Fuera de la grilla, corta
// después de abrir el registro, que se cierra igual. Con el registro ya abierto no gasta nada.
TEST_F(InventoryReplyTest, TheLifeRefillWritesTheRecordOrSpendsThePotion)
{
    const std::unique_ptr<Stage> tested = FreshOnTheHeap();
    tested->PublishedHero();
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12 + 6, ItemBytes(Apple, 0, 2), tested->Resources()).HasValue());
    Client::Scenes::WorldState& world = tested->world;
    Client::Scenes::WorldRuntime& runtime = world.Runtime();
    CrtRandom& random = tested->cloth.random;
    namespace Inventory = Client::Protocol::Inventory;
    const auto refill = [&](std::uint8_t place, std::uint16_t life)
    { return Client::Scenes::ApplyLifeRefill(Inventory::LifeRefill{.place = place, .life = life}, world, random); };

    CrtRandom expected = random;
    ASSERT_TRUE(refill(Inventory::MaximumPlace, 300).HasValue());
    ASSERT_TRUE(refill(Inventory::CurrentPlace, 120).HasValue());
    expected.Discard(2 * OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.OwnCharacter().maxLife, 300);
    EXPECT_EQ(world.OwnCharacter().life, 120);

    runtime.ui.field5767954 = 10;
    ASSERT_TRUE(refill(Inventory::EndTheWaitPlace, 0).HasValue());
    EXPECT_EQ(runtime.ui.field5767954, 0);
    EXPECT_EQ(world.OwnCharacter().life, 120);

    ASSERT_TRUE(refill(12 + 6, 0).HasValue());
    EXPECT_EQ(tested->Cell(6).id, Apple);
    EXPECT_EQ(tested->Cell(6).durability, 1);
    ASSERT_TRUE(refill(12 + 6, 0).HasValue());
    EXPECT_EQ(tested->Cell(6).id, EmptyOwnItemId);
    EXPECT_EQ(tested->Cell(6).occupied, 0u);
    EXPECT_EQ(tested->Cell(6).durability, 0);

    expected = random;
    const auto outside = refill(12 + 64, 0);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_TRUE(outside.Error().starts_with("0x4d35f6:")) << outside.Error();
    expected.Discard(OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());

    expected = random;
    {
        const Client::Render::Models::LogicalAccess open(runtime.ownRecordDepth, random,
                                                         Client::Render::Models::OwnRecordCloseRandoms);
        ASSERT_TRUE(refill(Inventory::CurrentPlace, 80).HasValue());
        EXPECT_EQ(random.State(), expected.State());
    }
    EXPECT_EQ(world.OwnCharacter().life, 80);
}

// 0x4d3820 (27): 0xfe los máximos de maná y BP, 0xff los actuales y otro lugar el maná y la poción; el 0xfd es un lugar
// más, fuera de la grilla, y corta después de escribir el maná.
TEST_F(InventoryReplyTest, TheManaRefillWritesTheManaAndTheBpOrSpendsThePotion)
{
    const std::unique_ptr<Stage> tested = FreshOnTheHeap();
    tested->PublishedHero();
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(12 + 9, ItemBytes(Apple, 0, 1), tested->Resources()).HasValue());
    Client::Scenes::WorldState& world = tested->world;
    CrtRandom& random = tested->cloth.random;
    namespace Inventory = Client::Protocol::Inventory;
    const auto refill = [&](std::uint8_t place, std::uint16_t mana, std::uint16_t bp)
    {
        return Client::Scenes::ApplyManaRefill(Inventory::ManaRefill{.place = place, .mana = mana, .bp = bp}, world,
                                               random);
    };
    const Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();

    CrtRandom expected = random;
    ASSERT_TRUE(refill(Inventory::MaximumPlace, 200, 40).HasValue());
    ASSERT_TRUE(refill(Inventory::CurrentPlace, 150, 30).HasValue());
    expected.Discard(2 * OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(own.maxMana, 200);
    EXPECT_EQ(own.maxBp, 40);
    EXPECT_EQ(own.mana, 150);
    EXPECT_EQ(own.bp, 30);

    ASSERT_TRUE(refill(12 + 9, 190, 7).HasValue());
    EXPECT_EQ(own.mana, 190);
    EXPECT_EQ(own.bp, 30);
    EXPECT_EQ(tested->Cell(9).id, EmptyOwnItemId);
    EXPECT_EQ(tested->Cell(9).occupied, 0u);

    world.Runtime().ui.field5767954 = 10;
    const auto wait = refill(Inventory::EndTheWaitPlace, 170, 0);
    ASSERT_FALSE(wait.HasValue());
    EXPECT_TRUE(wait.Error().starts_with("0x4d3917:")) << wait.Error();
    EXPECT_EQ(own.mana, 170);
    EXPECT_EQ(world.Runtime().ui.field5767954, 10);
}

// 0x4e31a3 (F3 14): la cuenta del cursor en 0, el ítem en su lugar y el sonido 0x45. 0x4e316e (F3 13): sin un
// jugador con la clave, 0x5139f0 no hace nada; con él, su equipo es de la CL9.2.
TEST_F(InventoryReplyTest, TheItemPutAndTheEquipmentOfOthers)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    Client::Scenes::WorldRuntime& runtime = tested.world.Runtime();
    runtime.cursorItemCount = 1;
    const auto put = Client::Scenes::ApplyItemPut(
        Client::Protocol::Inventory::ItemPut{.slot = 12 + 4, .item = ItemBytes(Apple)}, tested.Resources());
    ASSERT_TRUE(put.HasValue()) << put.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(tested.Cell(4).id, Apple);
    EXPECT_EQ(runtime.lastSoundId, 0x45);

    hero.key = 0x1234;
    const auto nobody = Client::Scenes::ApplyEquipmentChange(
        Client::Protocol::Inventory::EquipmentChange{.key = 0x4321, .equipment = {}}, tested.Resources());
    EXPECT_TRUE(nobody.HasValue()) << nobody.Error();
    const auto player = Client::Scenes::ApplyEquipmentChange(
        Client::Protocol::Inventory::EquipmentChange{.key = 0x1234, .equipment = {}}, tested.Resources());
    ASSERT_FALSE(player.HasValue());
    EXPECT_TRUE(player.Error().starts_with("0x513a1e:")) << player.Error();
}
} // namespace
