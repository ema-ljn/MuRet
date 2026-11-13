#include "Core/Expected.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/FriendWindows.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"

#include "support/ChatTexts.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

TEST(GameUiTest, TheLetterIOpensTheInventory)
{
    SteppingUi tested;
    Press(tested.input, Key::I);
    // Open(0x4000000) saca 4; con el inventario arriba de 0x10000, IsOpen(0) saca los 2 antes de encontrarlo.
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + 4);
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Panels().inventory, 1);
    // Con el inventario abierto, 0x5ee140 corre su paso (0x5e4ce0) y sus seis hermanas (test_game_inventory_step).
    const auto again = tested.Step(320, 240);
    ASSERT_TRUE(again.HasValue()) << again.Error();
    EXPECT_EQ(tested.Panels().inventory, 1);
}

// El registro del chat, fuera de su ventana en (−10, 0x65) con 0x10c × 0xfa desde su borde de abajo, queda bajo el
// mouse arriba a la izquierda (0x45aa80): 0x5ee140 no corre los paneles (0x5ee174).
TEST(GameUiTest, TheChatLogUnderTheMouseKeepsThePanelsStill)
{
    SteppingUi tested;
    Press(tested.input, Key::I);
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    ASSERT_EQ(tested.Panels().inventory, 1);
    ASSERT_TRUE(tested.Step(0, 0).HasValue());
    EXPECT_EQ(tested.Ui().hovered, Client::UI::ChatLogId);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, -10);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x65);
}

// En su ventana (0x6c7a88 en 1), la esquina va a (0xba, 0x1a4), y a x = 0 con el inventario y el personaje abiertos
// (0x636f29).
TEST(GameUiTest, InItsWindowTheChatLogMovesLeftOfTheInventoryAndTheCharacter)
{
    SteppingUi tested;
    tested.Ui().chatModes.placement = 1;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, 0xba);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x1a4);
}

