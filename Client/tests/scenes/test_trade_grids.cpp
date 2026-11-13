#include "Scenes/GameUi.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Trade.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/SocialRequests.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"
#include "World/WorldTerrain.h"

#include "support/ChatTexts.h"
#include "support/GoldenDir.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las grillas del comercio (CL9 hito 7, 08 P-C205 a) con el otro que trae el 0x12 al lado del héroe, en Lorencia (0) y
// Devias (2): levantar del inventario y soltar en la grilla propia (0x5c4110, 0x5deb1c → 0x5ca250 con 0x78a92a8: el 3C
// y el 24 a la grilla 1), la respuesta (0x4d1ce0), levantar de la grilla propia (0x5c52b1 y el 3C con el OK puesto) y
// soltar en el inventario (0x5ce4fe: el 24 desde la grilla 1 y la espera del OK), contra el ASM. Los nombres y los
// ítems son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::EmptyOwnItemId;
using Client::Tests::ViewportPlayerRecord;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x7ade;
constexpr std::uint16_t HeroKey = 33;
constexpr std::uint16_t OtherKey = 0x0456;
constexpr std::size_t Chosen = 2;
// Un bastón de 1 × 3 (0x0a05, nivel 2) y una espada de 1 × 3 que el otro pone.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr std::uint16_t SwordType = 0x0001;
// La ventana del inventario en (0x1c2, 0) con su grilla en x + 15 e y + 200; la del comercio en (0x104, 0), que deja su
// dibujo (0x5f3b40), con la propia en x + 15 e y + 0x10e.
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t InventoryGridLeft = InventoryX + 15;
constexpr std::int32_t InventoryGridTop = 200;
constexpr std::int32_t TradeX = 0x104;
constexpr std::int32_t TradeGridLeft = TradeX + 15;
constexpr std::int32_t TradeGridTop = 0x10e;
constexpr std::int32_t Cell = 20;
// El bastón en la casilla (1, 2) del inventario: lugar 12 + 17.
constexpr std::size_t StaffCell = 2 * 8 + 1;
// El paso con el inventario y el comercio abiertos y el registro propio abierto (sin los 3.848 de sus cierres): los 12
// de test_game_inventory_step.cpp con la ventana sola (la barra de skills, la cadena de Q, W y E y 0x5e4ce0) menos 3,
// porque el 0x20 abierto corta tres búsquedas en su primera consulta: la de cualquier panel (0x5a5511, de 0x1 hacia
// arriba, ya no llega a 0x10000), la de Q, W y E (0x5a343e) y la de los botones del pie (0x5e545f). Medido: la grilla
// del comercio y su ventana no sacan más.
constexpr int QuietStepRandoms = 12 - 3;
// IsOpen(0x20) de 0x5c5361 después de levantar y la del paso del ítem del cursor (0x5deb11).
constexpr int TradeQueryRandoms = 1;
// El relleno y el serial de un C3.
constexpr int C3Randoms = 2;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

Client::Protocol::Trade::Name NameOf(std::string_view text)
{
    Client::Protocol::Trade::Name name{};
    for (std::size_t at = 0; at < name.size(); ++at)
        name[at] = at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0};
    return name;
}

// Los siete bytes de un ítem del 24 y del 39 (0x5bc740): el tipo en el byte 0, el nivel en el 1, la durabilidad, el
// bit 8 del tipo en el 0x80 del 3 y los de arriba en el 5.
Client::Protocol::Inventory::ItemData ItemBytes(std::uint16_t type, std::uint8_t level)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{30},
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

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

Sent PlainOf(const Client::Protocol::Outgoing& outgoing)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    Sent sent{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
    for (const std::byte b : plain.Bytes())
        sent.bytes.push_back(std::to_integer<std::uint8_t>(b));
    return sent;
}

