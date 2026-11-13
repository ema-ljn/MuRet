#include "Scenes/SocialRequests.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Los pedidos a otro de los comandos del chat (0x56c440: /trade, /duelstart y /party; CL9 hito 7, fase 2) con los
// jugadores que trae el 0x12 (hito 1), en Lorencia (0) y Devias (2). Los nombres son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::uint16_t NearKey = 0x0456;
constexpr std::uint16_t FarKey = 0x0457;
constexpr std::size_t Chosen = 2;
// Cada celda que se compara cierra su entrada con 4 rand(); el C3, 2.
constexpr int CellRandoms = 4;
constexpr int C3Randoms = 2;
// Dos direcciones opuestas: (0 + 22,5) / 45 + 1 = 1,5 y (180 + 22,5) / 45 + 1 = 5,5.
constexpr float Front = 0.0f;
constexpr float Back = 180.0f;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

void Name(std::span<std::byte> name, std::string_view text)
{
    for (std::size_t at = 0; at < name.size(); ++at)
        name[at] = at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0};
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
        Name(hero.name, "Tejon");
        world.Runtime().hero = 0;
        world.Runtime().drawingEnabled = true;
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
                .log = log};
    }
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class SocialRequestsTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
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
        TwoPlayersArrive();
    }

    // La primera fila libre de cuatro celdas desde (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        for (int y = 100; y < 200; ++y)
            for (int x = 100; x < 200; ++x)
            {
                bool free = true;
                for (int step = 0; step < 4 && free; ++step)
                    free = cells[Client::Data::Terrain::CellIndex(Byte(x + step), Byte(y))] == 0;
                if (free)
                    return {Byte(x), Byte(y)};
            }
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    // El héroe en la primera celda; el 0x12 trae a Brezal al lado y a Zorzal tres más allá.
    void TwoPlayersArrive()
    {
        const auto [x, y] = FreeRow();
        ActorState& hero = game->world.Actors()[0];
        hero.cellX = x;
        hero.cellY = y;
        const std::array<ViewportPlayerRecord, 2> records{{{.key = NearKey,
                                                            .isNew = true,
                                                            .x = Byte(x + 1),
                                                            .y = y,
                                                            .name = "Brezal",
                                                            .targetX = Byte(x + 1),
                                                            .targetY = y},
                                                           {.key = FarKey,
                                                            .isNew = true,
                                                            .x = Byte(x + 3),
                                                            .y = y,
                                                            .name = "Zorzal",
                                                            .targetX = Byte(x + 3),
                                                            .targetY = y}}};
        auto read = Client::Protocol::Viewport::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyViewportPlayers(read.Value(), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ASSERT_EQ(Near().key, NearKey);
        ASSERT_EQ(Far().key, FarKey);
        hero.angles.z = Front;
        Near().angles.z = Back;
        Far().angles.z = Back;
    }

    ActorState& Near()
    {
        return game->world.Actors()[1];
    }

    ActorState& Far()
    {
        return game->world.Actors()[2];
    }

    Client::Scenes::SocialRequestContext Context()
    {
        return {.world = game->world,
                .random = m_random,
                .texts = m_texts,
                .send = [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); }};
    }

    void Choose(std::int32_t index)
    {
        game->world.Runtime().selectedTargets[Chosen] = index;
    }

    // El paquete en claro, sin el XOR32, y que va en C3.
    void ExpectSent(std::size_t at, std::initializer_list<int> bytes)
    {
        ASSERT_LT(at, m_sent.size());
        EXPECT_EQ(m_sent[at].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(m_sent[at].packet).Bytes(),
                                             Client::Tests::Bytes(bytes)));
    }

    std::unique_ptr<Game> game;
    CrtRandom m_random{Seed};
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x56c68b..0x56cdfc: el elegido al lado recibe el C1 05 36 con su clave (en C3) y el aviso 475 lleva su nombre. Las
// cuatro celdas cierran sus entradas (16 rand()) y el C3 saca 2.
TEST_P(SocialRequestsTest, TradeAsksTheChosenNextToTheHero)
{
    Choose(1);
    const auto outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1U);
    EXPECT_EQ(outcome.Value().notices[0].text, Client::Tests::ChatTradeRequestedText);
    EXPECT_EQ(outcome.Value().notices[0].type, Client::UI::SystemMessage);
    EXPECT_EQ(outcome.Value().notices[0].formatted, "pediste comerciar con Brezal");
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 5, 0x36, 0x04, 0x56});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());
    EXPECT_EQ(Near().cellXAccesses, 0);
    EXPECT_EQ(game->world.Actors()[0].cellYAccesses, 0);
}

