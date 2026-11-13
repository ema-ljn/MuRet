#include "Scenes/NpcWindows.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LoginModels.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/PanelRegistry.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las ventanas de los NPC del lado de la escena (CL9 hito 6; 04 § Las ventanas de los NPC), sin actores de NPC: el
// estado abierto lo pone el 30 como lo pone 0x4d1e00, y los paquetes que salen se miran byte a byte. Los ítems y los
// textos son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Render::Models::CrtRandom;
using Client::Scenes::NpcWindowState;
using Client::Scenes::WorldEntryResources;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Npc = Client::Protocol::NpcWindows;
namespace UI = Client::UI;

constexpr std::uint32_t Seed = 0x1357;
constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
constexpr std::size_t HeroSlot = 7;
// Un arma de 1 por 3, un escudo de 2 por 2 y una joya de 1 por 1 (Item.bmd sintético).
constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Shield = 0x0c00;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::size_t ChaosBoxNotEmptyText = 593;
constexpr std::size_t MixSucceededText = 595;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// Los siete bytes de un ítem para `type`, como los arma el GameServer (0x54b3a0 al revés).
Npc::ItemData ItemBytes(std::uint16_t type, std::uint8_t durability = 20)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{0},
            std::byte{durability},
            static_cast<std::byte>((type >> 1) & 0x80),
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

struct Stage
{
    Stage()
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().hero = HeroSlot;
        world.Actors()[HeroSlot] = Client::Tests::UnarmedPlayer();
        world.Actors()[HeroSlot].alive = true;
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        WorldEntryResources resources{.world = world,
                                      .selection = selection,
                                      .dataRoot = std::filesystem::path{},
                                      .cloth = cloth,
                                      .camera = camera,
                                      .cameraInput = cameraInput,
                                      .models = models,
                                      .items = &items,
                                      .itemSetTables = &sets,
                                      .log = log};
        resources.textMeasure = &measure;
        resources.texts = &texts;
        resources.sendNpcWindowPacket = [this](Client::Protocol::Outgoing packet)
        {
            sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
        return resources;
    }

    // El C1 del paquete `index` sin el XOR32, para compararlo byte a byte.
    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent.at(index).packet);
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{Sword, 1, 3, 0}, {Shield, 2, 2, 1}, {Jewel, 1, 1, 0xff}});
    Client::Data::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(
        {{ChaosBoxNotEmptyText, "la caja tiene algo"}, {594, "no salio"}, {MixSucceededText, "salio"}, {596, "no"}});
    LetterMeasure measure;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::vector<Client::Protocol::Outgoing> sent;
};

class NpcWindowRepliesTest : public ::testing::Test
{
protected:
    [[nodiscard]] Client::Core::Expected<void, std::string> Talk(std::uint8_t result)
    {
        return Client::Scenes::ApplyTalkResult(Npc::TalkResult{.result = result, .rates = {10, 20, 30, 40, 0, 0}},
                                               stage->Resources());
    }

    // El registro como lo arma el paso de la UI, con los cables de las ventanas.
    template <typename Body> void WithThePanels(Body body)
    {
        const WorldEntryResources resources = stage->Resources();
        Client::Scenes::GamePanels panels(stage->world, stage->cloth.random);
        Client::Scenes::LendTheNpcWindows(panels, resources);
        body(panels.Registry());
    }

    [[nodiscard]] NpcWindowState& Npc()
    {
        return stage->world.Runtime().npcWindows;
    }

    // En el heap: WorldState pesa unos 350 KB (la pila de MSVC es de 1 MB).
    std::unique_ptr<Stage> stage = std::make_unique<Stage>();
};