// El mundo del juego, en el heap.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        hero.byte88 = 1;
        world.Runtime().hero = 0;
        world.Runtime().drawingEnabled = true;
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        Client::Scenes::ClearTheTradeForSelection(world.Runtime().social.trade);
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log,
                .texts = &texts};
    }
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {SwordType, 1, 3, 0}, {0x1c00, 1, 1, 0xff}});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class TradeGridsTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        game = std::make_unique<Game>();
        const std::uint8_t map = GetParam();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        TheOtherArrives();
        TradeWithTheOther();
        services.items = &game->items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.sendItemMove = [this](const Client::Protocol::Inventory::ItemMove& move)
        {
            sent.push_back(PlainOf(Client::Protocol::Inventory::WriteItemMove(move)));
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendGamePacket = [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); };
        // El bastón del héroe, de nivel 2, en la casilla (1, 2) del inventario.
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        ASSERT_TRUE(Client::Scenes::PlaceMovedItem(Byte(12 + StaffCell), ItemBytes(StaffType, 2), game->Resources())
                        .HasValue());
        ASSERT_EQ(game->world.Inventory().cells[StaffCell].id, StaffType);
        // Las esquinas que dejan los dibujos (0x5f3b40 y 0x5f0c30) y el registro propio abierto alrededor de todo.
        runtime.hud.inventoryX = InventoryX;
        runtime.hud.inventoryY = 0;
        runtime.hud.tradeX = TradeX;
        runtime.hud.tradeY = 0;
        // La esquina del registro del chat fuera de su ventana, como la deja el paso de antes (0x636f04): la del
        // principio (0xba, 0x1a4) tapa la grilla propia y su paso (0x636e51, antes) se queda con el mouse.
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        runtime.ownRecordDepth = 1;
    }

    // La primera fila libre de dos celdas desde (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        for (int y = 100; y < 200; ++y)
            for (int x = 100; x < 200; ++x)
                if (cells[Client::Data::Terrain::CellIndex(Byte(x), Byte(y))] == 0 &&
                    cells[Client::Data::Terrain::CellIndex(Byte(x + 1), Byte(y))] == 0)
                    return {Byte(x), Byte(y)};
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    // El héroe en la primera celda y el 0x12 con Brezal en la de al lado.
    void TheOtherArrives()
    {
        const auto [x, y] = FreeRow();
        ActorState& hero = game->world.Actors()[0];
        hero.cellX = x;
        hero.cellY = y;
        const std::array<ViewportPlayerRecord, 1> records{{{.key = OtherKey,
                                                            .isNew = true,
                                                            .x = Byte(x + 1),
                                                            .y = y,
                                                            .name = "Brezal",
                                                            .targetX = Byte(x + 1),
                                                            .targetY = y}}};
        auto read = Client::Protocol::Viewport::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyViewportPlayers(read.Value(), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ASSERT_EQ(game->world.Actors()[1].key, OtherKey);
    }

    // /trade al elegido y el 37 con el sí, que abre el comercio con el inventario.
    void TradeWithTheOther()
    {
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        runtime.selectedTargets[Chosen] = 1;
        std::vector<Client::Protocol::Outgoing> asked;
        const auto requested = Client::Scenes::RequestATrade(
            {.world = game->world,
             .random = game->cloth.random,
             .texts = game->texts,
             .send = [&asked](Client::Protocol::Outgoing packet) { asked.push_back(std::move(packet)); }});
        ASSERT_TRUE(requested.HasValue()) << requested.Error();
        ASSERT_EQ(asked.size(), 1U);
        const auto opened = Client::Scenes::ApplyTradeResponse({1, NameOf("Brezal"), 150, 0}, game->Resources());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_EQ(runtime.panels.trade, 1);
        ASSERT_EQ(runtime.panels.inventory, 1);
    }

    // Un paso de la UI con el mouse en (x, y) y, si `click`, el flanco del clic.
    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click)
    {
        if (click)
            Client::Scenes::LatchTheLeftButton(game->world.Runtime().movement, true, true);
        return Client::Scenes::StepTheGameUi(Client::Scenes::GameUiContext{.world = game->world,
                                                                           .random = game->cloth.random,
                                                                           .input = input,
                                                                           .mouseX = x,
                                                                           .mouseY = y,
                                                                           .inventory = &services});
    }

    CrtRandom& Random()
    {
        return game->cloth.random;
    }

    std::unique_ptr<Game> game;
    Client::Input::InputState input;
    Client::Scenes::InventoryServices services;
    std::vector<Sent> sent;
};

