#include "Scenes/GameCommandsWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
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
#include <tuple>
#include <utility>
#include <vector>

// La ventana de los comandos, la D (CL8 hito 6, 08 P-C92; 06 § HUD y ventanas, "La party y los comandos"): el paso
// 0x40a450 y el dibujo 0x40a650, en (0x1c2, 0) de 640 × 480. Los botones van en x 495 (0x1c2 + 95 − 50) y en y 53 +
// 28 · comando; el 0 no se dibuja ni se elige. Los textos son inventados y miden 6 columnas por letra. Cada test corre
// en Lorencia y en Devias.
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
constexpr std::uint32_t Seed = 0x5eedd00du;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::int32_t ChaosCastle = 18;
constexpr std::int32_t Left = 0x1c2;
constexpr std::int32_t ButtonsLeft = Left + 45;
constexpr std::int32_t CloseX = Left + 25;
constexpr std::int32_t CloseY = 395;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color Gray{0.5f, 0.5f, 0.5f, 1.0f};
constexpr Color Highlight{1.0f, 0.8f, 0.2f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// Por comando: el nombre y los cuatro lados de la casilla.
constexpr std::size_t QuadsPerName = 5;
// El fondo (2), el título, los once botones y el de cerrar.
constexpr std::size_t FirstNameQuad = 15;

// La y de arriba del botón del comando.
constexpr std::int32_t RowTop(std::int32_t command)
{
    return 53 + 28 * command;
}

std::map<std::size_t, std::string> CommandTexts()
{
    std::map<std::size_t, std::string> texts{{938, "Comandos"},    {927, "Cerrar (D)"}, {255, "aviso cinco"},
                                             {1313, "aviso seis"}, {937, "aviso diez"}, {257, "aviso tres"},
                                             {507, "sin jefe"},    {1320, "sin grupo"}};
    constexpr std::array<std::size_t, 11> Names{943, 1124, 944, 945, 946, 1352, 1321, 1322, 947, 948, 949};
    for (std::size_t command = 0; command < Names.size(); ++command)
        texts[Names[command]] = "Orden " + std::to_string(command + 1);
    return texts;
}

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

class GameCommandsWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GameCommandsWindowTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->Runtime().map = GetParam();
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::UI::CommandsWindowState& Window()
    {
        return Runtime().panels.delegated.commandsWindow;
    }

    GameUiContext UiContext(std::int32_t x, std::int32_t y)
    {
        return GameUiContext{.world = *m_world, .random = m_random, .input = m_input, .mouseX = x, .mouseY = y};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheCommandsWindow(UiContext(x, y));
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
                               .texts = &m_texts,
                               .mouse = {.x = x, .y = y}};
        return Client::Scenes::DrawTheCommandsWindow(context);
    }

    // Lo que dejó un frame anterior: el fondo de 0x5f7440 sin mezcla.
    Client::Core::Expected<void, std::string> Redraw(std::int32_t x, std::int32_t y)
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
        m_sprites.SetBlend(BlendMode::Opaque);
        return Draw(x, y);
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
        Runtime().panels.delegated.commands = 1;
        Runtime().hud.commandsX = Left;
        Runtime().hud.commandsY = 0;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(CommandTexts());
};

// La D abre los comandos (+4 de [0x6e592c]); el paso siguiente mira el mouse desde la esquina que todavía no puso
// ningún dibujo, (0, 0). En Chaos Castle CanOpen no los deja.
TEST_P(GameCommandsWindowTest, TheDLetterOpensTheCommands)
{
    Press(Client::Input::Key::D);
    const auto stepped = Client::Scenes::StepTheGameUi(UiContext(300, 300));
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().panels.delegated.commands, 1);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);
    // Debajo del registro del chat, que con el mouse encima no deja correr los paneles (0x5ee174).
    const auto again = Client::Scenes::StepTheGameUi(UiContext(100, 200));
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_EQ(Runtime().panels.delegated.commands, 1);

    // El botón de los comandos del HUD la cierra.
    Click();
    ASSERT_TRUE(Client::Scenes::StepTheGameUi(UiContext(8, 0x1b4)).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.commands, 0);

    auto chaosCastle = std::make_unique<WorldState>();
    chaosCastle->Runtime().map = ChaosCastle;
    Client::Input::InputState input;
    input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::D});
    CrtRandom random(Seed);
    ASSERT_TRUE(
        Client::Scenes::StepTheGameUi(
            GameUiContext{.world = *chaosCastle, .random = random, .input = input, .mouseX = 300, .mouseY = 300})
            .HasValue());
    EXPECT_EQ(chaosCastle->Runtime().panels.delegated.commands, 0);
}

