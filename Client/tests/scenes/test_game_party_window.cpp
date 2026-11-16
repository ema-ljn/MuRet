#include "Scenes/GamePartyWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// La ventana de la party vacía (CL8 hito 6, 08 P-C92; 06 § HUD y ventanas, "La party y los comandos"): el paso
// 0x5e3c80 y el dibujo 0x5f0be0, en (0x1c2, 0) de 640 × 480. Los textos son inventados y miden 6 columnas por letra.
// Cada test corre en Lorencia y en Devias.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Client::App::PixelSize Screen800{800, 600};
constexpr std::uint32_t Seed = 0x5eed4242u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// 0x5f7465: la esquina con la que 0x5f7440 llama al dibujo.
constexpr std::int32_t Left = 0x1c2;
constexpr std::int32_t Top = 0;
// El botón de cerrar: de (Left + 25, 395) a 24 más.
constexpr std::int32_t CloseX = Left + 25;
constexpr std::int32_t CloseY = 395;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;

std::map<std::size_t, std::string> PartyTexts()
{
    return {{190, "Grupo"},       {191, "linea uno"},  {192, "linea dos"},   {193, "linea tres"},    {194, ""},
            {195, "linea cinco"}, {196, "linea seis"}, {197, "linea siete"}, {221, "Cerrar ventana"}};
}

// Un quad del lote: el slot (NoTexture sin textura, Marker si es la marca de un texto), la mezcla, el color y las
// esquinas 0 y 2 del fan.
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

// Las esquinas en píxeles de GL, con lo que pierden las cuentas en float de 0x621b60 (H − (y + h) · ry).
void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

// Lo que pide la UI: solo la lista de la party.
class RecordingRequests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& /*body*/) override
    {
        requests.emplace_back("F3 30");
    }
    void RequestLogout(std::uint8_t /*kind*/) override
    {
        requests.emplace_back("F1 02");
    }
    void RequestPartyList() override
    {
        requests.emplace_back("42");
    }
    void SendWindowClose() override
    {
        requests.emplace_back("31");
    }
    void SendSetQuest(std::uint8_t /*quest*/, std::uint8_t /*state*/) override
    {
        requests.emplace_back("A2");
    }
    void SendFriendPacket(Client::Protocol::Outgoing /*packet*/) override
    {
        requests.emplace_back("amigos");
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        requests.emplace_back("Macro.txt");
        return {};
    }
    void Log(std::string_view /*line*/) override
    {
        requests.emplace_back("log");
    }

    std::vector<std::string> requests;
};

class GamePartyWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GamePartyWindowTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->Runtime().map = GetParam();
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    GameUiContext UiContext(std::int32_t x, std::int32_t y, Client::Scenes::IGameUiRequests* requests = nullptr)
    {
        return GameUiContext{
            .world = *m_world, .random = m_random, .input = m_input, .mouseX = x, .mouseY = y, .requests = requests};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::StepThePartyWindow(UiContext(x, y), panels.Registry());
    }

    Client::Core::Expected<void, std::string> StepTheUi(std::int32_t x, std::int32_t y,
                                                        Client::Scenes::IGameUiRequests* requests)
    {
        return Client::Scenes::StepTheGameUi(UiContext(x, y, requests));
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
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
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawThePartyWindow(context, panels.Registry(), Left, Top);
    }

    void Click()
    {
        Client::Scenes::LatchTheLeftButton(Runtime().movement, true, true);
    }

    void Press(Client::Input::Key key)
    {
        m_input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = key});
    }

    // La ventana como la deja el primer dibujo.
    void OpenAndPlace()
    {
        Runtime().panels.party = 1;
        Runtime().hud.partyX = Left;
        Runtime().hud.partyY = Top;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(PartyTexts());
    const Client::Data::Text::TextTable* m_drawnTexts = &m_texts;
};

