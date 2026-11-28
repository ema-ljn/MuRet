#include "Hud/GameCommandsWindow.h"
#include "Kernel/MouseState.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Hud/GameUi.h"
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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The commands window, the D (CL8 milestone 6, 08 P-C92; 06 § HUD y ventanas, "La party y los comandos"): the step
// 0x40a450 and the drawing 0x40a650, at (0x1c2, 0) of 640 × 480. The buttons go at x 495 (0x1c2 + 95 − 50) and y 53 +
// 28 · command; 0 is neither drawn nor chosen. The texts are made up and measure 6 columns per letter. Each test runs
// in Lorencia and in Devias.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Hud::GameUiContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;

constexpr Client::Core::PixelSize Screen640{640, 480};
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
// Per command: the name and the four sides of its check box.
constexpr std::size_t QuadsPerName = 5;
// The background (2), the title, the eleven buttons and the close one.
constexpr std::size_t FirstNameQuad = 15;

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

// The corners in GL pixels, with what the float math of 0x621b60 loses (H − (y + h) · ry).
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

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Ui::CommandsWindowState& Window()
    {
        return Runtime().panels.delegated.commandsWindow;
    }

    GameUiContext UiContext(std::int32_t x, std::int32_t y)
    {
        return GameUiContext{.world = *m_world, .random = m_random, .input = m_input, .mouseX = x, .mouseY = y};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Hud::StepTheCommandsWindow(UiContext(x, y));
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x, std::int32_t y)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
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
        return Client::Hud::DrawTheCommandsWindow(context);
    }

    // What a previous frame left: the background of 0x5f7440 without blending.
    Client::Core::Expected<void, std::string> Redraw(std::int32_t x, std::int32_t y)
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
        m_sprites.SetBlend(BlendMode::Opaque);
        return Draw(x, y);
    }

    void Click()
    {
        Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
    }

    void Press(Client::Input::Key key)
    {
        m_input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = key});
    }

    // The window as the first drawing leaves it.
    void OpenAndPlace()
    {
        Runtime().panels.delegated.commands = 1;
        Runtime().hud.commandsX = Left;
        Runtime().hud.commandsY = 0;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Input::InputState m_input;
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(CommandTexts());
};

// D opens the commands (+4 of [0x6e592c]); the next step looks at the mouse from the corner no drawing has set yet,
// (0, 0). In Chaos Castle CanOpen does not allow them.
TEST_P(GameCommandsWindowTest, TheDLetterOpensTheCommands)
{
    Press(Client::Input::Key::D);
    const auto stepped = Client::Hud::StepTheGameUi(UiContext(300, 300));
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().panels.delegated.commands, 1);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(Runtime().ui.mouseOnUi, 0);
    // Below the chat log, which with the mouse over it does not let the panels run (0x5ee174).
    const auto again = Client::Hud::StepTheGameUi(UiContext(100, 200));
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(Runtime().ui.mouseOnUi, 1);
    EXPECT_EQ(Runtime().panels.delegated.commands, 1);

    // The HUD's commands button closes it.
    Click();
    ASSERT_TRUE(Client::Hud::StepTheGameUi(UiContext(8, 0x1b4)).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.commands, 0);

    auto chaosCastle = std::make_unique<WorldState>();
    chaosCastle->Runtime().map = ChaosCastle;
    Client::Input::InputState input;
    input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::D});
    CrtRandom random(Seed);
    ASSERT_TRUE(
        Client::Hud::StepTheGameUi(
            GameUiContext{.world = *chaosCastle, .random = random, .input = input, .mouseX = 300, .mouseY = 300})
            .HasValue());
    EXPECT_EQ(chaosCastle->Runtime().panels.delegated.commands, 0);
}

