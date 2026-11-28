#include "Network/Xor32Packet.h"
#include "Models/IModelLibrary.h"
#include "Social/SocialRequests.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Actors/Viewport.h"
#include "Input/InputState.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Models/ModelId.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Chat/GameChat.h"
#include "Hud/GameCommandsWindow.h"
#include "Movement/GameMovementState.h"
#include "Ui/GameUiContext.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "FrontEnd/LoginModels.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatCommands.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Terrain/WorldTerrain.h"

#include "support/Bytes.h"
#include "support/ChatTexts.h"
#include "support/PlayerActors.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportHandlers.h"
#include "support/ViewportPackets.h"
#include "Guild/GuildState.h"
#include "Ui/GameUiState.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The chat commands' requests to another player (0x56c440: /trade, /duelstart and /party; CL9 milestone 7, phase 2)
// with the players that the 0x12 brings (milestone 1), in Lorencia (0) and Devias (2). The names are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::uint16_t NearKey = 0x0456;
constexpr std::uint16_t FarKey = 0x0457;
constexpr std::size_t Chosen = 2;
// Each compared cell closes its entry with 4 rand(); the C3, 2.
constexpr int CellRandoms = 4;
constexpr int C3Randoms = 2;
// Two opposite directions: (0 + 22.5) / 45 + 1 = 1.5 and (180 + 22.5) / 45 + 1 = 5.5.
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
                .log = log,
                .states = &states};
    }
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
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
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->states.terrain.current = std::move(loaded).Value();
        game->world.Runtime().map = map;
        TwoPlayersArrive();
    }

    // The first free row of four cells from (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->states.terrain.current.attributes->cells;
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

    // The hero in the first cell; the 0x12 brings Brezal next to it and Zorzal three further.
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
        auto read = Client::Protocol::Actors::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Tests::ApplyViewportPlayers(read.Value(), game->Resources());
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

    Client::Social::SocialRequestContext Context()
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

    // The packet in clear, without the XOR32, and that it goes in C3.
    void ExpectSent(std::size_t at, std::initializer_list<int> bytes)
    {
        ASSERT_LT(at, m_sent.size());
        EXPECT_EQ(m_sent[at].delivery, Client::Network::Delivery::Encrypted);
        EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(m_sent[at].packet).Bytes(),
                                             Client::Tests::Bytes(bytes)));
    }

    std::unique_ptr<Game> game;
    CrtRandom m_random{Seed};
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x56c68b..0x56cdfc: the chosen one next to the hero receives the C1 05 36 with its key (in C3) and warning 475
// carries its name. The four cells close their entries (16 rand()) and the C3 draws 2.
TEST_P(SocialRequestsTest, TradeAsksTheChosenNextToTheHero)
{
    Choose(1);
    const auto outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1U);
    EXPECT_EQ(outcome.Value().notices[0].text, Client::Tests::ChatTradeRequestedText);
    EXPECT_EQ(outcome.Value().notices[0].type, Client::Ui::SystemMessage);
    EXPECT_EQ(outcome.Value().notices[0].formatted, "pediste comerciar con Brezal");
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 5, 0x36, 0x04, 0x56});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());
    EXPECT_EQ(Near().cellXAccesses, 0);
    EXPECT_EQ(game->world.Actors()[0].cellYAccesses, 0);
}

// 0x56ce17..0x56cfc6: without a chosen one, the first of the 400 next to the hero and facing it (directions 4 apart).
// Facing the same way there is nobody: Brezal reads its four cells and Zorzal, far in x, only two.
TEST_P(SocialRequestsTest, WithoutAChosenTheTradeGoesToTheOneInFront)
{
    Choose(-1);
    auto outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 5, 0x36, 0x04, 0x56});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());

    Near().angles.z = Front;
    m_random = CrtRandom(Seed);
    outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 6 * CellRandoms).State());
}

// 0x56c730, 0x56c7cf and 0x56c7dc: with the chosen one far away (only the x), the personal shop open or the world not
// loaded, /trade returns 1 without sending anything.
TEST_P(SocialRequestsTest, AFarChosenOrAnOpenShopSendsNoTrade)
{
    Choose(2);
    auto outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 2 * CellRandoms).State());

    Choose(1);
    game->world.Runtime().panels.personalShop = 1;
    m_random = CrtRandom(Seed);
    outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());

    game->world.Runtime().panels.personalShop = 0;
    game->world.Runtime().drawingEnabled = false;
    outcome = Client::Social::RequestATrade(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().notices.empty());
    EXPECT_TRUE(m_sent.empty());
}