// El clic sin dueño con el chat cerrado devuelve la lista de las flechas a 0 (0x636ead).
TEST(GameUiTest, TheUnownedClickTakesTheArrowsFromTheChatLog)
{
    SteppingUi tested;
    tested.Ui().focusedList = Client::UI::ChatLogId;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    EXPECT_EQ(tested.Ui().focusedList, 0);
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

// 0x7d36849 (P-C182): lo prende el WM_LBUTTONDBLCLK que fue el último mensaje, lo apaga cualquiera que siga y, sin
// mensajes, queda.
TEST(GameUiTest, TheDoubleClickLastsUntilTheNextMessage)
{
    Client::UI::GameUiState ui;
    Client::Scenes::LatchTheDoubleClick(ui, true, true);
    EXPECT_TRUE(ui.doubleClick);
    Client::Scenes::LatchTheDoubleClick(ui, false, false);
    EXPECT_TRUE(ui.doubleClick);
    Client::Scenes::LatchTheDoubleClick(ui, true, false);
    EXPECT_FALSE(ui.doubleClick);
}

// WM_LBUTTONUP apaga el flanco (0x4b3ce3): sin usar, se pierde al soltar, y apretado y soltado en el mismo paso no
// queda.
TEST(GameUiTest, TheReleasedButtonDropsTheEdge)
{
    Client::Scenes::GameMovementState state;
    Client::Scenes::LatchTheLeftButton(state, true, true);
    Client::Scenes::LatchTheLeftButton(state, false, false);
    EXPECT_FALSE(state.pendingClick);
    Client::Scenes::LatchTheLeftButton(state, true, false);
    EXPECT_FALSE(state.pendingClick);
    EXPECT_FALSE(state.leftHeld);
}

// 0x7d366ec: WM_LBUTTONUP lo prende con el mouse de ese momento y dura mientras el mouse no se mueva; WM_LBUTTONDOWN
// lo apaga (0x4b3cea, 0x4b344d y 0x4b3947).
TEST(GameUiTest, TheReleaseLatchLastsWhileTheMouseStaysStill)
{
    Client::Scenes::GameMovementState state;
    // Apretado y soltado en el mismo paso: el último mensaje es el soltado.
    Client::Scenes::LatchTheLeftRelease(state, true, true, false, 10, 20);
    EXPECT_TRUE(state.releasedClick);
    EXPECT_EQ(state.releaseX, 10);
    EXPECT_EQ(state.releaseY, 20);
    Client::Scenes::LatchTheLeftRelease(state, false, false, false, 10, 20);
    EXPECT_TRUE(state.releasedClick);
    Client::Scenes::LatchTheLeftRelease(state, false, false, false, 11, 20);
    EXPECT_FALSE(state.releasedClick);
    Client::Scenes::LatchTheLeftRelease(state, false, true, false, 5, 5);
    EXPECT_TRUE(state.releasedClick);
    Client::Scenes::LatchTheLeftRelease(state, true, false, true, 5, 5);
    EXPECT_FALSE(state.releasedClick);
    // Soltado y vuelto a apretar: el último es el apretón.
    Client::Scenes::LatchTheLeftRelease(state, true, true, true, 5, 5);
    EXPECT_FALSE(state.releasedClick);
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

// 0x40e6c0: el mouse sobre el cartel del set del inventario prende la ventana del set (+0x9304) y gasta el clic, sin
// mirarlo (Q-C472).
TEST(GameUiTest, TheMouseOverTheSetLabelOpensTheSetWindow)
{
    SteppingUi tested;
    tested.Panels().inventory = 1;
    tested.world.Runtime().hud.inventoryX = 0x1c2;
    tested.Click();
    const auto stepped = tested.Step(0x1c2 + 0x20, 0x20);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.world.ItemSets().byte9304, 1);
    EXPECT_FALSE(tested.world.Runtime().movement.pendingClick);
    EXPECT_EQ(tested.world.Runtime().movement.mouseUpdateTimeMax, 6);

    tested.world.ItemSets().byte9304 = 0;
    ASSERT_TRUE(tested.Step(0x1c2 + 0xb0, 0x20).HasValue());
    EXPECT_EQ(tested.world.ItemSets().byte9304, 0);
}

// 0x5a00a1..0x5a0124: PgUp y PgDn corren el registro tantos renglones como muestra.
TEST(GameUiTest, PageUpAndPageDownScrollTheChatLog)
{
    SteppingUi tested;
    auto& log = tested.world.Runtime().chat.log;
    tested.Ui().chatModes.placement = 1;
    for (int i = 0; i < 20; ++i)
        log.entries.push_back(Client::UI::ChatEntry{.name = "", .text = "x", .type = 0});
    Press(tested.input, Key::PageUp);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(log.scroll, log.rows);
    Release(tested.input, Key::PageUp);
    Press(tested.input, Key::PageDown);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(log.scroll, 0);
}

// 0x5a0124..0x5a03c3: con el foco en el EDIT del mensaje, arriba trae lo último dicho; con el foco en el juego, corre
// el nombre del susurro.
TEST(GameUiTest, TheArrowsWalkTheHistoryOrTheWhisperNames)
{
    SteppingUi tested;
    auto& input = tested.world.Runtime().chat.input;
    Client::UI::ShowTheChatFields(input, 0);
    Client::UI::RememberTheMessage(input, "dicho");
    Press(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(input.message.Value(), "dicho");
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);

    Release(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    Client::UI::ShowTheChatFields(input, 3);
    Press(tested.input, Key::Up);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(input.whisperNameAt, Client::UI::FirstWhisperNameSlot - 1);
}

// Con el chat abierto, los números son del EDIT y no eligen la skill.
TEST(GameUiTest, TheDigitsWithTheChatOpenDoNotPickASkill)
{
    SteppingUi tested;
    tested.Ui().chatOpen = true;
    Client::UI::ShowTheChatFields(tested.world.Runtime().chat.input, 0);
    Press(tested.input, Key::Digit1);
    const auto stepped = tested.Step(320, 100);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
}

// 0x5ac8d0: F2 cambia el chat normal del registro, F3 el modo del susurro y F4 el tamaño del registro y su esquina,
// cada una con el 0x19.
TEST(GameUiTest, TheFunctionKeysChangeTheChatModes)
{
    SteppingUi tested;
    const auto keys = [&tested]
    {
        return Client::Scenes::StepTheFunctionKeys(GameUiContext{
            .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    };
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());

    Press(tested.input, Key::F2);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.Ui().chatModes.showChat);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);

    Press(tested.input, Key::F3);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_FALSE(tested.world.Runtime().chat.input.whisperMode);
    // El latch: la F2 que sigue abajo no vuelve a cambiar.
    EXPECT_FALSE(tested.Ui().chatModes.showChat);

    Press(tested.input, Key::F4);
    ASSERT_TRUE(keys().HasValue());
    EXPECT_EQ(tested.Ui().chatModes.placement, 1);
    EXPECT_EQ(tested.world.Runtime().chat.log.rows, 3);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.x, 0xba);
    EXPECT_EQ(tested.world.Runtime().hud.chatFrame.y, 0x1a4);
}

