#include "Scenes/SocialRequests.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
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
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameCommandsWindow.h"
#include "Scenes/GameUi.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/PlayerActors.h"
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
#include <map>
#include <memory>
#include <span>
#include <string>
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

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// La ventana de comandos (0x406050 desde 0x40a499, CL9 hito 7, fase 3): el comando elegido sobre el personaje elegido
// con el clic derecho, en el paso de la ventana abierta.
class CommandsWindowTest : public SocialRequestsTest
{
protected:
    void SetUp() override
    {
        SocialRequestsTest::SetUp();
        if (HasFatalFailure())
            return;
        std::map<std::size_t, std::string> texts = Client::Tests::ChatTexts();
        texts[Client::UI::TradeLevelText] = "nivel bajo";
        texts[Client::UI::DuelBusyText] = "otro duelo";
        m_texts = Client::Tests::MakeTextTable(texts);
        game->world.OwnCharacter().level = 50;
        game->world.Runtime().panels.delegated.commands = 1;
    }

    Client::Scenes::GameChatContext ChatContext()
    {
        return {.world = game->world,
                .random = m_random,
                .texts = &m_texts,
                .measure = &m_letters,
                .send = [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); }};
    }

    // El paso de la ventana con el mouse lejos de ella, el comando y el elegido, y el clic derecho.
    Client::Core::Expected<void, std::string> RightClick(std::int32_t command, std::int32_t chosen)
    {
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        runtime.panels.delegated.commandsWindow.chosen = command;
        Choose(chosen);
        runtime.ui.rightClick = true;
        runtime.ui.rightDown = true;
        const Client::Scenes::GameChatContext chat = ChatContext();
        return Client::Scenes::StepTheCommandsWindow(Client::Scenes::GameUiContext{
            .world = game->world, .random = m_random, .input = m_input, .mouseX = 10, .mouseY = 10, .chat = &chat});
    }

    Client::UI::CommandsWindowState& Window()
    {
        return game->world.Runtime().panels.delegated.commandsWindow;
    }

    [[nodiscard]] std::string LastMessage() const
    {
        const auto& entries = game->world.Runtime().chat.log.entries;
        return entries.empty() ? std::string{} : entries.front().text;
    }

    LetterMeasure m_letters;
    Client::Input::InputState m_input;
};

// 0x406097..0x4061aa: el destino (+8) es el elegido a dos celdas o menos (Brezal al lado; Zorzal a tres, no), después
// de leer las cuatro celdas. 0x40638d..0x406afd (el 1): el nivel del registro propio con su acceso, el 36 en C3 con el
// aviso 475, y la ventana cerrada con los estados en 1.
TEST_P(CommandsWindowTest, TheTradeCommandAsksTheChosenAndClosesTheWindow)
{
    ASSERT_TRUE(RightClick(1, 2).HasValue());
    EXPECT_EQ(Window().target, -1);
    EXPECT_TRUE(game->world.Runtime().ui.rightClick) << "sin destino, el clic no se gasta";
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());

    m_random = CrtRandom(Seed);
    const auto stepped = RightClick(1, 1);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ExpectSent(0, {0xc1, 0x05, 0x36, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "pediste comerciar con Brezal");
    EXPECT_FALSE(game->world.Runtime().ui.rightClick);
    EXPECT_FALSE(game->world.Runtime().ui.rightDown);
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 0);
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(Window().target, -1);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(
                                    Seed, 4 * CellRandoms + Client::Render::Models::OwnRecordCloseRandoms + C3Randoms)
                                    .State());
}

// 0x40646a y 0x4064aa: sin el nivel de 0x6c8dc8, el aviso 478; con la tienda personal del otro, el 493 con el 2. Los
// dos dejan el comando en −1 y la ventana abierta, sin paquete.
TEST_P(CommandsWindowTest, TheTradeCommandRefusesALowLevelOrAShop)
{
    game->world.OwnCharacter().level = 5;
    ASSERT_TRUE(RightClick(1, 1).HasValue());
    EXPECT_EQ(LastMessage(), "nivel bajo");
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 1);

    game->world.OwnCharacter().level = 6;
    Client::Scenes::AddShopTitle(game->world, NearKey, 1, "Arcos");
    ASSERT_TRUE(RightClick(1, 1).HasValue());
    EXPECT_EQ(game->world.Runtime().chat.log.entries.front().type, Client::UI::ErrorMessage);
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_TRUE(m_sent.empty());
}

