#include "Scenes/GuildWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ventana del gremio (CL9 hito 8, fase 4): 0x64f3e0 con su paso (0x45aa80(0) y 0x651840) y su dibujo (0x651c20): el
// marco, el título, el gremio con el puntaje o los textos sin gremio, el botón de cerrar, las pestañas y el pedido de
// la alianza (E9). Los pasos y los dibujos de las pestañas cortan con su dirección (P-C287). En Lorencia (0) y en
// Devias (2). Los gremios y los textos son inventados.
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::GuildState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 23;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr float Left = 450.0f;
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::uint16_t InTheGuild = 0x3;
constexpr std::uint8_t MemberStatus = 0x00;

struct DrawnQuad
{
    int slot = NoTexture;
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads del lote, sin los marcadores de los textos.
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
        if (slot == Marker)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot, .first = vertices[first], .third = vertices[first + 2]});
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

template <std::size_t Size> void Fill(std::array<char, Size>& target, std::string_view text)
{
    target = {};
    for (std::size_t i = 0; i < text.size() && i + 1 < Size; ++i)
        target[i] = text[i];
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

Client::Data::Text::TextTable WindowTexts()
{
    const std::map<std::size_t, std::string> texts{{180, "Gremio"},      {185, "Sin gremio"}, {186, "busca uno"},
                                                   {187, "o arma otro"}, {220, "Salir"},      {946, "Aviso"},
                                                   {1330, "Miembros"},   {1352, "Alianza"}};
    auto table = Client::Data::Text::LoadTextTable(Client::Tests::TextTableBytes(texts));
    EXPECT_TRUE(table.HasValue());
    return std::move(table).Value();
}

class GuildWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GuildWindowTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Hero().guildStatus = 0xff;
        Hero().word1f2 = 0xffff;
        Runtime().panels.delegated.guild = 1;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }

    GuildState& Guild()
    {
        return Runtime().guild;
    }

    // El héroe en el gremio 3 ("Talas", 17 puntos), con o sin alianza.
    void JoinTheGuild(std::string_view alliance)
    {
        Hero().guildStatus = MemberStatus;
        Hero().word1f2 = InTheGuild;
        Fill(Guild().table[InTheGuild].guildName, "Talas");
        Fill(Guild().table[InTheGuild].unionName, alliance);
        Guild().score = 17;
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepTheGuildWindow(Client::Scenes::GuildWindowStepContext{
            .world = *m_world,
            .mouseX = m_mouseX,
            .mouseY = m_mouseY,
            .send = [this](Client::Protocol::Outgoing packet) { m_sent.push_back(std::move(packet)); }});
    }

    Client::Core::Expected<void, std::string> Draw()
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = Hero();
        GameHudContext context{.world = *m_world,
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
                               .mouse = {m_mouseX, m_mouseY}};
        return Client::Scenes::DrawTheGuildWindow(context);
    }

    // El mouse sobre la pestaña `tab` del paso (0x34 × 0x10 en x + 0xf, 0x43 y 0x77, y + 0x4b).
    void PointAtTheTab(std::int32_t tab)
    {
        constexpr std::array<std::int32_t, 3> Lefts{0xf, 0x43, 0x77};
        m_mouseX = WindowX + Lefts[static_cast<std::size_t>(tab)] + 1;
        m_mouseY = 0x4b + 1;
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Data::Text::TextTable m_texts = WindowTexts();
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::int32_t m_mouseX = 0;
    std::int32_t m_mouseY = 300;
};

// 0x651c20 con el héroe sin gremio (+0x368 en 0xff): el fondo, el título en negrita y los textos 185 a 187 en la
// normal, de a 0xf, y el botón de cerrar; ni el puntaje ni las pestañas. El paso tampoco llega a las pestañas. Sin
// rand().
TEST_P(GuildWindowTest, WithoutAGuildItShowsTheThreeTextsAndNoTabs)
{
    ASSERT_TRUE(Step().HasValue());
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 3U);
    ExpectQuadAt(quads[0], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[1], 0x105, Left, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[2], 0x118, Left + 25.0f, 395.0f, 24.0f, 24.0f);
    ASSERT_EQ(m_text.texts.size(), 4U);
    EXPECT_EQ(m_text.texts[0].text, "Gremio");
    EXPECT_EQ(m_text.texts[0].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[0].y, 0xc);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0U);
    EXPECT_EQ(m_text.texts[0].style.font, Client::Render::Text::FontKind::Bold);
    const std::array<std::string_view, 3> lines{"Sin gremio", "busca uno", "o arma otro"};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(line);
        const auto& text = m_text.texts[line + 1];
        EXPECT_EQ(text.text, lines[line]);
        EXPECT_EQ(text.x, WindowX + 0x14);
        EXPECT_EQ(text.y, 0x32 + 0xf * static_cast<int>(line));
        EXPECT_EQ(text.style.colors.text, 0xffe6e6e6u);
        EXPECT_EQ(text.style.font, Client::Render::Text::FontKind::Normal);
    }
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Draws(m_random), 0);
}

