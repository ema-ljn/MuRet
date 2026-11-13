#include "Scenes/ChatReceive.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/MessageArea.h"
#include "UI/WhisperSenders.h"

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

// El chat que llega (CL8 hito 3; 04 § El chat que llega): 0x4c6070, 0x4e33fc, 0x4e34ea, 0x4e366f, 0x4c65b0 y
// 0x4dd570 sobre el mundo. Los nombres y los textos son inventados.
namespace
{
using Client::Protocol::Messages::Chat;
using Client::Protocol::Messages::KeyChat;
using Client::Protocol::Messages::Notice;
using Client::Protocol::Messages::Whisper;
using Client::Protocol::Messages::WhisperResult;
using Client::Protocol::OptionData::OptionData;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ActorState;
using Client::Scenes::ChatReceiveContext;
using Client::Scenes::SceneId;
using Client::Scenes::WorldState;

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
                                  .screen = Client::App::PixelSize{640, 480},
                                  .scene = scene,
                                  .chosenSlot = std::uint8_t{1}};
    }

    // Un jugador vivo y a la vista en `index`.
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

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    // El estado del rand() después de `count` más desde la semilla.
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

// 0x4c6386: el chat normal va a un globo (0x557560 con 0) y al chat (el tipo 3), con 60 bytes desde pkt[0xd].
TEST_F(ChatReceiveTest, AChatGoesToABubbleAndToTheChat)
{
    Player(7, "Zorzal");
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Zorzal", "buen dia"), Context()).HasValue());

    const Client::Scenes::ChatBubble& bubble = Runtime().chat.bubbles[0];
    EXPECT_EQ(bubble.owner, std::optional<std::size_t>(7));
    EXPECT_EQ(bubble.name, "Zorzal");
    EXPECT_EQ(bubble.line1, "buen dia");
    EXPECT_EQ(bubble.time1, 2 * 8 + 0xa0);
    EXPECT_EQ(bubble.color, 3);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::ChatMessage);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "buen dia");
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4c62ef..0x4c6380: ~ al chat de la party, @@ al aviso del gremio, @ al gremio; sin la marca.
TEST_F(ChatReceiveTest, TheMarksPickTheChannel)
{
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", "~vamos"), Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", "@@reunion"), Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", "@listos"), Context()).HasValue());

    const auto& entries = Runtime().chat.log.entries;
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[2].type, Client::UI::PartyMessage);
    EXPECT_EQ(entries[2].text, "vamos");
    EXPECT_EQ(entries[1].type, Client::UI::GuildNoticeMessage);
    EXPECT_EQ(entries[1].text, "reunion");
    EXPECT_EQ(entries[0].type, Client::UI::GuildMessage);
    EXPECT_EQ(entries[0].text, "listos");
    // Nadie habla con ese nombre: ningún globo.
    EXPECT_FALSE(Runtime().chat.bubbles[0].owner.has_value());
}

// 0x4c62f6, 0x4c6334 y 0x4c6386: 59 bytes después de ~, 58 después de @@ y 60 sin marca.
TEST_F(ChatReceiveTest, EachBranchCopiesItsOwnByteCount)
{
    // De a un punto por letra, el registro no corta ningún texto en dos.
    m_letters.width = 1;
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", "~" + std::string(59, 'p')), Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", "@@" + std::string(58, 'g')), Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Tero", std::string(60, 'n')), Context()).HasValue());

    const auto& entries = Runtime().chat.log.entries;
    std::string all;
    for (const auto& entry : entries)
        all += entry.text;
    EXPECT_EQ(std::count(all.begin(), all.end(), 'p'), 59);
    EXPECT_EQ(std::count(all.begin(), all.end(), 'g'), 58);
    EXPECT_EQ(std::count(all.begin(), all.end(), 'n'), 60);
}

// 0x4c6352: # solo al globo, que con el modo 1 también va al registro y dura 1.000 pasos.
TEST_F(ChatReceiveTest, AShoutOnlyMakesALongBubble)
{
    Player(3, "Calandria");
    ASSERT_TRUE(Client::Scenes::ApplyChat(SpokenBy<Chat>("Calandria", "#aca"), Context()).HasValue());

    EXPECT_EQ(Runtime().chat.bubbles[0].line1, "aca");
    EXPECT_EQ(Runtime().chat.bubbles[0].time1, 1000);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::ChatMessage);
    EXPECT_EQ(Runtime().chat.area.count, 0);
}

// 0x4e34ea: el registro propio se abre una vez (3.848 rand() al cerrarlo) y, con nivel 9, el emisor queda anotado.
TEST_F(ChatReceiveTest, AWhisperClosesTheOwnRecordAndRemembersTheSender)
{
    m_world->OwnCharacter().level = 9;
    Runtime().chat.noticeSound = true;
    ASSERT_TRUE(Client::Scenes::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(Client::Render::Models::OwnRecordCloseRandoms));
    EXPECT_EQ(Client::UI::WhisperSenderAt(Runtime().chat.whisperSenders, 0), "Benteveo");
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x26));
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::WhisperMessage);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "psst");
}