// 0x56f1b1..0x56fd88: the C1 05 40 to the chosen one, 0x78aba4c with its key and warning 476 with its name.
TEST_P(SocialRequestsTest, PartyInvitesTheChosenAndKeepsItsKey)
{
    Choose(1);
    const auto outcome = Client::Social::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(outcome.Value().notices.size(), 1U);
    EXPECT_EQ(outcome.Value().notices[0].formatted, "invitaste a Brezal");
    ExpectSent(0, {0xc1, 5, 0x40, 0x04, 0x56});
    EXPECT_EQ(game->world.Runtime().social.party.requestKey, NearKey);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());
}

// 0x56f13f..0x56f1b0: in a party with another leader, warning 257 and nothing else (not even the cells).
TEST_P(SocialRequestsTest, OnlyTheLeaderInvites)
{
    Choose(1);
    auto& runtime = game->world.Runtime();
    runtime.hud.partyCount = 2;
    Name(runtime.social.party.slots[0].name, "Zorzal");
    Name(runtime.social.party.slots[1].name, "Tejon");
    auto outcome = Client::Social::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_EQ(outcome.Value().notices,
              (std::vector<Client::Chat::ChatCommandNotice>{
                  {.text = Client::Tests::ChatNotTheLeaderText, .type = Client::Ui::SystemMessage}}));
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    Name(runtime.social.party.slots[0].name, "Tejon");
    outcome = Client::Social::RequestAParty(Context());
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_EQ(m_sent.size(), 1U);
}

// 0x56d823..0x56e9ec: the AA carries the key and the name's ten bytes, to the chosen one or the one in front, without a
// warning.
TEST_P(SocialRequestsTest, TheDuelChallengesWithTheName)
{
    Choose(1);
    ASSERT_TRUE(Client::Social::RequestADuel(Context()).HasValue());
    ExpectSent(0, {0xc1, 0x0f, 0xaa, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms + C3Randoms).State());

    Choose(-1);
    ASSERT_TRUE(Client::Social::RequestADuel(Context()).HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
    Near().angles.z = Front;
    ASSERT_TRUE(Client::Social::RequestADuel(Context()).HasValue());
    EXPECT_EQ(m_sent.size(), 2U);
}

// /purchase (0x574c0c..0x57694d): the queries of 0x10000 and 0x20 draw one rand() each (those of 0x40, 0x100 and
// 0x8000, none) and the request reads no cells.
constexpr int PurchasePanelRandoms = 2;

// 0x574c97..0x57538b: the chosen one receives the C1 10 3F 05 with its key and its name even if far away or dead; a
// chosen hero does not. With or without a request, 1 and no warning.
TEST_P(SocialRequestsTest, PurchaseAsksTheChosenWithoutTheCells)
{
    Choose(2);
    Far().alive = false;
    auto outcome = Client::Social::RequestTheShopItems(Context(), std::nullopt);
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(outcome.Value().notices.empty());
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x57, 'Z', 'o', 'r', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms + C3Randoms).State());
    EXPECT_EQ(Far().cellXAccesses, 0);

    Choose(0);
    m_random = CrtRandom(Seed);
    outcome = Client::Social::RequestTheShopItems(Context(), "Tejon");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms).State());
}

