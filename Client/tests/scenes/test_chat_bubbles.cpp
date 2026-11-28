#include "Chat/ChatBubbles.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Guild/GuildState.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Audio/SoundId.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// The bubbles (CL8 milestone 3; 06 § El chat, "Los globos"): 0x557170, 0x557560 and 0x557650. The names and the texts
// are made up.
namespace
{
using Client::Chat::ChatBubbleMeasure;
using Client::Chat::ChatBubbleMode;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class ChatBubblesTest : public ::testing::Test
{
protected:
    ChatBubblesTest() : m_world(std::make_unique<WorldState>()) {}

    ActorState& Speaker(std::size_t index, std::string_view name, std::uint8_t kind = 1)
    {
        ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = kind;
        actor.pk = 2;
        actor.word1f2 = 0xffff;
        actor.name.fill(std::byte{0});
        for (std::size_t i = 0; i < name.size(); ++i)
            actor.name[i] = static_cast<std::byte>(name[i]);
        return actor;
    }

    [[nodiscard]] Client::Chat::ChatBubbleTable& Bubbles()
    {
        return m_world->Runtime().chat.bubbles;
    }

    [[nodiscard]] ChatBubbleMeasure Measure() const
    {
        return ChatBubbleMeasure{.measure = m_letters, .screen = Client::Core::PixelSize{640, 480}};
    }

    std::unique_ptr<WorldState> m_world;
    LetterMeasure m_letters;
};

// 0x557286: the same character's second message moves the live line up to the upper one.
TEST_F(ChatBubblesTest, TheSameSpeakerKeepsItsEntryAndMovesTheLineUp)
{
    Speaker(4, "Tordo");
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "uno", 4, ChatBubbleMode::Chat).HasValue());
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "dos", 4, ChatBubbleMode::Chat).HasValue());

    EXPECT_EQ(Bubbles()[0].line1, "dos");
    EXPECT_EQ(Bubbles()[0].line2, "uno");
    EXPECT_EQ(Bubbles()[0].time1, 2 * 3 + 0xa0);
    EXPECT_EQ(Bubbles()[0].time2, 2 * 3 + 0xa0);
    EXPECT_FALSE(Bubbles()[1].owner.has_value());
}

// 0x5572ee: with 20 bytes or more, the head above (+0x16c) and the tail below (+0x6c), with the same time.
TEST_F(ChatBubblesTest, ALongTextGoesInTwoLines)
{
    Speaker(4, "Tordo");
    const std::string text = "una frase muy bien larga";
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", text, 4, ChatBubbleMode::Chat).HasValue());

    EXPECT_EQ(Bubbles()[0].line2 + Bubbles()[0].line1, text);
    EXPECT_EQ(Bubbles()[0].line1.front(), ' ');
    EXPECT_EQ(Bubbles()[0].time1, 2 * static_cast<std::int32_t>(text.size()) + 0xa0);
    EXPECT_EQ(Bubbles()[0].time2, Bubbles()[0].time1);
}

// With the text empty, only the name: 100 steps in a new entry and 10 in its own.
TEST_F(ChatBubblesTest, AnEmptyTextOnlyShowsTheName)
{
    Speaker(4, "Tordo");
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].nameTime, 100);
    EXPECT_EQ(Bubbles()[0].time1, 0);
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].nameTime, 10);
}

// 0x557170: without being alive or in view, nothing; +0x88 at 4 gives the color 0.
TEST_F(ChatBubblesTest, OnlyLivingSeenSpeakersGetABubble)
{
    Speaker(4, "Tordo").seen = false;
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_FALSE(Bubbles()[0].owner.has_value());

    Speaker(5, "Guardia", 4);
    ASSERT_TRUE(
        Client::Chat::AddChatBubble(*m_world, Measure(), "Guardia", "alto", 5, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].color, 0);
}

// 0x557560: first the ones with +0x88 at 1 and then the ones at 2.
TEST_F(ChatBubblesTest, ByNamePrefersPlayers)
{
    Speaker(2, "Gemelo", 2);
    Speaker(9, "Gemelo", 1);
    ASSERT_TRUE(
        Client::Chat::AddChatBubbleByName(*m_world, Measure(), "Gemelo", "hola", ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].owner, std::optional<std::size_t>(9));
}