// 0x4d1e00 con 0 (el default de 0x4d2881): cerrar todo saca los rand() del comercio y de la tienda (2);
// 0x6599a0(0x10000) saca 9: su IsOpen, CanOpen (la tienda y el comercio), CloseByMask (el comercio), el inventario
// adentro (IsOpen, CanOpen con 2 y CloseByMask con 2) y la escritura del byte protegido. Después, cada consulta de la
// tienda abierta saca uno y da 1.
TEST_F(NpcWindowRepliesTest, TheDefaultOpensTheNpcShopWithTheRandomOfItsProtectedByte)
{
    const CrtRandom expected = Advanced(stage->cloth.random, 2 + 9);
    const auto talked = Talk(0);
    ASSERT_TRUE(talked.HasValue()) << talked.Error();
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    const UI::PanelState& state = stage->world.Runtime().panels;
    EXPECT_EQ(state.npcShop, 1);
    EXPECT_EQ(state.npcShopDepth, 0);
    EXPECT_EQ(state.inventory, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1c);
    EXPECT_TRUE(Npc().cursorWarpRequested);
    EXPECT_TRUE(stage->sent.empty());

    const CrtRandom query = Advanced(stage->cloth.random, 1);
    WithThePanels([](UI::PanelRegistry& panels) { EXPECT_TRUE(panels.IsOpen(UI::NpcShopPanel)); });
    EXPECT_EQ(stage->cloth.random.State(), query.State());
}

// 0x4d2532 y 0x4d253b: el almacén y la caja del caos, cada uno con el inventario. La caja manda el C1 04 BD 09 (con el
// XOR32, en claro), abre el diálogo 0x8e y copia las tasas.
TEST_F(NpcWindowRepliesTest, TheWarehouseAndTheChaosBoxOpenWithTheInventory)
{
    stage->world.Runtime().ui.field78abb20 = 5;
    Npc().warehouseLocked = 1;
    ASSERT_TRUE(Talk(2).HasValue());
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 1);
    EXPECT_EQ(stage->world.Runtime().panels.inventory, 1);
    EXPECT_EQ(Npc().warehouseLocked, 0);
    EXPECT_EQ(stage->world.Runtime().ui.field78abb20, 0);

    Npc().chaosMixState = 2;
    const auto chaos = Talk(3);
    ASSERT_TRUE(chaos.HasValue()) << chaos.Error();
    // Cerrar todo cerró el almacén con su C1 03 82; después, el BD 09.
    ASSERT_EQ(stage->sent.size(), 2u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x82})));
    EXPECT_EQ(stage->sent[1].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(stage->Plain(1).Bytes(), Bytes({0xc1, 0x04, 0xbd, 0x09})));
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 0);
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 1);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0x8e);
    EXPECT_EQ(Npc().chaosMixState, 0);
    EXPECT_EQ(Npc().chaosRates, (std::array<std::int32_t, 6>{10, 20, 30, 40, 0, 0}));
}

// 0x4d2591: el 7 abre el entrenador (0x8000): cerrar todo (2 rand()) y 0x6599a0(0x8000) con CanOpen y CloseByMask (la
// tienda y el comercio, 4), 0x449940(1) con la caja vacía y sin el inventario.
TEST_F(NpcWindowRepliesTest, TheTrainerOpensItsMenuWithoutTheInventory)
{
    auto& runtime = stage->world.Runtime();
    runtime.npcWindows.trainerBox[4].id = Jewel;
    const CrtRandom expected = Advanced(stage->cloth.random, 2 + 4);
    const auto talked = Talk(7);
    ASSERT_TRUE(talked.HasValue()) << talked.Error();
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(runtime.panels.delegated.trainer, 1);
    EXPECT_EQ(runtime.npcWindows.trainerBox[4].id, 0xffff);
    EXPECT_EQ(runtime.panels.inventory, 0);
    EXPECT_EQ(runtime.lastSoundId, 0x1c);
    EXPECT_TRUE(Npc().cursorWarpRequested);
    EXPECT_TRUE(stage->sent.empty());
}