// Con nivel 10 no lo anota; con el registro ya abierto, el cierre no saca nada.
TEST_F(ChatReceiveTest, AWhisperAtLevelTenWithTheRecordOpenTakesNoRandoms)
{
    m_world->OwnCharacter().level = 10;
    Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Client::Scenes::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(0));
    EXPECT_EQ(Client::UI::WhisperSenderAt(Runtime().chat.whisperSenders, 0), "");
}

// 0x4e34f1: con 0x77f3b98, nada: ni el registro propio.
TEST_F(ChatReceiveTest, BlockedWhispersDoNothing)
{
    Runtime().chat.whisperBlocked = true;
    ASSERT_TRUE(Client::Scenes::ApplyWhisper(SpokenBy<Whisper>("Benteveo", "psst"), Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(0));
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
}

// 0x4e366f: solo el 0; el texto 482 sin Text.bmd corta.
TEST_F(ChatReceiveTest, OnlyAFailedWhisperIsReported)
{
    EXPECT_TRUE(Client::Scenes::ApplyWhisperResult(WhisperResult{1}, Context()).HasValue());
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_FALSE(Client::Scenes::ApplyWhisperResult(WhisperResult{0}, Context()).HasValue());
}

// 0x4e33fc: el globo del que tiene la clave; una clave desde 0x8000 no la tiene nadie (MOVSX).
TEST_F(ChatReceiveTest, AKeyChatFindsTheSpeakerByItsKey)
{
    Player(12, "Hornero").key = 0x1234;
    Player(13, "Carancho").key = 0x8001;
    ASSERT_TRUE(
        Client::Scenes::ApplyKeyChat(KeyChat{.key = 0x1234, .text = std::string("hola\0x", 6)}, Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyKeyChat(KeyChat{.key = 0x8001, .text = "chau"}, Context()).HasValue());

    EXPECT_EQ(Runtime().chat.bubbles[0].owner, std::optional<std::size_t>(12));
    EXPECT_EQ(Runtime().chat.bubbles[0].name, "Hornero");
    EXPECT_EQ(Runtime().chat.bubbles[0].line1, "hola");
    EXPECT_FALSE(Runtime().chat.bubbles[1].owner.has_value());
}

// 0x4e341d y 0x4e3450: con el héroe en 0x80 y el que habla con el nombre de 0x6c8404, sin globo, la ventana 2 del
// gremio (0x6599a0(0x40000, 0): las consultas de la tienda de NPC y del comercio en 0x661b90 y las dos de 0x657ab0), el
// campo del alta prendido, el del chat cerrado, los buffers vacíos, la cuenta de Tab en 0 y el héroe en el 1999.
TEST_F(ChatReceiveTest, TheGuildMasterKeyChatOpensTheSecondGuildWindow)
{
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    ActorState& hero = m_world->Actors()[Runtime().hero];
    hero.guildStatus = 0x80;
    hero.word1f2 = 0xffff;
    Client::UI::ChatInputState& input = Runtime().chat.input;
    Runtime().ui.chatOpen = true;
    input.fieldCount = 2;
    Client::UI::CopyIntoTheBuffer(input.messageBuffer, "algo");
    input.messageLength = 4;
    ASSERT_TRUE(Client::Scenes::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, Context()).HasValue());

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

// 0x4e3492: el héroe con gremio lo conserva.
TEST_F(ChatReceiveTest, TheGuildMasterKeepsTheHerosGuild)
{
    ActorState& master = Player(20, "");
    constexpr std::array<std::uint8_t, 10> GuildMaster{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8, 0xb6, 0xbd, 0xba, 0xc5};
    for (std::size_t i = 0; i < GuildMaster.size(); ++i)
        master.name[i] = static_cast<std::byte>(GuildMaster[i]);
    master.byte1e3 = 0xcd;
    master.key = 0x0042;
    ActorState& hero = m_world->Actors()[Runtime().hero];
    hero.guildStatus = 0x80;
    hero.word1f2 = 12;
    ASSERT_TRUE(Client::Scenes::ApplyKeyChat(KeyChat{.key = 0x0042, .text = "hola"}, Context()).HasValue());
    EXPECT_EQ(hero.word1f2, 12);
    EXPECT_EQ(Runtime().panels.delegated.secondGuild, 1);
}

// 0x4c65b0: el 0 al aviso centrado; el 1 al chat fuera de la selección.
TEST_F(ChatReceiveTest, NoticesGoToTheCenterOrToTheChat)
{
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 0, .text = "atencion"}, Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 1, .text = "sistema"}, Context()).HasValue());

    EXPECT_EQ(Runtime().chat.area.noticeCount, 1);
    EXPECT_EQ(Runtime().chat.area.notices[0].text, "atencion");
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::SystemMessage);
    EXPECT_FALSE(Runtime().chat.selectionNotice.has_value());
}