// 0x5ac9ce..0x5aca42: F1 pasa de 0 a 1, a 2 y otra vez a 0, y de 3 (la ayuda del ítem) o 4 (la ventana /move) a 0;
// apaga el cartel del conjunto (+0x9305) y pide el 0x19. Abajo no vuelve a pasar.
TEST(GameUiTest, TheF1KeyWalksTheHelps)
{
    SteppingUi tested;
    std::int32_t& mode = tested.world.Runtime().panels.overlayMode;
    const auto keys = [&tested]
    {
        return Client::Scenes::StepTheFunctionKeys(GameUiContext{
            .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    };
    const auto f1 = [&]
    {
        Press(tested.input, Key::F1);
        const bool pressed = keys().HasValue();
        Release(tested.input, Key::F1);
        return pressed && keys().HasValue();
    };
    tested.world.ItemSets().byte9305 = 5;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 1);
    EXPECT_EQ(tested.world.ItemSets().byte9305, 0);
    EXPECT_EQ(tested.world.Runtime().lastSoundId, 0x19);
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 2);
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);
    mode = 3;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);
    mode = 4;
    ASSERT_TRUE(f1());
    EXPECT_EQ(mode, 0);

    Press(tested.input, Key::F1);
    ASSERT_TRUE(keys().HasValue());
    ASSERT_TRUE(keys().HasValue());
    EXPECT_EQ(mode, 1);
}

// 0x637cff: Impr Pant no es de las teclas del juego, sino de lo que corre detrás del paso de cualquier escena
// (Scenes::Screenshots): las teclas de función no tocan su latch.
TEST(GameUiTest, TheFunctionKeysLeaveThePrintScreenKey)
{
    SteppingUi tested;
    Press(tested.input, Key::PrintScreen);
    const auto keys = Client::Scenes::StepTheFunctionKeys(GameUiContext{
        .world = tested.world, .random = tested.random, .input = tested.input, .mouseX = 320, .mouseY = 100});
    ASSERT_TRUE(keys.HasValue()) << keys.Error();
    EXPECT_EQ(tested.Ui().keyLatches[0x2c], 0);
}

