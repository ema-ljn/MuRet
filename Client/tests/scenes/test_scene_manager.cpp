#include "Scenes/SceneManager.h"

#include "App/GameLoop.h"
#include "Kernel/PacingMode.h"
#include "Platform/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Textures/TextureSet.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/Cloth.h"
#include "Scenes/GameScene.h"
#include "Tables/GameData.h"
#include "Assets/GameModels.h"
#include "Session/MapLoad.h"
#include "Session/IWorldEntryPublisher.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Scenes/IScene.h"
#include "Session/ReturnToSelection.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneTransitions.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldLoadingScene.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Session/ModuleStates.h"
#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/FakeTextureStore.h"
#include "support/LetterMeasure.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Client::Scenes::SceneManager with test scenes: the original's scene machine (DAT_006d5dc8 and its eight `mov`,
// 02 § Las cinco escenas y sus transiciones), with enter, step, draw and leave, and the network on each frame. The
// real scenes have their own tests (test_loading_scene.cpp, test_login_scene.cpp).
namespace
{
using Client::App::GameLoop;
using Client::Core::Duration;
using Client::Core::Expected;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::Unexpected;
using Client::Data::Textures::LoadedTexture;
using Client::Input::InputState;
using Client::Kernel::PacingMode;
using Client::Kernel::SceneId;
using Client::Kernel::SceneName;
using Client::Platform::WindowState;
using Client::Render::TextureSlot;
using Client::Render::Frame::SceneCanvas;
using Client::Scenes::FirstScene;
using Client::Scenes::IScene;
using Client::Scenes::SceneFactory;
using Client::Scenes::SceneManager;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using std::chrono::milliseconds;

struct SceneDraw
{
    SceneId scene = SceneId::Loading;
    Duration clock{};
    float alpha = 0.0f;
    bool frameOpen = false;
};

// What the test scenes and the test share: what happened, in order, and how each one behaves.
struct Stage
{
    // "enter Loading", "step Loading", "leave Loading", "enter Login"…
    std::vector<std::string> journal;
    // Where each scene asks to go after its step; with no entry, it stays in the current one.
    std::map<SceneId, SceneId> next;
    // WantsToQuit.
    std::set<SceneId> quitting;
    // WantsTextInput.
    std::set<SceneId> typing;
    // HoldsTheLastFrame.
    std::set<SceneId> holding;
    std::map<SceneId, int> heldNotifications;
    // How many times each scene ran PrepareFrame.
    std::map<SceneId, int> prepared;
    std::optional<SceneId> failingEnter;
    // The scenes that fail in their step or their draw (IScene::Failure), with the error.
    std::map<SceneId, std::string> failingStep;
    std::map<SceneId, std::string> failingRender;
    // The slots each scene uploads on entering and releases on leaving, like its texture sets.
    std::map<SceneId, std::vector<std::uint16_t>> slots;
    FakeTextureStore textures;
    // The slots the store had already released when each scene entered.
    std::map<SceneId, std::vector<std::uint16_t>> releasedOnEnter;
    std::vector<SceneDraw> draws;
    // How many scenes the factory built, of each.
    std::map<SceneId, int> created;
    // The scene the factory has nothing for.
    std::optional<SceneId> withoutScene;
    RecordingFrameTarget target;
};

class ProbeScene final : public IScene
{
public:
    ProbeScene(SceneId id, Stage& stage) : m_id(id), m_stage(stage) {}

    Expected<void, std::string> Enter() override
    {
        Note("enter");
        m_stage.releasedOnEnter[m_id] = m_stage.textures.released;
        if (m_stage.failingEnter == m_id)
        {
            return Unexpected(std::string("falta Logo\\de-prueba.jpg"));
        }
        std::vector<LoadedTexture> textures;
        for (const std::uint16_t slot : m_stage.slots[m_id])
        {
            LoadedTexture texture;
            texture.slot = slot;
            textures.push_back(std::move(texture));
        }
        return m_stage.textures.Load(textures);
    }

    void Step(const InputState&) override
    {
        Note("step");
        if (const auto found = m_stage.failingStep.find(m_id); found != m_stage.failingStep.end())
        {
            m_failure = found->second;
        }
    }

    void Render(SceneCanvas& canvas, float alpha) override
    {
        Note("render");
        m_stage.draws.push_back(SceneDraw{
            .scene = m_id, .clock = canvas.frameClock, .alpha = alpha, .frameOpen = m_stage.target.FrameOpen()});
        if (const auto found = m_stage.failingRender.find(m_id); found != m_stage.failingRender.end())
        {
            m_failure = found->second;
        }
    }

    [[nodiscard]] std::optional<std::string> Failure() const override
    {
        return m_failure;
    }

    void Leave() override
    {
        Note("leave");
        for (const std::uint16_t slot : m_stage.slots[m_id])
        {
            m_stage.textures.Release(TextureSlot{slot});
        }
    }

    [[nodiscard]] std::optional<SceneId> Next() const override
    {
        const auto found = m_stage.next.find(m_id);
        if (found == m_stage.next.end())
        {
            return std::nullopt;
        }
        return found->second;
    }

    [[nodiscard]] bool WantsToQuit() const override
    {
        return m_stage.quitting.contains(m_id);
    }

    [[nodiscard]] bool WantsTextInput() const override
    {
        return m_stage.typing.contains(m_id);
    }

    void PrepareFrame() override
    {
        ++m_stage.prepared[m_id];
    }

    [[nodiscard]] bool HoldsTheLastFrame() const override
    {
        return m_stage.holding.contains(m_id);
    }

    void OnFrameHeld() override
    {
        ++m_stage.heldNotifications[m_id];
    }

private:
    void Note(const char* what)
    {
        m_stage.journal.push_back(std::string(what) + " " + std::string(SceneName(m_id)));
    }

