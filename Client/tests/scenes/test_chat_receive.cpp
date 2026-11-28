#include "Chat/ChatReceive.h"

#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Protocol/Chat/Messages.h"
#include "Protocol/Player/OptionData.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/SelectionInventory.h"
#include "Session/WorldEntry.h"
#include "FrontEnd/SelectionState.h"
#include "FrontEnd/LoginModels.h"
#include "Camera/GameCamera.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Protocol/Outgoing.h"
#include "Ui/PanelRegistry.h"
#include "support/RecordingSink.h"
#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "Chat/ChatInput.h"
#include "Ui/MessageArea.h"
#include "Chat/WhisperSenders.h"
#include "Audio/SoundId.h"

#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The incoming chat (CL8 milestone 3; 04 § El chat que llega): 0x4c6070, 0x4e33fc, 0x4e34ea, 0x4e366f, 0x4c65b0 and
// 0x4dd570 on the world. The names and the texts are made up.
namespace
{
using Client::Chat::ChatReceiveContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::SceneId;
using Client::Kernel::WorldState;
using Client::Protocol::Chat::Chat;
using Client::Protocol::Chat::KeyChat;
using Client::Protocol::Chat::Notice;
using Client::Protocol::Chat::Whisper;
using Client::Protocol::Chat::WhisperResult;
using Client::Protocol::Player::OptionData;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * width, 13};
    }

    int width = 6;
};

constexpr std::uint32_t Seed = 12345;

template <typename Spoken> Spoken SpokenBy(std::string_view name, std::string text)
{
    Spoken spoken;
    for (std::size_t i = 0; i < name.size(); ++i)
        spoken.name[i] = static_cast<std::byte>(name[i]);
    spoken.text = std::move(text);
    return spoken;
}

void Name(ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

class ChatReceiveTest : public ::testing::Test
{
protected:
    ChatReceiveTest() : m_world(std::make_unique<WorldState>()), m_random(Seed) {}

    [[nodiscard]] ChatReceiveContext Context(std::optional<SceneId> scene = SceneId::Game)
    {
        return ChatReceiveContext{.world = *m_world,
                                  .random = m_random,
                                  .measure = &m_letters,
                                  .screen = Client::Core::PixelSize{640, 480},
                                  .scene = scene,
                                  .chosenSlot = std::uint8_t{1}};
    }

    // A live player in view at `index`.
    ActorState& Player(std::size_t index, std::string_view name)
    {
        ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.word1f2 = 0xffff;
        Name(actor, name);
        return actor;
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    // The rand() state after `count` more from the seed.
    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
    LetterMeasure m_letters;
};

// 0x4c6386: the normal chat goes to a bubble (0x557560 with 0) and to the chat (type 3), with 60 bytes from pkt[0xd].
TEST_F(ChatReceiveTest, AChatGoesToABubbleAndToTheChat)
{
    Player(7, "Zorzal");
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Zorzal", "buen dia"), Context()).HasValue());

    const Client::Chat::ChatBubble& bubble = Runtime().chat.bubbles[0];
    EXPECT_EQ(bubble.owner, std::optional<std::size_t>(7));
    EXPECT_EQ(bubble.name, "Zorzal");
    EXPECT_EQ(bubble.line1, "buen dia");
    EXPECT_EQ(bubble.time1, 2 * 8 + 0xa0);
    EXPECT_EQ(bubble.color, 3);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::ChatMessage);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "buen dia");
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4c6098: at the login scene the 00 only asks for the list again; it reaches no bubble and no measure.
TEST_F(ChatReceiveTest, AtTheLoginTheChatOnlyAsksForTheListAgain)
{
    Player(7, "Zorzal");
    int requests = 0;
    ChatReceiveContext context = Context(SceneId::Login);
    context.measure = nullptr;
    context.requestServerList = [&requests] { ++requests; };
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Zorzal", "buen dia"), context).HasValue());

    EXPECT_EQ(requests, 1);
    EXPECT_TRUE(Runtime().chat.bubbles[0].name.empty());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4c62ef..0x4c6380: ~ to the party chat, @@ to the guild notice, @ to the guild; without the mark.
TEST_F(ChatReceiveTest, TheMarksPickTheChannel)
{
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", "~vamos"), Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", "@@reunion"), Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", "@listos"), Context()).HasValue());

    const auto& entries = Runtime().chat.log.entries;
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[2].type, Client::Ui::PartyMessage);
    EXPECT_EQ(entries[2].text, "vamos");
    EXPECT_EQ(entries[1].type, Client::Ui::GuildNoticeMessage);
    EXPECT_EQ(entries[1].text, "reunion");
    EXPECT_EQ(entries[0].type, Client::Ui::GuildMessage);
    EXPECT_EQ(entries[0].text, "listos");
    // Nobody with that name speaks: no bubble.
    EXPECT_FALSE(Runtime().chat.bubbles[0].owner.has_value());
}