// A made-up Text.bmd with 0x556dd0's posts.
Client::Tables::TextTable GuildPosts()
{
    const std::map<std::size_t, std::string> texts{{1295, "aliado"},     {1296, "jefe aliado"}, {1297, "rival"},
                                                   {1298, "jefe rival"}, {1299, "ocho"},        {1300, "jefe"},
                                                   {1301, "segundo"},    {1302, "guerrero"},    {1330, "socio"}};
    auto table = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

void NameTheGuild(WorldState& world, std::size_t index, std::string_view unionName, std::string_view guildName)
{
    Client::Guild::GuildTableEntry& entry = world.Runtime().guild.table[index];
    entry.unionName.fill('\0');
    entry.guildName.fill('\0');
    std::copy(unionName.begin(), unionName.end(), entry.unionName.begin());
    std::copy(guildName.begin(), guildName.end(), entry.guildName.begin());
}

// 0x556e95..0x557139: the alliance by +0x370 (with +0x368 at 0x80, the leader's text), +0x65 = +0x374 and the guild
// with the post of +0x368.
TEST_F(ChatBubblesTest, ASpeakerWithAGuildShowsTheUnionAndThePost)
{
    const Client::Tables::TextTable texts = GuildPosts();
    NameTheGuild(*m_world, 3, "Ceibos", "Horneros");
    struct Case
    {
        std::uint8_t relation;
        std::uint8_t status;
        std::string guild;
        std::string post;
    };
    const Case cases[] = {
        {4, 0x80, "<Ceibos> jefe aliado", "[Horneros] jefe"},
        {4, 0x00, "<Ceibos> aliado", "[Horneros] socio"},
        {2, 0x80, "<Ceibos> jefe rival", "[Horneros] jefe"},
        {2, 0x40, "<Ceibos> rival", "[Horneros] segundo"},
        {8, 0x20, "<Ceibos> ocho", "[Horneros] guerrero"},
        // 0x556eb4: with 1, "<%s> %s" and then the default, "<%s>".
        {1, 0x80, "<Ceibos>", "[Horneros] jefe"},
        {0, 0xff, "<Ceibos>", "[Horneros]"},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.guild + " " + c.post);
        ActorState& speaker = Speaker(4, "Tordo");
        speaker.word1f2 = 3;
        speaker.byte370 = c.relation;
        speaker.guildStatus = c.status;
        speaker.byte374 = 2;
        ChatBubbleMeasure measure = Measure();
        measure.texts = &texts;
        ASSERT_TRUE(
            Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
        EXPECT_EQ(Bubbles()[0].guild, c.guild);
        EXPECT_EQ(Bubbles()[0].guildPost, c.post);
        EXPECT_EQ(Bubbles()[0].guildByte, 2);
    }
}

// 0x556ea5 and 0x55701e: without an alliance, +0x18 empty; without a guild again, both empty and +0x65 at 0.
TEST_F(ChatBubblesTest, WithoutAUnionOrAGuildTheLinesAreEmpty)
{
    const Client::Tables::TextTable texts = GuildPosts();
    NameTheGuild(*m_world, 5, "", "Teros");
    ActorState& speaker = Speaker(4, "Tordo");
    speaker.word1f2 = 5;
    speaker.guildStatus = 0;
    speaker.byte374 = 1;
    ChatBubbleMeasure measure = Measure();
    measure.texts = &texts;
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "uno", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].guild, "");
    EXPECT_EQ(Bubbles()[0].guildPost, "[Teros] socio");
    EXPECT_EQ(Bubbles()[0].guildByte, 1);

    speaker.word1f2 = 0xffff;
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "dos", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].guild, "");
    EXPECT_EQ(Bubbles()[0].guildPost, "");
    EXPECT_EQ(Bubbles()[0].guildByte, 0);
}

// Without Text.bmd it stops; outside the table, too. A line that reaches the color (+0x64, 0x2e bytes from +0x36)
// stops (Q-C1283).
TEST_F(ChatBubblesTest, AGuildLineWithoutItsTextOrPastTheColorIsCut)
{
    NameTheGuild(*m_world, 3, "", "Horneros");
    ActorState& speaker = Speaker(4, "Tordo");
    speaker.word1f2 = 3;
    speaker.guildStatus = 0x80;
    EXPECT_FALSE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());

    const auto longPost = Client::Tables::LoadTextTable(
        Client::Tests::TextTableBytes({{1300, "un puesto tan largo que llega al color"}}));
    ASSERT_TRUE(longPost.HasValue());
    ChatBubbleMeasure measure = Measure();
    measure.texts = &longPost.Value();
    const auto cut = Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("Q-C1283"), std::string::npos);
    EXPECT_NE(cut.Error().find("0x557024"), std::string::npos);

    speaker.word1f2 = 2000;
    EXPECT_FALSE(Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
}