class TypedLetters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// 0x5a116f..0x5a2b47: Alt y un número dicen la macro y frenan las demás hasta que 0x77f3b74 vuelva a 0. Sin la
// entrada del chat, el error.
TEST(GameUiTest, AltAndADigitSayTheMacro)
{
    SteppingUi tested;
    tested.world.Runtime().chat.input.macros[0] = "uno";
    Press(tested.input, Key::AltLeft);
    Press(tested.input, Key::Digit1);
    const auto alone = tested.Step(320, 100);
    ASSERT_FALSE(alone.HasValue());
    EXPECT_NE(alone.Error().find("0x5a11cb"), std::string::npos);

    const auto texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    const TypedLetters letters;
    std::vector<Client::Protocol::Outgoing> sent;
    const Client::Scenes::GameChatContext chat{.world = tested.world,
                                               .random = tested.random,
                                               .texts = &texts,
                                               .measure = &letters,
                                               .send = [&sent](Client::Protocol::Outgoing packet)
                                               { sent.push_back(std::move(packet)); }};
    const auto step = [&]
    {
        return Client::Scenes::StepTheGameUi(GameUiContext{.world = tested.world,
                                                           .random = tested.random,
                                                           .input = tested.input,
                                                           .mouseX = 320,
                                                           .mouseY = 100,
                                                           .chat = &chat});
    };
    const auto said = step();
    ASSERT_TRUE(said.HasValue()) << said.Error();
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(tested.world.Runtime().hud.macroTime, Client::UI::MacroLockSteps);
    ASSERT_TRUE(step().HasValue());
    EXPECT_EQ(sent.size(), 1u);
}

// Una caja de mensaje (0x48ee60 caso 9) en el administrador del juego, abierta a mano: en la CL8 no la abre nadie.
std::int32_t OpenABox(WorldState& world, std::int32_t x, std::int32_t y)
{
    const TypedLetters letters;
    const auto opened = Client::UI::CreateTheOldWindow(
        world.Runtime().oldWindows,
        Client::UI::OldWindowRequest{.kind = Client::UI::QuestionCase,
                                     .x = x,
                                     .y = y,
                                     .data = "Seguro?",
                                     .aux = 0,
                                     .flags = Client::UI::KeepPlaceFlag},
        Client::UI::OldWindowTexts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"},
        Client::UI::OldWindowMeasure{
            .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}});
    EXPECT_TRUE(opened.HasValue()) << opened.Error();
    return opened.HasValue() ? opened.Value() : 0;
}

// La ventana de amigos de verdad (0x491080 desde el nivel, CL9 hito 9): el panel 1 abierto es que esté.
void OpenTheFriendsWindowIn(WorldState& world)
{
    const TypedLetters letters;
    auto& runtime = world.Runtime();
    const Client::UI::OldWindowTexts texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"};
    const Client::UI::OldWindowMeasure measure{
        .measure = letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}};
    const Client::UI::OldWindowStepView view{.texts = &texts, .measure = &measure};
    const auto opened =
        Client::UI::OpenTheFriendsWindow(runtime.oldWindows, Client::UI::FriendsWindowX, Client::UI::FriendsWindowY,
                                         Client::Scenes::OldWindowGlobalsOf(runtime), view);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    EXPECT_TRUE(opened.Value());
}

// 0x636e3c..0x636e5a: la lista de ventanas y las ventanas viejas toman el mouse antes que el registro del chat; sobre
// una caja, el clic no es del juego (0x636e5d) y el foco del administrador queda.
TEST(GameUiTest, TheOldWindowsTakeTheMouseBeforeTheChatLog)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    // El primer paso deja el registro en su esquina (0x45a9d0(−10, 0x65)).
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    ASSERT_TRUE(tested.Step(2, 30).HasValue());
    ASSERT_EQ(tested.Ui().hovered, Client::UI::ChatLogId);

    const std::int32_t box = OpenABox(tested.world, 0, 0);
    tested.Click();
    ASSERT_TRUE(tested.Step(2, 30).HasValue());
    EXPECT_EQ(tested.Ui().hovered, box);
    EXPECT_EQ(tested.Ui().mouseOnUi, 1);
    EXPECT_EQ(runtime.oldWindows.focus, box);
}

// Fuera de las ventanas, el clic les saca el foco y, con el chat cerrado, le da el teclado al juego
// (0x636e87..0x636ed1).
TEST(GameUiTest, TheUnownedClickTakesTheFocusFromTheOldWindows)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    const std::int32_t box = OpenABox(tested.world, 0, 0);
    runtime.oldWindows.focus = box;
    runtime.chat.input.focus = Client::UI::ChatFocus::Message;
    tested.Click();
    ASSERT_TRUE(tested.Step(320, 240).HasValue());
    EXPECT_EQ(runtime.oldWindows.focus, 0);
    EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Game);
}