// 0x4c62f6, 0x4c6334 and 0x4c6386: 59 bytes after ~, 58 after @@ and 60 without a mark.
TEST_F(ChatReceiveTest, EachBranchCopiesItsOwnByteCount)
{
    // At one point per letter, the log splits no text in two.
    m_letters.width = 1;
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", "~" + std::string(59, 'p')), Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", "@@" + std::string(58, 'g')), Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Tero", std::string(60, 'n')), Context()).HasValue());

    const auto& entries = Runtime().chat.log.entries;
    std::string all;
    for (const auto& entry : entries)
        all += entry.text;
    EXPECT_EQ(std::count(all.begin(), all.end(), 'p'), 59);
    EXPECT_EQ(std::count(all.begin(), all.end(), 'g'), 58);
    EXPECT_EQ(std::count(all.begin(), all.end(), 'n'), 60);
}

// 0x4c6352: # only to the bubble, which with mode 1 also goes to the log and lasts 1000 steps.
TEST_F(ChatReceiveTest, AShoutOnlyMakesALongBubble)
{
    Player(3, "Calandria");
    ASSERT_TRUE(Client::Chat::ApplyChat(SpokenBy<Chat>("Calandria", "#aca"), Context()).HasValue());

    EXPECT_EQ(Runtime().chat.bubbles[0].line1, "aca");
    EXPECT_EQ(Runtime().chat.bubbles[0].time1, 1000);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::ChatMessage);
    EXPECT_EQ(Runtime().chat.area.count, 0);
}

// 0x4e34ea: the own record opens once (3848 rand() on closing it) and, with level 9, the sender is noted.
TEST_F(ChatReceiveTest, AWhisperClosesTheOwnRecordAndRemembersTheSender)
{
    m_world->OwnCharacter().level = 9;
    Runtime().chat.noticeSound = true;
    ASSERT_TRUE(Client::Chat::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(Client::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Client::Chat::WhisperSenderAt(Runtime().chat.whisperSenders, 0), "Benteveo");
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x26});
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::WhisperMessage);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "psst");
}

// With level 10 it does not note it; with the record already open, the close draws nothing.
TEST_F(ChatReceiveTest, AWhisperAtLevelTenWithTheRecordOpenTakesNoRandoms)
{
    m_world->OwnCharacter().level = 10;
    Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Client::Chat::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(0));
    EXPECT_EQ(Client::Chat::WhisperSenderAt(Runtime().chat.whisperSenders, 0), "");
}

// 0x4e34f1: with 0x77f3b98, nothing: not even the own record.
TEST_F(ChatReceiveTest, BlockedWhispersDoNothing)
{
    Runtime().chat.whisperBlocked = true;
    ASSERT_TRUE(Client::Chat::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(0));
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
}

// 0x4e366f: only the 0; the text 482 without Text.bmd stops.
TEST_F(ChatReceiveTest, OnlyAFailedWhisperIsReported)
{
    EXPECT_TRUE(Client::Chat::ApplyWhisperResult(WhisperResult{1}, Context()).HasValue());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_FALSE(Client::Chat::ApplyWhisperResult(WhisperResult{0}, Context()).HasValue());
}

