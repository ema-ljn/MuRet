#include "Scenes/ChatBubbles.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "support/SceneDataFiles.h"

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

// Los globos (CL8 hito 3; 06 § El chat, "Los globos"): 0x557170, 0x557560 y 0x557650. Los nombres y los textos son
// inventados.
namespace
{
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::ActorState;
using Client::Scenes::ChatBubbleMeasure;
using Client::Scenes::ChatBubbleMode;
using Client::Scenes::WorldState;

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

    [[nodiscard]] Client::Scenes::ChatBubbleTable& Bubbles()
    {
        return m_world->Runtime().chat.bubbles;
    }

    [[nodiscard]] ChatBubbleMeasure Measure() const
    {
        return ChatBubbleMeasure{.measure = m_letters, .screen = Client::App::PixelSize{640, 480}};
    }

    std::unique_ptr<WorldState> m_world;
    LetterMeasure m_letters;
};

// 0x557286: el segundo mensaje del mismo personaje sube el renglón vivo al de arriba.
TEST_F(ChatBubblesTest, TheSameSpeakerKeepsItsEntryAndMovesTheLineUp)
{
    Speaker(4, "Tordo");
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "uno", 4, ChatBubbleMode::Chat).HasValue());
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "dos", 4, ChatBubbleMode::Chat).HasValue());

    EXPECT_EQ(Bubbles()[0].line1, "dos");
    EXPECT_EQ(Bubbles()[0].line2, "uno");
    EXPECT_EQ(Bubbles()[0].time1, 2 * 3 + 0xa0);
    EXPECT_EQ(Bubbles()[0].time2, 2 * 3 + 0xa0);
    EXPECT_FALSE(Bubbles()[1].owner.has_value());
}

// 0x5572ee: con 20 bytes o más, la cabeza arriba (+0x16c) y la cola abajo (+0x6c), con el mismo tiempo.
TEST_F(ChatBubblesTest, ALongTextGoesInTwoLines)
{
    Speaker(4, "Tordo");
    const std::string text = "una frase muy bien larga";
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", text, 4, ChatBubbleMode::Chat).HasValue());

    EXPECT_EQ(Bubbles()[0].line2 + Bubbles()[0].line1, text);
    EXPECT_EQ(Bubbles()[0].line1.front(), ' ');
    EXPECT_EQ(Bubbles()[0].time1, 2 * static_cast<std::int32_t>(text.size()) + 0xa0);
    EXPECT_EQ(Bubbles()[0].time2, Bubbles()[0].time1);
}

// Con el texto vacío, solo el nombre: 100 pasos en una entrada nueva y 10 en la suya.
TEST_F(ChatBubblesTest, AnEmptyTextOnlyShowsTheName)
{
    Speaker(4, "Tordo");
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].nameTime, 100);
    EXPECT_EQ(Bubbles()[0].time1, 0);
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].nameTime, 10);
}

// 0x557170: sin estar vivo o a la vista, nada; +0x88 en 4 da el color 0.
TEST_F(ChatBubblesTest, OnlyLivingSeenSpeakersGetABubble)
{
    Speaker(4, "Tordo").seen = false;
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_FALSE(Bubbles()[0].owner.has_value());

    Speaker(5, "Guardia", 4);
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Guardia", "alto", 5, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].color, 0);
}

// 0x557560: primero los de +0x88 en 1 y después los de 2.
TEST_F(ChatBubblesTest, ByNamePrefersPlayers)
{
    Speaker(2, "Gemelo", 2);
    Speaker(9, "Gemelo", 1);
    ASSERT_TRUE(
        Client::Scenes::AddChatBubbleByName(*m_world, Measure(), "Gemelo", "hola", ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].owner, std::optional<std::size_t>(9));
}

// Un Text.bmd inventado con los puestos de 0x556dd0.
Client::Data::Text::TextTable GuildPosts()
{
    const std::map<std::size_t, std::string> texts{{1295, "aliado"},     {1296, "jefe aliado"}, {1297, "rival"},
                                                   {1298, "jefe rival"}, {1299, "ocho"},        {1300, "jefe"},
                                                   {1301, "segundo"},    {1302, "guerrero"},    {1330, "socio"}};
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

void NameTheGuild(WorldState& world, std::size_t index, std::string_view unionName, std::string_view guildName)
{
    Client::Scenes::GuildTableEntry& entry = world.Runtime().guild.table[index];
    entry.unionName.fill('\0');
    entry.guildName.fill('\0');
    std::copy(unionName.begin(), unionName.end(), entry.unionName.begin());
    std::copy(guildName.begin(), guildName.end(), entry.guildName.begin());
}

// 0x556e95..0x557139: la alianza por +0x370 (con +0x368 en 0x80, el texto del jefe), +0x65 = +0x374 y el gremio con el
// puesto de +0x368.
TEST_F(ChatBubblesTest, ASpeakerWithAGuildShowsTheUnionAndThePost)
{
    const Client::Data::Text::TextTable texts = GuildPosts();
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
        // 0x556eb4: con 1, "<%s> %s" y después el default, "<%s>".
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
            Client::Scenes::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
        EXPECT_EQ(Bubbles()[0].guild, c.guild);
        EXPECT_EQ(Bubbles()[0].guildPost, c.post);
        EXPECT_EQ(Bubbles()[0].guildByte, 2);
    }
}

