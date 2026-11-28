#include "Social/GameSocialDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Hud/GamePanels.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Social/GameSocialDrawing (CL9 milestone 7, phase 2): the duel scoreboard (0x5a6cc0) and the request box (0x5f37f0),
// against their ASM, in Lorencia (0) and in Devias (2). The names and texts are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Ui::GameHudContext;

constexpr std::uint32_t Seed = 7;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .color = vertices[first].color,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

template <std::size_t Size> void Write(std::array<std::byte, Size>& name, std::string_view text)
{
    name = {};
    for (std::size_t at = 0; at < text.size() && at < Size; ++at)
        name[at] = static_cast<std::byte>(text[at]);
}

int Draws(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 1000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

class GameSocialDrawingTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GameSocialDrawingTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Write(m_world->Hero().name, "Tejon");
        Write(Runtime().social.duel.rivalName, "Brezal");
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    GameHudContext Context(std::int32_t x = 0, std::int32_t y = 0)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
        return GameHudContext{.world = *m_world,
                              .random = m_random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &m_texts,
                              .mouse = {.x = x, .y = y}};
    }

    void Score()
    {
        GameHudContext context = Context();
        Client::Hud::GamePanels panels(*m_world, m_random);
        Client::Social::DrawTheDuelScore(context, panels.Registry());
    }

    Client::Core::Expected<void, std::string> Request(std::int32_t x, std::int32_t y)
    {
        GameHudContext context = Context(x, y);
        return Client::Social::DrawTheDuelRequest(context);
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{910, "quiere un duelo"}, {911, "acepta o no"}});
};

TEST_P(GameSocialDrawingTest, WithoutADuelTheScoreDrawsNothing)
{
    Runtime().panels.inventory = 1;
    Score();
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// 0x5a6cc0 with the whole world (640): the band and its frame from 170. Without the inventory, 0x5bb8d0 queries
// nothing.
TEST_P(GameSocialDrawingTest, TheScoreDrawsTheBandTheNamesAndThePoints)
{
    Client::Social::DuelState& duel = Runtime().social.duel;
    duel.active = 1;
    duel.ownScore = 3;
    duel.rivalScore = 12;
    Score();
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 5u + 7u);
    ExpectQuadAt(quads[0], NoTexture, 170.0f, 20.0f, 300.0f, 40.0f);
    EXPECT_EQ(quads[0].color.alpha, 0.5f);
    ExpectQuadAt(quads[1], NoTexture, 170.0f, 20.0f, 300.0f, 1.0f);
    ExpectQuadAt(quads[2], NoTexture, 170.0f, 60.0f, 300.0f, 1.0f);
    ExpectQuadAt(quads[3], NoTexture, 170.0f, 20.0f, 1.0f, 40.0f);
    ExpectQuadAt(quads[4], NoTexture, 470.0f, 20.0f, 1.0f, 40.0f);
    EXPECT_EQ(quads[4].color.red, 0.65f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 7u);
    // 0x623690: x − width / 2 (six columns per letter).
    const std::vector<std::string> expected{"Tejon", "Tejon", "vs", "Brezal", "Brezal", "3", "12"};
    const std::vector<int> xs{241 - 15, 240 - 15, 320 - 6, 401 - 18, 400 - 18, 240 - 3, 400 - 6};
    const std::vector<int> ys{0x1f, 0x1e, 0x1e, 0x1f, 0x1e, 0x30, 0x30};
    const std::vector<std::uint32_t> colors{0xff000000u, 0xffff9600u, 0xff0096ffu, 0xff000000u,
                                            0xff1919ffu, 0xffff9600u, 0xff1919ffu};
    for (std::size_t at = 0; at < texts.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_EQ(texts[at].text, expected[at]);
        EXPECT_EQ(texts[at].x, xs[at]);
        EXPECT_EQ(texts[at].y, ys[at]);
        EXPECT_EQ(texts[at].style.colors.text, colors[at]);
        EXPECT_EQ(texts[at].style.colors.background, 0u);
        EXPECT_EQ(texts[at].style.font,
                  at < 5 ? Client::Render::Text::FontKind::BigBold : Client::Render::Text::FontKind::Normal);
    }
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xff1919ffu);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Draws(m_random), 0);
}

// With the inventory open, the 450 world moves the center to 225 and each of the eight calls to 0x5bb8d0 queries the
// NPC shop (0x10000) and the trade (0x20): 16 rand().
TEST_P(GameSocialDrawingTest, EachWorldWidthOfTheScoreDrawsItsQueries)
{
    Runtime().social.duel.active = 1;
    Runtime().panels.inventory = 1;
    Score();
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_FALSE(quads.empty());
    ExpectQuadAt(quads[0], NoTexture, 75.0f, 20.0f, 300.0f, 40.0f);
    EXPECT_EQ(m_text.texts[2].x, 225 - 6);
    EXPECT_EQ(Draws(m_random), 8 * 2);
}

// 0x5f37f0: without the request, nothing; with it, the box, the additive image, the buttons and the texts.
TEST_P(GameSocialDrawingTest, TheRequestDrawsTheBoxTheImageAndTheButtons)
{
    ASSERT_TRUE(Request(0, 0).HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());

    Runtime().ui.field78abad9 = 1;
    ASSERT_TRUE(Request(0xed, 0xfa).HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 6u + 1u + 2u + 6u);
    ExpectQuadAt(quads[0], 0xfc, 213.0f, 110.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[1], 0xfb, 213.0f, 115.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[4], 0xfb, 213.0f, 235.0f, 213.0f, 40.0f);
    ExpectQuadAt(quads[5], 0xfc, 213.0f, 275.0f, 213.0f, 5.0f);
    ExpectQuadAt(quads[6], 0x650, 256.0f, 120.0f, 128.0f, 128.0f);
    EXPECT_EQ(quads[6].blend, BlendMode::Additive);
    ExpectQuadAt(quads[7], 0xf2, 237.0f, 250.0f, 70.0f, 21.0f);
    EXPECT_EQ(quads[7].blend, BlendMode::Opaque);
    ExpectQuadAt(quads[8], 0xf3, 332.0f, 250.0f, 70.0f, 21.0f);
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 6u);
    EXPECT_EQ(texts[0].text, "[Brezal]");
    EXPECT_EQ(texts[0].y, 0x8d);
    EXPECT_EQ(texts[1].style.colors.text, 0xff18cfffu);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::BigBold);
    EXPECT_EQ(texts[2].text, "quiere un duelo");
    EXPECT_EQ(texts[3].text, "acepta o no");
    EXPECT_EQ(texts[3].y, 0xc9);
    EXPECT_EQ(texts[4].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(texts[5].y, 0xc8);
    EXPECT_EQ(texts[5].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Runtime().hud.dcFont, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Draws(m_random), 0);
}

// The no with the mouse over it (0x14c..0x192) and, without Text.bmd, the cut.
TEST_P(GameSocialDrawingTest, TheNoButtonLightsAndMissingTextsCut)
{
    Runtime().ui.field78abad9 = 1;
    ASSERT_TRUE(Request(0x191, 0x10e).HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_GE(quads.size(), 9u);
    EXPECT_EQ(quads[7].slot, 0xf1);
    EXPECT_EQ(quads[8].slot, 0xf4);
    GameHudContext context = Context();
    context.texts = nullptr;
    const auto cut = Client::Social::DrawTheDuelRequest(context);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("910"), std::string::npos) << cut.Error();
}

INSTANTIATE_TEST_SUITE_P(Maps, GameSocialDrawingTest, ::testing::Values(0, 2));
} // namespace
