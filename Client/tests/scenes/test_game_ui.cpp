#include "Core/Expected.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace
{
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Input::MouseButton;
using Client::Render::Models::CrtRandom;
using Client::Scenes::FramePickingContext;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

// 0x5a05e5..0x5a10e2: el registro propio de la barra de skills, abierto y cerrado en cada paso.
constexpr int SkillBarRandoms = 3848;
// 0x5a343e..0x5a3587: las consultas del comercio (0x20) y de la tienda de NPC (0x10000) de la cadena de Q, W y E.
constexpr int SlotGateRandoms = 2;
// 0x5a5511: IsOpen(0), con todo cerrado o con un panel arriba de 0x10000.
constexpr int AnyPanelRandoms = 2;
constexpr int BaseStepRandoms = SkillBarRandoms + SlotGateRandoms + AnyPanelRandoms;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

void Press(InputState& input, Key key)
{
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
}

void Release(InputState& input, Key key)
{
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
}

class SteppingUi
{
public:
    Client::Core::Expected<void, std::string> Step(std::int32_t x = 0, std::int32_t y = 0)
    {
        return Client::Scenes::StepTheGameUi(
            GameUiContext{.world = world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }
    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x = 0, std::int32_t y = 0)
    {
        return Client::Scenes::StepTheGameDialogs(
            GameUiContext{.world = world, .random = random, .input = input, .mouseX = x, .mouseY = y});
    }
    void Click()
    {
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true);
    }
    Client::UI::GameUiState& Ui()
    {
        return world.Runtime().ui;
    }
    Client::UI::PanelState& Panels()
    {
        return world.Runtime().panels;
    }

    WorldState world;
    CrtRandom random;
    InputState input;
};

TEST(GameUiTest, TheBaseStepDrawsTheSkillBarTheSlotGatesAndTheAnyPanelTail)
{
    SteppingUi tested;
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    const auto stepped = tested.Step();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    EXPECT_EQ(tested.Ui().skillTooltip, -1);
}

TEST(GameUiTest, TheMouseOverTheBarBelongsToTheUi)
{
    SteppingUi tested;
    tested.Ui().mouseOnUi = 1;
    ASSERT_TRUE(tested.Step(320, 0x1af).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    ASSERT_TRUE(tested.Step(320, 0x1b0).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, EscapeOpensTheMenuWithTheTwoQueriesOfThePanelKey)
{
    SteppingUi tested;
    Press(tested.input, Key::Escape);
    const CrtRandom expected = Advanced(tested.random, 2);
    const auto stepped = tested.Dialogs();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::UI::EscapeMenuDialog);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);
    // El latch no deja leer la misma Esc dos veces.
    tested.world.Runtime().lastSoundId.reset();
    ASSERT_TRUE(tested.Dialogs().HasValue());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::UI::EscapeMenuDialog);
    EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());
}

TEST(GameUiTest, EscapeClosesTheMenuWithoutQueries)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::UI::EscapeMenuDialog;
    Press(tested.input, Key::Escape);
    const auto before = tested.random.State();
    ASSERT_TRUE(tested.Dialogs().HasValue());
    EXPECT_EQ(tested.random.State(), before);
    EXPECT_EQ(tested.world.Runtime().dialogs.current, 0);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);
}

TEST(GameUiTest, TheMenuStepsWithTheUiBlockAndKeepsTheMouse)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::UI::EscapeMenuDialog;
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, TheLastMenuButtonGoesBackToTheGame)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::EscapeMenuDialog;
    runtime.movement.mouseUpdateTime = 3;
    tested.Click();
    ASSERT_TRUE(tested.Dialogs(0x110, 185).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
}

TEST(GameUiTest, TheOtherMenuButtonsAreOfMilestoneSix)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::UI::EscapeMenuDialog;
    tested.Click();
    const auto stepped = tested.Dialogs(0x110, 65);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x62402f"), std::string::npos) << stepped.Error();
}

TEST(GameUiTest, TheLetterIOpensTheInventory)
{
    SteppingUi tested;
    Press(tested.input, Key::I);
    // Open(0x4000000) saca 4; con el inventario arriba de 0x10000, IsOpen(0) saca los 2 antes de encontrarlo.
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 4);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 1);
    // Con el inventario abierto, 0x5ee140 corre sus hijas, que son del hito 4.
    const auto again = tested.Step();
    ASSERT_FALSE(again.HasValue());
    EXPECT_NE(again.Error().find("0x5ee1ac"), std::string::npos) << again.Error();
}