// Poner el bastón en la grilla propia y sacarlo. Levantarlo del inventario: la marca, el cursor desde el lugar 29 y
// las dos consultas del comercio. Soltarlo sobre la casilla (2, 0) de la grilla propia: el OK en 0 y el C1 04 3C 00,
// después el 24 del inventario (0, 29) a la grilla 1 en el lugar 2, cada uno con su C3. La respuesta (C3 0C 24 con 1)
// lo pone en las casillas 2, 10 y 18 con la columna 2 y la fila 0, y el sonido 0x1d. Con el OK puesto otra vez,
// levantarlo desde la casilla 10 lo levanta desde la 2 (el lugar 2, sin el 12), vacía su huella y manda el 3C con el
// OK en 0. Soltarlo en la casilla (3, 1) del inventario manda el 24 de la grilla 1 (2) al inventario (0, 23) y, sin el
// OK, pone la espera del OK en 0x96. La respuesta (con 0) lo vuelve al inventario.
TEST_P(TradeGridsTest, PutAnItemInTheTradeAndTakeItOut)
{
    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    Client::Scenes::TradeState& trade = runtime.social.trade;
    auto& inventory = game->world.Inventory().cells;

    // Levantar el bastón del inventario.
    CrtRandom expected = Advanced(Random(), QuietStepRandoms + 2 * TradeQueryRandoms);
    auto stepped = Step(InventoryGridLeft + Cell + 5, InventoryGridTop + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.cursorItem.id, StaffType);
    EXPECT_EQ(runtime.cursorGrid, Client::Scenes::CursorGrid::Inventory);
    EXPECT_EQ(runtime.slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(inventory[StaffCell].id, EmptyOwnItemId);
    EXPECT_TRUE(sent.empty());

    // Sin el clic sobre la grilla propia: las marcas de la huella, 2 si entra.
    const std::int32_t dropX = TradeGridLeft + 2 * Cell + 5;
    const std::int32_t dropY = TradeGridTop + Cell + 5;
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms);
    stepped = Step(dropX, dropY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(trade.ownGrid[2].warning, 2);
    EXPECT_EQ(trade.ownGrid[10].warning, 2);
    EXPECT_EQ(trade.ownGrid[18].warning, 2);
    EXPECT_TRUE(sent.empty());

    // Soltarlo: el 3C y el 24, cada uno con los dos rand() del C3.
    trade.ownOk = 1;
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms + 2 * C3Randoms);
    stepped = Step(dropX, dropY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x00, 29, 0x05, 2 << 3, 30, 0x00, 0x00, 0x50,
                                                        0x00, 0x01, 0x02}));
    EXPECT_TRUE(sent[1].encrypted);
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(runtime.itemMoveTarget, 2);
    EXPECT_FALSE(runtime.movement.pendingClick);

    // La respuesta: 0x5bc740 en la grilla propia, sin rand() con el registro abierto.
    expected = Random();
    auto applied = Client::Scenes::ApplyItemMoveResult({1, 2, ItemBytes(StaffType, 2)}, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);
    for (const std::size_t cell : {2U, 10U, 18U})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(trade.ownGrid[cell].id, StaffType);
        EXPECT_EQ(trade.ownGrid[cell].gridColumn, 2);
        EXPECT_EQ(trade.ownGrid[cell].gridRow, 0);
    }
    EXPECT_EQ(trade.ownGrid[2].occupied, 1U);
    EXPECT_EQ(trade.ownGrid[10].occupied, 0U);

    // Sin el clic sobre la casilla 10: la marca de la huella, el ítem del cartel y 0x5aed40 con 2 y la casilla.
    sent.clear();
    const std::int32_t takeX = TradeGridLeft + 2 * Cell + 5;
    const std::int32_t takeY = TradeGridTop + Cell + 5;
    expected = Advanced(Random(), QuietStepRandoms);
    stepped = Step(takeX, takeY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::UI::HoveredItem::Place::OwnTradeCell);
    EXPECT_EQ(runtime.ui.hoveredItem.index, 10U);
    EXPECT_EQ(runtime.ui.field78abb28, 2);
    EXPECT_EQ(runtime.ui.field78abb2c, 10);
    EXPECT_EQ(runtime.ui.skillTooltipX, TradeGridLeft + 10 + 2 * Cell);
    EXPECT_EQ(runtime.ui.skillTooltipY, TradeGridTop);
    EXPECT_EQ(trade.ownGrid[18].warning, 2);

    // Levantarlo con el OK puesto: el 3C con el OK en 0 después de IsOpen(0x20).
    trade.ownOk = 1;
    expected = Advanced(Random(), QuietStepRandoms + 2 * TradeQueryRandoms + C3Randoms);
    stepped = Step(takeX, takeY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.cursorGrid, Client::Scenes::CursorGrid::OwnTrade);
    EXPECT_EQ(runtime.slot78a6758, 2);
    for (const std::size_t cell : {2U, 10U, 18U})
        EXPECT_EQ(trade.ownGrid[cell].id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::UI::HoveredItem::Place::None);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);

    // Soltarlo en la casilla (3, 1) del inventario: el 24 desde la grilla 1 y la espera del OK.
    sent.clear();
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms + C3Randoms);
    stepped = Step(InventoryGridLeft + 3 * Cell + 5, InventoryGridTop + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x01, 0x02, 0x05, 2 << 3, 30, 0x00, 0x00,
                                                        0x50, 0x00, 0x00, 12 + 8 + 3}));
    EXPECT_EQ(trade.okWait, Client::Scenes::TradeOkWait);
    EXPECT_EQ(runtime.itemMoveTarget, 12 + 8 + 3);

    // La respuesta, en el inventario.
    applied = Client::Scenes::ApplyItemMoveResult({0, 12 + 8 + 3, ItemBytes(StaffType, 2)}, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(inventory[8 + 3].id, StaffType);
    EXPECT_EQ(inventory[16 + 3].id, StaffType);
    EXPECT_EQ(inventory[24 + 3].id, StaffType);
    EXPECT_EQ(runtime.cursorItemCount, 0);
}