// 0x40a463..0x40a495: with +4 at 1, the mouse in [x, x + 0xbe) × [y, y + 0x1b1) belongs to the UI; 0x406050 sets the
// twelve states to 1 on every step. Without +4, nothing. It draws no rand().
TEST_P(GameCommandsWindowTest, TheStepTakesTheMouseAndResetsTheStates)
{
    Client::Ui::GameUiState& ui = Runtime().ui;
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
            EXPECT_EQ(state, Client::Ui::CommandReady);
    }
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x406097..0x4061aa: with a command and a character chosen (0x6c8e30), 0x406050 reads the four cells (their 16
// rand()) and, with someone who is not a player, leaves the target (+8) at −1; without the right click it does no
// more. Without a command, it does not even read them. The command on a player is in SocialRequestsTest
// (CommandsWindowTest).
TEST_P(GameCommandsWindowTest, AChosenCommandOnAChosenCharacterReadsTheCells)
{
    OpenAndPlace();
    Runtime().selectedTargets[2] = 5;
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(m_random.State(), Seed);
    Window().chosen = 2;
    const auto stepped = Step(0, 0);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Window().target, -1);
    EXPECT_NE(m_random.State(), Seed);
}

// 0x40a49e..0x40a51d: the click strictly inside a button (108 × 20 from (495, 53 + 28 · c)) spends it, releases the
// button and chooses the command; 0 is not chosen.
TEST_P(GameCommandsWindowTest, AClickInsideAButtonChoosesItsCommand)
{
    OpenAndPlace();
    Client::Kernel::MouseState& mouse = Runtime().mouse;
    for (const auto& [x, y] : {std::pair{ButtonsLeft, RowTop(3) + 1}, std::pair{ButtonsLeft + 108, RowTop(3) + 1},
                               std::pair{ButtonsLeft + 1, RowTop(3)}, std::pair{ButtonsLeft + 1, RowTop(3) + 20},
                               std::pair{ButtonsLeft + 1, RowTop(0) + 1}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Window().chosen, -1);
        EXPECT_TRUE(mouse.pendingClick);
    }
    ASSERT_TRUE(Step(ButtonsLeft + 107, RowTop(3) + 19).HasValue());
    EXPECT_EQ(Window().chosen, 3);
    EXPECT_FALSE(mouse.pendingClick);
    EXPECT_FALSE(mouse.leftHeld);

    // Without the click, the mouse over another one changes nothing; with the click, the last one.
    ASSERT_TRUE(Step(ButtonsLeft + 1, RowTop(11) + 1).HasValue());
    EXPECT_EQ(Window().chosen, 3);
    Click();
    ASSERT_TRUE(Step(ButtonsLeft + 1, RowTop(11) + 1).HasValue());
    EXPECT_EQ(Window().chosen, 11);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x40a51f..0x40a5a1: the close button closes it without Close: the two sounds, 0x77f3b10 at 0, 0x6c8dcc at 6 and the
// object as the entry leaves it. The button down stays.
TEST_P(GameCommandsWindowTest, TheCloseButtonResetsTheCommands)
{
    OpenAndPlace();
    Client::Kernel::MouseState& mouse = Runtime().mouse;
    mouse.updateTime = 5;
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
    EXPECT_FALSE(mouse.pendingClick);
    EXPECT_TRUE(mouse.leftHeld);
    EXPECT_EQ(mouse.updateTime, 0);
    EXPECT_EQ(mouse.updateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_EQ(m_random.State(), Seed);
}

// Before the first drawing the corner is (0, 0): the buttons go at x 45 and the close one at (25, 395).
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

// 0x40a650: the corner (0x1c2, 0), the background with whatever blending is on, the bold title centered in the box,
// the eleven Message_box buttons (0xf0), the close one and the names centered at x + 50 with their check box (four
// sides of 0x92 0x86 0x79 without texture). At the end the big font and blending off. It draws no rand().
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
    // The 120 box from 485, with the text shifted (120 − 48) / 2.
    EXPECT_EQ(texts[0].text, "Comandos");
    EXPECT_EQ(texts[0].x, 485);
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].firstColumn, -36);
    EXPECT_EQ(texts[0].columns, 120);
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