// 0x56ce17..0x56cfc6: sin elegido, el primero de los 400 al lado y enfrente (direcciones a 4). Mirando para el mismo
// lado no hay a quién: Brezal lee sus cuatro celdas y Zorzal, lejos en la x, solo dos.
TEST_P(SocialRequestsTest, WithoutAChosenTheTradeGoesToTheOneInFront)
{
    Choose(-1);
    auto outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 5, 0x36, 0x04, 0x56});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());

    Near().angles.z = Front;
    m_random = CrtRandom(Seed);
    outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 6 * CellRandoms).State());
}

// 0x56c730, 0x56c7cf y 0x56c7dc: el elegido lejos (solo las x), con la tienda personal abierta o sin el mundo cargado,
// /trade devuelve 1 sin mandar nada.
TEST_P(SocialRequestsTest, AFarChosenOrAnOpenShopSendsNoTrade)
{
    Choose(2);
    auto outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 2 * CellRandoms).State());

    Choose(1);
    game->world.Runtime().panels.personalShop = 1;
    m_random = CrtRandom(Seed);
    outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());

    game->world.Runtime().panels.personalShop = 0;
    game->world.Runtime().drawingEnabled = false;
    outcome = Client::Scenes::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_TRUE(m_sent.empty());
}

// 0x56f1b1..0x56fd88: el C1 05 40 al elegido, 0x78aba4c con su clave y el aviso 476 con su nombre.
TEST_P(SocialRequestsTest, PartyInvitesTheChosenAndKeepsItsKey)
{
    Choose(1);
    const auto outcome = Client::Scenes::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1U);
    EXPECT_EQ(outcome.Value().notices[0].formatted, "invitaste a Brezal");
    ExpectSent(0, {0xc1, 5, 0x40, 0x04, 0x56});
    EXPECT_EQ(game->world.Runtime().social.party.requestKey, NearKey);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());
}

// 0x56f13f..0x56f1b0: en una party de otro líder, el aviso 257 y nada más (ni las celdas).
TEST_P(SocialRequestsTest, OnlyTheLeaderInvites)
{
    Choose(1);
    auto& runtime = game->world.Runtime();
    runtime.hud.partyCount = 2;
    Name(runtime.social.party.slots[0].name, "Zorzal");
    Name(runtime.social.party.slots[1].name, "Tejon");
    auto outcome = Client::Scenes::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_EQ(outcome.Value().notices,
              (std::vector<Client::UI::ChatCommandNotice>{
                  {.text = Client::Tests::ChatNotTheLeaderText, .type = Client::UI::SystemMessage}}));
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    Name(runtime.social.party.slots[0].name, "Tejon");
    outcome = Client::Scenes::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_EQ(m_sent.size(), 1U);
}

// 0x56d823..0x56e9ec: el AA lleva la clave y los diez bytes del nombre, al elegido o al de enfrente, sin aviso.
TEST_P(SocialRequestsTest, TheDuelChallengesWithTheName)
{
    Choose(1);
    ASSERT_TRUE(Client::Scenes::RequestADuel(Context()).HasValue());
    ExpectSent(0, {0xc1, 0x0f, 0xaa, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());

    Choose(-1);
    ASSERT_TRUE(Client::Scenes::RequestADuel(Context()).HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
    Near().angles.z = Front;
    ASSERT_TRUE(Client::Scenes::RequestADuel(Context()).HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
}

INSTANTIATE_TEST_SUITE_P(Maps, SocialRequestsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
