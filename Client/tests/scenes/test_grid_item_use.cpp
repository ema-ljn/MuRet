#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/FloorItems.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GridItemUse.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
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
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El clic derecho sobre el ítem de la grilla desde 0x5c7bc6 y la joya sobre un ítem (0x5d82a8), CL9 hito 5, fase 2:
// los libros y las esferas, la fruta, el 0x1a0b, el resto y las tres joyas, con el C1 06 26 byte a byte y la cuenta de
// rand(). Los ítems, sus lugares, los stats y los textos son inventados.
namespace
{
using Client::Core::Expected;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::FloorItems::UseItem;
using Client::Render::Models::CrtRandom;
using Client::Scenes::InventoryMoveContext;
using Client::Scenes::InventoryServices;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::World::ItemState;
namespace Scenes = Client::Scenes;

constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
// Los dos rand() del envío en C3: el relleno y el serial.
constexpr int SendRandoms = 2;
constexpr std::int32_t Left = 0x1c2 + 15;
constexpr std::int32_t Top = 200;
constexpr std::uint8_t Kind = 3;

constexpr std::uint16_t Sword = 0x0001;           // 1 × 3
constexpr std::uint16_t Bolts = 0x0807;           // 1 × 1
constexpr std::uint16_t Wings = 0x1806;           // 1 × 1 acá: el último tipo de la joya
constexpr std::uint16_t Orb180f = 0x180f;         // fuera de las esferas
constexpr std::uint16_t Item1a0b = 0x1a0b;        // 1 × 1
constexpr std::uint16_t Fruit = 0x1a0f;           // 1 × 1
constexpr std::uint16_t Item1a1e = 0x1a1e;        // 1 × 1, el otro que acepta la joya
constexpr std::uint16_t Item1a25 = 0x1a25;        // 1 × 1, el del diálogo 0xa9
constexpr std::uint16_t Potion = 0x1c00;          // 1 × 1
constexpr std::uint16_t JewelOfBless = 0x1c0d;    // 1 × 1
constexpr std::uint16_t JewelOfSoul = 0x1c0e;     // 1 × 1
constexpr std::uint16_t JewelOfLife = 0x1c10;     // 1 × 1
constexpr std::uint16_t JewelOfCreation = 0x1c16; // 1 × 1
// Los libros y las esferas: los bordes de los tres tramos y el 0x1814 del medio.
constexpr std::array<std::uint16_t, 7> SkillItems{0x1e00, 0x1fff, 0x1807, 0x180e, 0x1810, 0x1814, 0x1818};
constexpr std::uint16_t RequiredLevel = 50;
constexpr std::size_t RequiredLevelOffset = 0x3a;

Client::Data::Tables::ItemTable MakeItems()
{
    std::vector<Client::Tests::ItemShape> shapes{{Sword, 1, 3, 0},
                                                 {Bolts, 1, 1, 1},
                                                 {Wings, 1, 1, 7},
                                                 {Orb180f, 1, 1, 0xff},
                                                 {Item1a0b, 1, 1, 0xff},
                                                 {Fruit, 1, 1, 0xff},
                                                 {Item1a1e, 1, 1, 7},
                                                 {Item1a25, 1, 1, 8},
                                                 {Potion, 1, 1, 0xff},
                                                 {JewelOfBless, 1, 1, 0xff},
                                                 {JewelOfSoul, 1, 1, 0xff},
                                                 {JewelOfLife, 1, 1, 0xff},
                                                 {JewelOfCreation, 1, 1, 0xff}};
    std::vector<Client::Tests::ItemWord> words;
    for (const std::uint16_t type : SkillItems)
    {
        shapes.push_back({type, 1, 1, 0xff});
        words.push_back({type, RequiredLevelOffset, RequiredLevel});
    }
    return Client::Tests::ItemTableOf(shapes, words);
}

class GridItemUseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().ui.field78abb26 = Kind;
        services.items = &items;
        services.texts = &texts;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.push_back(index);
            messageTypes.push_back(type);
            return Expected<void, std::string>{};
        };
        services.sendUseItem = [this](const UseItem& use)
        {
            sent.push_back(use);
            return Expected<void, std::string>{};
        };
    }

    [[nodiscard]] InventoryMoveContext Moves()
    {
        return InventoryMoveContext{.world = world, .random = random, .services = services};
    }

    // El ítem `type` desde la casilla (column, row), como lo deja 0x5bc740: la copia en cada casilla, con el origen.
    void Put(std::uint16_t type, std::uint8_t column, std::uint8_t row, std::uint32_t scalar1Low = 0)
    {
        const auto footprint = Scenes::ItemFootprintOf(&items, type, "Put");
        ASSERT_TRUE(footprint.HasValue()) << footprint.Error();
        for (std::int32_t y = 0; y < footprint.Value().height; ++y)
        {
            for (std::int32_t x = 0; x < footprint.Value().width; ++x)
            {
                ItemState& cell = Cell(column + x, row + y);
                cell = ItemState{};
                cell.id = type;
                cell.scalar1Low = scalar1Low;
                cell.durability = 0x2c;
                cell.gridColumn = column;
                cell.gridRow = row;
                cell.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    ItemState& Cell(std::int32_t column, std::int32_t row)
    {
        return world.Inventory().cells.at(static_cast<std::size_t>(row) * Scenes::InventoryColumns +
                                          static_cast<std::size_t>(column));
    }

    static std::size_t Index(std::int32_t column, std::int32_t row)
    {
        return static_cast<std::size_t>(row) * Scenes::InventoryColumns + static_cast<std::size_t>(column);
    }

    Expected<void, std::string> RightClick(std::int32_t column, std::int32_t row)
    {
        Scenes::GamePanels panels(world, random);
        return Scenes::UseTheLaterGridItem(Moves(), panels.Registry(), Index(column, row));
    }

    // El C1 06 26 que salió, descifrado.
    [[nodiscard]] std::vector<std::byte> SentBytes(std::size_t which) const
    {
        const auto packet = Client::Protocol::FloorItems::WriteUseItem(sent.at(which));
        const auto decoded = Xor32Decode(packet.packet);
        const std::span<const std::byte> plain = decoded.Bytes();
        return {plain.begin(), plain.end()};
    }

    [[nodiscard]] static CrtRandom Advanced(CrtRandom from, int count)
    {
        from.Discard(static_cast<std::uint32_t>(count));
        return from;
    }

    // Los stats con los que se usan los libros: el nivel justo, la energía y la fuerza justas con lo que suman los
    // ítems.
    void Qualify()
    {
        Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = RequiredLevel;
        own.energy = 30;
        own.word30 = 10;
        own.strength = 20;
        own.word2a = 5;
    }

    WorldState world;
    CrtRandom random{0x2b6d1e5u};
    Client::Data::Tables::ItemTable items = MakeItems();
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{376, "usar %s?"},
                                                                        {474, "no se puede usar ahora"},
                                                                        {749, "desde el nivel diez"},
                                                                        {1865, "joya sobre el cuerno"},
                                                                        {1905, "solo una clase"},
                                                                        {1909, "sin equipo"}});
    InventoryServices services;
    std::vector<std::size_t> messages;
    std::vector<std::int32_t> messageTypes;
    std::vector<UseItem> sent;
};