// 0x4c6634..0x4c665f: el 2 va al área de los avisos con el 483 (0x554610 con 1) y su texto, sin el 483, a la lista del
// aviso de la ventana del gremio (0x651680: adelante, renglón por renglón).
TEST_F(ChatReceiveTest, TheGuildNoticeGoesToTheAreaAndToTheGuildWindow)
{
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes({{483, "Gremio: %s"}}));
    ASSERT_TRUE(table.HasValue());
    ChatReceiveContext context = Context();
    context.texts = &table.Value();
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 2, .text = "reunion"}, context).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 2, .text = "hoy"}, context).HasValue());

    EXPECT_EQ(Runtime().chat.area.noticeCount, 2);
    const auto& notices = Runtime().chat.area.notices;
    EXPECT_TRUE(std::any_of(notices.begin(), notices.begin() + 2,
                            [](const auto& notice) { return notice.text == "Gremio: reunion"; }));
    const Client::Scenes::GuildWindowState& window = Runtime().guild.window;
    ASSERT_EQ(window.notice.size(), 2u);
    EXPECT_EQ(window.notice[0].text, "hoy");
    EXPECT_EQ(window.notice[1].text, "reunion");
    EXPECT_EQ(window.noticeList.selection, 2);
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4c65f9: en la selección, el primer 1 va a la franja de la barra y los demás al chat.
TEST_F(ChatReceiveTest, TheFirstSystemNoticeInTheSelectionGoesToTheBar)
{
    const ChatReceiveContext selection = Context(SceneId::CharacterSelect);
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 1, .text = "primero"}, selection).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 1, .text = "segundo"}, selection).HasValue());

    EXPECT_EQ(Runtime().chat.selectionNotice, std::optional<std::string>("primero"));
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "segundo");
}

// 0x4c6669..0x4c66b3: el cartel (10 a 15) solo encola en el juego, con texto y de 1 a 30 repeticiones: 0x46b720(pkt[4],
// pkt[6..7], texto, tipo − 10, pkt[0xc] · 0,1f, pkt[8..0xb]), sin rand().
TEST_F(ChatReceiveTest, TheSlideNoticeQueuesOnlyInTheGame)
{
    const auto& slides = m_world->Runtime().hud.slides;
    EXPECT_TRUE(
        Client::Scenes::ApplyNotice(Notice{.type = 11, .byte4 = 1, .text = "evento"}, Context(SceneId::CharacterSelect))
            .HasValue());
    EXPECT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 11, .byte4 = 31, .text = "evento"}, Context()).HasValue());
    EXPECT_TRUE(Client::Scenes::ApplyNotice(Notice{.type = 11, .byte4 = 1, .text = ""}, Context()).HasValue());
    EXPECT_TRUE(slides.normal.entries.empty());
    EXPECT_TRUE(slides.bold.entries.empty());

    ASSERT_TRUE(
        Client::Scenes::ApplyNotice(
            Notice{.type = 11, .byte4 = 2, .word6 = 5, .color = 0xff112233u, .speed = 30, .text = "evento"}, Context())
            .HasValue());
    ASSERT_TRUE(
        Client::Scenes::ApplyNotice(Notice{.type = 14, .byte4 = 1, .color = 0x80ffffffu, .text = "aviso"}, Context())
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

// 0x4dd570: 3.848 rand() al cerrar el registro, las teclas del elegido y lo demás después.
TEST_F(ChatReceiveTest, TheOptionDataFillsTheKeysAndTheOptions)
{
    Client::Scenes::OwnCharacterRecord& own = m_world->OwnCharacter();
    own.words6c[0] = 0x00110a00u; // las skills 0x00, 0x0a, 0x11 y 0x00 en +0x6c..+0x6f
    OptionData data;
    data.skillKeys.fill(0xff);
    data.skillKeys[2] = 0x11;
    data.skillKeys[5] = 0x0a;
    data.skillKeys[6] = 0x33;
    data.flags = 0x01 | 0x10;
    data.quickSlots = {0x01, 0x04, 0x09};
    data.chatWindow = 0x37;

    ASSERT_TRUE(Client::Scenes::ApplyOptionData(data, Context()).HasValue());

    EXPECT_EQ(m_random.State(), After(Client::Render::Models::OwnRecordCloseRandoms));
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

// Sin renglones, el registro sale de su ventana y los renglones quedan.
TEST_F(ChatReceiveTest, ZeroRowsTakeTheLogOutOfItsWindow)
{
    Runtime().ui.chatModes.placement = 1;
    OptionData data;
    data.skillKeys.fill(0xff);
    data.chatWindow = 0x04;
    ASSERT_TRUE(Client::Scenes::ApplyOptionData(data, Context()).HasValue());

    EXPECT_EQ(Runtime().ui.chatModes.placement, 0);
    EXPECT_EQ(Runtime().chat.log.rows, 6);
    EXPECT_FALSE(Runtime().ui.autoAttack);
    EXPECT_TRUE(Runtime().chat.slidesOn);
}

// 0x4dd645: sin elegido corta antes de abrir el registro: ningún rand().
TEST_F(ChatReceiveTest, TheOptionDataWithoutAChosenSlotIsCutBeforeTheRecord)
{
    ChatReceiveContext context = Context();
    context.chosenSlot.reset();
    OptionData data;
    EXPECT_FALSE(Client::Scenes::ApplyOptionData(data, context).HasValue());
    EXPECT_EQ(m_random.State(), After(0));
}
} // namespace