// 0x406b02 (el 2): el C1 10 3F 05 con la clave y el nombre, en C3, y la ventana cerrada.
TEST_P(CommandsWindowTest, TheShopCommandAsksForTheItems)
{
    ASSERT_TRUE(RightClick(2, 1).HasValue());
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 0);
}

// 0x4061d3..0x406275 y 0x4072df (el 3 y el 10): fuera de la party, el 3 invita (el 40 y el aviso 476, la ventana
// queda) y el 10 no se puede; en la party, al revés, y el 10 lo sigue (0x6c8e50).
TEST_P(CommandsWindowTest, ThePartyCommandsInviteOrFollow)
{
    ASSERT_TRUE(RightClick(10, 1).HasValue());
    EXPECT_EQ(Window().states[10], 0);
    EXPECT_EQ(game->world.Runtime().social.party.followTarget, -1);
    ASSERT_TRUE(RightClick(3, 1).HasValue());
    ExpectSent(0, {0xc1, 0x05, 0x40, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "invitaste a Brezal");
    EXPECT_EQ(Window().chosen, 3);
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 1);

    Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
    runtime.hud.partyCount = 2;
    Name(runtime.social.party.slots[0].name, "Tejon");
    Name(runtime.social.party.slots[1].name, "Brezal");
    ASSERT_TRUE(RightClick(3, 1).HasValue());
    EXPECT_EQ(Window().states[3], 0);
    EXPECT_EQ(m_sent.size(), 1U);
    ASSERT_TRUE(RightClick(10, 1).HasValue());
    EXPECT_EQ(runtime.social.party.followTarget, 1);
}

// 0x40a04e (el 4): el nombre al EDIT del susurro y el Enter del WndProc, que abre el chat. 0x4079a6 (el 11): sin duelo
// el AA; con el rival, el AB; con otro, el aviso 915.
TEST_P(CommandsWindowTest, TheWhisperAndTheDuelCommands)
{
    ASSERT_TRUE(RightClick(4, 1).HasValue());
    EXPECT_EQ(game->world.Runtime().chat.input.whisper.Value(), "Brezal");
    EXPECT_TRUE(game->world.Runtime().ui.chatOpen);

    ASSERT_TRUE(RightClick(11, 1).HasValue());
    ExpectSent(0, {0xc1, 0x0f, 0xaa, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    Client::Scenes::DuelState& duel = game->world.Runtime().social.duel;
    duel.active = 1;
    Name(duel.rivalName, "Brezal");
    ASSERT_TRUE(RightClick(11, 1).HasValue());
    ExpectSent(1, {0xc1, 0x03, 0xab});
    Name(duel.rivalName, "Zorzal");
    ASSERT_TRUE(RightClick(11, 1).HasValue());
    EXPECT_EQ(LastMessage(), "otro duelo");
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(m_sent.size(), 2U);
}

// Player.bmd para todos los tipos: la animación del comando 8 (0x4ea030).
class CommandModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &player;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// Los comandos del gremio (del 5 al 8, CL9 hito 8): sus avisos inventados y el cable en claro (el XOR32 en línea).
class GuildCommandsTest : public CommandsWindowTest
{
protected:
    void SetUp() override
    {
        CommandsWindowTest::SetUp();
        if (HasFatalFailure())
            return;
        std::map<std::size_t, std::string> texts = Client::Tests::ChatTexts();
        texts[255] = "ya tenes gremio";
        texts[477] = "pediste el gremio a %s";
        texts[507] = "no es maestro";
        texts[1358] = "alianza con %s";
        texts[1359] = "rivales con %s";
        texts[1360] = "fin con %s";
        texts[1385] = "sin gremio";
        m_texts = Client::Tests::MakeTextTable(texts);
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
    }

    ActorState& Hero()
    {
        return game->world.Actors()[game->world.Runtime().hero];
    }

    Client::Core::Expected<void, std::string> RightClickWithModels(std::int32_t command, std::int32_t chosen)
    {
        Client::Scenes::WorldRuntime& runtime = game->world.Runtime();
        runtime.panels.delegated.commandsWindow.chosen = command;
        Choose(chosen);
        runtime.ui.rightClick = true;
        runtime.ui.rightDown = true;
        Client::Scenes::GameChatContext chat = ChatContext();
        chat.models = &m_models;
        return Client::Scenes::StepTheCommandsWindow(Client::Scenes::GameUiContext{
            .world = game->world, .random = m_random, .input = m_input, .mouseX = 10, .mouseY = 10, .chat = &chat});
    }

    void ExpectSentInTheClear(std::size_t at, std::initializer_list<int> bytes)
    {
        ASSERT_LT(at, m_sent.size());
        EXPECT_EQ(m_sent[at].delivery, Client::Network::Delivery::Clear);
        EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(m_sent[at].packet).Bytes(),
                                             Client::Tests::Bytes(bytes)));
    }

    CommandModels m_models;
};