// The chosen one: its button gray, its name bold and orange and its check box marked (0x64a, 5 × 5 at (477, y + 2)).
// With the mouse over it (strictly inside 108 × 20) another name goes bold and orange, and with the click, gray.
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

    // On the edge there is no highlight; with the button down, the highlight is gray.
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

// 0x40abdb..0x40ac81: the chosen one's notice by its state, centered at (x + 0x5f, y + 0x44) in bold, black and
// without background. With the state at 1 there is no notice.
TEST_P(GameCommandsWindowTest, TheChosenStateGivesItsNotice)
{
    Runtime().panels.delegated.commands = 1;
    for (const auto& [chosen, state, notice] :
         {std::tuple{5, 0, "aviso cinco"}, std::tuple{6, 0, "aviso seis"}, std::tuple{10, 0, "aviso diez"},
          std::tuple{3, 0, "aviso tres"}, std::tuple{2, 0, ""}, std::tuple{2, 0xff, "sin jefe"},
          std::tuple{9, 0xfe, "sin grupo"}, std::tuple{9, 1, ""}, std::tuple{9, 7, ""}})
    {
        SCOPED_TRACE(testing::Message() << chosen << ", " << state);
        Window().states.fill(Client::Ui::CommandReady);
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

// 0x40a7d4..0x40a875: with the mouse over the close button, the help (text 927 with "D" through sprintf) 13 higher.
// MuRet does not use the text as a format: with a '%' or with 100 bytes or more, it stops.
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

// 0x40aa80..0x40abd8: a chosen player (0x6c8e30, +0x88 at 1, not the hero, of type 0x20a or with +0x3e3) carries the
// 0x65b banner of 128 × 32 at the mouse + 5 and its name twice: in the title box (x + 0x23, y + 0x2d) in 0xff67bfdf
// and centered at (mouse x + 0x45, mouse y + 0xc) in white, both over 0x141414 and with the big font. The second's
// glColor is white if the chosen command is for them (+8) and ready, and (1, 0.2, 0.2) if not. It draws no rand().
TEST_P(GameCommandsWindowTest, TheChosenPlayerGoesNextToTheMouse)
{
    constexpr std::size_t Chosen = 5;
    constexpr std::int32_t MouseX = 200;
    constexpr std::int32_t MouseY = 150;
    constexpr Color NotReady{1.0f, 0.2f, 0.2f, 1.0f};
    Client::Kernel::ActorState& player = m_world->Actors()[Chosen];
    player.byte88 = 1;
    player.type = 0x20a;
    constexpr std::string_view Name = "Brezal";
    for (std::size_t letter = 0; letter < Name.size(); ++letter)
        player.name[letter] = static_cast<std::byte>(Name[letter]);
    Runtime().panels.delegated.commands = 1;
    Runtime().selectedTargets[2] = static_cast<std::int32_t>(Chosen);

    ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
    std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), FirstNameQuad + 11 * QuadsPerName + 3);
    const DrawnQuad& card = quads[FirstNameQuad + 11 * QuadsPerName];
    ExpectQuadAt(card, 0x65b, MouseX + 5.0f, MouseY + 5.0f, 128.0f, 32.0f);
    EXPECT_FLOAT_EQ(card.third.u, 1.0f);
    EXPECT_EQ(card.color, White);
    EXPECT_EQ(quads[quads.size() - 2].color, White);
    EXPECT_EQ(quads.back().color, NotReady);
    EXPECT_EQ(quads.back().blend, BlendMode::Alpha);
    ASSERT_EQ(m_text.texts.size(), 14U);
    const auto& title = m_text.texts[12];
    EXPECT_EQ(title.text, Name);
    EXPECT_EQ(title.x, Left + 0x23);
    EXPECT_EQ(title.y, 0x2d);
    EXPECT_TRUE(title.windowed);
    EXPECT_EQ(title.columns, 120);
    EXPECT_EQ(title.style.font, FontKind::BigBold);
    EXPECT_EQ(title.style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(title.style.colors.background, 0x00141414u);
    const auto& nextToTheMouse = m_text.texts[13];
    EXPECT_EQ(nextToTheMouse.text, Name);
    EXPECT_EQ(nextToTheMouse.x, MouseX + 0x45 - 18);
    EXPECT_EQ(nextToTheMouse.y, MouseY + 0xc);
    EXPECT_EQ(nextToTheMouse.style.font, FontKind::BigBold);
    EXPECT_EQ(nextToTheMouse.style.colors.text, 0xffffffffu);
    EXPECT_EQ(nextToTheMouse.style.colors.background, 0x00141414u);

    // Command 3 for them and ready: white. With the state at 0 (and its notice) or for another one, red.
    Window().chosen = 3;
    Window().target = static_cast<std::int32_t>(Chosen);
    ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
    EXPECT_EQ(QuadsOf(m_sprites).back().color, White);
    Window().states[3] = 0;
    ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
    quads = QuadsOf(m_sprites);
    ASSERT_EQ(m_text.texts.size(), 15U);
    EXPECT_EQ(quads[quads.size() - 2].color, NotReady);
    Window().states[3] = 1;
    Window().target = 4;
    ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
    EXPECT_EQ(QuadsOf(m_sprites).back().color, NotReady);

    // Another type with +0x3e3 too; without +0x3e3, a monster or the hero, nothing.
    Window().chosen = -1;
    player.type = 0x10;
    player.byte3e3 = 1;
    ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
    EXPECT_EQ(m_text.texts.size(), 14U);
    for (const auto& [type, kind, chosen] : {std::tuple{std::uint16_t{0x10}, std::uint8_t{1}, Chosen},
                                             std::tuple{std::uint16_t{0x20a}, std::uint8_t{2}, Chosen},
                                             std::tuple{std::uint16_t{0x20a}, std::uint8_t{1}, Runtime().hero}})
    {
        SCOPED_TRACE(testing::Message() << type << ", " << int{kind} << ", " << chosen);
        player.type = type;
        player.byte3e3 = 0;
        player.byte88 = kind;
        Client::Kernel::ActorState& actor = m_world->Actors()[chosen];
        actor.type = type;
        actor.byte88 = kind;
        Runtime().selectedTargets[2] = static_cast<std::int32_t>(chosen);
        ASSERT_TRUE(Redraw(MouseX, MouseY).HasValue());
        EXPECT_EQ(m_text.texts.size(), 12U);
        EXPECT_EQ(QuadsOf(m_sprites).size(), FirstNameQuad + 11 * QuadsPerName);
    }
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x40aa8d, 0x40ab87 and 0x40abe5: a chosen one outside the 400 or a command outside the twelve reads outside its
// table.
TEST_P(GameCommandsWindowTest, AStrayChosenOrCommandCuts)
{
    Runtime().panels.delegated.commands = 1;
    Runtime().selectedTargets[2] = static_cast<std::int32_t>(m_world->Actors().size());
    const auto outside = Redraw(0, 0);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x40aa8d"), std::string::npos) << outside.Error();

    Client::Kernel::ActorState& player = m_world->Actors()[5];
    player.byte88 = 1;
    player.type = 0x20a;
    Runtime().selectedTargets[2] = 5;
    Window().target = 5;
    Window().chosen = 12;
    const auto strayForHim = Redraw(0, 0);
    ASSERT_FALSE(strayForHim.HasValue());
    EXPECT_NE(strayForHim.Error().find("0x40ab87"), std::string::npos) << strayForHim.Error();

    Runtime().selectedTargets[2] = -1;
    const auto stray = Redraw(0, 0);
    ASSERT_FALSE(stray.HasValue());
    EXPECT_NE(stray.Error().find("0x40abe5"), std::string::npos) << stray.Error();
}

// With +4 at 0, neither the drawing nor the corner.
TEST_P(GameCommandsWindowTest, ClosedItDrawsNothing)
{
    ASSERT_TRUE(Redraw(0, 0).HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_EQ(Runtime().hud.commandsX, 0);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, GameCommandsWindowTest, ::testing::Values(Lorencia, Devias));
} // namespace