    SceneId m_id;
    Stage& m_stage;
    std::optional<std::string> m_failure;
};

// The SceneManager with the test scenes, the fake clock, the recording canvas and a log that keeps its lines.
// `polls` counts the times pollNetwork ran.
struct Harness
{
    Harness()
        : log(sink, LogLevel::Debug),
          manager(
              Factory(), stage.target, clock,
              [this]() -> Expected<void, std::string>
              {
                  ++polls;
                  if (pollFailure)
                      return Unexpected(*pollFailure);
                  return {};
              },
              log, [this]() { return std::exchange(order, std::nullopt); }, {},
              [this](Duration now) { afterFrames.push_back(now); },
              [this]() -> Expected<void, std::string>
              {
                  afterSteps.push_back(stage.journal.size());
                  if (afterStepFailure)
                      return Unexpected(*afterStepFailure);
                  return {};
              },
              [this]() -> Expected<void, std::string>
              {
                  drawHooks.push_back(
                      DrawHook{.after = false, .frameOpen = stage.target.FrameOpen(), .journal = stage.journal.size()});
                  if (beforeDrawFailure)
                      return Unexpected(*beforeDrawFailure);
                  return {};
              },
              [this](SceneCanvas& canvas) -> Expected<void, std::string>
              {
                  drawHooks.push_back(DrawHook{.after = true,
                                               .frameOpen = stage.target.FrameOpen(),
                                               .journal = stage.journal.size(),
                                               .screen = canvas.screen});
                  if (afterDrawFailure)
                      return Unexpected(*afterDrawFailure);
                  return {};
              })
    {
    }

    SceneFactory Factory()
    {
        return [this](SceneId id) -> std::unique_ptr<IScene>
        {
            if (stage.withoutScene == id)
            {
                return nullptr;
            }
            ++stage.created[id];
            return std::make_unique<ProbeScene>(id, stage);
        };
    }

    [[nodiscard]] std::vector<std::string> Lines(LogLevel level) const
    {
        std::vector<std::string> lines;
        for (std::size_t i = 0; i < sink.lines.size(); ++i)
        {
            if (sink.levels[i] == level)
            {
                lines.push_back(sink.lines[i]);
            }
        }
        return lines;
    }

    Stage stage;
    FakeClock clock;
    RecordingSink sink;
    Log log;
    int polls = 0;
    std::optional<std::string> pollFailure;
    // The scene the network orders on the next Poll (WorldRuntime::orderedScene in main.cpp).
    std::optional<SceneId> order;
    // The clocks afterSceneFrame ran with (WinMain's loop in main.cpp).
    std::vector<Duration> afterFrames;
    // How many lines the journal had at each afterSceneStep (the message clocks in main.cpp), and its error.
    std::vector<std::size_t> afterSteps;
    std::optional<std::string> afterStepFailure;
    // Each beforeSceneDraw and afterSceneDraw (the screenshot in main.cpp): which, whether the frame was open, how many
    // lines the journal had and, for the after one, the canvas's screen; and their errors.
    struct DrawHook
    {
        bool after = false;
        bool frameOpen = false;
        std::size_t journal = 0;
        Client::Core::PixelSize screen{};
    };
    std::vector<DrawHook> drawHooks;
    std::optional<std::string> beforeDrawFailure;
    std::optional<std::string> afterDrawFailure;
    SceneManager manager;
    InputState input;
};

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// An application error stops every visible operation of the same frame.
TEST(SceneManagerTest, PollFailureStopsSceneOperationsAndPreservesTheFirstDiagnostic)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::Loading).HasValue());
    tested.stage.holding.insert(SceneId::Loading);
    tested.pollFailure = "falló la aplicación";
    tested.manager.Poll();
    tested.manager.Step(tested.input);
    tested.manager.Step(tested.input);
    tested.manager.Render(0.5f);
    tested.manager.Request(SceneId::Login);
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.manager.Failure(), tested.pollFailure);
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading"}));
    EXPECT_TRUE(tested.stage.target.frames.empty());
    EXPECT_EQ(tested.stage.heldNotifications[SceneId::Loading], 0);
    tested.pollFailure = "otro error";
    tested.manager.Poll();
    EXPECT_EQ(tested.polls, 1);
    EXPECT_EQ(tested.manager.Failure(), "falló la aplicación");
}

// Poll is valid before Start; its fatal error is not mistaken for an Enter that can be retried.
TEST(SceneManagerTest, APollFailureBeforeStartCannotBeClearedByStart)
{
    Stage stage;
    stage.slots[SceneId::Loading] = {0x71e, 0x723};
    FakeClock clock;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    int polls = 0;
    {
        SceneManager manager(
            [&stage](SceneId id) -> std::unique_ptr<IScene>
            {
                ++stage.created[id];
                return std::make_unique<ProbeScene>(id, stage);
            },
            stage.target, clock,
            [&polls]() -> Expected<void, std::string>
            {
                ++polls;
                return Unexpected(std::string(polls == 1 ? "primero" : "segundo"));
            },
            log);
        manager.Poll();
        ASSERT_EQ(manager.Failure(), "primero");
        const auto started = manager.Start(FirstScene);
        EXPECT_FALSE(started.HasValue());
        if (!started.HasValue())
        {
            EXPECT_EQ(started.Error(), "primero");
        }
        manager.Step(InputState{});
        manager.Render(0.5f);
        manager.Request(SceneId::Login);
        EXPECT_TRUE(manager.HoldsTheLastFrame());
        manager.Poll();
        EXPECT_EQ(manager.Failure(), "primero");
        EXPECT_EQ(manager.Current(), std::nullopt);
        EXPECT_EQ(polls, 1);
        EXPECT_TRUE(stage.created.empty());
        EXPECT_TRUE(stage.journal.empty());
        EXPECT_TRUE(stage.heldNotifications.empty());
        EXPECT_TRUE(stage.target.frames.empty());
    }
    EXPECT_TRUE(stage.textures.loaded.empty());
    EXPECT_TRUE(stage.textures.released.empty());
    EXPECT_TRUE(stage.journal.empty());
}

TEST(SceneManagerTest, PollFailureInGameLoopLeavesExactlyOnceWithoutDrawing)
{
    Harness tested;
    tested.pollFailure = "falló la aplicación";
    tested.stage.slots[SceneId::Loading] = {0x71e, 0x723};
    {
        SceneManager manager(
            tested.Factory(), tested.stage.target, tested.clock,
            [&tested]() -> Expected<void, std::string>
            {
                ++tested.polls;
                return Unexpected(*tested.pollFailure);
            },
            tested.log);
        ASSERT_TRUE(manager.Start(SceneId::Loading).HasValue());
        EXPECT_EQ(tested.stage.textures.loaded, (std::vector<std::uint16_t>{0x71e, 0x723}));
        GameLoop loop(tested.clock, manager, PacingMode::Decoupled);
        tested.clock.Advance(milliseconds{100});
        loop.RunFrame(WindowState{}, tested.input);
        ASSERT_TRUE(manager.Failure().has_value());
        EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading"}));
        EXPECT_TRUE(tested.stage.target.frames.empty());
    }
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "leave Loading"}));
    EXPECT_EQ(tested.stage.textures.released, (std::vector<std::uint16_t>{0x71e, 0x723}));
    EXPECT_EQ(tested.stage.textures.Size(), 0u);
}