// La grilla del otro (0x78a5a20) solo se mira: la marca sin pisar el 99, el ítem del cartel y 0x5aed40 con 3 y la
// casilla; el clic no la levanta y sigue a la grilla propia. El clic derecho sobre la propia corta (P-C235).
TEST_P(TradeGridsTest, TheOtherGridIsOnlyLookedAt)
{
    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    Client::Scenes::TradeState& trade = runtime.social.trade;
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({9, ItemBytes(SwordType, 0)}, game->Resources()).HasValue());
    trade.otherGrid[17].warning = Client::Scenes::ChangedFromTheCopy;
    const CrtRandom expected = Advanced(Random(), QuietStepRandoms);
    const auto stepped = Step(TradeGridLeft + Cell + 5, 0x46 + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(trade.otherGrid[9].warning, 2);
    EXPECT_EQ(trade.otherGrid[17].warning, Client::Scenes::ChangedFromTheCopy);
    EXPECT_EQ(trade.otherGrid[25].warning, 2);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::UI::HoveredItem::Place::OtherTradeCell);
    EXPECT_EQ(runtime.ui.hoveredItem.index, 17U);
    EXPECT_EQ(runtime.ui.field78abb28, 3);
    EXPECT_EQ(runtime.ui.field78abb2c, 17);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(trade.otherGrid[9].id, SwordType);
    EXPECT_TRUE(sent.empty());

    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({1, 4, ItemBytes(SwordType, 0)}, game->Resources()).HasValue());
    // El botón izquierdo suelto (WM_LBUTTONUP): el clic de antes ya no queda.
    Client::Scenes::LatchTheLeftButton(runtime.movement, false, false);
    runtime.ui.rightClick = true;
    const auto cut = Step(TradeGridLeft + 4 * Cell + 5, TradeGridTop + 5, false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5c5895"), std::string::npos) << cut.Error();
}