TEST(GameUiTest, TheInventoryButtonOfTheHudTakesTheClick)
{
    SteppingUi tested;
    tested.Click();
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 4);
    ASSERT_TRUE(tested.Step(0x1a0, 0x1d0).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 1);
    EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
}

TEST(GameUiTest, RWithNothingOpenOpensTheOwnRecordAndAsksThreeTimes)
{
    SteppingUi tested;
    Press(tested.input, Key::R);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + SkillBarRandoms + 3);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().repairMode, 0);
}

TEST(GameUiTest, BWithNothingOpenAsksTheNpcShop)
{
    SteppingUi tested;
    Press(tested.input, Key::B);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 1);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x5a35ca: sin el ítem de la ranura en el inventario, 0x558110 da −1 y la tecla no hace nada más.
TEST(GameUiTest, TheQuickSlotKeysWithoutTheirItemDoNothing)
{
    for (const Key key : {Key::Q, Key::W, Key::E})
    {
        SteppingUi tested;
        Press(tested.input, key);
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
        const auto stepped = tested.Step();
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// 0x5a35e0: con el ítem, 0x5af520 y el C1 26, de la CL9.3. La poción chica de vida, la de maná y el antídoto.
TEST(GameUiTest, TheQuickSlotKeysWithTheirItemAreACut)
{
    constexpr std::array<std::pair<Key, std::uint16_t>, 3> Slots{
        {{Key::Q, std::uint16_t{0x1c00}}, {Key::W, std::uint16_t{0x1c04}}, {Key::E, std::uint16_t{0x1c08}}}};
    for (const auto& [key, type] : Slots)
    {
        SteppingUi tested;
        tested.world.Inventory().cells[9].id = type;
        tested.world.Inventory().cells[9].occupied = 1;
        Press(tested.input, key);
        const auto stepped = tested.Step();
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x5a35e0"), std::string::npos) << stepped.Error();
    }
}

// 0x5a2b93: el clic derecho sobre una ranura se gasta aunque esté vacía.
TEST(GameUiTest, TheRightClickOnAnEmptyQuickSlotIsSpent)
{
    SteppingUi tested;
    tested.Ui().rightClick = true;
    const auto stepped = tested.Step(0xd0 + 0x1f, 0x1c4);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(tested.Ui().rightClick);
}

TEST(GameUiTest, TheRightClickOnAQuickSlotWithItsItemIsACut)
{
    SteppingUi tested;
    tested.world.Inventory().cells[0].id = 0x1c05;
    tested.world.Inventory().cells[0].occupied = 1;
    tested.Ui().rightClick = true;
    const auto stepped = tested.Step(0xd0 + 0x1f, 0x1c4);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5a2bc8"), std::string::npos) << stepped.Error();
    EXPECT_FALSE(tested.Ui().rightClick);
}

TEST(GameUiTest, ADialogClosesTheSlotGateAfterItsTwoQueries)
{
    SteppingUi tested;
    tested.world.Runtime().dialogs.current = Client::UI::EscapeMenuDialog;
    Press(tested.input, Key::Q);
    Press(tested.input, Key::I);
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 0);
}

TEST(GameUiTest, TheSkillBarButtonTogglesTheListAndItsListIsACut)
{
    SteppingUi tested;
    tested.world.OwnCharacter().byte6a = 1;
    tested.Click();
    const auto stepped = tested.Step(0x140, 0x1c0);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x5a070e"), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.Ui().skillListOpen, 1);
    EXPECT_EQ(tested.Ui().skillTooltipX, 0x140);
    EXPECT_EQ(tested.Ui().skillTooltipY, 0x1bc);
    EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
}

TEST(GameUiTest, TheSiegePanelTogglesWithTabAndTakesTheMouse)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.siegePanelKind = std::uint8_t{0};
    ASSERT_TRUE(tested.Step(0x1ea, 0x104).HasValue());
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
    Press(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step(0x1ea, 0x104).HasValue());
    EXPECT_EQ(runtime.siegePanel.visible, 0);
    EXPECT_EQ(tested.Ui().mouseOnUi, 0);
    // Con Tab abajo, el latch no la vuelve a leer.
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(runtime.siegePanel.visible, 0);
    Release(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step().HasValue());
    Press(tested.input, Key::Tab);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(runtime.siegePanel.visible, 1);
}