// 0x5c8fb9..0x5c991b: cada libro y cada esfera con el nivel de Item.bmd (+0x3a), la energía (+0x1e y +0x30 contra el
// +0x22 del ítem) y la fuerza (+0x18 y +0x2a contra el +0x1e) manda el C1 06 26 con la casilla de arriba a la
// izquierda + 12, 0 y [0x78abb26]: el rand() del comercio, los dos del envío y después los 3.848 del cierre del
// registro propio.
TEST_F(GridItemUseTest, EachBookAndOrbWithItsRequirementsSendsTheUseByteByByte)
{
    Qualify();
    for (const std::uint16_t type : SkillItems)
    {
        SCOPED_TRACE(type);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        Put(type, 3, 2);
        Cell(3, 2).word22 = 40;
        Cell(3, 2).word1e = 25;
        const CrtRandom expected = Advanced(random, 1 + SendRandoms + Own);
        const auto used = RightClick(3, 2);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, 2 * 8 + 3 + 12, 0x00, Kind})));
        EXPECT_EQ(world.Runtime().ui.field5767954, 10);
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
        EXPECT_TRUE(messages.empty());
    }
}

// 0x5c90ca..0x5c90e3: tres JC sobre WORD. Con un punto de menos en el nivel, la energía o la fuerza no hay aviso ni
// envío: solo el cierre del registro. La suma de 16 bits envuelve: 0xfff0 de energía con 0x20 de los ítems da 0x10.
TEST_F(GridItemUseTest, ABookWithoutItsLevelEnergyOrStrengthDoesNothing)
{
    Put(0x1e00, 0, 0);
    Cell(0, 0).word22 = 40;
    Cell(0, 0).word1e = 25;
    const auto refused = [this](auto&& lower)
    {
        Qualify();
        lower(world.OwnCharacter());
        const CrtRandom expected = Advanced(random, Own);
        const auto used = RightClick(0, 0);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_TRUE(sent.empty());
        EXPECT_TRUE(messages.empty());
        EXPECT_EQ(world.Runtime().ui.field5767954, 0);
    };
    refused([](Scenes::OwnCharacterRecord& own) { own.level = RequiredLevel - 1; });
    refused([](Scenes::OwnCharacterRecord& own) { own.word30 = 9; });
    refused([](Scenes::OwnCharacterRecord& own) { own.word2a = 4; });
    refused(
        [](Scenes::OwnCharacterRecord& own)
        {
            own.energy = 0xfff0;
            own.word30 = 0x20;
        });
}