// 0x40a463..0x40a495: con +4 en 1, el mouse en [x, x + 0xbe) × [y, y + 0x1b1) es de la UI; 0x406050 pone los doce
// estados en 1 en cada paso. Sin +4, nada. No saca rand().
TEST_P(GameCommandsWindowTest, TheStepTakesTheMouseAndResetsTheStates)
{
    Client::UI::GameUiState& ui = Runtime().ui;
    Window().states[3] = 0;
    ASSERT_TRUE(Step(Left, 0).HasValue());
    EXPECT_EQ(ui.mouseOnUi, 0);
    EXPECT_EQ(Window().states[3], 0) << "cerrada no corre 0x406050";

    OpenAndPlace();
    for (const auto& [x, y, inside] :
         {std::tuple{Left, 0, true}, std::tuple{Left + 0xbd, 0x1b0, true}, std::tuple{Left - 1, 10, false},
          std::tuple{Left + 0xbe, 10, false}, std::tuple{Left + 10, 0x1b1, false}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        ui.mouseOnUi = 0;
        Window().states[3] = 0;
        Window().states[11] = 0xfe;
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(ui.mouseOnUi, inside ? 1 : 0);
        for (const std::uint8_t state : Window().states)
            EXPECT_EQ(state, Client::UI::CommandReady);
    }
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x406097: con un comando y un personaje elegidos (0x6c8e30), lo que hace el comando es de la CL9.4. Sin comando,
// el personaje elegido no corta.
TEST_P(GameCommandsWindowTest, AChosenCommandOnAChosenCharacterCuts)
{
    OpenAndPlace();
    Runtime().selectedTargets[2] = 5;
    ASSERT_TRUE(Step(0, 0).HasValue());
    Window().chosen = 2;
    const auto stepped = Step(0, 0);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x406097"), std::string::npos) << stepped.Error();
}

// 0x40a49e..0x40a51d: el clic estrictamente adentro de un botón (108 × 20 desde (495, 53 + 28 · c)) lo gasta, suelta
// el botón y elige el comando; el 0 no se elige.
TEST_P(GameCommandsWindowTest, AClickInsideAButtonChoosesItsCommand)
{
    OpenAndPlace();
    Client::Scenes::GameMovementState& movement = Runtime().movement;
    for (const auto& [x, y] : {std::pair{ButtonsLeft, RowTop(3) + 1}, std::pair{ButtonsLeft + 108, RowTop(3) + 1},
                               std::pair{ButtonsLeft + 1, RowTop(3)}, std::pair{ButtonsLeft + 1, RowTop(3) + 20},
                               std::pair{ButtonsLeft + 1, RowTop(0) + 1}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Window().chosen, -1);
        EXPECT_TRUE(movement.pendingClick);
    }
    ASSERT_TRUE(Step(ButtonsLeft + 107, RowTop(3) + 19).HasValue());
    EXPECT_EQ(Window().chosen, 3);
    EXPECT_FALSE(movement.pendingClick);
    EXPECT_FALSE(movement.leftHeld);

    // Sin el clic, el mouse sobre otro no cambia nada; con el clic, el último.
    ASSERT_TRUE(Step(ButtonsLeft + 1, RowTop(11) + 1).HasValue());
    EXPECT_EQ(Window().chosen, 3);
    Click();
    ASSERT_TRUE(Step(ButtonsLeft + 1, RowTop(11) + 1).HasValue());
    EXPECT_EQ(Window().chosen, 11);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x40a51f..0x40a5a1: el botón de cerrar la cierra sin Close: los dos sonidos, 0x77f3b10 en 0, 0x6c8dcc en 6 y el
// objeto como lo deja la entrada. El botón abajo queda.
TEST_P(GameCommandsWindowTest, TheCloseButtonResetsTheCommands)
{
    OpenAndPlace();
    Client::Scenes::GameMovementState& movement = Runtime().movement;
    movement.mouseUpdateTime = 5;
    Window().chosen = 4;
    Window().target = 9;
    for (const auto& [x, y] : {std::pair{CloseX - 1, CloseY}, std::pair{CloseX + 24, CloseY},
                               std::pair{CloseX, CloseY - 1}, std::pair{CloseX, CloseY + 24}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Runtime().panels.delegated.commands, 1);
    }
    ASSERT_TRUE(Step(CloseX + 23, CloseY + 23).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.commands, 0);
    EXPECT_EQ(Window().chosen, -1);
    EXPECT_EQ(Window().target, -1);
    EXPECT_FALSE(movement.pendingClick);
    EXPECT_TRUE(movement.leftHeld);
    EXPECT_EQ(movement.mouseUpdateTime, 0);
    EXPECT_EQ(movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, 0x1c);
    EXPECT_EQ(m_random.State(), Seed);
}

// Antes del primer dibujo la esquina es (0, 0): los botones van en x 45 y el de cerrar en (25, 395).
TEST_P(GameCommandsWindowTest, BeforeTheFirstDrawTheCornerIsZero)
{
    Runtime().panels.delegated.commands = 1;
    Click();
    ASSERT_TRUE(Step(50, RowTop(2) + 5).HasValue());
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_EQ(Window().chosen, 2);
    Click();
    ASSERT_TRUE(Step(30, 400).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.commands, 0);
}

// 0x40a650: la esquina (0x1c2, 0), el fondo con la mezcla que haya, el título en negrita centrado en la caja, los once
// botones de Message_box (0xf0), el de cerrar y los nombres centrados en x + 50 con su casilla (cuatro lados de
// 0x92 0x86 0x79 sin textura). Al final la fuente grande y la mezcla apagada. No saca rand().
TEST_P(GameCommandsWindowTest, TheWindowDrawsItsButtonsAndNames)
{
    Runtime().panels.delegated.commands = 1;
    ASSERT_TRUE(Redraw(0, 0).HasValue());
    EXPECT_EQ(Runtime().hud.commandsX, Left);
    EXPECT_EQ(Runtime().hud.commandsY, 0);

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), FirstNameQuad + 11 * QuadsPerName);
    ExpectQuadAt(quads[0], 0x104, 450.0f, 0.0f, 190.0f, 256.0f);
    EXPECT_EQ(quads[0].blend, BlendMode::Opaque);
    ExpectQuadAt(quads[1], 0x105, 450.0f, 256.0f, 190.0f, 177.0f);
    EXPECT_FLOAT_EQ(quads[1].third.v, 0.69140625f);
    EXPECT_EQ(quads[2].slot, Marker);
    EXPECT_EQ(quads[2].blend, BlendMode::Alpha);
    for (std::int32_t command = 1; command <= 11; ++command)
    {
        SCOPED_TRACE(command);
        const DrawnQuad& button = quads[static_cast<std::size_t>(2 + command)];
        ExpectQuadAt(button, 0xf0, 495.0f, static_cast<float>(RowTop(command)), 100.0f, 25.0f);
        EXPECT_FLOAT_EQ(button.third.u, 0.83203125f);
        EXPECT_EQ(button.color, White);

        const std::size_t name = FirstNameQuad + static_cast<std::size_t>(command - 1) * QuadsPerName;
        EXPECT_EQ(quads[name].slot, Marker);
        EXPECT_EQ(quads[name].color, White);
        const float box = static_cast<float>(RowTop(command) + 8);
        const Color frame{static_cast<float>(0x92) / 255.0f, static_cast<float>(0x86) / 255.0f,
                          static_cast<float>(0x79) / 255.0f, 1.0f};
        ExpectQuadAt(quads[name + 1], NoTexture, 475.0f, box, 9.0f, 1.0f);
        ExpectQuadAt(quads[name + 2], NoTexture, 475.0f, box + 8.0f, 9.0f, 1.0f);
        ExpectQuadAt(quads[name + 3], NoTexture, 475.0f, box, 1.0f, 9.0f);
        ExpectQuadAt(quads[name + 4], NoTexture, 483.0f, box, 1.0f, 9.0f);
        EXPECT_EQ(quads[name + 1].color, frame);
        EXPECT_EQ(quads[name + 1].blend, BlendMode::Opaque);
    }
    ExpectQuadAt(quads[14], 0x118, 475.0f, 395.0f, 24.0f, 24.0f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 12U);
    EXPECT_EQ(texts[0].text, "Comandos");
    EXPECT_EQ(texts[0].x, 485 + (120 - 48) / 2);
    EXPECT_EQ(texts[0].y, 12);
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[0].style.colors.background, 0xff141414u);
    EXPECT_EQ(texts[1].text, "Orden 1");
    EXPECT_EQ(texts[1].x, 545 - 21);
    EXPECT_EQ(texts[1].y, RowTop(1) + 8);
    EXPECT_EQ(texts[1].style.font, FontKind::Normal);
    EXPECT_EQ(texts[1].style.colors.background, 0x00141414u);
    EXPECT_EQ(texts[11].text, "Orden 11");
    EXPECT_EQ(texts[11].x, 545 - 24);
    EXPECT_EQ(texts[11].y, RowTop(11) + 8);

    EXPECT_EQ(Runtime().hud.dcFont, FontKind::BigBold);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(m_random.State(), Seed);
}