// 0x40627a..0x4062b1 y 0x4085d1..0x408a23 (el 5): el héroe sin gremio pide entrar al del maestro elegido: 0x78aba40 con
// su clave, C1 05 50 04 56 en claro y el aviso 477 con su nombre; sin rand() más que los de las cuatro celdas. Con el
// héroe en un gremio (+0x368 distinto de 0xff, +0x1f2 todavía negativo), el aviso 255 y el comando en −1.
TEST_P(GuildCommandsTest, TheGuildCommandAsksToJoin)
{
    ASSERT_TRUE(RightClick(7, 1).HasValue());
    EXPECT_EQ(Window().states[7], 0xfe);
    ASSERT_TRUE(RightClick(5, 1).HasValue());
    EXPECT_EQ(Window().states[5], 0xff);
    EXPECT_TRUE(m_sent.empty());

    Near().word1f2 = 3;
    Near().guildStatus = 0x80;
    m_random = CrtRandom(Seed);
    const auto asked = RightClick(5, 1);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSentInTheClear(0, {0xc1, 0x05, 0x50, 0x04, 0x56});
    EXPECT_EQ(game->world.Runtime().guild.requester, 0x0456);
    EXPECT_EQ(LastMessage(), "pediste el gremio a Brezal");
    EXPECT_EQ(Window().chosen, 5);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());

    Hero().guildStatus = 0x00;
    ASSERT_TRUE(RightClick(5, 1).HasValue());
    EXPECT_EQ(LastMessage(), "ya tenes gremio");
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(m_sent.size(), 1U);
}

// 0x4062b6..0x4062f9 y 0x408a24..0x409065 (el 6): con los dos maestros, el estado sale del +4 de la entrada del gremio
// del héroe en la tabla: sin alianza, C1 07 E5 01 01 04 56 y el aviso 1358; ya con alianza, el estado en 0 y nada.
TEST_P(GuildCommandsTest, TheAllianceCommandReadsTheGuildTable)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 3;
    Near().guildStatus = 0x80;
    Near().word1f2 = 4;
    const auto asked = RightClick(6, 1);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_EQ(Window().states[6], 1);
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSentInTheClear(0, {0xc1, 0x07, 0xe5, 0x01, 0x01, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "alianza con Brezal");

    auto& unionName = game->world.Runtime().guild.table[3].unionName;
    unionName[0] = 'C';
    unionName[1] = 'e';
    ASSERT_TRUE(RightClick(6, 1).HasValue());
    EXPECT_EQ(Window().states[6], 0);
    EXPECT_EQ(m_sent.size(), 1U);
}

// 0x409066 (el 7): C1 07 E5 02 01 y el aviso 1359. 0x409663 (el 8): la animación 0xb7 del héroe, el C1 05 18 con su
// dirección y la acción 0x92, C1 07 E5 02 02 y el aviso 1360. Sin los modelos, el 8 corta antes de mandar nada.
TEST_P(GuildCommandsTest, TheHostilityCommandsSendTheirRequests)
{
    Hero().guildStatus = 0x80;
    Hero().word1f2 = 3;
    Near().guildStatus = 0x80;
    Near().word1f2 = 4;
    ASSERT_TRUE(RightClick(7, 1).HasValue());
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSentInTheClear(0, {0xc1, 0x07, 0xe5, 0x02, 0x01, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "rivales con Brezal");

    const auto cut = RightClick(8, 1);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x40966f"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_sent.size(), 1U);

    m_random = CrtRandom(Seed);
    const auto ended = RightClickWithModels(8, 1);
    ASSERT_TRUE(ended.HasValue()) << ended.Error();
    EXPECT_EQ(Hero().animation.action, 0xb7);
    ASSERT_EQ(m_sent.size(), 3U);
    const auto direction = Client::Scenes::ActionDirection(Hero().angles.z);
    ASSERT_TRUE(direction.has_value());
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(m_sent[1].packet).Bytes(),
                                         Client::Tests::Bytes({0xc1, 0x05, 0x18, *direction, 0x92})));
    ExpectSentInTheClear(2, {0xc1, 0x07, 0xe5, 0x02, 0x02, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "fin con Brezal");
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildCommandsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
INSTANTIATE_TEST_SUITE_P(Maps, SocialRequestsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
INSTANTIATE_TEST_SUITE_P(Maps, CommandsWindowTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
