#include "Scenes/ChatBubbles.h"

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

// 0x556e95: un personaje con gremio lee la tabla de los gremios, de la CL9.4.
TEST_F(ChatBubblesTest, ASpeakerWithAGuildIsCut)
{
    Speaker(4, "Tordo").word1f2 = 3;
    EXPECT_FALSE(
        Client::Scenes::AddChatBubble(*m_world, Measure(), "Tordo", "hola", 4, ChatBubbleMode::Chat).HasValue());
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