// Las consultas en línea (0x5c90e9..0x5c912b): con el almacén abierto, el aviso 474 del tipo 2 sin consultar el
// comercio; con 0x5767954 en más de 0, nada, después del rand() del comercio.
TEST_F(GridItemUseTest, ABookWithTheWarehouseOpenOrTheWaitWarnsOrWaits)
{
    Qualify();
    Put(0x1810, 5, 5);
    world.Runtime().panels.warehouse = 1;
    CrtRandom expected = Advanced(random, Own);
    ASSERT_TRUE(RightClick(5, 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(messageTypes, std::vector<std::int32_t>{2});
    EXPECT_TRUE(sent.empty());

    world.Runtime().panels.warehouse = 0;
    world.Runtime().ui.field5767954 = 3;
    expected = Advanced(random, 1 + Own);
    ASSERT_TRUE(RightClick(5, 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().ui.field5767954, 3);
}

// 0x5c8e37..0x5c8fb8: una espada (sobre su casilla del medio) y el 0x180f no son de ningún caso: la copia al cursor
// y, sin el almacén ni 0x7d368a1, el cursor en ceros. Sin rand() (IsOpen(0x40) no
// saca) y la grilla como estaba. Con el almacén abierto, corta (hito 6).
TEST_F(GridItemUseTest, AnyOtherItemLeavesTheCursorEmptyAndTheWarehouseCuts)
{
    Put(Sword, 1, 1, 3u << 3);
    Put(Orb180f, 4, 4);
    for (const std::size_t index : {Index(1, 2), Index(4, 4)})
    {
        SCOPED_TRACE(index);
        world.Runtime().cursorItem.id = Potion;
        world.Runtime().cursorGrid = Scenes::CursorGrid::None;
        world.Runtime().ui.field78abb28 = 0;
        const CrtRandom expected = random;
        Scenes::GamePanels panels(world, random);
        const auto used = Scenes::UseTheLaterGridItem(Moves(), panels.Registry(), index);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().cursorItem.id, 0);
        EXPECT_EQ(world.Runtime().cursorItem.occupied, 0u);
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(world.Runtime().cursorGrid, Scenes::CursorGrid::Inventory);
        EXPECT_EQ(world.Runtime().ui.field78abb28, 1);
        EXPECT_TRUE(sent.empty());
    }
    EXPECT_EQ(Cell(1, 1).id, Sword);
    EXPECT_EQ(Cell(1, 3).id, Sword);
    EXPECT_EQ(Cell(4, 4).id, Orb180f);

    world.Runtime().panels.warehouse = 1;
    const auto cut = RightClick(1, 1);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5c8e8f"), std::string::npos) << cut.Error();
    EXPECT_NE(cut.Error().find("hito 6"), std::string::npos) << cut.Error();
}

// 0x5c7c10..0x5c8447: la fruta abre y cierra el registro propio para el nivel (3.848 rand()) y, con menos de 10, avisa
// con el 749 (0x632e30: el 0x8a). Desde el 10 la guarda en 0x78a59c8 con su casilla, lo abre y lo cierra otra vez
// (otros 3.848) para el equipo y la clase: con algo puesto, el 1909.
TEST_F(GridItemUseTest, TheFruitAsksTheLevelAndThenTheEmptyEquipment)
{
    Put(Fruit, 6, 3, 1u << 3);
    world.OwnCharacter().level = 9;
    CrtRandom expected = Advanced(random, Own);
    ASSERT_TRUE(RightClick(6, 3).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Client::UI::DialogRow(world.Runtime().dialogBox, 0), "desde el nivel diez");
    EXPECT_EQ(world.Runtime().dialogs.current, Client::UI::MessageBoxDialog);
    EXPECT_EQ(world.Runtime().dialogItem.id, 0);

    world.Runtime().dialogs.current = 0;
    world.OwnCharacter().level = 10;
    world.OwnCharacter().equipment[11].id = 0x1a14;
    expected = Advanced(random, 2 * Own);
    ASSERT_TRUE(RightClick(6, 3).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Client::UI::DialogRow(world.Runtime().dialogBox, 0), "sin equipo");
    EXPECT_EQ(world.Runtime().dialogItem.id, Fruit);
    EXPECT_EQ(world.Runtime().dialogItem.occupied, 1u);
    EXPECT_EQ(world.Runtime().dialogItemCell, 3 * 8 + 6);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_TRUE(sent.empty());
}

// 0x5c83ad..0x5c83f3: sin equipo, la fruta de nivel 4 (el dword +4 entero en 0x20) sin la clase 4 avisa con el 1905;
// con la clase 4, o con otro bit en el dword (Q-C1244), sigue al diálogo 0x96, que corta con su texto (0x63330f,
// CL9.3). Un nivel 0 también.
TEST_F(GridItemUseTest, TheFourthFruitIsForTheFourthClassAndTheDialogCuts)
{
    world.OwnCharacter().level = 200;
    world.OwnCharacter().characterClass = 1;
    Put(Fruit, 0, 7, 4u << 3);
    ASSERT_TRUE(RightClick(0, 7).HasValue());
    EXPECT_EQ(Client::UI::DialogRow(world.Runtime().dialogBox, 0), "solo una clase");

    const auto dialog = [this](std::uint32_t scalar1Low, std::uint8_t heroClass)
    {
        world.OwnCharacter().characterClass = heroClass;
        Cell(0, 7).scalar1Low = scalar1Low;
        const CrtRandom expected = Advanced(random, 2 * Own);
        const auto cut = RightClick(0, 7);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x63330f"), std::string::npos) << cut.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    };
    dialog(4u << 3, 4);
    dialog((4u << 3) | 4u, 1);
    dialog(0, 1);
}

// 0x5c8448..0x5c8473: el 0x1a0b de nivel 0 se usa; el de nivel 1 no con el héroe maestro del gremio (+0x368 en 0x80);
// el de nivel 2, nunca. El uso es el de la grilla: dos rand(), sin el registro propio.
TEST_F(GridItemUseTest, The1a0bOfLevelZeroOrOneIsUsed)
{
    Put(Item1a0b, 7, 0);
    const auto use = [this](std::uint32_t level, std::uint8_t guild, bool sends)
    {
        SCOPED_TRACE(level);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        Cell(7, 0).scalar1Low = level << 3;
        world.Actors()[world.Runtime().hero].guildStatus = guild;
        const CrtRandom expected = Advanced(random, sends ? 1 + SendRandoms : 0);
        ASSERT_TRUE(RightClick(7, 0).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), sends ? 1u : 0u);
        if (sends)
        {
            EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, 7 + 12, 0x00, Kind})));
        }
    };
    use(0, 0x80, true);
    use(1, 0, true);
    use(1, 0x80, false);
    use(2, 0, false);
}

