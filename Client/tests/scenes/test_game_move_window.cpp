#include "Scenes/GameMoveWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/MoveRequirements.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Protocol/Chat/ChatSend.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "support/ChatTexts.h"
#include "support/RecordingTextDrawer.h"
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
#include <vector>

// La ventana /move (CL8 hito 6; 06 § HUD y ventanas, "La ventana /move"): el paso 0x448580 y el dibujo 0x448fa0. Con
// 640 de ancho: 190 de ventana, el paso de la letra 12, renglones de 14 desde la y 40 y el texto un píxel más abajo.
// Los destinos y los textos son inventados; el texto mide 6 columnas por letra.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed1234u;
// 0x40b4d0: el registro propio, al cerrarse, por cada destino que mira.
constexpr std::uint32_t OwnRecordRandoms = 3848;
constexpr std::uint32_t Allowed = 0xffffffffu;
constexpr std::uint32_t Denied = 0xff1127a4u;
constexpr std::uint32_t Missing = 0xff1a33ffu;

Client::Data::Text::MoveRequirement Destination(std::int32_t index, std::string name, std::int32_t level,
                                                std::uint32_t zen)
{
    return Client::Data::Text::MoveRequirement{
        .index = index, .name = std::move(name), .searchName = {}, .level = level, .zen = zen, .gate = 0};
}

std::map<std::size_t, std::string> MoveTexts()
{
    std::map<std::size_t, std::string> texts = Client::Tests::ChatTexts();
    texts[933] = "Viaje";
    texts[934] = "Lugar";
    texts[935] = "Nivel";
    texts[936] = "Zen";
    texts[1002] = "Salir";
    return texts;
}

CrtRandom Advanced(std::uint32_t count)
{
    CrtRandom random(Seed);
    random.Discard(count);
    return random;
}

struct DrawnQuad
{
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// Los quads sin textura del lote (los de 0x621aa0), en orden.
std::vector<DrawnQuad> ColorQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture.has_value())
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.color = vertices[first].color, .first = vertices[first], .third = vertices[first + 2]});
        }
    }
    return quads;
}

class GameMoveWindowTest : public ::testing::Test
{
protected:
    GameMoveWindowTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        runtime.panels.overlayMode = Client::UI::MoveOverlayMode;
        // Tres destinos: el primero pasa; el segundo pide más nivel; el tercero, más zen.
        runtime.moves = {Destination(1, "Campo", 10, 100), Destination(2, "Bosque", 50, 100),
                         Destination(3, "Lago", 10, 1000)};
        Client::Scenes::OwnCharacterRecord& own = m_world->OwnCharacter();
        own.level = 20;
        own.zen = 500;
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool withChat = true)
    {
        const Client::Scenes::GameChatContext chat{.world = *m_world,
                                                   .random = m_random,
                                                   .texts = &m_texts,
                                                   .send = [this](Client::Protocol::Outgoing packet)
                                                   { m_sent.push_back(std::move(packet)); }};
        return Client::Scenes::StepTheMoveWindow(GameUiContext{.world = *m_world,
                                                               .random = m_random,
                                                               .input = m_input,
                                                               .mouseX = x,
                                                               .mouseY = y,
                                                               .screen = Screen640,
                                                               .chat = withChat ? &chat : nullptr,
                                                               .texts = &m_texts});
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
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
                               .texts = m_drawnTexts,
                               .mouse = {.x = x, .y = y}};
        return Client::Scenes::DrawTheOverlays(context);
    }

    void HoldTheButton()
    {
        m_world->Runtime().movement.leftHeld = true;
        m_world->Runtime().movement.pendingClick = true;
    }

    [[nodiscard]] std::int32_t Mode() const
    {
        return m_world->Runtime().panels.overlayMode;
    }

    // Lo que manda el chat con "/mover " y el nombre, con el nombre del héroe.
    [[nodiscard]] bool Said(std::string_view text) const
    {
        const Client::Scenes::ActorState& hero = m_world->Actors()[m_world->Runtime().hero];
        const Client::Protocol::Outgoing expected = Client::Protocol::Chat::WriteChat(hero.name, text);
        return m_sent.size() == 1 && std::ranges::equal(m_sent[0].packet.Bytes(), expected.packet.Bytes());
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::vector<Client::Protocol::Outgoing> m_sent;
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(MoveTexts());
    const Client::Data::Text::TextTable* m_drawnTexts = &m_texts;
};