// 0x556ea5 y 0x55701e: sin alianza, +0x18 vacío; sin gremio otra vez, los dos vacíos y +0x65 en 0.
TEST_F(ChatBubblesTest, WithoutAUnionOrAGuildTheLinesAreEmpty)
{
    const Client::Data::Text::TextTable texts = GuildPosts();
    NameTheGuild(*m_world, 5, "", "Teros");
    ActorState& speaker = Speaker(4, "Tordo");
    speaker.word1f2 = 5;
    speaker.guildStatus = 0;
    speaker.byte374 = 1;
    ChatBubbleMeasure measure = Measure();
    measure.texts = &texts;
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, measure, "Tordo", "uno", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].guild, "");
    EXPECT_EQ(Bubbles()[0].guildPost, "[Teros] socio");
    EXPECT_EQ(Bubbles()[0].guildByte, 1);

    speaker.word1f2 = 0xffff;
    ASSERT_TRUE(Client::Scenes::AddChatBubble(*m_world, measure, "Tordo", "dos", 4, ChatBubbleMode::Chat).HasValue());
    EXPECT_EQ(Bubbles()[0].guild, "");
    EXPECT_EQ(Bubbles()[0].guildPost, "");
    EXPECT_EQ(Bubbles()[0].guildByte, 0);
}

// Sin Text.bmd, o con un renglón de 0x1e bytes o más (Q-C1283), corta; fuera de la tabla, también.
TEST_F(ChatBubblesTest, AGuildLineThatDoesNotFitIsCut)
{
    NameTheGuild(*m_world, 3, "", "Horneros");
    ActorState& speaker = Speaker(4, "Tordo");
    speaker.word1f2 = 3;
    speaker.guildStatus = 0x80;
    EXPECT_FALSE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());

    const auto longPost =
        Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes({{1300, "un puesto largo de mas"}}));
    ASSERT_TRUE(longPost.HasValue());
    ChatBubbleMeasure measure = Measure();
    measure.texts = &longPost.Value();
    const auto cut = Client::Scenes::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("Q-C1283"), std::string::npos);

    speaker.word1f2 = 2000;
    EXPECT_FALSE(Client::Scenes::AddChatBubble(*m_world, measure, "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
}

// 0x557650: los relojes bajan y, con el dueño muerto, quedan en 0.
TEST_F(ChatBubblesTest, TheStepAgesTheBubblesAndForgetsTheDead)
{
    Speaker(4, "Tordo");
    Speaker(6, "Zorzal");
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Zorzal", "chau", 6, ChatBubbleMode::Chat).HasValue());
    m_world->Actors()[6].alive = false;

    const Client::Scenes::ChatBubbleStepInput input{.mouseX = 0, .mouseY = 0, .screen = {640, 480}};
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_EQ(Bubbles()[0].time1, 2 * 4 + 0xa0 - 1);
    EXPECT_EQ(Bubbles()[1].time1, 0);
    EXPECT_EQ(Bubbles()[1].owner, std::optional<std::size_t>(6));
}

// 0x5577a2: con el campo del chat abierto, en la zona segura, el derecho sobre el globo de otro con +0x376 en 0xff
// pone su nombre en el EDIT y en el buffer del susurro, gasta el derecho y suena.
TEST_F(ChatBubblesTest, TheRightClickOnABubbleWhispersToItsOwner)
{
    Speaker(4, "Tordo").word376 = 0xff;
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    Bubbles()[0].x = 100;
    Bubbles()[0].y = 50;
    Bubbles()[0].width = 40;
    Bubbles()[0].height = 20;
    auto& runtime = m_world->Runtime();
    m_world->Actors()[runtime.hero].safeZone = 1;
    runtime.ui.chatOpen = true;
    runtime.ui.rightClick = true;

    const Client::Scenes::ChatBubbleStepInput input{.mouseX = 110, .mouseY = 60, .screen = {640, 480}};
    ASSERT_TRUE(Client::Scenes::StepTheChatBubbles(*m_world, input).HasValue());

    EXPECT_EQ(Client::UI::CStringOf(runtime.chat.input.whisperBuffer), "Tordo");
    EXPECT_EQ(runtime.chat.input.whisperLength, 5);
    EXPECT_EQ(runtime.chat.input.whisper.Value(), "Tordo");
    EXPECT_FALSE(runtime.ui.rightClick);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
}

// Con las 100 ocupadas, un personaje nuevo no tiene globo.
TEST_F(ChatBubblesTest, AFullTableTakesNoNewSpeaker)
{
    for (auto& bubble : Bubbles())
        bubble.time1 = 1;
    Speaker(4, "Tordo");
    ASSERT_TRUE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
    for (const auto& bubble : Bubbles())
        EXPECT_FALSE(bubble.owner.has_value());
}
} // namespace