// 0x5a4c6e..0x5a4ea6: la P abre la party, que cierra lo demás, pone la cuenta en 0 y pide la lista (C1 03 42); el
// paso de la ventana no corrió todavía (0x5ee140 va antes de las letras).
TEST_P(GamePartyWindowTest, ThePLetterOpensThePartyAndAsksForItsList)
{
    RecordingRequests requests;
    Runtime().hud.partyCount = -1;
    Press(Client::Input::Key::P);
    const auto stepped = StepTheUi(100, 100, &requests);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().panels.party, 1);
    EXPECT_EQ(Runtime().hud.partyCount, 0);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"42"}));
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);

    // 0x5e3cb5: el paso siguiente mira el mouse desde la esquina de 0x78a7068, que sin un dibujo sigue en (0, 0).
    // Debajo del registro del chat, que con el mouse encima no deja correr los paneles (0x5ee174).
    const auto again = StepTheUi(100, 200, &requests);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_EQ(requests.requests.size(), 1U);
}

// 0x5a03c3..0x5a05e5: el botón de la party del HUD hace lo mismo que la P; sin el cable, abrirla corta.
TEST_P(GamePartyWindowTest, TheHudButtonOpensThePartyAndWithoutTheWireItCuts)
{
    RecordingRequests requests;
    Click();
    ASSERT_TRUE(StepTheUi(0x15d, 0x1c5, &requests).HasValue());
    EXPECT_EQ(Runtime().panels.party, 1);
    EXPECT_EQ(requests.requests, (std::vector<std::string>{"42"}));

    Runtime().panels.party = 0;
    Press(Client::Input::Key::P);
    const auto stepped = StepTheUi(100, 100, nullptr);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x659aa0"), std::string::npos) << stepped.Error();
    EXPECT_EQ(Runtime().panels.party, 0);
}

// 0x5e3cb5..0x5e3ceb: con la party abierta, el mouse en [x, x + 0xbe) × [y, y + 0x1b1) es de la UI. Cerrada, el paso
// no hace nada. No saca rand().
TEST_P(GamePartyWindowTest, TheMouseOverTheWindowIsOfTheUi)
{
    Client::UI::GameUiState& ui = Runtime().ui;
    Runtime().hud.partyX = Left;
    ASSERT_TRUE(Step(Left, Top).HasValue());
    EXPECT_EQ(ui.mouseOnUi, 0) << "cerrada";

    OpenAndPlace();
    for (const auto& [x, y, inside] :
         {std::tuple{Left, Top, true}, std::tuple{Left + 0xbd, Top + 0x1b0, true}, std::tuple{Left - 1, 10, false},
          std::tuple{Left + 0xbe, 10, false}, std::tuple{Left + 10, Top + 0x1b1, false}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ui.mouseOnUi = 0;
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(ui.mouseOnUi, inside ? 1 : 0);
    }
    EXPECT_EQ(m_random.State(), Seed);
    EXPECT_EQ(Runtime().panels.party, 1);
}

// 0x5e40d4..0x5e411d: el clic en [x + 0x19, + 0x18) × [y + 0x18b, + 0x18) lo gasta, pone 0x77f3b10 en 0 y 0x6c8dcc
// en 6 y cierra con Close(4), que suena sin sacar rand(). El botón abajo (0x7d36874) queda.
TEST_P(GamePartyWindowTest, TheCloseButtonClosesTheParty)
{
    OpenAndPlace();
    Client::Scenes::GameMovementState& movement = Runtime().movement;
    movement.mouseUpdateTime = 4;
    ASSERT_TRUE(Step(CloseX, CloseY).HasValue());
    EXPECT_EQ(Runtime().panels.party, 1) << "sin el clic";
    for (const auto& [x, y] : {std::pair{CloseX - 1, CloseY}, std::pair{CloseX + 24, CloseY},
                               std::pair{CloseX, CloseY - 1}, std::pair{CloseX, CloseY + 24}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Runtime().panels.party, 1);
        EXPECT_TRUE(movement.pendingClick);
    }
    ASSERT_TRUE(Step(CloseX + 23, CloseY + 23).HasValue());
    EXPECT_EQ(Runtime().panels.party, 0);
    EXPECT_FALSE(movement.pendingClick);
    EXPECT_TRUE(movement.leftHeld);
    EXPECT_EQ(movement.mouseUpdateTime, 0);
    EXPECT_EQ(movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Seed);
}

// Antes del primer dibujo la esquina es (0, 0): el botón de cerrar está en (25, 395).
TEST_P(GamePartyWindowTest, BeforeTheFirstDrawTheCornerIsZero)
{
    Runtime().panels.party = 1;
    Click();
    ASSERT_TRUE(Step(30, 400).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_EQ(Runtime().panels.party, 0);
}

// 0x5e3cf0..0x5e3d03: con miembros, el paso corta después de mirar el mouse.
TEST_P(GamePartyWindowTest, WithMembersTheStepCuts)
{
    OpenAndPlace();
    Runtime().hud.partyCount = 2;
    const auto stepped = Step(Left + 5, 5);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5e3d03"), std::string::npos) << stepped.Error();
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);

    // Con la cuenta negativa, como sin miembros.
    Runtime().hud.partyCount = -1;
    ASSERT_TRUE(Step(Left + 5, 5).HasValue());
}

// 0x5f12d0..0x5f130a: cerrada, el ancho del mundo (dos consultas con el inventario abierto) y los colores −1 y 0; sin
// miembros no dibuja nada.
TEST_P(GamePartyWindowTest, TheClosedPartyOnlyAsksTheWorldWidth)
{
    Runtime().hud.textColors = {.text = 0xffff8080u, .background = 0x12345678u};
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0U);
    EXPECT_EQ(m_random.State(), Seed);

    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.partyX, 0) << "cerrada no pone la esquina";

    Runtime().hud.partyCount = 1;
    const auto drawn = Draw(0, 0);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5f130a"), std::string::npos) << drawn.Error();
}