// Con gremio: "%s ( Score:%d )" en 0xff64ffc8 bajo el título, las rayas bajo las pestañas 0 y 2 (sin textura), las
// tres pestañas (la 1, la elegida desde 0x64f3e0, blanca y 2 más alta y más arriba; las otras en 0,6) con sus textos
// en y + 0x4b + 4 (la elegida, uno más arriba), y el dibujo de la pestaña corta en 0x6507d0.
TEST_P(GuildWindowTest, WithAGuildItDrawsTheScoreAndTheTabsBeforeTheTabCut)
{
    JoinTheGuild("");
    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x6507d0"), std::string::npos) << drawn.Error();
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 8U);
    ExpectQuadAt(quads[3], NoTexture, Left + 0xf, 0x5b, 54.0f, 1.0f);
    ExpectQuadAt(quads[4], NoTexture, Left + 0xf + 0x6c, 0x5b, 54.0f, 1.0f);
    EXPECT_FLOAT_EQ(quads[3].first.color.red, 146.0f / 255.0f);
    ExpectQuadAt(quads[5], 0x646, Left + 0xf, 0x4b, 52.0f, 16.0f);
    ExpectQuadAt(quads[6], 0x646, Left + 0xf + 0x36, 0x4b - 2, 52.0f, 18.0f);
    ExpectQuadAt(quads[7], 0x646, Left + 0xf + 0x6c, 0x4b, 52.0f, 16.0f);
    EXPECT_FLOAT_EQ(quads[5].first.color.red, 0.6f);
    EXPECT_FLOAT_EQ(quads[6].first.color.red, 1.0f);
    ASSERT_EQ(m_text.texts.size(), 5U);
    EXPECT_EQ(m_text.texts[1].text, "Talas ( Score:17 )");
    EXPECT_EQ(m_text.texts[1].x, WindowX + 0x23);
    EXPECT_EQ(m_text.texts[1].y, 0x35);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xff64ffc8u);
    EXPECT_EQ(m_text.texts[2].text, "Aviso");
    EXPECT_EQ(m_text.texts[2].y, 0x4b + 4);
    EXPECT_EQ(m_text.texts[3].text, "Miembros");
    EXPECT_EQ(m_text.texts[3].y, 0x4b + 3);
    EXPECT_EQ(m_text.texts[4].text, "Alianza");
    EXPECT_EQ(m_text.texts[4].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(Draws(m_random), 0);
}

// Con +0x368 puesto y +0x1f2 en −1, el nombre sale del byte 9 del miembro 74 (Q-C1281); fuera de la tabla, corta.
TEST_P(GuildWindowTest, TheHerosGuildNameReadsBeforeTheTable)
{
    JoinTheGuild("");
    Hero().word1f2 = 0xffff;
    Guild().members[74].name[9] = 'x';
    static_cast<void>(Draw());
    ASSERT_GE(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[1].text, "x ( Score:17 )");
    Hero().word1f2 = 2000;
    const auto cut = Draw();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x651da0"), std::string::npos) << cut.Error();
}

// 0x651895..0x6518e3: el clic en el botón de cerrar lo consume, deja 0x6c8dcc en 6 y cierra (0x6522f0: la pestaña 1 y
// el sonido 0x1c, después del 0x19 del clic).
TEST_P(GuildWindowTest, TheCloseButtonClosesTheWindow)
{
    Guild().window.tab = 2;
    m_mouseX = WindowX + 0x19 + 3;
    m_mouseY = 0x18b + 3;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step().HasValue());
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().panels.delegated.guild, 0);
    EXPECT_EQ(Guild().window.tab, 1);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
}

// 0x651907..0x651bb5: con el izquierdo abajo, la pestaña bajo el mouse. La de la alianza pide la lista (C1 03 E9 en
// claro) solo la primera vez y solo con alianza; después, el paso de la pestaña corta con su dirección.
TEST_P(GuildWindowTest, TheTabsSwitchAndTheUnionTabAsksForTheListOnce)
{
    JoinTheGuild("Ceibo");
    Runtime().movement.leftHeld = true;
    PointAtTheTab(2);
    auto stepped = Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x650c10"), std::string::npos) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 1);
    ASSERT_EQ(m_sent.size(), 1U);
    const auto bytes = m_sent[0].packet.Bytes();
    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(bytes[0], std::byte{0xc1});
    EXPECT_EQ(bytes[1], std::byte{0x03});
    EXPECT_EQ(bytes[2], std::byte{0xe9});

    PointAtTheTab(0);
    stepped = Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x64fc00"), std::string::npos) << stepped.Error();
    EXPECT_EQ(Guild().window.tab, 0);

    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(m_sent.size(), 1U);

    // Sin el izquierdo abajo, la pestaña no cambia.
    Runtime().movement.leftHeld = false;
    PointAtTheTab(1);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Draws(m_random), 0);
}

// Sin alianza, la pestaña cambia y no sale el E9.
TEST_P(GuildWindowTest, WithoutAnAllianceTheUnionTabSendsNothing)
{
    JoinTheGuild("");
    Runtime().movement.leftHeld = true;
    PointAtTheTab(2);
    static_cast<void>(Step());
    EXPECT_EQ(Guild().window.tab, 2);
    EXPECT_EQ(Guild().window.unionRequested, 0);
    EXPECT_TRUE(m_sent.empty());
}

INSTANTIATE_TEST_SUITE_P(Maps, GuildWindowTest, ::testing::Values(0, 2));
} // namespace