// 0x574c0c..0x574c78 and 0x5766a7: an open NPC shop, storage, trade, chaos box or trainer gives warning 1121 and it
// goes out as chat. The queries stop at the first open one.
TEST_P(SocialRequestsTest, PurchaseStopsWithAnOpenPanel)
{
    Choose(1);
    auto& panels = game->world.Runtime().panels;
    const auto blocked = [&](std::uint8_t& open, int randoms)
    {
        open = 1;
        m_random = CrtRandom(Seed);
        const auto outcome = Client::Social::RequestTheShopItems(Context(), "Brezal");
        open = 0;
        ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
        EXPECT_FALSE(outcome.Value().handled);
        EXPECT_EQ(outcome.Value().notices, (std::vector<Client::Chat::ChatCommandNotice>{
                                               {Client::Chat::ShopTrainerText, Client::Ui::SystemMessage}}));
        EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, randoms).State());
    };
    blocked(panels.npcShop, 1);
    blocked(panels.warehouse, 1);
    blocked(panels.trade, 2);
    blocked(panels.chaosBox, 2);
    panels.delegated.trainer = 1;
    m_random = CrtRandom(Seed);
    const auto trainer = Client::Social::RequestTheShopItems(Context(), "Brezal");
    ASSERT_TRUE(trainer.HasValue()) << trainer.Error();
    EXPECT_FALSE(trainer.Value().handled);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms).State());
    EXPECT_TRUE(m_sent.empty());
}

// 0x575501..0x575e7f: without a chosen one, the 3F 05 goes to every living one with the typed name (the hero too) and
// the dead one does not get it (07 Q-C967).
TEST_P(SocialRequestsTest, PurchaseAsksEveryoneWithTheName)
{
    Choose(-1);
    Name(Near().name, "Zorzal");
    auto outcome = Client::Social::RequestTheShopItems(Context(), "Zorzal");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(m_sent.size(), 2U);
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x56, 'Z', 'o', 'r', 'z', 'a', 'l', 0, 0, 0, 0});
    ExpectSent(1, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x57, 'Z', 'o', 'r', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms + 2 * C3Randoms).State());

    Near().alive = false;
    m_sent.clear();
    outcome = Client::Social::RequestTheShopItems(Context(), "Zorzal");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x57, 'Z', 'o', 'r', 'z', 'a', 'l', 0, 0, 0, 0});

    m_sent.clear();
    outcome = Client::Social::RequestTheShopItems(Context(), "Tejon");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x00, 0x21, 'T', 'e', 'j', 'o', 'n', 0, 0, 0, 0, 0});

    m_sent.clear();
    m_random = CrtRandom(Seed);
    outcome = Client::Social::RequestTheShopItems(Context(), "Nadie");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms).State());
}

// 0x575daf..0x575e66: with the buffer empty, the first living one facing the hero, even if far away. Without a value
// (sscanf did not fill it), the error.
TEST_P(SocialRequestsTest, PurchaseWithAnEmptyNameAsksTheOneInFront)
{
    Choose(-1);
    Near().angles.z = Front;
    auto outcome = Client::Social::RequestTheShopItems(Context(), "");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_TRUE(outcome.Value().handled);
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x57, 'Z', 'o', 'r', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms + C3Randoms).State());

    Far().angles.z = Front;
    m_random = CrtRandom(Seed);
    outcome = Client::Social::RequestTheShopItems(Context(), "");
    ASSERT_TRUE(outcome.HasValue()) << outcome.Error();
    EXPECT_EQ(m_sent.size(), 1U);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, PurchasePanelRandoms).State());

    const auto unknown = Client::Social::RequestTheShopItems(Context(), std::nullopt);
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_NE(unknown.Error().find("Q-C967"), std::string::npos) << unknown.Error();
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

// The commands window (0x406050 from 0x40a499, CL9 milestone 7, phase 3): the chosen command on the character chosen
// with the right click, in the open window's step.
class CommandsWindowTest : public SocialRequestsTest
{
protected:
    void SetUp() override
    {
        SocialRequestsTest::SetUp();
        if (HasFatalFailure())
            return;
        std::map<std::size_t, std::string> texts = Client::Tests::ChatTexts();
        texts[Client::Chat::TradeLevelText] = "nivel bajo";
        texts[Client::Chat::DuelBusyText] = "otro duelo";
        m_texts = Client::Tests::MakeTextTable(texts);
        game->world.OwnCharacter().level = 50;
        game->world.Runtime().panels.delegated.commands = 1;
    }