// 0x5c67f3..0x5c685f: el 0x1a1d mira la tienda de NPC (con su rand()), el almacén, el comercio (con el suyo), 0x1000 y
// 0x4000, y con la primera abierta no hace nada (0x5c9b0b). Con todas cerradas, y siempre con el 0x1c13 y el 0x1a12,
// el 91 corta (hito 11).
TEST_F(GridItemUseTest, TheTicket1a1dLooksAtTheShopFirst)
{
    Scenes::GamePanels panels(world, random);
    Client::UI::PanelState& state = world.Runtime().panels;
    const auto look = [&](std::uint8_t& open, int randoms)
    {
        open = 1;
        const CrtRandom expected = Advanced(random, randoms);
        const auto used = Scenes::UseTheEventTicket(panels.Registry(), 0x1a1d);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        open = 0;
    };
    {
        SCOPED_TRACE("tienda");
        look(state.npcShop, 1);
    }
    {
        SCOPED_TRACE("almacén");
        look(state.warehouse, 1);
    }
    {
        SCOPED_TRACE("comercio");
        look(state.trade, 2);
    }
    const CrtRandom expected = Advanced(random, 2);
    const auto cut = Scenes::UseTheEventTicket(panels.Registry(), 0x1a1d);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5c6865"), std::string::npos) << cut.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(sent.empty());

    state.npcShop = 1;
    for (const auto& [type, address] : {std::pair{0x1c13, "0x5c6c36"}, std::pair{0x1a12, "0x5c7010"}})
    {
        const CrtRandom unchanged = random;
        const auto other = Scenes::UseTheEventTicket(panels.Registry(), type);
        ASSERT_FALSE(other.HasValue());
        EXPECT_NE(other.Error().find(address), std::string::npos) << other.Error();
        EXPECT_EQ(random.State(), unchanged.State());
        EXPECT_TRUE(Scenes::IsAnEventTicket(type));
    }
    EXPECT_FALSE(Scenes::IsAnEventTicket(0x1a1e));
}