// Q-C446: Esc lo lee 0x623e70 con la ventana de amigos (el panel 1) cerrada, y abre el menú; con ella abierta, llega a
// 0x490490 y cierra la caja de arriba.
TEST(GameUiTest, EscapeReachesTheOldWindowsOnlyWithTheFriendsWindowOpen)
{
    for (const bool friendsOpen : {false, true})
    {
        SCOPED_TRACE(friendsOpen);
        SteppingUi tested;
        auto& runtime = tested.world.Runtime();
        if (friendsOpen)
            OpenTheFriendsWindowIn(tested.world);
        const std::int32_t box = OpenABox(tested.world, 300, 0);
        Press(tested.input, Key::Escape);
        const auto dialogs = tested.Dialogs(639, 300);
        ASSERT_TRUE(dialogs.HasValue()) << dialogs.Error();
        // La de amigos mide el texto de su botón del estado en cada paso (0x4a3030).
        const TypedLetters letters;
        const auto stepped = Client::Scenes::StepTheGameUi(GameUiContext{.world = tested.world,
                                                                         .random = tested.random,
                                                                         .input = tested.input,
                                                                         .mouseX = 639,
                                                                         .mouseY = 300,
                                                                         .measure = &letters});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(runtime.oldWindows.windows.contains(box), !friendsOpen);
        EXPECT_EQ(runtime.dialogs.current == Client::UI::EscapeMenuDialog, !friendsOpen);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
    }
}

// 0x490538..0x490548: F5 solo se lee con el teclado en la ventana del juego.
TEST(GameUiTest, F5IsReadOnlyWithTheKeyboardInTheGame)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    Press(tested.input, Key::F5);
    runtime.chat.input.focus = Client::UI::ChatFocus::Message;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.Ui().keyLatches[0x74], 0);
    runtime.chat.input.focus = Client::UI::ChatFocus::Game;
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.Ui().keyLatches[0x74], 1);
}

// Q-C573: en el juego de la CL8 no hay ventanas viejas, y el primer F6 deja 0x490490 sin F6 para siempre; tampoco
// esconde una caja que se abre después.
TEST(GameUiTest, TheFirstF6LeavesF6Stuck)
{
    SteppingUi tested;
    auto& runtime = tested.world.Runtime();
    const CrtRandom expected = Advanced(tested.random, BaseStepRandoms);
    Press(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(runtime.oldWindows.hiddenByF6, 1);
    Release(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());

    const std::int32_t box = OpenABox(tested.world, 300, 0);
    Press(tested.input, Key::F6);
    ASSERT_TRUE(tested.Step(320, 100).HasValue());
    EXPECT_EQ(runtime.oldWindows.windows.at(box).base.state, Client::UI::ShownWindow);
    EXPECT_EQ(tested.Ui().keyLatches[0x75], 1);
}

// 0x59ee4b..0x59ef10: el monstruo elegido (+0x88 en 2) sigue con el ataque automático; en el mapa 6, no, y suelta
// también 0x6c8e38. Los otros casos, CharacterPickingTest.
TEST(GameUiTest, ThePreviousMonsterStaysOutsideTheMapSix)
{
    PickingFrame tested;
    auto& runtime = tested.world.Runtime();
    tested.world.Actors()[5].byte88 = 2;
    runtime.selectedTargets[2] = 5;
    runtime.selectedTargets[4] = 5;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(runtime.selectedTargets[2], 5);
    EXPECT_EQ(runtime.selectedTargets[4], 5);
    runtime.map = 6;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(runtime.selectedTargets[2], -1);
    EXPECT_EQ(runtime.selectedTargets[4], -1);
}
} // namespace