TEST(SceneManagerTest, StartsInLoading)
{
    Harness tested;
    EXPECT_EQ(tested.manager.Current(), std::nullopt);

    const auto started = tested.manager.Start(FirstScene);

    ASSERT_TRUE(started.HasValue()) << started.Error();
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading"}));
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);
}

// 0x622e40 ends the loading with the scene at 2 (0x622f93): the one leaving goes before the other enters, and the
// new one's step is the next Step's.
TEST(SceneManagerTest, LoadingGoesToLogin)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::Login);
    EXPECT_EQ(tested.stage.journal,
              (std::vector<std::string>{"enter Loading", "step Loading", "leave Loading", "enter Login"}));
    const std::vector<std::string> info = tested.Lines(LogLevel::Info);
    ASSERT_FALSE(info.empty());
    EXPECT_TRUE(Contains(info.back(), "Loading (1)")) << info.back();
    EXPECT_TRUE(Contains(info.back(), "Login (2)")) << info.back();
}

// The accepted login (0x634c60 sees it in its step with the state at 0x14) goes to the selection (0x634dce). The
// LoginScene asks for it with the session in LoggedIn (test_login_scene.cpp); here, the test scene asks for it when
// the test says.
TEST(SceneManagerTest, LoginGoesToCharacterSelectWhenLoggedIn)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    tested.manager.Step(tested.input);
    ASSERT_EQ(tested.manager.Current(), SceneId::Login);

    tested.stage.next[SceneId::Login] = SceneId::CharacterSelect;
    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::CharacterSelect);
    EXPECT_EQ(tested.stage.journal.back(), "enter CharacterSelect");
    EXPECT_EQ(tested.stage.journal[tested.stage.journal.size() - 2], "leave Login");
}

// There is no `mov` from the loading to the game: one error in the log, once, and it stays in the loading.
TEST(SceneManagerTest, AnInvalidTransitionIsRejected)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Game;

    tested.manager.Step(tested.input);
    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "step Loading", "step Loading"}));
    const std::vector<std::string> errors = tested.Lines(LogLevel::Error);
    ASSERT_EQ(errors.size(), 1u) << "el mismo pedido se avisa una vez";
    EXPECT_TRUE(Contains(errors[0], "Loading (1)")) << errors[0];
    EXPECT_TRUE(Contains(errors[0], "Game (5)")) << errors[0];
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);

    // The rejection leaves nothing stuck: a valid request changes the scene.
    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);
}

// The warning is one per consecutive request: if the scene stops asking for a step and asks for the same again, or if
// the next scene asks for what the previous one was refused, the log says it again.
TEST(SceneManagerTest, ARejectionIsLoggedAgainWhenTheRequestComesBack)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Game;
    tested.manager.Step(tested.input);
    tested.stage.next.erase(SceneId::Loading);
    tested.manager.Step(tested.input);
    tested.stage.next[SceneId::Loading] = SceneId::Game;
    tested.manager.Step(tested.input);
    ASSERT_EQ(tested.Lines(LogLevel::Error).size(), 2u) << "volvió después de un paso sin pedido";

    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    ASSERT_EQ(tested.manager.Current(), SceneId::Login);
    tested.stage.next[SceneId::Login] = SceneId::Game;
    tested.manager.Step(tested.input);

    const std::vector<std::string> errors = tested.Lines(LogLevel::Error);
    ASSERT_EQ(errors.size(), 3u) << "lo pidió otra escena";
    EXPECT_TRUE(Contains(errors[2], "Login (2)")) << errors[2];
    EXPECT_TRUE(Contains(errors[2], "Game (5)")) << errors[2];
}

// Without a scene (before Start, or after an Enter that failed) there is nowhere to go from: a request builds no
// scene, changes nothing and does not warn.
TEST(SceneManagerTest, ARequestWithoutASceneChangesNothing)
{
    Harness tested;
    tested.manager.Request(SceneId::Login);
    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    EXPECT_TRUE(tested.stage.created.empty());
    EXPECT_TRUE(tested.Lines(LogLevel::Error).empty());

    tested.stage.failingEnter = SceneId::Loading;
    ASSERT_FALSE(tested.manager.Start(FirstScene).HasValue());
    tested.manager.Request(SceneId::Login);

    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    EXPECT_EQ(tested.stage.created, (std::map<SceneId, int>{{SceneId::Loading, 1}}));
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "leave Loading"}));
    EXPECT_EQ(tested.Lines(LogLevel::Error).size(), 1u) << "solo el del Enter que falló";
}

// Start is the first scene: with one already entered it is an error, and the current one stays, without Leave or
// another factory (otherwise the current one would be destroyed without releasing its textures). Request is for
// changing.
TEST(SceneManagerTest, StartingAgainKeepsTheScene)
{
    Harness tested;
    tested.stage.slots[SceneId::Loading] = {0x71e, 0x723};
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());

    const auto again = tested.manager.Start(SceneId::Login);

    ASSERT_FALSE(again.HasValue());
    EXPECT_TRUE(Contains(again.Error(), "Loading (1)")) << again.Error();
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading"}));
    EXPECT_EQ(tested.stage.created, (std::map<SceneId, int>{{SceneId::Loading, 1}}));
    EXPECT_EQ(tested.stage.textures.Size(), 2u);
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);
    EXPECT_EQ(tested.Lines(LogLevel::Error).size(), 1u);
}

// After a Start that failed, another starts from scratch: if it enters, the earlier error is gone.
TEST(SceneManagerTest, AStartAfterAFailedOneClearsTheFailure)
{
    Harness tested;
    tested.stage.failingEnter = SceneId::Loading;
    ASSERT_FALSE(tested.manager.Start(FirstScene).HasValue());
    ASSERT_TRUE(tested.manager.Failure().has_value());
    tested.stage.failingEnter.reset();

    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());

    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);
}