// 0x5a3296..0x5a32c9 y 0x5d8c9a: el sonido lee la casilla lugar − 12 después del envío. Con un lugar del equipo o más
// allá de la grilla, el original lee la memoria de al lado; el paquete ya salió y MuRet no la lee: sin sonido ni corte.
TEST_F(GridItemUseTest, AUseFromOutsideTheGridSendsWithoutTheSound)
{
    for (const std::int32_t slot : {3, 12 + static_cast<std::int32_t>(Scenes::InventoryCellCount)})
    {
        SCOPED_TRACE(slot);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        world.Runtime().lastSoundId.reset();
        Scenes::GamePanels panels(world, random);
        const CrtRandom expected = Advanced(random, 1 + SendRandoms);
        const auto used = Scenes::UseInventoryItemOn(Moves(), panels.Registry(),
                                                     Scenes::InventoryUse{.slot = slot, .target = 20, .where = "test"});
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, static_cast<std::uint8_t>(slot), 20, Kind})));
        EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
    }
}

// La joya del cursor sobre un ítem de la grilla (0x5ca250 → 0x5d82a8), con el clic y el inventario abierto.
class JewelTest : public GridItemUseTest
{
protected:
    void SetUp() override
    {
        GridItemUseTest::SetUp();
        world.Runtime().cursorItemCount = 1;
        world.Runtime().cursorGrid = Scenes::CursorGrid::Inventory;
        world.Runtime().slot78a6758 = JewelCell + 12;
    }

    Expected<bool, std::string> Drop(std::uint16_t jewel, std::int32_t column, std::int32_t row,
                                     bool inventoryOpen = true)
    {
        world.Runtime().cursorItem = ItemState{};
        world.Runtime().cursorItem.id = jewel;
        world.Runtime().cursorItem.occupied = 1;
        world.Runtime().movement.pendingClick = true;
        world.Runtime().ui.field5767954 = 0;
        sent.clear();
        Scenes::GamePanels panels(world, random);
        return Scenes::DropOnTheInventoryGrid(Moves(), Scenes::GridDrop{.mouseX = Left + column * 20 + 10,
                                                                        .mouseY = Top + row * 20 + 10,
                                                                        .left = Left,
                                                                        .top = Top,
                                                                        .inventoryOpen = inventoryOpen,
                                                                        .panels = &panels.Registry()});
    }

    static constexpr std::int32_t JewelCell = 5;
};

// 0x5d82a8..0x5d8cd3: sobre la casilla del medio de una espada de nivel 3, la joya de bendición manda el C1 06 26 con
// su lugar ([0x78a6758]), el del origen de la espada (0x77f3cc8) y [0x78abb26]: dos rand(); da verdadero. El sonido
// lee la casilla de la joya, vacía.
TEST_F(JewelTest, TheBlessOnASwordSendsItsPlaceAndTheSwordsByteByByte)
{
    Put(Sword, 2, 1, 3u << 3);
    const CrtRandom expected = Advanced(random, 1 + SendRandoms);
    const auto dropped = Drop(JewelOfBless, 2, 2);
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, JewelCell + 12, 1 * 8 + 2 + 12, Kind})));
    EXPECT_EQ(world.Runtime().itemMoveTarget, 1 * 8 + 2 + 12);
    EXPECT_EQ(world.Runtime().ui.field5767954, 10);
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
}