// 0x4e33fc: the bubble of the one with the key; a key from 0x8000 belongs to nobody (MOVSX).
TEST_F(ChatReceiveTest, AKeyChatFindsTheSpeakerByItsKey)
{
    Player(12, "Hornero").key = 0x1234;
    Player(13, "Carancho").key = 0x8001;
    ASSERT_TRUE(
        Client::Chat::ApplyKeyChat(KeyChat{.key = 0x1234, .text = std::string("hola\0x", 6)}, Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyKeyChat(KeyChat{.key = 0x8001, .text = "chau"}, Context()).HasValue());

    EXPECT_EQ(Runtime().chat.bubbles[0].owner, std::optional<std::size_t>(12));
    EXPECT_EQ(Runtime().chat.bubbles[0].name, "Hornero");
    EXPECT_EQ(Runtime().chat.bubbles[0].line1, "hola");
    EXPECT_FALSE(Runtime().chat.bubbles[1].owner.has_value());
}

// 0x4e341d and 0x4e3450: with the hero at 0x80 and the speaker with the name of 0x6c8404, without a bubble, the guild's
// window 2 (0x6599a0(0x40000, 0): the NPC shop and trade queries at 0x661b90 and the two of 0x657ab0), the sign-up
// field on, the chat's closed, the buffers empty, the Tab count at 0 and the hero at the 1999.
TEST_F(ChatReceiveTest, TheGuildMasterKeyChatOpensTheSecondGuildWindow)
{
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    ActorState& hero = m_world->Hero();
    hero.guildStatus = 0x80;
    hero.word1f2 = 0xffff;
    Client::Chat::ChatInputState& input = Runtime().chat.input;
    Runtime().ui.chatOpen = true;
    input.fieldCount = 2;
    Client::Chat::CopyIntoTheBuffer(input.messageBuffer, "algo");
    input.messageLength = 4;
    ASSERT_TRUE(Client::Chat::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, Context()).HasValue());

    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
    EXPECT_TRUE(input.dialogFieldOn);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(input.fieldCount, 0);
    EXPECT_EQ(input.messageLength, 0);
    EXPECT_EQ(input.messageBuffer[0], '\0');
    EXPECT_EQ(hero.word1f2, 1999);
    EXPECT_FALSE(Runtime().chat.bubbles[0].owner.has_value());
    EXPECT_EQ(m_random.State(), After(4));
}