    Client::Chat::GameChatContext ChatContext()
    {
        return {.world = game->world,
                .random = m_random,
                .texts = &m_texts,
                .measure = &m_letters,
                .send = [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); }};
    }

    // The window's step with the mouse away from it, the command and the chosen one, and the right click.
    Client::Core::Expected<void, std::string> RightClick(std::int32_t command, std::int32_t chosen)
    {
        Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
        runtime.panels.delegated.commandsWindow.chosen = command;
        Choose(chosen);
        runtime.ui.rightClick = true;
        runtime.ui.rightDown = true;
        const Client::Chat::GameChatContext chat = ChatContext();
        return Client::Hud::StepTheCommandsWindow(Client::Ui::GameUiContext{
            .world = game->world, .random = m_random, .input = m_input, .mouseX = 10, .mouseY = 10, .chat = &chat});
    }

    Client::Ui::CommandsWindowState& Window()
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

// 0x406097..0x4061aa: the target (+8) is the chosen one two cells away or less (Brezal next to it; Zorzal at three,
// no), after reading the four cells. 0x40638d..0x406afd (the 1): the own record's level with its access, the 36 in C3
// with warning 475, and the window closed with the states at 1.
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
    EXPECT_EQ(
        m_random.State(),
        Client::Tests::AfterDraws(Seed, 4 * CellRandoms + Client::Models::OwnRecordCloseRandoms + C3Randoms).State());
}

// 0x40646a and 0x4064aa: without 0x6c8dc8's level, warning 478; with the other's personal shop, 493 with the 2. Both
// leave the command at −1 and the window open, without a packet.
TEST_P(CommandsWindowTest, TheTradeCommandRefusesALowLevelOrAShop)
{
    game->world.OwnCharacter().level = 5;
    ASSERT_TRUE(RightClick(1, 1).HasValue());
    EXPECT_EQ(LastMessage(), "nivel bajo");
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 1);

    game->world.OwnCharacter().level = 6;
    Client::PersonalShop::AddShopTitle(game->world, NearKey, 1, "Arcos");
    ASSERT_TRUE(RightClick(1, 1).HasValue());
    EXPECT_EQ(game->world.Runtime().chat.log.entries.front().type, Client::Ui::ErrorMessage);
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_TRUE(m_sent.empty());
}

// 0x406b02 (the 2): the C1 10 3F 05 with the key and the name, in C3, and the window closed.
TEST_P(CommandsWindowTest, TheShopCommandAsksForTheItems)
{
    ASSERT_TRUE(RightClick(2, 1).HasValue());
    ExpectSent(0, {0xc1, 0x10, 0x3f, 0x05, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 0);
}

// 0x4061d3..0x406275 and 0x4072df (the 3 and the 10): outside the party, the 3 invites (the 40 and warning 476, the
// window stays) and the 10 is not allowed; in the party, the reverse, and the 10 follows them (0x6c8e50).
TEST_P(CommandsWindowTest, ThePartyCommandsInviteOrFollow)
{
    ASSERT_TRUE(RightClick(10, 1).HasValue());
    EXPECT_EQ(Window().states[10], 0);
    EXPECT_EQ(game->world.Runtime().movement.followTarget, -1);
    ASSERT_TRUE(RightClick(3, 1).HasValue());
    ExpectSent(0, {0xc1, 0x05, 0x40, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "invitaste a Brezal");
    EXPECT_EQ(Window().chosen, 3);
    EXPECT_EQ(game->world.Runtime().panels.delegated.commands, 1);

    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    runtime.hud.partyCount = 2;
    Name(runtime.social.party.slots[0].name, "Tejon");
    Name(runtime.social.party.slots[1].name, "Brezal");
    ASSERT_TRUE(RightClick(3, 1).HasValue());
    EXPECT_EQ(Window().states[3], 0);
    EXPECT_EQ(m_sent.size(), 1U);
    ASSERT_TRUE(RightClick(10, 1).HasValue());
    EXPECT_EQ(runtime.movement.followTarget, 1);
}

// 0x40a04e (the 4): the name to the whisper EDIT and the WndProc's Enter, which opens the chat. 0x4079a6 (the 11):
// without a duel the AA; with the rival, the AB; with another, warning 915.
TEST_P(CommandsWindowTest, TheWhisperAndTheDuelCommands)
{
    ASSERT_TRUE(RightClick(4, 1).HasValue());
    EXPECT_EQ(game->world.Runtime().chat.input.whisper.text.Value(), "Brezal");
    EXPECT_TRUE(game->world.Runtime().ui.chatOpen);

    ASSERT_TRUE(RightClick(11, 1).HasValue());
    ExpectSent(0, {0xc1, 0x0f, 0xaa, 0x04, 0x56, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0});
    Client::Social::DuelState& duel = game->world.Runtime().social.duel;
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

// Player.bmd for every type: command 8's animation (0x4ea030).
class CommandModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &player;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
};

// The guild commands (5 to 8, CL9 milestone 8): their made-up warnings and the cable in clear (the XOR32 inline).
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
        texts[1355] = "no tenes gremio";
        texts[1385] = "sin gremio";
        m_texts = Client::Tests::MakeTextTable(texts);
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
    }

    ActorState& Hero()
    {
        return game->world.Hero();
    }

    Client::Core::Expected<void, std::string> RightClickWithModels(std::int32_t command, std::int32_t chosen)
    {
        Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
        runtime.panels.delegated.commandsWindow.chosen = command;
        Choose(chosen);
        runtime.ui.rightClick = true;
        runtime.ui.rightDown = true;
        Client::Chat::GameChatContext chat = ChatContext();
        chat.models = &m_models;
        return Client::Hud::StepTheCommandsWindow(Client::Ui::GameUiContext{
            .world = game->world, .random = m_random, .input = m_input, .mouseX = 10, .mouseY = 10, .chat = &chat});
    }

    void ExpectSentInTheClear(std::size_t at, std::initializer_list<int> bytes)
    {
        ASSERT_LT(at, m_sent.size());
        EXPECT_EQ(m_sent[at].delivery, Client::Network::Delivery::Clear);
        EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(m_sent[at].packet).Bytes(),
                                             Client::Tests::Bytes(bytes)));
    }

    CommandModels m_models;
};