// Con cada ventana abierta (y el inventario), el mundo es de 260 (0x5bb8e3..0x5bb9a3).
TEST_F(NpcWindowRepliesTest, TheWorldIs260WideWithEachWindowOpen)
{
    for (const std::uint8_t result : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
    {
        stage = std::make_unique<Stage>();
        ASSERT_TRUE(Talk(result).HasValue());
        WithThePanels([](UI::PanelRegistry& panels) { EXPECT_EQ(panels.WorldWidth(), 260); });
    }
}

// 0x657ab0(0x10000): el byte en 0 (un rand()), el C1 03 31 en claro y la grilla vacía; después, el inventario.
TEST_F(NpcWindowRepliesTest, ClosingTheShopSends31AndEmptiesTheGrid)
{
    ASSERT_TRUE(Talk(0).HasValue());
    Npc().grid[5].id = Jewel;
    Npc().grid[5].occupied = 1;
    // Close: IsOpen (1), CloseByMask con su IsOpen (1) y la escritura (1); el inventario no saca.
    const CrtRandom expected = Advanced(stage->cloth.random, 3);
    WithThePanels([](UI::PanelRegistry& panels) { ASSERT_TRUE(panels.Close(UI::NpcShopPanel).HasValue()); });
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->world.Runtime().panels.npcShop, 0);
    EXPECT_EQ(stage->world.Runtime().panels.inventory, 0);
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    EXPECT_EQ(Npc().grid[5].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(Npc().grid[5].occupied, 0);
}

// 0x65852b: con la traba del 24, el almacén no cierra y CloseByMask da 0, pero cierra lo demás; 0x658741: con algo en
// la caja del caos, el texto 593 y 0.
TEST_F(NpcWindowRepliesTest, TheLockAndAFullChaosBoxKeepTheirWindowsOpen)
{
    ASSERT_TRUE(Talk(2).HasValue());
    stage->world.Runtime().ui.field78abb39 = 1;
    stage->world.Runtime().panels.party = 1;
    WithThePanels(
        [](UI::PanelRegistry& panels)
        {
            const auto closed = panels.CloseByMask(UI::WarehousePanel | UI::PartyPanel);
            ASSERT_TRUE(closed.HasValue());
            EXPECT_FALSE(closed.Value());
        });
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 1);
    EXPECT_EQ(stage->world.Runtime().panels.party, 0);
    EXPECT_TRUE(stage->sent.empty());

    stage = std::make_unique<Stage>();
    ASSERT_TRUE(Talk(3).HasValue());
    stage->sent.clear();
    Npc().chaosBox[3].id = Jewel;
    Npc().chaosBox[3].occupied = 1;
    WithThePanels([](UI::PanelRegistry& panels) { ASSERT_TRUE(panels.Close(UI::ChaosBoxPanel).HasValue()); });
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 1);
    EXPECT_TRUE(stage->sent.empty());
    ASSERT_FALSE(stage->world.Runtime().chat.log.entries.empty());
    EXPECT_EQ(stage->world.Runtime().chat.log.entries.back().text, "la caja tiene algo");

    Npc().chaosBox[3].id = Client::Scenes::EmptyOwnItemId;
    WithThePanels([](UI::PanelRegistry& panels) { ASSERT_TRUE(panels.Close(UI::ChaosBoxPanel).HasValue()); });
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 0);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0);
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x87})));
}