// 0x4e345c → 0x6599a0: opening the guild also closes Senior, Hunt and the three battle controls.
TEST_F(ChatReceiveTest, TheGuildMasterUsesTheSharedCastleAndBattleClosers)
{
    Client::FrontEnd::ClearSelectionInventory(*m_world, m_random);
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    m_world->Hero().guildStatus = 0x80;
    for (int kind = 0; kind < 5; ++kind)
    {
        auto& delegated = Runtime().panels.delegated;
        delegated = {};
        delegated.castleSiegeList = kind == 0 ? 1 : 0;
        delegated.secondCastleSiege = kind == 1 ? 1 : 0;
        delegated.siegeWeapon = kind == 2 ? 1 : kind == 3 ? 2 : 0;
        delegated.siegeGate = kind == 4 ? 1 : 0;
        Runtime().castleSiege.siegeBattleUi.weaponChoice = 3;
        Runtime().castleSiege.siegeBattleUi.gateKey = 0x1234;
        Runtime().castleSiege.senior.closeWith87 = false;
        std::vector<Client::Protocol::Outgoing> sent;
        auto context = Context();
        context.guildSend = [&](Client::Protocol::Outgoing packet)
        {
            // Senior lowers the flag before the 31; Hunt and the weapons after; the gate also clears its key before.
            EXPECT_EQ(delegated.castleSiegeList, 0);
            EXPECT_EQ(delegated.secondCastleSiege, kind == 1 ? 1 : 0);
            EXPECT_EQ(delegated.siegeWeapon, kind == 2 ? 1 : kind == 3 ? 2 : 0);
            EXPECT_EQ(delegated.siegeGate, 0);
            if (kind == 4)
            {
                EXPECT_EQ(Runtime().castleSiege.siegeBattleUi.gateKey, 0);
            }
            sent.push_back(std::move(packet));
        };
        auto expected = m_random;
        expected.Discard(4); // The same protected accesses as opening the guild without another window.

        auto result = Client::Chat::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, context);

        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(Client::Tests::SameBytes(sent.front().packet.Bytes(), Client::Tests::Bytes({0xc1, 3, 0x31})));
        EXPECT_EQ(delegated.secondGuild, 1);
        EXPECT_EQ(delegated.castleSiegeList, 0);
        EXPECT_EQ(delegated.secondCastleSiege, 0);
        EXPECT_EQ(delegated.siegeWeapon, 0);
        EXPECT_EQ(delegated.siegeGate, 0);
        if (kind == 2 || kind == 3)
        {
            EXPECT_EQ(Runtime().castleSiege.siegeBattleUi.weaponChoice, 0);
        }
        EXPECT_EQ(m_random.State(), expected.State());
    }

    // 0x659933→0x661b90: even occupied=0 blocks CanOpen if the WORD of the Senior's box is not 0xffff.
    auto& delegated = Runtime().panels.delegated;
    delegated = {};
    delegated.castleSiegeList = 1;
    auto& ghost = Runtime().npcWindows.chaosBox[31];
    ghost.id = 7;
    ghost.occupied = 0;
    std::vector<Client::Protocol::Outgoing> sent;
    auto context = Context();
    context.guildSend = [&](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); };
    auto expected = m_random;
    expected.Discard(2); // CanOpen consulta tienda/comercio antes de rechazar; no entra a CloseByMask.
    const auto result = Client::Chat::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(delegated.castleSiegeList, 1);
    EXPECT_EQ(delegated.secondGuild, 0);
    EXPECT_EQ(ghost.id, 7);
    EXPECT_EQ(ghost.occupied, 0u);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x659889..0x659894: CanOpen rejects the warehouse before the closes. 0x4e3461 goes on without looking at the return.
TEST_F(ChatReceiveTest, TheGuildMasterKeepsTheWarehouseWhenTheNativeGuardRefusesTheGuild)
{
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    m_world->Hero().guildStatus = 0x80;
    Client::FrontEnd::SelectionState selection(*m_world);
    Client::Models::ClothWorld cloth;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Scenes::WorldEntryResources resources{.world = *m_world,
                                                  .selection = selection,
                                                  .dataRoot = {},
                                                  .cloth = cloth,
                                                  .camera = camera,
                                                  .cameraInput = cameraInput,
                                                  .models = models,
                                                  .log = log};
    Runtime().panels.warehouse = 1;
    Runtime().npcWindows.warehouseLocked = 1;
    Runtime().npcWindows.grid[0].id = 3;
    Runtime().npcWindows.grid[0].occupied = 1;
    std::vector<Client::Protocol::Outgoing> sent;
    auto context = Context();
    context.worldEntry = &resources;
    context.guildSend = [&](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); };
    auto expected = m_random;
    expected.Discard(2); // Only the protected queries of the NPC shop and the trade.

    auto result = Client::Chat::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, context);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Runtime().panels.warehouse, 1);
    EXPECT_EQ(Runtime().npcWindows.warehouseLocked, 1);
    EXPECT_EQ(Runtime().npcWindows.grid[0].id, 3);
    EXPECT_EQ(Runtime().npcWindows.grid[0].occupied, 1u);
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 0);
    EXPECT_TRUE(Runtime().chat.input.dialogFieldOn);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_EQ(Runtime().chat.input.fieldCount, 0);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x4e3492: the hero with a guild keeps it.