// 0x5f0c01..0x5f11c2: abierta y vacía, la esquina, el fondo sin mezcla (0x104 y 0x105), el título centrado en la caja
// de 120 desde x + 0x23, los renglones 191 a 197 en x + 0x14 (el vacío no se dibuja) y el botón de cerrar.
TEST_P(GamePartyWindowTest, TheEmptyPartyDrawsItsFrameTitleAndLines)
{
    Runtime().panels.party = 1;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    EXPECT_EQ(Runtime().hud.partyX, Left);
    EXPECT_EQ(Runtime().hud.partyY, Top);

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 10U);
    ExpectQuadAt(quads[0], 0x104, 450.0f, 0.0f, 190.0f, 256.0f);
    EXPECT_EQ(quads[0].blend, BlendMode::Opaque);
    EXPECT_EQ(quads[0].color, White);
    EXPECT_FLOAT_EQ(quads[0].third.u, 0.7421875f);
    ExpectQuadAt(quads[1], 0x105, 450.0f, 256.0f, 190.0f, 177.0f);
    EXPECT_EQ(quads[1].blend, BlendMode::Opaque);
    for (std::size_t marker = 2; marker < 9; ++marker)
    {
        EXPECT_EQ(quads[marker].slot, Marker);
        EXPECT_EQ(quads[marker].blend, BlendMode::Alpha);
    }
    ExpectQuadAt(quads[9], 0x118, 475.0f, 395.0f, 24.0f, 24.0f);
    EXPECT_EQ(quads[9].blend, BlendMode::Alpha);
    EXPECT_FLOAT_EQ(quads[9].third.u, 0.75f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 7U);
    // La caja de 120 desde 485, con el fondo en toda ella y el texto corrido (120 − 30) / 2.
    EXPECT_EQ(texts[0].text, "Grupo");
    EXPECT_EQ(texts[0].x, 485);
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].firstColumn, -45);
    EXPECT_EQ(texts[0].columns, 120);
    EXPECT_EQ(texts[0].y, 12);
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(texts[0].style.colors.background, 0xff141414u);
    const std::vector<std::pair<std::string, int>> lines{{"linea uno", 50},   {"linea dos", 65},
                                                         {"linea tres", 80},  {"linea cinco", 115},
                                                         {"linea seis", 130}, {"linea siete", 145}};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(lines[line].first);
        EXPECT_EQ(texts[line + 1].text, lines[line].first);
        EXPECT_EQ(texts[line + 1].x, 470);
        EXPECT_EQ(texts[line + 1].y, lines[line].second);
        EXPECT_EQ(texts[line + 1].style.font, FontKind::Normal);
        EXPECT_EQ(texts[line + 1].style.colors.text, 0xffe6e6e6u);
        EXPECT_EQ(texts[line + 1].style.colors.background, 0U);
    }
    // La medida del último renglón, en 640 × 480.
    EXPECT_EQ(Runtime().hud.arrowsTextWidth, 66);
    EXPECT_EQ(Runtime().hud.arrowsTextHeight, 13);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Alpha);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5f11c2..0x5f12c6: con el mouse sobre el botón de cerrar (izquierda ≤ mouse < izquierda + 24), su ayuda (221) 13