// El elegido: su botón gris, su nombre en negrita y naranja y su casilla marcada (0x64a, 5 × 5 en (477, y + 2)). Con
// el mouse encima (estrictamente adentro de 108 × 20) otro nombre va en negrita y naranja, y con el clic, gris.
TEST_P(GameCommandsWindowTest, TheChosenAndTheHoveredStandOut)
{
    Runtime().panels.delegated.commands = 1;
    Window().chosen = 4;
    ASSERT_TRUE(Redraw(ButtonsLeft + 1, RowTop(7) + 1).HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), FirstNameQuad + 11 * QuadsPerName + 1);
    EXPECT_EQ(quads[2 + 4].color, Gray);
    EXPECT_EQ(quads[2 + 3].color, White);
    const auto nameOf = [](std::int32_t command)
    { return FirstNameQuad + static_cast<std::size_t>(command - 1) * QuadsPerName + (command > 4 ? 1U : 0U); };
    EXPECT_EQ(quads[nameOf(4)].color, Highlight);
    ExpectQuadAt(quads[nameOf(4) + 5], 0x64a, 477.0f, static_cast<float>(RowTop(4) + 10), 5.0f, 5.0f);
    EXPECT_FLOAT_EQ(quads[nameOf(4) + 5].third.u, 0.625f);
    EXPECT_EQ(quads[nameOf(4) + 5].blend, BlendMode::Alpha);
    EXPECT_EQ(quads[nameOf(7)].color, Highlight);
    EXPECT_EQ(quads[nameOf(6)].color, White);
    EXPECT_EQ(m_text.texts[4].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[7].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[6].style.font, FontKind::Normal);

    // En el borde no hay resaltado; con el botón abajo, el resaltado es gris.
    ASSERT_TRUE(Redraw(ButtonsLeft, RowTop(7) + 1).HasValue());
    EXPECT_EQ(m_text.texts[7].style.font, FontKind::Normal);
    ASSERT_TRUE(Redraw(ButtonsLeft + 1, RowTop(7) + 20).HasValue());
    EXPECT_EQ(m_text.texts[7].style.font, FontKind::Normal);
    Click();
    ASSERT_TRUE(Redraw(ButtonsLeft + 107, RowTop(7) + 19).HasValue());
    const std::vector<DrawnQuad> clicked = QuadsOf(m_sprites);
    EXPECT_EQ(clicked[nameOf(7)].color, Gray);
    EXPECT_EQ(clicked[nameOf(4)].color, Gray);
}

