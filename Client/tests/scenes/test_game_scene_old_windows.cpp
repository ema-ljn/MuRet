#include "Scenes/GameScene.h"

#include "Session/ServerSession.h"
#include "Core/Expected.h"
#include "Tables/SkillTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Assets/GameModels.h"
#include "Scenes/GameUi.h"
#include "Session/MapLoad.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/FriendModel.h"
#include "UI/FriendWindows.h"
#include "UI/GameUiState.h"
#include "Platform/IClipboard.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "Session/ModuleStates.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/SameBytes.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Cadena de H9: eventos físicos -> GameScene, y mensajes durante Draw -> ServerSession -> Connection -> FakeSocket.
// Las pasadas intermedias usan los assets locales y la implementación real; los paquetes y nombres son sintéticos.
namespace
{
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class SceneClipboard final : public Client::Platform::IClipboard
{
public:
    [[nodiscard]] std::optional<std::string> ReadText() override
    {
        ++reads;
        return text;
    }

    [[nodiscard]] bool WriteText(std::string_view value) override
    {
        text = value;
        return true;
    }

    std::string text = "€";
    int reads = 0;
};

class GameSceneOldWindowsTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_NO_FATAL_FAILURE(m_online.LogIn());
        m_world = std::make_unique<Client::Kernel::WorldState>();
        m_states = std::make_unique<Client::Session::ModuleStates>();
        m_models = std::make_unique<Client::Assets::GameModels>();
        const auto models = m_models->Load(m_dataRoot, m_textures, m_online.log);
        ASSERT_TRUE(models.HasValue()) << models.Error();
        m_world->Runtime().map = 0;
        const auto map =
            Client::Scenes::LoadMap(0, {m_dataRoot, *m_world, Client::Scenes::SceneId::Game, 0, m_cloth.random});
        ASSERT_TRUE(map.HasValue()) << map.Error();
        m_world->Actors()[0].position = {13550.0f, 12550.0f, 0.0f};
        m_world->OwnCharacter().ClearEquipmentForEntry();
        m_scene = std::make_unique<Client::Scenes::GameScene>(Client::Scenes::SceneContext{.dataRoot = m_dataRoot,
                                                                                           .textures = m_textures,
                                                                                           .log = m_online.log,
                                                                                           .clock = &m_clock,
                                                                                           .cloth = &m_cloth,
                                                                                           .input = &m_input,
                                                                                           .world = m_world.get(),
                                                                                           .textMeasure = &m_letters,
                                                                                           .clipboard = &m_clipboard,
                                                                                           .states = m_states.get()},
                                                              &m_online.session, m_models.get(), nullptr,
                                                              &m_models->TextureSlots(), nullptr, &m_skills);
        ASSERT_TRUE(m_scene->Enter().HasValue());
        m_world->Runtime().drawingEnabled = false;
        m_scene->Step(m_input);
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        m_world->Runtime().drawingEnabled = true;
        m_world->Runtime().warpCounter = 0;
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        m_online.socket.sent.clear();
    }

    void Render()
    {
        m_scene->PrepareFrame();
        m_scene->Render(m_target.BeginFrame(m_clock.Now()), 0.0f);
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        m_target.Present();
    }

    void OpenFriends()
    {
        const Client::UI::OldWindowStepView view{
            .mouseX = 639, .mouseY = 479, .texts = &m_texts, .measure = &m_measure};
        const auto opened = Client::UI::OpenTheFriendsWindow(
            m_world->Runtime().oldWindows, Client::UI::FriendsWindowX, Client::UI::FriendsWindowY,
            Client::Scenes::OldWindowGlobalsOf(m_world->Runtime()), view);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_TRUE(opened.Value());
        Friends().base.state = Client::UI::DraggedWindow;
    }

    Client::UI::OldWindow& Friends()
    {
        auto& manager = m_world->Runtime().oldWindows;
        return manager.windows.at(manager.friends);
    }

    void QueueName()
    {
        Friends().tabs[0].base.queue.push_back(
            Client::UI::OldMessage{.type = Client::UI::NameMessage, .a = 0, .b = 1, .text = "Tordo"});
    }

    void DoubleClickTheFriendRow()
    {
        Client::UI::AddAFriend(m_world->Runtime().oldWindows.friendModel, "Tero", 0, 0xff);
        Client::UI::RefreshTheFriendTab(m_world->Runtime().oldWindows);
        Client::UI::OldList& list = Friends().tabs[0].tab.list;
        Client::UI::SelectInTheList(list, 1, 1);
        m_input.Apply({.kind = Client::Input::InputEvent::Kind::Motion,
                       .x = static_cast<float>(list.base.x + 1),
                       .y = static_cast<float>(Client::UI::ListRowY(list, 1, 0) - 2)});
        m_input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonUp, .button = Client::Input::MouseButton::Left});
        m_input.Apply(
            {.kind = Client::Input::InputEvent::Kind::ButtonDoubleClick, .button = Client::Input::MouseButton::Left});
    }

    Client::Tests::LoggedInSession m_online;
    Client::Tests::FakeTextureStore m_textures;
    Client::Tests::FakeClock m_clock;
    Client::Models::ClothWorld m_cloth;
    Client::Input::InputState m_input;
    SceneClipboard m_clipboard;
    LetterMeasure m_letters;
    const Client::UI::OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"};
    const Client::UI::OldWindowMeasure m_measure{
        .measure = m_letters, .dcFont = Client::Render::Text::FontKind::Normal, .screen = {640, 480}};
    Client::Tables::SkillTable m_skills{std::vector<std::byte>(Client::Tables::SkillTableSize)};
    std::unique_ptr<Client::Kernel::WorldState> m_world;
    std::unique_ptr<Client::Session::ModuleStates> m_states;
    std::unique_ptr<Client::Assets::GameModels> m_models;
    Client::Tests::RecordingFrameTarget m_target;
    std::unique_ptr<Client::Scenes::GameScene> m_scene;
};