TEST(GameUiTest, TheSiegePanelPagesAndFades)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    runtime.siegePanelKind = std::uint8_t{0};
    const auto& panel = runtime.siegePanel;
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 130, panel.y + 20).HasValue());
    EXPECT_EQ(panel.page, 2);
    EXPECT_FALSE(runtime.movement.pendingClick);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 130, panel.y + 20).HasValue());
    EXPECT_EQ(panel.page, 1);
    // El alfa: 1 + 0,1f queda debajo de 1,1f en la x87 (1,1000000015 contra 1,1000000238) y se guarda como 1,1f; la
    // suma que sigue pasa y vuelve a 0,5.
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 1.1f);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 0.5f);
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 10, panel.y + 30).HasValue());
    EXPECT_EQ(panel.alpha, 0.6f);
    // Afuera de los dos, el clic sigue.
    tested.Click();
    ASSERT_TRUE(tested.Step(panel.x + 60, panel.y + 100).HasValue());
    EXPECT_TRUE(runtime.movement.pendingClick);
}

TEST(GameUiTest, TheClickLatchKeepsTheEdgeUntilSomeoneUsesIt)
{
    Client::Scenes::GameMovementState state;
    Client::Scenes::LatchTheLeftButton(state, true, true);
    EXPECT_TRUE(state.pendingClick);
    EXPECT_TRUE(state.leftHeld);
    state.pendingClick = false;
    state.leftHeld = false;
    // El mismo botón abajo, sin otro WM_LBUTTONDOWN: nada vuelve.
    Client::Scenes::LatchTheLeftButton(state, false, true);
    EXPECT_FALSE(state.pendingClick);
    EXPECT_FALSE(state.leftHeld);
    Client::Scenes::LatchTheLeftButton(state, false, false);
    EXPECT_FALSE(state.leftHeld);
}

class PickingFrame
{
public:
    Client::Core::Expected<void, std::string> Pick(bool alt = false)
    {
        return Client::Scenes::PickInTheFrame(FramePickingContext{.world = world,
                                                                  .random = random,
                                                                  .ownRecordDepth = world.Runtime().ownRecordDepth,
                                                                  .targets = world.Runtime().selectedTargets,
                                                                  .altDown = alt});
    }
    WorldState world;
    CrtRandom random;
};

TEST(GameUiTest, ThePickingOfTheWizardAndTheElfReadsTheOwnRecord)
{
    for (const std::uint8_t heroClass : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{1}, std::uint8_t{3}})
    {
        PickingFrame tested;
        tested.world.Actors()[tested.world.Runtime().hero].characterClass = heroClass;
        const int draws = heroClass == 0 || heroClass == 2 ? SkillBarRandoms : 0;
        const CrtRandom expected = Advanced(tested.random, draws);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << int{heroClass};
        EXPECT_EQ(tested.world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    }
}

TEST(GameUiTest, ThePickingSkipsTheOwnRecordWithTheUiOrAlt)
{
    PickingFrame tested;
    tested.world.Runtime().ui.mouseOnUi = 1;
    auto before = tested.random.State();
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(tested.random.State(), before);
    tested.world.Runtime().ui.mouseOnUi = 0;
    before = tested.random.State();
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(tested.random.State(), before);
}

TEST(GameUiTest, ThePickingAsksTheWorldWidthWithThePartyClosed)
{
    PickingFrame tested;
    tested.world.Actors()[tested.world.Runtime().hero].characterClass = 1;
    tested.world.Runtime().panels.inventory = 1;
    const CrtRandom expected = Advanced(tested.random, 2);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

TEST(GameUiTest, FollowingThePreviousTargetIsACut)
{
    PickingFrame tested;
    tested.world.Runtime().selectedTargets[2] = 5;
    const auto picked = tested.Pick();
    ASSERT_FALSE(picked.HasValue());
    EXPECT_NE(picked.Error().find("0x59ee0b"), std::string::npos) << picked.Error();
    // En el mapa 6 no lo sigue.
    tested.world.Runtime().map = 6;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(tested.world.Runtime().selectedTargets[2], -1);
}
} // namespace