// 0x40abdb..0x40ac81: el aviso del elegido según su estado, centrado en (x + 0x5f, y + 0x44) en negrita, negro y sin
// fondo. Con el estado en 1 no hay aviso.
TEST_P(GameCommandsWindowTest, TheChosenStateGivesItsNotice)
{
    Runtime().panels.delegated.commands = 1;
    for (const auto& [chosen, state, notice] :
         {std::tuple{5, 0, "aviso cinco"}, std::tuple{6, 0, "aviso seis"}, std::tuple{10, 0, "aviso diez"},
          std::tuple{3, 0, "aviso tres"}, std::tuple{2, 0, ""}, std::tuple{2, 0xff, "sin jefe"},
          std::tuple{9, 0xfe, "sin grupo"}, std::tuple{9, 1, ""}, std::tuple{9, 7, ""}})
    {
        SCOPED_TRACE(testing::Message() << chosen << ", " << state);
        Window().states.fill(Client::UI::CommandReady);
        Window().chosen = chosen;
        Window().states[static_cast<std::size_t>(chosen)] = static_cast<std::uint8_t>(state);
        ASSERT_TRUE(Redraw(0, 0).HasValue());
        const std::string_view expected = notice;
        if (expected.empty())
        {
            EXPECT_EQ(m_text.texts.size(), 12U);
            EXPECT_EQ(Runtime().hud.dcFont, FontKind::BigBold);
            continue;
        }
        ASSERT_EQ(m_text.texts.size(), 13U);
        const auto& drawn = m_text.texts.back();
        EXPECT_EQ(drawn.text, expected);
        EXPECT_EQ(drawn.x, 545 - static_cast<int>(expected.size()) * 3);
        EXPECT_EQ(drawn.y, 0x44);
        EXPECT_EQ(drawn.style.font, FontKind::Bold);
        EXPECT_EQ(drawn.style.colors.background, 0U);
        EXPECT_EQ(QuadsOf(m_sprites).back().color, Black);
        EXPECT_EQ(Runtime().hud.dcFont, FontKind::Bold);
    }
}