// Request, from outside the scenes (the network of CL6 and CL9, with the F1 02 of 0x4c2710), follows the same table.
TEST(SceneManagerTest, ARequestFollowsTheSameTable)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());

    tested.manager.Request(SceneId::CharacterSelect);
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.Lines(LogLevel::Error).size(), 1u);

    tested.manager.Request(SceneId::Login);
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);
}

// An Enter that fails (a missing texture) stops: the error stays in Failure, so main.cpp closes, and in the log; the
// scene that failed releases what it managed to upload (its Leave), and there is no longer a scene that steps or
// draws.
TEST(SceneManagerTest, AFailedEnterStopsWithItsError)
{
    Harness tested;
    tested.stage.failingEnter = SceneId::Login;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "step Loading", "leave Loading",
                                                              "enter Login", "leave Login"}));
    ASSERT_TRUE(tested.manager.Failure().has_value());
    EXPECT_TRUE(Contains(*tested.manager.Failure(), "falta Logo\\de-prueba.jpg")) << *tested.manager.Failure();
    const std::vector<std::string> errors = tested.Lines(LogLevel::Error);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(Contains(errors[0], "falta Logo\\de-prueba.jpg")) << errors[0];

    const std::size_t events = tested.stage.journal.size();
    tested.manager.Step(tested.input);
    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.stage.journal.size(), events);
    EXPECT_TRUE(tested.stage.target.frames.empty());
}

// A scene that stops in its draw (the game at a stage MuRet lacks, IScene::Failure): the error stays in Failure and
// in the log once, the stopped frame is not presented, it holds and nothing runs again; the scene stays the current
// one and does its Leave on closing.
TEST(SceneManagerTest, ARenderFailureStopsTheSceneWithoutPresenting)
{
    Harness tested;
    tested.stage.failingRender[SceneId::Loading] = "0x5fc353: falta el pasto";
    ASSERT_TRUE(tested.manager.Start(SceneId::Loading).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Render(0.0f);

    ASSERT_EQ(tested.manager.Failure(), std::optional<std::string>("0x5fc353: falta el pasto"));
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    ASSERT_EQ(tested.stage.target.frames.size(), 1u);
    EXPECT_FALSE(tested.stage.target.frames[0].presented);
    const std::vector<std::string> errors = tested.Lines(LogLevel::Error);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(Contains(errors[0], "0x5fc353")) << errors[0];
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());

    tested.manager.Step(tested.input);
    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "render Loading"}));
    EXPECT_EQ(tested.stage.target.frames.size(), 1u);
}

// The same in the step: that step's request for another scene is not looked at.
TEST(SceneManagerTest, AStepFailureStopsTheSceneBeforeItsRequest)
{
    Harness tested;
    tested.stage.failingStep[SceneId::Loading] = "0x636290: falta algo";
    ASSERT_TRUE(tested.manager.Start(SceneId::Loading).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Step(tested.input);
    tested.manager.Render(0.0f);

    ASSERT_EQ(tested.manager.Failure(), std::optional<std::string>("0x636290: falta algo"));
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "step Loading"}));
    EXPECT_TRUE(tested.stage.target.frames.empty());
    EXPECT_FALSE(tested.manager.TakeTheSceneChange());
}

// The 0x637700 part after the step (afterSceneStep) runs after a step that went well, before the scene's request;
// its error stops like a step's, and a step that failed does not run it.
TEST(SceneManagerTest, AfterTheStepRunsBeforeTheRequestAndItsFailureStopsTheScene)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::Loading).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    // After "step Loading" and before the loading's Leave.
    ASSERT_EQ(tested.afterSteps.size(), 1u);
    EXPECT_EQ(tested.stage.journal[tested.afterSteps[0] - 1], "step Loading");
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);

    tested.afterStepFailure = "0x45bb20: falta algo";
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.manager.Failure(), std::optional<std::string>("0x45bb20: falta algo"));
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.afterSteps.size(), 2u);
}

// The 0x637700 part around the draw: beforeSceneDraw before BeginFrame and afterSceneDraw after the scene's draw,
// with its canvas and the frame open, before the Present.
TEST(SceneManagerTest, TheDrawHooksRunAroundTheSceneDraw)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.manager.Render(0.0f);

    ASSERT_EQ(tested.drawHooks.size(), 2u);
    EXPECT_FALSE(tested.drawHooks[0].after);
    EXPECT_FALSE(tested.drawHooks[0].frameOpen);
    EXPECT_EQ(tested.drawHooks[0].journal, 1u) << "antes de render Loading";
    EXPECT_TRUE(tested.drawHooks[1].after);
    EXPECT_TRUE(tested.drawHooks[1].frameOpen);
    EXPECT_EQ(tested.stage.journal[tested.drawHooks[1].journal - 1], "render Loading");
    EXPECT_EQ(tested.drawHooks[1].screen.width, 640);
    ASSERT_EQ(tested.stage.target.frames.size(), 1u);
    EXPECT_TRUE(tested.stage.target.frames[0].presented);
}

// Their errors stop like the draw's: the before one, without opening the frame or drawing the scene; the after one,
// without presenting it. Then nothing runs again.
TEST(SceneManagerTest, ADrawHookFailureStopsTheSceneWithoutPresenting)
{
    {
        Harness tested;
        ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
        tested.beforeDrawFailure = "0x6381c7: sin hora";
        tested.manager.Render(0.0f);
        EXPECT_EQ(tested.manager.Failure(), std::optional<std::string>("0x6381c7: sin hora"));
        EXPECT_TRUE(tested.stage.target.frames.empty());
        EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading"}));
        EXPECT_EQ(tested.Lines(LogLevel::Error).size(), 1u);
    }
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.afterDrawFailure = "0x63b150: sin disco";
    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.manager.Failure(), std::optional<std::string>("0x63b150: sin disco"));
    ASSERT_EQ(tested.stage.target.frames.size(), 1u);
    EXPECT_FALSE(tested.stage.target.frames[0].presented);
    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.drawHooks.size(), 2u);
}