// 0x40627a..0x4062b1 and 0x4085d1..0x408a23 (the 5): the hero without a guild asks to join the chosen master's:
// 0x78aba40 with its key, C1 05 50 04 56 in clear and warning 477 with its name; no rand() but the four cells'. With
// the hero in a guild (+0x368 other than 0xff, +0x1f2 still negative), warning 255 and the command at −1.
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

// 0x4062b6..0x4062f9 and 0x408a24..0x409065 (the 6): with both masters, the state comes from +4 of the hero guild's
// entry in the table: without an alliance, C1 07 E5 01 01 04 56 and warning 1358; already allied, the state at 0 and
// nothing.
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

// 0x409066 (the 7): C1 07 E5 02 01 and warning 1359. 0x409663 (the 8): the hero's animation 0xb7, the C1 05 18 with
// its direction and action 0x92, C1 07 E5 02 02 and warning 1360. Without the models, the 8 cuts before sending
// anything.
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
    const auto direction = Client::Actors::ActionDirection(Hero().angles.z);
    ASSERT_TRUE(direction.has_value());
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(m_sent[1].packet).Bytes(),
                                         Client::Tests::Bytes({0xc1, 0x05, 0x18, *direction, 0x92})));
    ExpectSentInTheClear(2, {0xc1, 0x07, 0xe5, 0x02, 0x02, 0x04, 0x56});
    EXPECT_EQ(LastMessage(), "fin con Brezal");
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());
}