TEST_F(ChatReceiveTest, TheGuildMasterKeepsTheHerosGuild)
{
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    ActorState& hero = m_world->Hero();
    hero.guildStatus = 0x80;
    hero.word1f2 = 12;
    ASSERT_TRUE(Client::Chat::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, Context()).HasValue());
    EXPECT_EQ(hero.word1f2, 12);
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
}

// 0x4c65b0: the 0 to the centered notice; the 1 to the chat outside the selection.
TEST_F(ChatReceiveTest, NoticesGoToTheCenterOrToTheChat)
{
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 0, .text = "atencion"}, Context()).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 1, .text = "sistema"}, Context()).HasValue());

    EXPECT_EQ(Runtime().chat.area.noticeCount, 1);
    EXPECT_EQ(Runtime().chat.area.notices[0].text, "atencion");
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::Ui::SystemMessage);
    EXPECT_FALSE(Runtime().chat.selectionNotice.has_value());
}

// 0x4c6634..0x4c665f: the 2 goes to the notices' area with the 483 (0x554610 with 1) and its text, without the 483, to
// the list of the guild window's notice (0x651680: in front, line by line).
TEST_F(ChatReceiveTest, TheGuildNoticeGoesToTheAreaAndToTheGuildWindow)
{
    auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes({{483, "Gremio: %s"}}));
    ASSERT_TRUE(table.HasValue());
    ChatReceiveContext context = Context();
    context.texts = &table.Value();
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 2, .text = "reunion"}, context).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 2, .text = "hoy"}, context).HasValue());

    EXPECT_EQ(Runtime().chat.area.noticeCount, 2);
    const auto& notices = Runtime().chat.area.notices;
    EXPECT_TRUE(std::any_of(notices.begin(), notices.begin() + 2,
                            [](const auto& notice) { return notice.text == "Gremio: reunion"; }));
    const Client::Guild::GuildWindowState& window = Runtime().guild.window;
    ASSERT_EQ(window.notice.size(), 2u);
    EXPECT_EQ(window.notice[0].text, "hoy");
    EXPECT_EQ(window.notice[1].text, "reunion");
    EXPECT_EQ(window.noticeList.selection, 2);
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4c65f9: in the selection, the first 1 goes to the bar's strip and the others to the chat.
TEST_F(ChatReceiveTest, TheFirstSystemNoticeInTheSelectionGoesToTheBar)
{
    const ChatReceiveContext selection = Context(SceneId::CharacterSelect);
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 1, .text = "primero"}, selection).HasValue());
    ASSERT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 1, .text = "segundo"}, selection).HasValue());

    EXPECT_EQ(Runtime().chat.selectionNotice, std::optional<std::string>("primero"));
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "segundo");
}