// 0x448619..0x448633: el mouse sobre la ventana (0 < x ≤ 190, 0 < y ≤ 3 · 14 + 60) es de la UI. Sin el botón, cada
// destino abre y cierra el registro propio y nada más.
TEST_F(GameMoveWindowTest, TheMouseOverTheWindowIsOfTheUi)
{
    Client::UI::GameUiState& ui = m_world->Runtime().ui;
    ASSERT_TRUE(Step(190, 102).HasValue());
    EXPECT_EQ(ui.mouseOnUi, 1);
    ui.mouseOnUi = 0;
    ASSERT_TRUE(Step(191, 50).HasValue());
    EXPECT_EQ(ui.mouseOnUi, 0);
    ASSERT_TRUE(Step(50, 103).HasValue());
    EXPECT_EQ(ui.mouseOnUi, 0);
    EXPECT_EQ(m_random.State(), Advanced(3 * 3 * OwnRecordRandoms).State());
    EXPECT_EQ(Mode(), Client::UI::MoveOverlayMode);
    EXPECT_TRUE(m_sent.empty());
}

// 0x4487d7..0x448f0e: con el botón abajo sobre el primer renglón (40 a 53), "/mover Campo" como el chat de Enter, la
// ventana cerrada y el botón y el flanco apagados. Solo abrió el registro del primero.
TEST_F(GameMoveWindowTest, AClickOnAnAllowedDestinationSaysIt)
{
    HoldTheButton();
    ASSERT_TRUE(Step(50, 40).HasValue());
    EXPECT_TRUE(Said("/mover Campo"));
    EXPECT_EQ(Mode(), 0);
    EXPECT_FALSE(m_world->Runtime().movement.leftHeld);
    EXPECT_FALSE(m_world->Runtime().movement.pendingClick);
    EXPECT_EQ(m_world->Runtime().chat.input.lastSaid, "/mover Campo");
    EXPECT_EQ(m_random.State(), Advanced(OwnRecordRandoms).State());
}

// El segundo renglón (54 a 67) pide nivel 50 y el héroe tiene 20: nada. Con la clase 3 o 4 pide 2/3 (33).
TEST_F(GameMoveWindowTest, TheLevelIsTwoThirdsForTheThirdAndFourthClasses)
{
    HoldTheButton();
    m_world->OwnCharacter().level = 33;
    ASSERT_TRUE(Step(50, 60).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Mode(), Client::UI::MoveOverlayMode);
    EXPECT_TRUE(m_world->Runtime().movement.leftHeld);

    m_world->OwnCharacter().characterClass = 0x23;
    ASSERT_TRUE(Step(50, 60).HasValue());
    EXPECT_TRUE(Said("/mover Bosque"));
    EXPECT_EQ(Mode(), 0);
}

// El zen se compara sin signo y el PK desde 6 no deja moverse; fuera de las columnas 3 a 187, tampoco.
TEST_F(GameMoveWindowTest, TheZenThePkAndTheEdgesKeepTheWindow)
{
    HoldTheButton();
    ASSERT_TRUE(Step(50, 70).HasValue());
    m_world->Actors()[m_world->Runtime().hero].pk = 6;
    ASSERT_TRUE(Step(50, 40).HasValue());
    m_world->Actors()[m_world->Runtime().hero].pk = 5;
    ASSERT_TRUE(Step(2, 40).HasValue());
    ASSERT_TRUE(Step(188, 40).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Mode(), Client::UI::MoveOverlayMode);
    ASSERT_TRUE(Step(187, 53).HasValue());
    EXPECT_TRUE(Said("/mover Campo"));
}

// 0x448f43..0x448f81: el renglón de abajo (de 102 − 14 − 6 a 102 − 6) la cierra con el botón abajo.
TEST_F(GameMoveWindowTest, TheBottomRowClosesTheWindow)
{
    ASSERT_TRUE(Step(50, 90).HasValue());
    EXPECT_EQ(Mode(), Client::UI::MoveOverlayMode);
    HoldTheButton();
    ASSERT_TRUE(Step(50, 96).HasValue());
    EXPECT_EQ(Mode(), Client::UI::MoveOverlayMode);
    ASSERT_TRUE(Step(50, 82).HasValue());
    EXPECT_EQ(Mode(), 0);
    EXPECT_FALSE(m_world->Runtime().movement.leftHeld);
    EXPECT_FALSE(m_world->Runtime().movement.pendingClick);
    EXPECT_TRUE(m_sent.empty());
}

// Sin la entrada del chat, el clic sobre un destino corta.
TEST_F(GameMoveWindowTest, WithoutTheChatAClickCuts)
{
    HoldTheButton();
    const auto stepped = Step(50, 40, false);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x4487d7"), std::string::npos) << stepped.Error();
}