// 0x4c5f60: la lista de la tienda va a 0x78a7070 con 0x5bc150 (8 por fila desde el lugar) y la de la caja del caos a
// 0x7d2d380 con 0x5bc740(.., 8, 4, ..), con 0x7d2dc80 en 2.
TEST_F(NpcWindowRepliesTest, TheListFillsTheShopGridAndTheChaosBox)
{
    Npc().grid[40].id = Jewel;
    const Npc::ItemList shop{.grid = 0,
                             .items = {{.slot = 2, .item = ItemBytes(Sword)}, {.slot = 4, .item = ItemBytes(Shield)}}};
    const auto listed = Client::Scenes::ApplyItemList(shop, stage->Resources());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    const auto& grid = Npc().grid;
    EXPECT_EQ(grid[40].id, Client::Scenes::EmptyOwnItemId);
    for (const std::size_t cell : {2u, 10u, 18u})
    {
        EXPECT_EQ(grid[cell].id, Sword) << cell;
        EXPECT_EQ(grid[cell].gridColumn, 2) << cell;
        EXPECT_EQ(grid[cell].gridRow, 0) << cell;
    }
    EXPECT_EQ(grid[2].occupied, 1);
    EXPECT_EQ(grid[10].occupied, 0);
    for (const std::size_t cell : {4u, 5u, 12u, 13u})
        EXPECT_EQ(grid[cell].id, Shield) << cell;

    const Npc::ItemList chaos{.grid = 3, .items = {{.slot = 9, .item = ItemBytes(Jewel)}}};
    ASSERT_TRUE(Client::Scenes::ApplyItemList(chaos, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().chaosMixState, 2);
    EXPECT_EQ(Npc().chaosBox[9].id, Jewel);
    EXPECT_EQ(Npc().chaosBox[9].gridColumn, 1);
    EXPECT_EQ(Npc().chaosBox[9].gridRow, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x44);

    const Npc::ItemList trainer{.grid = 5, .items = {}};
    const auto cut = Client::Scenes::ApplyItemList(trainer, stage->Resources());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4c5fb4"), std::string::npos);
}

// El 32: el ítem comprado en su lugar del inventario, el sonido 0x1d y 0x5767950 en 0; con 0xff, solo 0x5767950.
TEST_F(NpcWindowRepliesTest, ABoughtItemGoesToItsInventoryPlace)
{
    Npc().buyLock = 1;
    ASSERT_TRUE(Client::Scenes::ApplyBuyResult({.slot = 0xff, .item = {}}, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().buyLock, 0);
    Npc().buyLock = 1;
    const auto bought = Client::Scenes::ApplyBuyResult({.slot = 12 + 9, .item = ItemBytes(Jewel)}, stage->Resources());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    EXPECT_EQ(stage->world.Inventory().cells[9].id, Jewel);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1d);
    EXPECT_EQ(Npc().buyLock, 0);
}

// El 33 y el 34: el zen nuevo en +0xecc con el registro propio abierto; el 34, también 0x550d10 adentro; el 81, los dos
// zen (+0xed0 y +0xecc).
TEST_F(NpcWindowRepliesTest, SellRepairAndTheWarehouseMoneyWriteTheZen)
{
    stage->world.Runtime().cursorItemCount = 1;
    const CrtRandom sold = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Scenes::ApplySellResult({.result = 1, .money = 1234}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), sold.State());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1234u);
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1d);

    const auto repaired = Client::Scenes::ApplyRepairResult({.money = 1000}, stage->Resources());
    ASSERT_TRUE(repaired.HasValue()) << repaired.Error();
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1000u);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x25);
    ASSERT_TRUE(Client::Scenes::ApplyRepairResult({.money = 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1000u);

    const CrtRandom moved = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Scenes::ApplyWarehouseMoney({.result = 1, .warehouseMoney = 500, .inventoryMoney = 700},
                                                    stage->Resources())
                    .HasValue());
    EXPECT_EQ(stage->cloth.random.State(), moved.State());
    EXPECT_EQ(stage->world.OwnCharacter().warehouseZen, 500u);
    EXPECT_EQ(stage->world.OwnCharacter().zen, 700u);
}

// Guardar y sacar zen del almacén: el 83 con 0xc, con la traba y lo que esperaba en −1, manda 0x4e6f80(tipo, zen), C1
// 08 81 con el XOR32 y en claro, y deja la traba y 0x78abb1c en 1.
TEST_F(NpcWindowRepliesTest, StoringAndTakingZenSends81WhenThePasswordIsAccepted)
{
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{1}})
    {
        stage = std::make_unique<Stage>();
        ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 1}, stage->Resources()).HasValue());
        Npc().pendingMove = {.slot = -1, .second = type, .third = 0x12345, .fourth = 0};
        ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        ASSERT_EQ(stage->sent.size(), 1u);
        EXPECT_EQ(stage->sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, type, 0x45, 0x23, 0x01, 0x00})));
        EXPECT_EQ(Npc().warehouseLocked, 1);
        EXPECT_EQ(Npc().field78abb1c, 1);
        // Otra vez, con 0x78abb1c en 1, no manda nada.
        ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        EXPECT_EQ(stage->sent.size(), 1u);
    }
    ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 0xb}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0x86);
}

// El 83 con 0xc y un ítem esperando (0x4e47b0..0x4e47c6): 0x449d80 con los cuatro valores guardados, el C1 0E 24 del
// ítem del cursor con el XOR32 y en C3 (dos rand()), y la traba del 24 (0x78abb39) prendida. Con la traba ya prendida,
// 0x449d80 no manda nada, pero el 83 deja igual 0x6c932f y 0x78abb1c en 1.
TEST_F(NpcWindowRepliesTest, TakingAnItemSends24WhenThePasswordIsAccepted)
{
    for (const bool alreadyMoving : {false, true})
    {
        SCOPED_TRACE(alreadyMoving);
        stage = std::make_unique<Stage>();
        auto& runtime = stage->world.Runtime();
        ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 1}, stage->Resources()).HasValue());
        Npc().pendingMove = {.slot = 2, .second = 17, .third = 0, .fourth = 30};
        runtime.cursorItem = Client::World::ItemState{.id = Shield, .scalar1Low = 2u << 3, .durability = 9};
        runtime.cursorItemCount = 1;
        runtime.ui.field78abb39 = alreadyMoving ? 1 : 0;
        const CrtRandom expected = Advanced(stage->cloth.random, alreadyMoving ? 0 : 2);
        ASSERT_TRUE(Client::Scenes::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        EXPECT_EQ(Npc().warehouseLocked, 1);
        EXPECT_EQ(Npc().field78abb1c, 1);
        if (alreadyMoving)
        {
            EXPECT_TRUE(stage->sent.empty());
            continue;
        }
        ASSERT_EQ(stage->sent.size(), 1u);
        EXPECT_EQ(stage->sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x02, 17, 0x00, 0x10, 9, 0x00, 0x00, 0x60, 0x00, 0x00, 30})));
    }
}