// 0x557024 (Q-C1283): "[%s] %s" goes to +0x36 without a limit. With 8 letters and a 19-byte post (Text.bmd's 1300,
// "Maestro del gremio "), 30 bytes: its zero falls at +0x54 and the shop title ends up empty. With the 1301 (32
// bytes), the tail stays as the title. With fewer, the title does not change. In Lorencia and in Devias.
TEST_F(ChatBubblesTest, ALongGuildLineSpillsIntoTheShopTitle)
{
    const auto texts = Client::Tables::LoadTextTable(Client::Tests::TextTableBytes(
        {{1300, "Maestro del gremio "}, {1301, "Asistente del maestro del gremio"}, {1330, "socio"}}));
    ASSERT_TRUE(texts.HasValue());
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        m_world->Runtime().map = map;
        NameTheGuild(*m_world, 3, "", "Horneros");
        m_world->Runtime().social.personalShop.titles[4] =
            Client::Social::ShopTitle{.key = 0x0444, .name = "Tordo", .title = "Vendo"};
        ActorState& speaker = Speaker(4, "Tordo");
        speaker.word1f2 = 3;
        ChatBubbleMeasure measure = Measure();
        measure.texts = &texts.Value();
        const auto said = [&](std::uint8_t status)
        {
            speaker.guildStatus = status;
            const auto added = Client::Chat::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat);
            EXPECT_TRUE(added.HasValue()) << added.Error();
            return Bubbles()[0];
        };

        const Client::Chat::ChatBubble member = said(0);
        EXPECT_EQ(member.guildPost, "[Horneros] socio");
        EXPECT_EQ(member.shopTitle, "Vendo");

        const Client::Chat::ChatBubble master = said(0x80);
        EXPECT_EQ(master.guildPost, "[Horneros] Maestro del gremio ");
        EXPECT_EQ(master.guildPost.size(), 30u);
        EXPECT_EQ(master.shopTitle, "");

        const Client::Chat::ChatBubble assistant = said(0x40);
        EXPECT_EQ(assistant.guildPost, "[Horneros] Asistente del maestro del gremio");
        EXPECT_EQ(assistant.shopTitle, "ro del gremio");
    }
}

// 0x557650: the clocks go down and, with the owner dead, stay at 0.
TEST_F(ChatBubblesTest, TheStepAgesTheBubblesAndForgetsTheDead)
{
    Speaker(4, "Tordo");
    Speaker(6, "Zorzal");
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Zorzal", "chau", 6, ChatBubbleMode::Chat).HasValue());
    m_world->Actors()[6].alive = false;

    const Client::Chat::ChatBubbleStepInput input{.mouseX = 0, .mouseY = 0, .screen = {640, 480}};
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_EQ(Bubbles()[0].time1, 2 * 4 + 0xa0 - 1);
    EXPECT_EQ(Bubbles()[1].time1, 0);
    EXPECT_EQ(Bubbles()[1].owner, std::optional<std::size_t>(6));
}

// 0x5577a2: with the chat field open, in the safe zone, the right click on another's bubble with +0x376 at 0xff puts
// its name in the EDIT and in the whisper buffer, spends the right click and sounds.
TEST_F(ChatBubblesTest, TheRightClickOnABubbleWhispersToItsOwner)
{
    Speaker(4, "Tordo").word376 = 0xff;
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    Bubbles()[0].x = 100;
    Bubbles()[0].y = 50;
    Bubbles()[0].width = 40;
    Bubbles()[0].height = 20;
    auto& runtime = m_world->Runtime();
    m_world->Hero().safeZone = 1;
    runtime.ui.chatOpen = true;
    runtime.ui.rightClick = true;

    const Client::Chat::ChatBubbleStepInput input{.mouseX = 110, .mouseY = 60, .screen = {640, 480}};
    ASSERT_TRUE(Client::Chat::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_EQ(Client::Chat::CStringOf(runtime.chat.input.whisperBuffer), "Tordo");
    EXPECT_EQ(runtime.chat.input.whisperLength, 5);
    EXPECT_EQ(runtime.chat.input.whisper.text.Value(), "Tordo");
    EXPECT_FALSE(runtime.ui.rightClick);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
}

TEST_F(ChatBubblesTest, AFullTableTakesNoNewSpeaker)
{
    for (auto& bubble : Bubbles())
        bubble.time1 = 1;
    Speaker(4, "Tordo");
    ASSERT_TRUE(Client::Chat::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    for (const auto& bubble : Bubbles())
        EXPECT_FALSE(bubble.owner.has_value());
}
} // namespace