// más arriba, que deja la fuente normal y los colores −1 y 0.
TEST_P(GamePartyWindowTest, TheCloseButtonShowsItsHelpUnderTheMouse)
{
    Runtime().panels.party = 1;
    for (const auto& [x, y] :
         {std::pair{CloseX - 1, CloseY}, std::pair{CloseX + 24, CloseY}, std::pair{CloseX, CloseY + 24}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        m_text.texts.clear();
        ASSERT_TRUE(Draw(x, y).HasValue());
        EXPECT_EQ(m_text.texts.size(), 7U);
    }
    m_text.texts.clear();
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(Draw(CloseX + 23, CloseY).HasValue());
    ASSERT_EQ(m_text.texts.size(), 8U);
    const auto& help = m_text.texts.back();
    EXPECT_EQ(help.text, "Cerrar ventana");
    EXPECT_EQ(help.x, CloseX);
    EXPECT_EQ(help.y, CloseY - 13);
    EXPECT_EQ(help.style.font, FontKind::Normal);
    EXPECT_EQ(help.style.colors.text, 0xffffffffu);
    EXPECT_EQ(help.style.colors.background, 0U);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0U);
    // El marco y el fondo de la ayuda, después del botón, y la mezcla de antes.
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 16U);
    for (std::size_t quad = 10; quad < 15; ++quad)
        EXPECT_EQ(quads[quad].slot, NoTexture);
    EXPECT_EQ(quads[15].slot, Marker);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Alpha);
}

// 0x5f0dde..0x5f0df0: con miembros, el dibujo abierto corta después del título; sin Text.bmd, corta en el título.
TEST_P(GamePartyWindowTest, TheMembersAndAMissingTableCut)
{
    Runtime().panels.party = 1;
    Runtime().hud.partyCount = 3;
    const auto members = Draw(0, 0);
    ASSERT_FALSE(members.HasValue());
    EXPECT_NE(members.Error().find("0x5f0df0"), std::string::npos) << members.Error();
    EXPECT_EQ(m_text.texts.size(), 1U);

    Runtime().hud.partyCount = 0;
    m_drawnTexts = nullptr;
    const auto missing = Draw(0, 0);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5f0cf6"), std::string::npos) << missing.Error();
}

// En 800 × 600 la caja del título es de 150 píxeles: DrawColumns la pone en _ftol(485 · 1,25) = 606, con el texto
// corrido (150 − 30) / 2 = 60 columnas, sin pasar la x por 640 × 480.
TEST_P(GamePartyWindowTest, TheTitleBoxScalesWithTheScreen)
{
    Runtime().panels.party = 1;
    m_sprites.Begin(Screen800);
    ASSERT_TRUE(Draw(0, 0).HasValue());
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].x, 485);
    EXPECT_EQ(m_text.texts[0].y, 12);
    EXPECT_EQ(m_text.texts[0].firstColumn, -60);
    EXPECT_EQ(m_text.texts[0].columns, 150);
}

// 0x554180 con caja llama a 0x467060 aunque el título esté vacío: mide el "0" (6 columnas) y dibuja solo el fondo, en
// 6 + 2 · ((120 − 6) / 2) columnas.
TEST_P(GamePartyWindowTest, AnEmptyTitleDrawsOnlyItsBox)
{
    std::map<std::size_t, std::string> texts = PartyTexts();
    texts[190].clear();
    m_texts = Client::Tests::MakeTextTable(texts);
    Runtime().panels.party = 1;
    ASSERT_TRUE(Draw(0, 0).HasValue());
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "");
    EXPECT_EQ(m_text.texts[0].x, 485);
    EXPECT_EQ(m_text.texts[0].firstColumn, -57);
    EXPECT_EQ(m_text.texts[0].columns, 120);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GamePartyWindowTest, ::testing::Values(Lorencia, Devias));
} // namespace