// An Enter error does not stop the independent session while the host decides to retry.
TEST(SceneManagerTest, PollContinuesAfterFailedEnterAndAnEnterRetryStillSucceeds)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.failingEnter = SceneId::Login;
    tested.manager.Request(SceneId::Login);
    ASSERT_TRUE(tested.manager.Failure().has_value());
    const auto enterFailure = tested.manager.Failure();
    tested.manager.Poll();
    tested.manager.Poll();
    EXPECT_EQ(tested.polls, 2);
    EXPECT_EQ(tested.manager.Failure(), enterFailure);
    tested.stage.failingEnter.reset();
    ASSERT_TRUE(tested.manager.Start(SceneId::Login).HasValue());
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);
    tested.manager.Poll();
    EXPECT_EQ(tested.polls, 3);
}

TEST(SceneManagerTest, PollFailureAfterFailedEnterWinsAndRemainsTerminal)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.failingEnter = SceneId::Login;
    tested.manager.Request(SceneId::Login);
    ASSERT_TRUE(tested.manager.Failure().has_value());
    const auto events = tested.stage.journal.size();
    tested.pollFailure = "falló Poll después de Enter";
    tested.manager.Poll();
    EXPECT_EQ(tested.polls, 1);
    EXPECT_EQ(tested.manager.Failure(), "falló Poll después de Enter");
    tested.stage.failingEnter.reset();
    const auto retried = tested.manager.Start(SceneId::Login);
    ASSERT_FALSE(retried.HasValue());
    EXPECT_EQ(retried.Error(), "falló Poll después de Enter");
    tested.pollFailure = "otro fallo de Poll";
    tested.manager.Poll();
    tested.manager.Step(tested.input);
    tested.manager.Render(0.5f);
    tested.manager.Request(SceneId::Loading);
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.manager.Failure(), "falló Poll después de Enter");
    EXPECT_EQ(tested.polls, 1);
    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    EXPECT_EQ(tested.stage.journal.size(), events);
    EXPECT_EQ(tested.stage.created[SceneId::Login], 1);
    EXPECT_TRUE(tested.stage.target.frames.empty());
}

TEST(SceneManagerTest, AFailedFirstEnterFailsTheStart)
{
    Harness tested;
    tested.stage.failingEnter = SceneId::Loading;

    const auto started = tested.manager.Start(FirstScene);

    ASSERT_FALSE(started.HasValue());
    EXPECT_TRUE(Contains(started.Error(), "falta Logo\\de-prueba.jpg")) << started.Error();
    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    EXPECT_EQ(tested.manager.Failure(), started.Error());
}

TEST(SceneManagerTest, AFactoryWithoutTheSceneFails)
{
    Harness tested;
    tested.stage.withoutScene = SceneId::Login;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), std::nullopt);
    ASSERT_TRUE(tested.manager.Failure().has_value());
    EXPECT_TRUE(Contains(*tested.manager.Failure(), "Login (2)")) << *tested.manager.Failure();
}

// Like 0x63c150 at the end of 0x622e40: the loading's six slots are released before the login's 18 are uploaded
// (Leave before the next Enter).
TEST(SceneManagerTest, LeavingReleasesTheTextures)
{
    Harness tested;
    const std::vector<std::uint16_t> loading = {0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723};
    const std::vector<std::uint16_t> login = {0x15, 0x16, 0x5c2, 0x5c3, 0x635};
    tested.stage.slots[SceneId::Loading] = loading;
    tested.stage.slots[SceneId::Login] = login;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.stage.releasedOnEnter[SceneId::Login], loading);
    EXPECT_EQ(tested.stage.textures.Size(), login.size());
    for (const std::uint16_t slot : loading)
    {
        EXPECT_FALSE(tested.stage.textures.Find(TextureSlot{slot}).has_value()) << slot;
    }
}

TEST(SceneManagerTest, TheSceneLeavesWhenTheManagerCloses)
{
    Harness tested;
    tested.stage.slots[SceneId::Loading] = {0x71e, 0x723};
    {
        SceneManager closing(tested.Factory(), tested.stage.target, tested.clock, {}, tested.log);
        ASSERT_TRUE(closing.Start(FirstScene).HasValue());
        ASSERT_EQ(tested.stage.textures.Size(), 2u);
    }

    EXPECT_EQ(tested.stage.journal.back(), "leave Loading");
    EXPECT_EQ(tested.stage.textures.Size(), 0u);
}

// 4 to 4 (0x4c363f, with the F1 02 in the selection): it leaves and enters a new scene, which uploads everything
// again, like the original with 0x7d3a13f at 0 (0x4c38eb).
TEST(SceneManagerTest, ReentersTheSelectionFresh)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::CharacterSelect).HasValue());
    tested.stage.next[SceneId::CharacterSelect] = SceneId::CharacterSelect;

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::CharacterSelect);
    EXPECT_EQ(tested.stage.created[SceneId::CharacterSelect], 2);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter CharacterSelect", "step CharacterSelect",
                                                              "leave CharacterSelect", "enter CharacterSelect"}));
}

// The network runs on each Poll, also before Start and with the window minimized, which does not draw: the GameLoop
// calls Poll once per frame (CL4.1), like 0x4e1d50 on each loop iteration.
TEST(SceneManagerTest, PollsTheNetworkOncePerFrame)
{
    Harness tested;
    tested.manager.Poll();
    EXPECT_EQ(tested.polls, 1);
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());

    GameLoop loop{tested.clock, tested.manager, PacingMode::Decoupled};
    WindowState minimized;
    minimized.minimized = true;
    for (int frame = 0; frame < 10; ++frame)
    {
        loop.RunFrame(minimized, tested.input);
        tested.clock.Advance(milliseconds{16});
    }
    WindowState shown;
    for (int frame = 0; frame < 10; ++frame)
    {
        loop.RunFrame(shown, tested.input);
        tested.clock.Advance(milliseconds{16});
    }

    EXPECT_EQ(tested.polls, 1 + static_cast<int>(loop.Stats().frames));
    EXPECT_EQ(tested.stage.target.frames.size(), 10u) << "minimizada no dibuja";
}