// 0x4c6669..0x4c66b3: the banner (10 to 15) only queues in the game, with text and from 1 to 30 repeats:
// 0x46b720(pkt[4], pkt[6..7], text, type − 10, pkt[0xc] · 0.1f, pkt[8..0xb]), without rand().
TEST_F(ChatReceiveTest, TheSlideNoticeQueuesOnlyInTheGame)
{
    const auto& slides = m_world->Runtime().hud.slides;
    EXPECT_TRUE(
        Client::Chat::ApplyNotice(Notice{.type = 11, .byte4 = 1, .text = "evento"}, Context(SceneId::CharacterSelect))
            .HasValue());
    EXPECT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 11, .byte4 = 31, .text = "evento"}, Context()).HasValue());
    EXPECT_TRUE(Client::Chat::ApplyNotice(Notice{.type = 11, .byte4 = 1, .text = ""}, Context()).HasValue());
    EXPECT_TRUE(slides.normal.entries.empty());
    EXPECT_TRUE(slides.bold.entries.empty());

    ASSERT_TRUE(
        Client::Chat::ApplyNotice(
            Notice{.type = 11, .byte4 = 2, .word6 = 5, .color = 0xff112233u, .speed = 30, .text = "evento"}, Context())
            .HasValue());
    ASSERT_TRUE(
        Client::Chat::ApplyNotice(Notice{.type = 14, .byte4 = 1, .color = 0x80ffffffu, .text = "aviso"}, Context())
            .HasValue());

    ASSERT_EQ(slides.normal.entries.size(), 2U);
    auto entry = slides.normal.entries.begin();
    EXPECT_EQ(entry->first, 0U);
    EXPECT_EQ(entry->second.kind, 0);
    EXPECT_EQ(entry->second.text, "evento");
    EXPECT_EQ(entry->second.color, 0xff112233u);
    EXPECT_EQ(entry->second.speed, static_cast<float>(30.0 * static_cast<double>(0.1f)));
    EXPECT_EQ(std::next(entry)->first, 5U);
    ASSERT_EQ(slides.bold.entries.size(), 1U);
    EXPECT_EQ(slides.bold.entries.begin()->second.kind, 0);
    EXPECT_EQ(slides.bold.entries.begin()->second.text, "aviso");
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4dd570: 3848 rand() on closing the record, the selected one's keys and the rest after.
TEST_F(ChatReceiveTest, TheOptionDataFillsTheKeysAndTheOptions)
{
    Client::Kernel::OwnCharacterRecord& own = m_world->OwnCharacter();
    own.words6c[0] = 0x00110a00u; // the skills 0x00, 0x0a, 0x11 and 0x00 at +0x6c..+0x6f
    OptionData data;
    data.skillKeys.fill(0xff);
    data.skillKeys[2] = 0x11;
    data.skillKeys[5] = 0x0a;
    data.skillKeys[6] = 0x33;
    data.flags = 0x01 | 0x10;
    data.quickSlots = {0x01, 0x04, 0x09};
    data.chatWindow = 0x37;

    ASSERT_TRUE(Client::Chat::ApplyOptionData(data, Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(Client::Models::OwnRecordCloseRandoms));
    const auto& keys = own.skillKeys[1];
    EXPECT_EQ(keys[0], 0xff);
    EXPECT_EQ(keys[1], 5);
    EXPECT_EQ(keys[2], 2);
    EXPECT_EQ(keys[3], 0xff);
    EXPECT_EQ(own.skillKeys[0][2], 0);
    EXPECT_TRUE(Runtime().ui.autoAttack);
    EXPECT_FALSE(Runtime().chat.noticeSound);
    EXPECT_FALSE(Runtime().chat.slidesOn);
    EXPECT_EQ(Runtime().hud.quickSlots.types[0], 0x1c01);
    EXPECT_EQ(Runtime().hud.quickSlots.types[2], 0x1c09);
    EXPECT_EQ(Runtime().ui.chatModes.placement, 1);
    EXPECT_EQ(Runtime().chat.log.rows, 9);
    EXPECT_EQ(Runtime().chat.log.alpha, 0.7f);
}

// Without lines, the log leaves its window and the lines stay.
TEST_F(ChatReceiveTest, ZeroRowsTakeTheLogOutOfItsWindow)
{
    Runtime().ui.chatModes.placement = 1;
    OptionData data;
    data.skillKeys.fill(0xff);
    data.chatWindow = 0x04;
    ASSERT_TRUE(Client::Chat::ApplyOptionData(data, Context()).HasValue());

    EXPECT_EQ(Runtime().ui.chatModes.placement, 0);
    EXPECT_EQ(Runtime().chat.log.rows, 6);
    EXPECT_FALSE(Runtime().ui.autoAttack);
    EXPECT_TRUE(Runtime().chat.slidesOn);
}

// 0x4dd645: without a selected one it stops before opening the record: no rand().
TEST_F(ChatReceiveTest, TheOptionDataWithoutAChosenSlotIsCutBeforeTheRecord)
{
    ChatReceiveContext context = Context();
    context.chosenSlot.reset();
    OptionData data;
    EXPECT_FALSE(Client::Chat::ApplyOptionData(data, context).HasValue());
    EXPECT_EQ(m_random.State(), After(0));
}
} // namespace