// El 86 con 1 (0x4da2c5): el texto 595, la caja vacía y el ítem nuevo en el lugar 0, con 0x7d2dc80 en 2.
TEST_F(NpcWindowRepliesTest, ASuccessfulMixLeavesTheNewItemInTheChaosBox)
{
    Npc().chaosBox[20].id = Jewel;
    Npc().chaosMixState = 1;
    const auto mixed = Client::Scenes::ApplyChaosMixResult({.result = 1, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(mixed.HasValue()) << mixed.Error();
    EXPECT_EQ(Npc().chaosMixState, 2);
    EXPECT_EQ(Npc().chaosBox[20].id, Client::Scenes::EmptyOwnItemId);
    for (const std::size_t cell : {0u, 8u, 16u})
        EXPECT_EQ(Npc().chaosBox[cell].id, Sword) << cell;
    EXPECT_EQ(stage->world.Runtime().chat.log.entries.back().text, "salio");

    ASSERT_TRUE(Client::Scenes::ApplyChaosMixResult({.result = 2, .item = {}}, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().chaosMixState, 0);
}

// El 24 con 2 y con 3 (0x4d1cf6.. y 0x4d1d20..): 0x5bc740 con el último argumento en 0 en la grilla del almacén (8
// por fila) y en la caja del caos, y la cola de 0x5bd405 con el registro propio abierto; después, el sonido 0x1d.
TEST_F(NpcWindowRepliesTest, TheItemMoveResultPlacesInTheWarehouseAndInTheChaosBox)
{
    stage->world.Runtime().cursorItemCount = 1;
    const CrtRandom stored = Advanced(stage->cloth.random, Own);
    const auto warehouse =
        Client::Scenes::ApplyItemMoveResult({.result = 2, .slot = 10, .item = ItemBytes(Shield)}, stage->Resources());
    ASSERT_TRUE(warehouse.HasValue()) << warehouse.Error();
    EXPECT_EQ(stage->cloth.random.State(), stored.State());
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    for (const std::size_t cell : {10u, 11u, 18u, 19u})
    {
        EXPECT_EQ(Npc().grid[cell].id, Shield) << cell;
        EXPECT_EQ(Npc().grid[cell].gridColumn, 2) << cell;
        EXPECT_EQ(Npc().grid[cell].gridRow, 1) << cell;
    }
    EXPECT_EQ(Npc().grid[10].occupied, 1);
    EXPECT_EQ(Npc().grid[19].occupied, 0);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1d);

    stage->world.Runtime().lastSoundId = 0;
    const auto chaos =
        Client::Scenes::ApplyItemMoveResult({.result = 3, .slot = 1, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(chaos.HasValue()) << chaos.Error();
    for (const std::size_t cell : {1u, 9u, 17u})
        EXPECT_EQ(Npc().chaosBox[cell].id, Sword) << cell;
    EXPECT_EQ(Npc().chaosBox[1].occupied, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, 0x1d);
}

// 0x4d28a9..0x4d28f6: x = (260 · mx / 640) · ancho / 640, y = alto · my / 480.
TEST(NpcTalkCursorWarpTest, MovesTheCursorIntoTheWorldOf260)
{
    const auto warp = Client::Scenes::NpcTalkCursorWarp(320, 240, 1280, 960);
    EXPECT_EQ(warp.x, 260);
    EXPECT_EQ(warp.y, 480);
    const auto small = Client::Scenes::NpcTalkCursorWarp(639, 479, 800, 600);
    EXPECT_EQ(small.x, (259 * 800) / 640);
    EXPECT_EQ(small.y, (600 * 479) / 480);
}
} // namespace