// 0x40a7d4..0x40a875: con el mouse sobre el botón de cerrar, la ayuda (el texto 927 con "D" por sprintf) 13 más
// arriba. MuRet no usa el texto como formato: con un '%' o con 100 bytes o más, corta.
TEST_P(GameCommandsWindowTest, TheCloseHelpIsTheTextWithoutAFormat)
{
    Runtime().panels.delegated.commands = 1;
    ASSERT_TRUE(Redraw(CloseX + 23, CloseY + 23).HasValue());
    ASSERT_EQ(m_text.texts.size(), 13U);
    EXPECT_EQ(m_text.texts[1].text, "Cerrar (D)");
    EXPECT_EQ(m_text.texts[1].x, CloseX);
    EXPECT_EQ(m_text.texts[1].y, CloseY - 13);
    EXPECT_EQ(m_text.texts[1].style.font, FontKind::Normal);
    ASSERT_TRUE(Redraw(CloseX + 24, CloseY).HasValue());
    EXPECT_EQ(m_text.texts.size(), 12U);

    std::map<std::size_t, std::string> texts = CommandTexts();
    texts[927] = std::string(99, 'x');
    m_texts = Client::Tests::MakeTextTable(texts);
    ASSERT_TRUE(Redraw(CloseX, CloseY).HasValue());
    for (const std::string& help : {std::string(100, 'x'), std::string("Cerrar (%s)")})
    {
        texts[927] = help;
        m_texts = Client::Tests::MakeTextTable(texts);
        const auto drawn = Redraw(CloseX, CloseY);
        ASSERT_FALSE(drawn.HasValue());
        EXPECT_NE(drawn.Error().find("0x40a875"), std::string::npos) << drawn.Error();
    }
}

// 0x40aa80..0x40abe5: un personaje elegido (0x6c8e30) lleva su nombre junto al mouse, de la CL9.4; un comando fuera
// de los doce lee fuera del objeto.
TEST_P(GameCommandsWindowTest, TheChosenCharacterAndAStrayCommandCut)
{
    Runtime().panels.delegated.commands = 1;
    Runtime().selectedTargets[2] = 0;
    const auto named = Redraw(0, 0);
    ASSERT_FALSE(named.HasValue());
    EXPECT_NE(named.Error().find("0x40aa8d"), std::string::npos) << named.Error();

    Runtime().selectedTargets[2] = -1;
    Window().chosen = 12;
    const auto stray = Redraw(0, 0);
    ASSERT_FALSE(stray.HasValue());
    EXPECT_NE(stray.Error().find("0x40abe5"), std::string::npos) << stray.Error();
}

// Con +4 en 0, ni el dibujo ni la esquina.
TEST_P(GameCommandsWindowTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Redraw(0, 0).HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.commandsX, 0);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameCommandsWindowTest, ::testing::Values(Lorencia, Devias));
} // namespace