// Lo que no va a la grilla del comercio (0x5cf3c9..0x5cf40b) queda en el cursor sin mandar nada, y el clic queda en
// la grilla. Lo que no entra se marca con 3.
TEST_P(TradeGridsTest, WhatIsNotTradedStaysInTheCursor)
{
    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    runtime.cursorItem = ItemState{};
    runtime.cursorItem.id = 0x1a14;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::Inventory;
    game->items = Client::Tests::ItemTableOf({{0x1a14, 1, 1, 0}, {StaffType, 1, 3, 0}});
    const auto stepped = Step(TradeGridLeft + 5, TradeGridTop + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);

    // Un bastón que no entra desde la fila 2 (3 de alto): la marca 3 en las dos casillas de la grilla.
    runtime.cursorItem.id = StaffType;
    const auto low = Step(TradeGridLeft + 5, TradeGridTop + 3 * Cell + 5, false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.social.trade.ownGrid[2 * 8].warning, 3);
    EXPECT_EQ(runtime.social.trade.ownGrid[3 * 8].warning, 3);
    EXPECT_EQ(runtime.social.trade.ownGrid[1 * 8].warning, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, TradeGridsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));

// 0x4d1b76..0x4d1c7d: el precio del lugar de origen pasa al nuevo en el mapa de la tienda que se mira; a un lugar de
// fuera de la tienda se pierde. 0x4d1d70: el 3D que llegó con la traba se repite al final (1, hecho: cierra el
// comercio).
class ItemMoveReplyTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    ItemMoveReplyTest()
    {
        game->world.Runtime().map = GetParam();
        game->world.Runtime().ownRecordDepth = 1;
    }
    std::unique_ptr<Game> game = std::make_unique<Game>();
};

TEST_P(ItemMoveReplyTest, ThePriceFollowsTheItem)
{
    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    Client::Scenes::PersonalShopState& shops = runtime.social.personalShop;
    shops.pricesBuilt = true;
    shops.mode = Client::Scenes::OwnShopView;
    shops.ownPrices = {{0x4c, 500}, {0x50, 700}};
    runtime.slot78a6758 = 0x4c;
    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({0, 0x4d, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x4d, 500}, {0x50, 700}}));
    runtime.slot78a6758 = 0x4d;
    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({0, 0x20, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x50, 700}}));
    // Mirando otra tienda, el de esa; sin la vista, ninguno.
    shops.mode = Client::Scenes::OtherShopView;
    shops.otherPrices = {{0x50, 900}};
    runtime.slot78a6758 = 0x50;
    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({0, 0x51, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{0x51, 900}}));
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x50, 700}}));
    shops.mode = Client::Scenes::NoShopView;
    runtime.slot78a6758 = 0x51;
    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({0, 0x52, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{0x51, 900}}));
}

TEST_P(ItemMoveReplyTest, TheDeferredTradeResultIsReplayed)
{
    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    Client::Scenes::GamePanels panels(game->world, game->cloth.random);
    ASSERT_TRUE(panels.Registry().Open(Client::UI::TradePanel).HasValue());
    runtime.ui.field78abb39 = 1;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({1, {0xc1, 0x04, 0x3d, 0x01}}, game->Resources()).HasValue());
    EXPECT_EQ(runtime.social.trade.resultDeferred, 1);
    EXPECT_EQ(runtime.panels.trade, 1);
    ASSERT_TRUE(Client::Scenes::ApplyItemMoveResult({0xff, 0, {}}, game->Resources()).HasValue());
    EXPECT_EQ(runtime.social.trade.resultDeferred, 0);
    EXPECT_EQ(runtime.panels.trade, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, ItemMoveReplyTest, ::testing::Values(0, 2));
} // namespace