TEST(SceneManagerTest, RendersBetweenBeginAndPresent)
{
    Harness tested;
    tested.manager.Render(0.0f);
    EXPECT_TRUE(tested.stage.target.frames.empty()) << "sin escena no hay frame";
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.clock.Advance(milliseconds{1234});

    tested.manager.Render(0.25f);
    tested.clock.Advance(milliseconds{40});
    tested.manager.Render(0.5f);

    ASSERT_EQ(tested.stage.target.frames.size(), 2u);
    EXPECT_EQ(tested.stage.target.frames[0].clock, milliseconds{1234});
    EXPECT_EQ(tested.stage.target.frames[1].clock, milliseconds{1274});
    EXPECT_TRUE(tested.stage.target.frames[0].presented);
    EXPECT_TRUE(tested.stage.target.frames[1].presented);
    ASSERT_EQ(tested.stage.draws.size(), 2u);
    EXPECT_TRUE(tested.stage.draws[0].frameOpen);
    EXPECT_EQ(tested.stage.draws[0].clock, milliseconds{1234});
    EXPECT_EQ(tested.stage.draws[0].alpha, 0.25f);
    EXPECT_TRUE(tested.stage.draws[1].frameOpen);
    EXPECT_EQ(tested.stage.draws[1].alpha, 0.5f);
}
// P-C61: the end of each loop runs afterSceneFrame with the clock, with the frame drawn or held (the original runs
// the frame without presenting it), and not without a scene or with an error.
TEST(SceneManagerTest, FinishingTheLoopRunsAfterTheFrame)
{
    Harness tested;
    tested.manager.FinishTheLoop();
    EXPECT_TRUE(tested.afterFrames.empty()) << "sin escena no hay vuelta del juego";
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_TRUE(tested.afterFrames.empty()) << "el Start no termina una vuelta";
    tested.clock.Advance(milliseconds{1234});

    tested.manager.Render(0.0f);
    EXPECT_TRUE(tested.afterFrames.empty()) << "el dibujo solo no termina la vuelta";
    tested.manager.FinishTheLoop();
    tested.stage.holding.insert(SceneId::Loading);
    tested.clock.Advance(milliseconds{40});
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
    tested.manager.FinishTheLoop();

    EXPECT_EQ(tested.afterFrames, (std::vector<Duration>{milliseconds{1234}, milliseconds{1274}}));

    tested.stage.holding.clear();
    tested.stage.failingRender[SceneId::Loading] = "0x5fc353: falta el pasto";
    tested.manager.Render(0.0f);
    ASSERT_TRUE(tested.manager.Failure().has_value());
    tested.manager.FinishTheLoop();
    EXPECT_EQ(tested.afterFrames.size(), 2u);
}

// A scene that asks to quit the game (the OK of the login's 0x70 notice, Q-C69): the SceneManager says so, so
// main.cpp ends the loop with EXIT_SUCCESS. Without a scene, or with one that does not ask, no.
TEST(SceneManagerTest, SaysWhenTheSceneWantsToQuit)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.WantsToQuit());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.WantsToQuit());

    tested.stage.quitting.insert(SceneId::Loading);

    EXPECT_TRUE(tested.manager.WantsToQuit());
    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
}

// A scene with a text field open (the login's account): the SceneManager says so, so main.cpp turns SDL's text input
// on only meanwhile. Without a scene, or with one that does not ask, no.
TEST(SceneManagerTest, SaysWhenTheSceneWantsTextInput)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.WantsTextInput());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.WantsTextInput());

    tested.stage.typing.insert(SceneId::Loading);

    EXPECT_TRUE(tested.manager.WantsTextInput());
}

// A step that entered another scene says so once, so the GameLoop does not count how long the entry took
// (GameLoop.h). A step without a change, or the Start, does not.
TEST(SceneManagerTest, SaysOnceWhenAStepChangedTheScene)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.TakeTheSceneChange());
    tested.manager.Step(tested.input);
    EXPECT_FALSE(tested.manager.TakeTheSceneChange());

    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);

    ASSERT_EQ(tested.manager.Current(), SceneId::Login);
    EXPECT_TRUE(tested.manager.TakeTheSceneChange());
    EXPECT_FALSE(tested.manager.TakeTheSceneChange());
}

// A scene that holds the last frame (the login while it connects, Q-C85): the SceneManager says so, so the GameLoop
// does not draw and the previous frame stays on screen. Without a scene, or with one that does not ask, no.
TEST(SceneManagerTest, SaysWhenTheSceneHoldsTheLastFrame)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());

    tested.stage.holding.insert(SceneId::Loading);

    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
}
// The query that prevents the Render also notifies the scene, even without an intermediate Step.
TEST(SceneManagerTest, OnlyAHeldFrameNotifiesTheCurrentScene)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_TRUE(tested.stage.heldNotifications.empty());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_TRUE(tested.stage.heldNotifications.empty());
    tested.stage.holding.insert(SceneId::Loading);
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.stage.heldNotifications[SceneId::Loading], 1);
    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.stage.heldNotifications[SceneId::Loading], 2);
    tested.stage.holding.clear();
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.stage.heldNotifications[SceneId::Loading], 2);
}

// 0x6371a0 reads what it decides at the start of each frame: PrepareFrame runs once per frame, the first time it is
// asked, and asking again or drawing does not run it again. Poll and Step start another frame, and Render ends it.
TEST(SceneManagerTest, PreparesTheFrameOncePerFrame)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());

    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.stage.prepared[SceneId::Loading], 1);

    tested.manager.Render(0.0f);
    EXPECT_EQ(tested.stage.prepared[SceneId::Loading], 2) << "un Render sin preguntar también lee";

    tested.manager.Poll();
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    tested.manager.Step(tested.input);
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    EXPECT_EQ(tested.stage.prepared[SceneId::Loading], 4);
}

// Full resources for the two orchestration tests that go through producers. Their map 0 with an empty pool isolates
// the draw's gate; the real initial 54 case lives in test_game_scene_wait.
struct ManagerGameResources
{
    Expected<void, std::string> Load(Client::Kernel::WorldState& world, Client::Terrain::TerrainState& terrain,
                                     Client::Models::ClothWorld& cloth, FakeTextureStore& textures, Log& log)
    {
        auto loadedData = Client::Tables::LoadGameData(dataRoot);
        if (!loadedData.HasValue())
            return Unexpected(loadedData.Error());
        data = std::move(loadedData).Value();
        if (auto loadedModels = models.Load(dataRoot, textures, log); !loadedModels.HasValue())
            return loadedModels;
        world.Runtime().map = 0; // The caller writes the world before 0x61a560, which keeps the observed one.
        return Client::Scenes::LoadMap(0, {dataRoot, world, terrain, SceneId::Game, 0, cloth.random});
    }
    std::filesystem::path dataRoot = Client::Tests::GoldenDir();
    Client::Tables::GameData data;
    Client::Assets::GameModels models;
};

class ManagerEntryPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override
    {
        ++calls;
    }
    int calls = 0;
};

// 06, entry steps 2..6: the mosaic is held with counter>30. This orchestration fixture lends a real map: on
// publishing 30 the full frame is presented, with no resource error.
TEST(SceneManagerTest, TheMosaicStaysUntilTheCounterIsThirty)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    FakeClock clock;
    Client::Models::ClothWorld cloth;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    ManagerGameResources gameResources;
    const auto loaded = gameResources.Load(world, states.terrain, cloth, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    // 0x633f30: the selection already marked the world loaded.
    world.Runtime().drawingEnabled = true;
    const Client::Scenes::SceneContext context{.dataRoot = gameResources.dataRoot,
                                               .textures = textures,
                                               .log = log,
                                               .cloth = &cloth,
                                               .world = &world,
                                               .states = &states};
    RecordingFrameTarget target({800, 600});
    SceneManager manager(
        [&](SceneId id) -> std::unique_ptr<IScene>
        {
            if (id == SceneId::WorldLoading)
            {
                return std::make_unique<Client::Scenes::WorldLoadingScene>(context);
            }
            if (id == SceneId::Game)
            {
                return std::make_unique<Client::Scenes::GameScene>(
                    context, nullptr, &gameResources.models, &gameResources.data.items->table,
                    &gameResources.models.TextureSlots(), &*gameResources.data.gates, &*gameResources.data.skills);
            }
            return nullptr;
        },
        target, clock, nullptr, log);
    const InputState input;
    // The draw of a GameLoop frame, which may have no steps: the network and the draw, if it does not hold.
    const auto draw = [&]()
    {
        manager.Poll();
        if (!manager.HoldsTheLastFrame())
        {
            manager.Render(0.0f);
        }
    };
    ASSERT_TRUE(manager.Start(SceneId::WorldLoading).HasValue());
    EXPECT_EQ(world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
    draw();
    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_EQ(target.frames[0].events.size(), 4u) << "el mosaico";

    manager.Step(input);
    ASSERT_EQ(manager.Current(), SceneId::Game);
    draw();
    manager.Step(input);
    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    draw();
    EXPECT_EQ(target.frames.size(), 1u) << "con el contador de la carga sigue el mosaico";
    world.Runtime().warpCounter = 31;
    draw();
    EXPECT_EQ(target.frames.size(), 1u) << "con 31 sigue el mosaico";

    world.Runtime().warpCounter = 30;
    draw();

    ASSERT_EQ(target.frames.size(), 2u);
    EXPECT_TRUE(target.frames[1].presented);
    EXPECT_FALSE(target.frames[1].events.empty());
    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    EXPECT_FALSE(manager.HoldsTheLastFrame());
}

// The F1 02 with 1 writes scene 4 while the network is read (0x4c363f): the Poll takes the order and the next step
// is already the selection's, which enters before its step. The order is taken once.
TEST(SceneManagerTest, ANetworkOrderEntersBeforeTheNextStep)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::Game).HasValue());
    tested.order = SceneId::CharacterSelect;

    tested.manager.Poll();

    EXPECT_FALSE(tested.order.has_value());
    EXPECT_EQ(tested.manager.Current(), SceneId::Game);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Game"}));

    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::CharacterSelect);
    EXPECT_EQ(tested.stage.journal,
              (std::vector<std::string>{"enter Game", "leave Game", "enter CharacterSelect", "step CharacterSelect"}));
    EXPECT_TRUE(tested.manager.TakeTheSceneChange());

    tested.manager.Poll();
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.stage.journal.back(), "step CharacterSelect");
    EXPECT_EQ(tested.stage.created[SceneId::CharacterSelect], 1);
}

// Q-C79: the original would change scenes in the loading too; MuRet follows the table, and the order from there is an
// error in the log and the scene takes its step.
TEST(SceneManagerTest, ANetworkOrderOutsideTheTableIsRejected)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    tested.order = SceneId::CharacterSelect;

    tested.manager.Poll();
    tested.manager.Step(tested.input);

    EXPECT_EQ(tested.manager.Current(), SceneId::Loading);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Loading", "step Loading"}));
    const std::vector<std::string> errors = tested.Lines(LogLevel::Error);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_TRUE(Contains(errors[0], "Loading (1)")) << errors[0];
    EXPECT_TRUE(Contains(errors[0], "CharacterSelect (4)")) << errors[0];
    EXPECT_EQ(tested.manager.Failure(), std::nullopt);
}

// A Poll that fails does not take the order: the error is terminal and there is no other step.
TEST(SceneManagerTest, AFailedPollDoesNotTakeTheOrder)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::Game).HasValue());
    tested.order = SceneId::CharacterSelect;
    tested.pollFailure = "falló la aplicación";

    tested.manager.Poll();
    tested.manager.Step(tested.input);

    EXPECT_TRUE(tested.order.has_value());
    EXPECT_EQ(tested.manager.Current(), SceneId::Game);
    EXPECT_EQ(tested.stage.journal, (std::vector<std::string>{"enter Game"}));
}

// The selection of the return, without the list or the 54: on entering it marks the world loaded (what 0x633f30 does
// with the list) and, when the test chooses, goes to scene 3 with PrepareSelectedActor, which lowers the world load
// flag (0x633efe).
class ChoosingSelection final : public IScene
{
public:
    ChoosingSelection(Client::Kernel::WorldState& world, Client::Crt::CrtRandom& random, const bool& choose)
        : m_world(world), m_random(random), m_choose(choose)
    {
    }

    Expected<void, std::string> Enter() override
    {
        m_world.Runtime().drawingEnabled = true;
        return {};
    }

    void Step(const InputState& /*input*/) override
    {
        if (m_choose && !m_chosen)
        {
            m_world.PrepareSelectedActor(Client::Protocol::FrontEnd::CharacterEntry{}, m_random);
            m_chosen = true;
        }
    }