// 0x448fa0: el fondo de 190 × 102 al 80 %, el renglón rojo en (3, 82) de 185 × 14, el resaltado del primero bajo el
// mouse; el título en negrita centrado en 95 y las columnas en 45, 105 y 155; cada destino en su y (41, 55 y 69) con
// sus colores; el 1002 en 83. Los tres registros propios y la mezcla apagada al final.
TEST_F(GameMoveWindowTest, TheWindowDrawsItsDestinations)
{
    ASSERT_TRUE(Draw(50, 45).HasValue());

    const std::vector<DrawnQuad> quads = ColorQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 3U);
    EXPECT_EQ(quads[0].color, (Color{0.0f, 0.0f, 0.0f, 0.8f}));
    EXPECT_FLOAT_EQ(quads[0].first.x, 1.0f);
    EXPECT_FLOAT_EQ(quads[0].third.x, 191.0f);
    EXPECT_FLOAT_EQ(quads[0].first.y, 479.0f);
    EXPECT_FLOAT_EQ(quads[0].third.y, 480.0f - 103.0f);
    EXPECT_EQ(quads[1].color, (Color{0.6f, 0.0f, 0.0f, 1.0f}));
    EXPECT_FLOAT_EQ(quads[1].first.x, 3.0f);
    EXPECT_FLOAT_EQ(quads[1].third.x, 188.0f);
    EXPECT_FLOAT_EQ(quads[1].first.y, 480.0f - 82.0f);
    EXPECT_FLOAT_EQ(quads[1].third.y, 480.0f - 96.0f);
    EXPECT_EQ(quads[2].color, (Color{0.8f, 0.8f, 0.1f, 0.6f}));
    EXPECT_FLOAT_EQ(quads[2].first.y, 480.0f - 40.0f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 14U);
    EXPECT_EQ(texts[0].text, "Viaje");
    EXPECT_EQ(texts[0].x, 95 - 15);
    EXPECT_EQ(texts[0].y, 5);
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xff1accffu);
    EXPECT_EQ(texts[1].text, "Lugar");
    EXPECT_EQ(texts[1].x, 45 - 15);
    EXPECT_EQ(texts[1].y, 25);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[1].style.colors.text, 0xffffb27fu);
    EXPECT_EQ(texts[3].text, "Zen");
    EXPECT_EQ(texts[3].x, 155 - 9);

    const auto row = [&](std::size_t first, std::string_view name, std::string_view level, std::string_view zen, int y,
                         std::uint32_t nameColor, std::uint32_t levelColor, std::uint32_t zenColor)
    {
        EXPECT_EQ(texts[first].text, name);
        EXPECT_EQ(texts[first].y, y);
        EXPECT_EQ(texts[first].style.colors.text, nameColor) << name;
        EXPECT_EQ(texts[first + 1].text, level);
        EXPECT_EQ(texts[first + 1].style.colors.text, levelColor) << name;
        EXPECT_EQ(texts[first + 2].text, zen);
        EXPECT_EQ(texts[first + 2].style.colors.text, zenColor) << name;
        EXPECT_EQ(texts[first].style.colors.background, 0U);
    };
    row(4, "Campo", "10", "100", 41, Allowed, Allowed, Allowed);
    row(7, "Bosque", "50", "100", 55, Denied, Missing, Denied);
    row(10, "Lago", "10", "1000", 69, Denied, Denied, Missing);
    EXPECT_EQ(texts[13].text, "Salir");
    EXPECT_EQ(texts[13].y, 83);
    EXPECT_EQ(texts[13].style.colors.text, Allowed);

    EXPECT_EQ(m_random.State(), Advanced(3 * OwnRecordRandoms).State());
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(m_world->Runtime().hud.dcFont, Client::Render::Text::FontKind::Normal);
}

// Sin el resaltado: un destino que no pasa, o alguien que agarró el mouse (0x5762eb4). El zen va con signo (_itoa).
TEST_F(GameMoveWindowTest, TheHighlightNeedsAnAllowedRowAndAFreeMouse)
{
    m_world->Runtime().moves[1].zen = 0x80000000u;
    ASSERT_TRUE(Draw(50, 60).HasValue());
    EXPECT_EQ(ColorQuadsOf(m_sprites).size(), 2U);
    EXPECT_EQ(m_text.texts[9].text, "-2147483648");

    m_world->Runtime().ui.capture = 1;
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(Draw(50, 45).HasValue());
    EXPECT_EQ(ColorQuadsOf(m_sprites).size(), 2U);
}

// Sin destinos, el fondo de 60 de alto y el 1002 en 60 − 14 − 5; sin Text.bmd, corta.
TEST_F(GameMoveWindowTest, AnEmptyListDrawsTheFrameAndTheClose)
{
    m_world->Runtime().moves.clear();
    ASSERT_TRUE(Draw(0, 0).HasValue());
    ASSERT_EQ(m_text.texts.size(), 5U);
    EXPECT_EQ(m_text.texts[4].text, "Salir");
    EXPECT_EQ(m_text.texts[4].y, 41);
    EXPECT_EQ(m_random.State(), Seed);

    m_drawnTexts = nullptr;
    const auto drawn = Draw(0, 0);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("933"), std::string::npos) << drawn.Error();
}
} // namespace