TEST_F(GameSceneOldWindowsTest, OriginalDrawingSendsAPendingNameThroughTheSessionOnce)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    QueueName();

    ASSERT_NO_FATAL_FAILURE(Render());
    ASSERT_TRUE(m_online.session.Poll().HasValue());

    const auto expected =
        Client::Tests::Bytes({0xc1, 0x0d, 0xc1, 0x1c, 0xcf, 0x0f, 0xf4, 0xe8, 0xcb, 0x63, 0x9d, 0x2b, 0x62});
    EXPECT_TRUE(Client::Tests::SameBytes(m_online.socket.sent, expected));
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    ASSERT_NO_FATAL_FAILURE(Render());
    ASSERT_TRUE(m_online.session.Poll().HasValue());
    EXPECT_TRUE(Client::Tests::SameBytes(m_online.socket.sent, expected));
}

TEST_F(GameSceneOldWindowsTest, TwoIntermediateDrawingsKeepThePendingPacketUntilTheTabSteps)
{
    ASSERT_NO_FATAL_FAILURE(Render());
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    QueueName();
    const std::int32_t lastId = m_world->Runtime().oldWindows.lastId;

    for (int draw = 0; draw < 2; ++draw)
    {
        ASSERT_NO_FATAL_FAILURE(Render());
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        EXPECT_TRUE(m_online.socket.sent.empty());
        ASSERT_EQ(Friends().tabs[0].base.queue.size(), 1u);
        EXPECT_EQ(Friends().tabs[0].base.queue.front().type, Client::UI::NameMessage);
        EXPECT_EQ(m_world->Runtime().oldWindows.lastId, lastId);
    }

    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_TRUE(m_online.session.Poll().HasValue());
    // 0x4a2f70 no pasa a la pestaña mientras el marco está arrastrado; este primer paso termina el arrastre.
    EXPECT_TRUE(m_online.socket.sent.empty());
    EXPECT_EQ(Friends().base.state, Client::UI::ShownWindow);
    ASSERT_EQ(Friends().tabs[0].base.queue.size(), 1u);
    const auto expected =
        Client::Tests::Bytes({0xc1, 0x0d, 0xc1, 0x1c, 0xcf, 0x0f, 0xf4, 0xe8, 0xcb, 0x63, 0x9d, 0x2b, 0x62});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_TRUE(m_online.session.Poll().HasValue());
    EXPECT_TRUE(Client::Tests::SameBytes(m_online.socket.sent, expected));
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    ASSERT_NO_FATAL_FAILURE(Render());
    ASSERT_TRUE(m_online.session.Poll().HasValue());
    EXPECT_TRUE(Client::Tests::SameBytes(m_online.socket.sent, expected));
}