    void Render(SceneCanvas& /*canvas*/, float /*alpha*/) override {}

    void Leave() override {}

    [[nodiscard]] std::optional<SceneId> Next() const override
    {
        return m_chosen ? std::optional<SceneId>{SceneId::WorldLoading} : std::nullopt;
    }

    [[nodiscard]] bool WantsToQuit() const override
    {
        return false;
    }

    [[nodiscard]] bool WantsTextInput() const override
    {
        return false;
    }

    [[nodiscard]] bool HoldsTheLastFrame() const override
    {
        return false;
    }

private:
    Client::Kernel::WorldState& m_world;
    Client::Crt::CrtRandom& m_random;
    const bool& m_choose;
    bool m_chosen = false;
};

// Back to selection and a new load: keeps the scene changes and the second wait. The visual selection is an explicit
// double; at the end the real ApplyWorldEntry loads the map/actor before allowing the new frame. A missing-terrain
// failure is not used as the expected end.
TEST(SceneManagerTest, GameSelectionGameHoldsAndReloadsAgain)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    FakeClock clock;
    Client::Models::ClothWorld cloth;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection(world);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    ManagerGameResources gameResources;
    const auto loaded = gameResources.Load(world, states.terrain, cloth, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    // 0x633f30: the selection already marked the world loaded.
    world.Runtime().drawingEnabled = true;
    const Client::Scenes::SceneContext context{.dataRoot = gameResources.dataRoot,
                                               .textures = textures,
                                               .log = log,
                                               .clock = &clock,
                                               .cloth = &cloth,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .selection = &selection,
                                               .world = &world,
                                               .states = &states};
    const Client::Tests::LetterMeasure letters;
    const Client::Scenes::WorldEntryResources resources{.world = world,
                                                        .selection = selection,
                                                        .dataRoot = gameResources.dataRoot,
                                                        .cloth = cloth,
                                                        .camera = camera,
                                                        .cameraInput = cameraInput,
                                                        .models = gameResources.models,
                                                        .items = &gameResources.data.items->table,
                                                        .itemSetTables = &*gameResources.data.itemSets,
                                                        .log = log,
                                                        .textMeasure = &letters,
                                                        .texts = &*gameResources.data.text,
                                                        .states = &states};
    RecordingFrameTarget target({800, 600});
    bool returning = false;
    bool choose = false;
    std::map<SceneId, int> created;
    SceneManager manager(
        [&](SceneId id) -> std::unique_ptr<IScene>
        {
            ++created[id];
            if (id == SceneId::WorldLoading)
            {
                return std::make_unique<Client::Scenes::WorldLoadingScene>(context);
            }
            if (id == SceneId::Game)
            {
                return std::make_unique<Client::Scenes::GameScene>(
                    context, nullptr, &gameResources.models, &gameResources.data.items->table,
                    &gameResources.models.TextureSlots(), &*gameResources.data.gates, &*gameResources.data.skills);
            }
            if (id == SceneId::CharacterSelect)
            {
                return std::make_unique<ChoosingSelection>(world, cloth.random, choose);
            }
            return nullptr;
        },
        target, clock,
        [&]() -> Expected<void, std::string>
        {
            if (std::exchange(returning, false))
            {
                return Client::Scenes::ApplyReturnToSelection(resources);
            }
            return {};
        },
        log, [&world]() { return std::exchange(world.Runtime().orderedScene, std::nullopt); });
    const InputState input;
    const auto draw = [&]()
    {
        manager.Poll();
        if (!manager.HoldsTheLastFrame())
        {
            manager.Render(0.0f);
        }
    };
    ASSERT_TRUE(manager.Start(SceneId::WorldLoading).HasValue());
    draw();
    manager.Step(input);
    ASSERT_EQ(manager.Current(), SceneId::Game);
    draw();
    ASSERT_EQ(target.frames.size(), 1u) << "el juego retiene con el contador de la carga";

    returning = true;
    draw();

    EXPECT_EQ(manager.Current(), SceneId::Game) << "la orden entra en el próximo paso";
    EXPECT_FALSE(world.Runtime().drawingEnabled);
    EXPECT_EQ(world.Runtime().map, -1);
    EXPECT_EQ(target.frames.size(), 1u) << "sin el mundo cargado, el juego no dibuja";

    manager.Step(input);
    ASSERT_EQ(manager.Current(), SceneId::CharacterSelect);
    EXPECT_TRUE(manager.TakeTheSceneChange());
    draw();
    ASSERT_EQ(target.frames.size(), 2u);
    EXPECT_TRUE(target.frames[1].presented);

    choose = true;
    manager.Step(input);
    ASSERT_EQ(manager.Current(), SceneId::WorldLoading);
    EXPECT_EQ(world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
    draw();
    ASSERT_EQ(target.frames.size(), 3u);
    EXPECT_EQ(target.frames[2].events.size(), 4u) << "el mosaico, otra vez";

    manager.Step(input);
    ASSERT_EQ(manager.Current(), SceneId::Game);
    draw();
    manager.Step(input);
    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    draw();
    EXPECT_EQ(target.frames.size(), 3u) << "el juego nuevo retiene con el contador de la carga";
    world.Runtime().warpCounter = 31;
    draw();
    EXPECT_EQ(target.frames.size(), 3u) << "con 31 sigue el mosaico";

    Client::Protocol::FrontEnd::CharacterSelectResult reply;
    reply.map = 0;
    reply.x = 133;
    reply.y = 127;
    reply.pk = 3;
    ManagerEntryPublisher publisher;
    const auto entered = Client::Scenes::ApplyWorldEntry(reply, 5, resources, publisher);
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(publisher.calls, 1);
    EXPECT_EQ(world.Runtime().warpCounter, 30);
    draw();

    ASSERT_EQ(target.frames.size(), 4u);
    EXPECT_TRUE(target.frames[3].presented);
    EXPECT_FALSE(target.frames[3].events.empty());
    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    EXPECT_FALSE(manager.HoldsTheLastFrame());
    EXPECT_EQ(created[SceneId::WorldLoading], 2);
    EXPECT_EQ(created[SceneId::Game], 2);
    EXPECT_EQ(created[SceneId::CharacterSelect], 1);
}
} // namespace