// 0x5d82d4..0x5d8316: hasta 0x1806 y el 0x1a1e, sin los virotes ni las flechas; la de bendición hasta el nivel 5, la
// del alma hasta el 8 y la de la vida en cualquiera. Lo que no va da verdadero sin rand() ni envío.
TEST_F(JewelTest, EachJewelGoesOnItsTypesAndLevels)
{
    struct Case
    {
        std::uint16_t jewel;
        std::uint16_t type;
        std::uint32_t level;
        bool sends;
    };
    for (const Case& tried :
         {Case{JewelOfBless, Sword, 5, true}, Case{JewelOfBless, Sword, 6, false}, Case{JewelOfSoul, Sword, 8, true},
          Case{JewelOfSoul, Sword, 9, false}, Case{JewelOfLife, Sword, 15, true}, Case{JewelOfLife, Wings, 0, true},
          Case{JewelOfLife, Item1a1e, 0, true}, Case{JewelOfLife, Bolts, 0, false}, Case{JewelOfSoul, Potion, 0, false},
          Case{JewelOfBless, Item1a0b, 0, false}})
    {
        SCOPED_TRACE(tried.type);
        SCOPED_TRACE(tried.jewel);
        world.Inventory().ClearForInventoryReply();
        Put(tried.type, 4, 4, tried.level << 3);
        const CrtRandom expected = Advanced(random, tried.sends ? 1 + SendRandoms : 0);
        const auto dropped = Drop(tried.jewel, 4, 4);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(sent.size(), tried.sends ? 1u : 0u);
    }
}

// 0x5d8324..0x5d8372: la de bendición sobre el 0x1a25 sin la durabilidad 0xff guarda la casilla de la joya y el
// destino (0x7d3a0dc y 0x7d3a0e0) y abre el diálogo 0xa9 con el texto 1865, sin envío, aunque tenga nivel 6
// (Q-C1246); con 0xff, como cualquier tipo de más de 0x1806, nada.
TEST_F(JewelTest, TheBlessOnThe1a25OpensItsDialog)
{
    Put(Item1a25, 6, 6, 6u << 3);
    const auto dropped = Drop(JewelOfBless, 6, 6);
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().dialogs.current, 0xa9);
    EXPECT_EQ(Client::UI::DialogRow(world.Runtime().dialogBox, 0), "joya sobre el cuerno");
    EXPECT_EQ(world.Runtime().dialogBox.jewelCell, JewelCell);
    EXPECT_EQ(world.Runtime().dialogBox.jewelTarget, 6 * 8 + 6 + 12);

    world.Runtime().dialogs.current = 0;
    Cell(6, 6).durability = 0xff;
    const auto full = Drop(JewelOfBless, 6, 6);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_TRUE(full.Value());
    EXPECT_EQ(world.Runtime().dialogs.current, 0);
    EXPECT_TRUE(sent.empty());
}

// Las consultas en línea (0x5d838c..0x5d83d7): con el almacén abierto, el destino queda en 0x77f3cc8 y avisa con el
// 474. La joya de la creación, o el inventario cerrado, no entran: da falso.
TEST_F(JewelTest, TheWarehouseWarnsAndCreationOrAClosedInventoryDoNotEnter)
{
    Put(Sword, 0, 0, 0);
    world.Runtime().panels.warehouse = 1;
    const auto warned = Drop(JewelOfLife, 0, 1);
    ASSERT_TRUE(warned.HasValue()) << warned.Error();
    EXPECT_TRUE(warned.Value());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(world.Runtime().itemMoveTarget, 12);
    EXPECT_TRUE(sent.empty());

    world.Runtime().panels.warehouse = 0;
    for (const auto& [jewel, open] : {std::pair{JewelOfCreation, true}, std::pair{JewelOfBless, false}})
    {
        const auto dropped = Drop(jewel, 0, 1, open);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_FALSE(dropped.Value());
        EXPECT_TRUE(sent.empty());
    }
}
} // namespace