TEST_F(GameSceneOldWindowsTest, IntermediateDeleteDrawingsKeepTheRuntimeFocusIdsAndEnterLatch)
{
    ASSERT_NO_FATAL_FAILURE(Render());
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    auto& runtime = m_world->Runtime();
    Client::UI::AddAFriend(runtime.oldWindows.friendModel, "Tero", 0, 1);
    Client::UI::RefreshTheFriendTab(runtime.oldWindows);
    Client::UI::SelectInTheList(Friends().tabs[0].tab.list, 1, 1);
    Client::UI::PostToTheWindow(runtime.oldWindows, Friends().tabs[0].base.id, Client::UI::CommandMessage, 2, 0);
    const std::int32_t lastId = runtime.oldWindows.lastId;
    const std::int32_t focusedList = runtime.ui.focusedList;
    constexpr std::uint8_t EnterKey = 0x0d;
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Enter});
    m_input.ConsumeEdges();

    for (int draw = 0; draw < 2; ++draw)
    {
        ASSERT_NO_FATAL_FAILURE(Render());
        ASSERT_TRUE(m_online.session.Poll().HasValue());
        EXPECT_TRUE(m_online.socket.sent.empty());
        EXPECT_EQ(runtime.oldWindows.lastId, lastId);
        EXPECT_EQ(runtime.oldWindows.modal, 0);
        EXPECT_EQ(runtime.oldWindows.focusedEdit, 0);
        EXPECT_EQ(runtime.ui.focusedList, focusedList);
        EXPECT_EQ(runtime.chat.input.focus, Client::UI::ChatFocus::Game);
        EXPECT_EQ(runtime.ui.keyLatches[EnterKey], 0);
        ASSERT_EQ(Friends().tabs[0].base.queue.size(), 1u);
        EXPECT_EQ(Friends().tabs[0].base.queue.front().type, Client::UI::CommandMessage);
    }
}

TEST_F(GameSceneOldWindowsTest, OrderedInputEventsEditTheFocusedOldWindowThroughTheScene)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    auto& manager = m_world->Runtime().oldWindows;
    Client::UI::PostToTheWindow(manager, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1, 0);
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_NE(manager.letter, 0);
    Client::UI::OldEdit& edit = manager.windows.at(manager.letter).add.edit;
    ASSERT_EQ(m_world->Runtime().chat.input.focus, Client::UI::ChatFocus::OldWindow);

    m_input.Apply({.kind = Client::Input::InputEvent::Kind::Text, .text = "ac"});
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Left});
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::Text, .text = "b"});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();

    EXPECT_EQ(edit.text.Value(), "abc");
    EXPECT_EQ(edit.text.Cursor(), 2u);
    EXPECT_TRUE(m_online.socket.sent.empty());
    m_input.ConsumeEdges();

    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::CtrlLeft});
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Backspace});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_EQ(edit.text.Size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(edit.text.Value()[2]), 0x7fU);
    EXPECT_TRUE(m_online.socket.sent.empty());
}

TEST_F(GameSceneOldWindowsTest, ClipboardInputUsesTheSceneContextForTheFocusedOldEdit)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    m_scene->Step(m_input);
    Client::UI::PostToTheWindow(m_world->Runtime().oldWindows, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1,
                                0);
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_NE(m_world->Runtime().oldWindows.letter, 0);
    auto& edit = m_world->Runtime().oldWindows.windows.at(m_world->Runtime().oldWindows.letter).add.edit;
    edit.text.SetText("ab");
    edit.text.SetCursor(1);

    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::CtrlLeft});
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::V});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();

    EXPECT_EQ(edit.text.Size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(edit.text.Value()[1]), 0x80U);
    EXPECT_EQ(m_clipboard.reads, 1);
    EXPECT_TRUE(m_online.socket.sent.empty());
}