// 0x573f85..0x574bd5 (/guild): without a guild, the chosen one next to the hero: 0x78aba40 with its key,
// C1 05 50 04 56 in clear, warning 477 with its name and only the four cells' rand(). Without a chosen one, the one in
// front (Brezal faces the hero). Without the world loaded, nothing. With a guild, warning 255 without looking at
// anyone.
TEST_P(GuildCommandsTest, TheGuildChatCommandAsksTheChosenOrTheOneInFront)
{
    Choose(1);
    auto asked = Client::Hud::RequestAGuildJoin(ChatContext());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_TRUE(asked.Value().handled);
    ASSERT_EQ(asked.Value().notices.size(), 1U);
    EXPECT_EQ(asked.Value().notices[0].type, Client::Ui::SystemMessage);
    EXPECT_EQ(asked.Value().notices[0].formatted, "pediste el gremio a Brezal");
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSentInTheClear(0, {0xc1, 0x05, 0x50, 0x04, 0x56});
    EXPECT_EQ(game->world.Runtime().guild.requester, 0x0456);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());

    Choose(-1);
    game->world.Runtime().guild.requester = 0;
    m_random = CrtRandom(Seed);
    asked = Client::Hud::RequestAGuildJoin(ChatContext());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_EQ(m_sent.size(), 2U);
    ExpectSentInTheClear(1, {0xc1, 0x05, 0x50, 0x04, 0x56});
    EXPECT_EQ(game->world.Runtime().guild.requester, 0x0456);

    game->world.Runtime().drawingEnabled = false;
    asked = Client::Hud::RequestAGuildJoin(ChatContext());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_TRUE(asked.Value().handled);
    EXPECT_TRUE(asked.Value().notices.empty());
    EXPECT_EQ(m_sent.size(), 2U);

    Hero().guildStatus = 0x00;
    m_random = CrtRandom(Seed);
    asked = Client::Hud::RequestAGuildJoin(ChatContext());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_EQ(asked.Value().notices, (std::vector<Client::Chat::ChatCommandNotice>{{255, Client::Ui::SystemMessage}}));
    EXPECT_EQ(m_sent.size(), 2U);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// 0x570d7e..0x573f48 (/union, /rival and /rivaloff): without a guild, warning 1355. With one, to the chosen one or the
// one in front without checking its guild: C1 07 E5 01 01 and 1358, E5 02 01 and 1359, and animation 0xb7, the
// C1 05 18 with action 0x92, E5 02 02 and 1360. Without the models, /rivaloff cuts before sending anything.
TEST_P(GuildCommandsTest, TheRelationChatCommandsSendTheirRequests)
{
    using Client::Chat::GuildRelationCommand;
    Choose(1);
    auto asked = Client::Hud::RequestAGuildRelation(ChatContext(), GuildRelationCommand::Union);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_EQ(asked.Value().notices, (std::vector<Client::Chat::ChatCommandNotice>{{1355, Client::Ui::SystemMessage}}));
    EXPECT_TRUE(m_sent.empty());

    Hero().guildStatus = 0x00;
    Hero().word1f2 = 3;
    asked = Client::Hud::RequestAGuildRelation(ChatContext(), GuildRelationCommand::Union);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_EQ(asked.Value().notices.size(), 1U);
    EXPECT_EQ(asked.Value().notices[0].formatted, "alianza con Brezal");
    ASSERT_EQ(m_sent.size(), 1U);
    ExpectSentInTheClear(0, {0xc1, 0x07, 0xe5, 0x01, 0x01, 0x04, 0x56});

    Choose(-1);
    asked = Client::Hud::RequestAGuildRelation(ChatContext(), GuildRelationCommand::Rival);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_EQ(asked.Value().notices[0].formatted, "rivales con Brezal");
    ASSERT_EQ(m_sent.size(), 2U);
    ExpectSentInTheClear(1, {0xc1, 0x07, 0xe5, 0x02, 0x01, 0x04, 0x56});

    const auto cut = Client::Hud::RequestAGuildRelation(ChatContext(), GuildRelationCommand::RivalOff);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x571079"), std::string::npos) << cut.Error();
    EXPECT_EQ(m_sent.size(), 2U);

    Choose(1);
    m_random = CrtRandom(Seed);
    Client::Chat::GameChatContext chat = ChatContext();
    chat.models = &m_models;
    asked = Client::Hud::RequestAGuildRelation(chat, GuildRelationCommand::RivalOff);
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_EQ(asked.Value().notices[0].formatted, "fin con Brezal");
    EXPECT_EQ(Hero().animation.action, 0xb7);
    ASSERT_EQ(m_sent.size(), 4U);
    const auto direction = Client::Actors::ActionDirection(Hero().angles.z);
    ASSERT_TRUE(direction.has_value());
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Xor32Decode(m_sent[2].packet).Bytes(),
                                         Client::Tests::Bytes({0xc1, 0x05, 0x18, *direction, 0x92})));
    ExpectSentInTheClear(3, {0xc1, 0x07, 0xe5, 0x02, 0x02, 0x04, 0x56});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(Seed, 4 * CellRandoms).State());
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildCommandsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
INSTANTIATE_TEST_SUITE_P(Maps, SocialRequestsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
INSTANTIATE_TEST_SUITE_P(Maps, CommandsWindowTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