TEST_F(GameSceneOldWindowsTest, FocusLostCancelsPreeditInTheFollowingSceneStep)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    m_scene->Step(m_input);
    Client::UI::PostToTheWindow(m_world->Runtime().oldWindows, Friends().tabs[0].base.id, Client::UI::CommandMessage, 1,
                                0);
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_NE(m_world->Runtime().oldWindows.letter, 0);
    auto& edit = m_world->Runtime().oldWindows.windows.at(m_world->Runtime().oldWindows.letter).add.edit;

    m_input.Apply({.kind = Client::Input::InputEvent::Kind::Composition, .text = "preedit"});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    ASSERT_EQ(edit.compositionText, "preedit");
    m_input.ConsumeEdges();
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::FocusLost});
    m_scene->Step(m_input);

    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_TRUE(edit.compositionText.empty());
}

TEST_F(GameSceneOldWindowsTest, ADoubleClickReachesTheGameWithoutRearmingTheOrdinaryClick)
{
    m_input.Apply(
        {.kind = Client::Input::InputEvent::Kind::ButtonDoubleClick, .button = Client::Input::MouseButton::Left});

    m_scene->Step(m_input);

    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_TRUE(m_world->Runtime().ui.doubleClick);
    EXPECT_FALSE(m_world->Runtime().movement.pendingClick);
    EXPECT_FALSE(m_world->Runtime().movement.leftHeld);
    EXPECT_TRUE(m_input.IsDown(Client::Input::MouseButton::Left));
    m_input.ConsumeEdges();
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_TRUE(m_world->Runtime().ui.doubleClick);
    EXPECT_FALSE(m_world->Runtime().movement.leftHeld);
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::Message});
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
}

// ConsumeEdges puede correr antes del lector del juego: no pierde ni el set pendiente ni el clear de otro mensaje.
TEST_F(GameSceneOldWindowsTest, AnUnpublishedDoubleClickUpdateSurvivesEdgeConsumption)
{
    m_input.Apply(
        {.kind = Client::Input::InputEvent::Kind::ButtonDoubleClick, .button = Client::Input::MouseButton::Left});
    m_input.ConsumeEdges();

    m_scene->Step(m_input);

    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_TRUE(m_world->Runtime().ui.doubleClick);
    m_input.Apply({.kind = Client::Input::InputEvent::Kind::Message});
    m_input.ConsumeEdges();
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
}

// 0x4a2f7b..0x4a2f8a salta la pestaña arrastrada; sin otro mensaje, el segundo paso aún debe ver 0x7d36849.
TEST_F(GameSceneOldWindowsTest, DraggedFriendsConsumeTheDoubleClickOnTheNextStepWithoutAMessage)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    DoubleClickTheFriendRow();

    m_scene->Step(m_input);

    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_EQ(Friends().base.state, Client::UI::ShownWindow);
    EXPECT_TRUE(m_world->Runtime().ui.doubleClick);
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    m_input.ConsumeEdges();
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
    ASSERT_EQ(Friends().tabs[0].base.queue.size(), 1u);
    EXPECT_EQ(Friends().tabs[0].base.queue.front().type, Client::UI::ChosenRowMessage);
    // RunSteps sólo llama ConsumeEdges tras el primer subpaso: entre el segundo y el tercero tampoco hay mensajes.
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
}

// 0x461fa0 consume el global. Con el mismo pump, el siguiente paso no puede volver a publicar ese doble clic.
TEST_F(GameSceneOldWindowsTest, AConsumedDoubleClickIsNotRepublishedWithoutANewMessage)
{
    ASSERT_NO_FATAL_FAILURE(OpenFriends());
    Friends().base.state = Client::UI::ShownWindow;
    DoubleClickTheFriendRow();

    m_scene->Step(m_input);

    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
    ASSERT_EQ(Friends().tabs[0].base.queue.size(), 1u);
    m_input.ConsumeEdges();
    m_scene->Step(m_input);
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_FALSE(m_world->Runtime().ui.doubleClick);
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
}
} // namespace
