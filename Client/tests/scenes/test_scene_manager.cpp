#include "Scenes/SceneManager.h"

#include "App/GameLoop.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/TextureSet.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameScene.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/MapLoad.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Scenes/IScene.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTransitions.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/FakeTextureStore.h"
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

// Client::Scenes::SceneManager con escenas de prueba: la máquina de escenas del original
// (DAT_006d5dc8 y sus ocho `mov`, 02 § Las cinco escenas y sus transiciones), con entrar, paso,
// dibujo y salir, y la red en cada frame. Las escenas de verdad tienen sus tests
// (test_loading_scene.cpp, test_login_scene.cpp).
namespace
{
using Client::App::GameLoop;
using Client::App::PacingMode;
using Client::App::WindowState;
using Client::Core::Duration;
using Client::Core::Expected;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::Unexpected;
using Client::Data::Textures::LoadedTexture;
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Scenes::FirstScene;
using Client::Scenes::IScene;
using Client::Scenes::SceneCanvas;
using Client::Scenes::SceneFactory;
using Client::Scenes::SceneId;
using Client::Scenes::SceneManager;
using Client::Scenes::SceneName;
using Client::Tests::FakeClock;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using std::chrono::milliseconds;

// Lo que vio el Render de una escena de prueba.
struct SceneDraw
{
    SceneId scene = SceneId::Loading;
    Duration clock{};
    float alpha = 0.0f;
    bool frameOpen = false;
};

// Lo que comparten las escenas de prueba y el test: qué pasó, en orden, y cómo se porta cada una.
struct Stage
{
    // "enter Loading", "step Loading", "leave Loading", "enter Login"…
    std::vector<std::string> journal;
    // Adónde pide ir cada escena después de su paso; sin entrada, sigue en la que está.
    std::map<SceneId, SceneId> next;
    // Las escenas que piden cerrar el juego (WantsToQuit).
    std::set<SceneId> quitting;
    // Las escenas que piden el texto tecleado (WantsTextInput).
    std::set<SceneId> typing;
    // Las escenas que retienen el último frame (HoldsTheLastFrame).
    std::set<SceneId> holding;
    std::map<SceneId, int> heldNotifications;
    // Cuántas veces corrió PrepareFrame cada escena.
    std::map<SceneId, int> prepared;
    // La escena cuyo Enter falla.
    std::optional<SceneId> failingEnter;
    // Las escenas que fallan en su paso o en su dibujo (IScene::Failure), con el error.
    std::map<SceneId, std::string> failingStep;
    std::map<SceneId, std::string> failingRender;
    // Los slots que sube cada escena al entrar y suelta al salir, como sus juegos de texturas.
    std::map<SceneId, std::vector<std::uint16_t>> slots;
    FakeTextureStore textures;
    // Los slots que el store ya había soltado cuando entró cada escena.
    std::map<SceneId, std::vector<std::uint16_t>> releasedOnEnter;
    std::vector<SceneDraw> draws;
    // Cuántas escenas armó la fábrica, de cada una.
    std::map<SceneId, int> created;
    // La escena para la que la fábrica no tiene nada.
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

// El SceneManager con las escenas de prueba, el reloj falso, el lienzo que anota y un log que
// guarda. `polls` cuenta las veces que corrió pollNetwork.
struct Harness
{
    Harness()
        : log(sink, LogLevel::Debug), manager(
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

    // Las líneas del log con el nivel `level`.
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
    // La escena que ordena la red en el próximo Poll (WorldRuntime::orderedScene en main.cpp).
    std::optional<SceneId> order;
    // Los relojes con los que corrió afterSceneFrame (la vuelta de WinMain en main.cpp).
    std::vector<Duration> afterFrames;
    // Cuántas líneas tenía el diario en cada afterSceneStep (los relojes de los mensajes en main.cpp), y su error.
    std::vector<std::size_t> afterSteps;
    std::optional<std::string> afterStepFailure;
    SceneManager manager;
    InputState input;
};

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// Un error de aplicación corta todas las operaciones visibles del mismo frame.
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

// Poll es válido antes de Start; su error fatal no se confunde con un Enter que se puede reintentar.
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

// 0x622e40 termina la carga con la escena en 2 (0x622f93): la que sale se va antes de que entre la
// otra, y el paso de la nueva es el del próximo Step.
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

// El login aceptado (0x634c60 lo ve en su paso con el estado en 0x14) va a la selección
// (0x634dce). La LoginScene lo pide con la sesión en LoggedIn (test_login_scene.cpp); acá, la de
// prueba lo pide cuando el test dice.
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

// De la carga al juego no hay ningún `mov`: un error en el log, una vez, y sigue en la carga.
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

    // El rechazo no deja nada trabado: un pedido que vale cambia de escena.
    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);
}

// El aviso es uno por pedido seguido: si la escena deja de pedir un paso y vuelve a pedir lo mismo,
// o si la escena que sigue pide lo que se le rechazó a la anterior, el log lo dice otra vez.
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

// Sin escena (antes de Start, o después de un Enter que falló) no hay de dónde ir: un pedido no
// arma ninguna escena, no cambia nada y no avisa.
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

// Start es la primera escena: con una ya entrada es un error, y la de ahora sigue, sin Leave ni
// otra fábrica (si no, la de ahora se destruiría sin soltar sus texturas). Para cambiar está
// Request.
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

// Después de un Start que falló, otro empieza de cero: si entra, ya no queda el error de antes.
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

// Request, desde afuera de las escenas (la red de la CL6 y la CL9, con el F1 02 de 0x4c2710), sigue
// la misma tabla.
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

// Un Enter que falla (una textura que falta) corta: el error queda en Failure, para que main.cpp
// cierre, y en el log; la escena que falló suelta lo que alcanzó a subir (su Leave), y ya no hay
// escena que pase ni que dibuje.
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

// Una escena que corta en su dibujo (el juego en una etapa que MuRet no tiene, IScene::Failure): el
// error queda en Failure y en el log una vez, el frame cortado no se presenta, retiene y nada vuelve a
// correr; la escena sigue siendo la de ahora y hace su Leave al cerrar.
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

// Lo mismo en el paso: el pedido de otra escena de ese paso no se mira.
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

// Lo de 0x637700 detrás del paso (afterSceneStep) corre después de un paso que salió bien, antes del pedido de la
// escena; su error corta como el de un paso, y un paso que falló no lo corre.
TEST(SceneManagerTest, AfterTheStepRunsBeforeTheRequestAndItsFailureStopsTheScene)
{
    Harness tested;
    ASSERT_TRUE(tested.manager.Start(SceneId::Loading).HasValue());
    tested.stage.next[SceneId::Loading] = SceneId::Login;
    tested.manager.Step(tested.input);
    // Después de "step Loading" y antes del Leave de la carga.
    ASSERT_EQ(tested.afterSteps.size(), 1u);
    EXPECT_EQ(tested.stage.journal[tested.afterSteps[0] - 1], "step Loading");
    EXPECT_EQ(tested.manager.Current(), SceneId::Login);

    tested.afterStepFailure = "0x45bb20: falta algo";
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.manager.Failure(), std::optional<std::string>("0x45bb20: falta algo"));
    tested.manager.Step(tested.input);
    EXPECT_EQ(tested.afterSteps.size(), 2u);
}

// Un error de Enter no corta la sesión independiente mientras el host decide reintentar.
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

// Como 0x63c150 al final de 0x622e40: los seis slots de la carga se sueltan antes de que suban los
// 18 del login (Leave antes que el Enter que sigue).
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

// Al cerrar, la escena de ahora suelta lo suyo, como al cambiar.
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

// 4 a 4 (0x4c363f, con el F1 02 en la selección): se sale y se entra a una escena nueva, que sube
// todo otra vez, como el original con 0x7d3a13f en 0 (0x4c38eb).
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

// La red corre en cada Poll, también antes de Start y con la ventana minimizada, que no dibuja:
// el GameLoop llama a Poll una vez por frame (CL4.1), como 0x4e1d50 en cada vuelta del bucle.
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

// Cada Render es un frame: BeginFrame con el reloj, el Render de la escena con el frame abierto y
// Present.
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
// P-C61: el fin de cada vuelta corre afterSceneFrame con el reloj, con el frame dibujado o retenido (el original
// corre el frame sin presentarlo), y no sin escena ni con un error.
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

// Una escena que pide cerrar el juego (el OK del aviso 0x70 del login, Q-C69): el SceneManager lo
// dice, para que main.cpp corte el bucle con EXIT_SUCCESS. Sin escena, o con una que no lo pide,
// no.
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

// Una escena con un campo de texto abierto (la cuenta del login): el SceneManager lo dice, para que
// main.cpp prenda el texto tecleado de SDL solo mientras tanto. Sin escena, o con una que no lo
// pide, no.
TEST(SceneManagerTest, SaysWhenTheSceneWantsTextInput)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.WantsTextInput());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.WantsTextInput());

    tested.stage.typing.insert(SceneId::Loading);

    EXPECT_TRUE(tested.manager.WantsTextInput());
}

// Un paso que entró a otra escena lo dice una vez, para que el GameLoop no cuente lo que tardó la entrada
// (GameLoop.h). Un paso sin cambio, o el Start, no.
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

// Una escena que retiene el último frame (el login mientras conecta, Q-C85): el SceneManager lo
// dice, para que el GameLoop no dibuje y en la pantalla quede el frame anterior. Sin escena, o con
// una que no lo pide, no.
TEST(SceneManagerTest, SaysWhenTheSceneHoldsTheLastFrame)
{
    Harness tested;
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());
    ASSERT_TRUE(tested.manager.Start(FirstScene).HasValue());
    EXPECT_FALSE(tested.manager.HoldsTheLastFrame());

    tested.stage.holding.insert(SceneId::Loading);

    EXPECT_TRUE(tested.manager.HoldsTheLastFrame());
}
// La consulta que impide el Render también avisa a la escena, incluso sin un Step intermedio.
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

// 0x6371a0 lee lo que decide al empezar cada frame: PrepareFrame corre una vez por frame, la primera
// vez que se pregunta, y preguntar otra vez o dibujar no lo vuelve a correr. Poll y Step empiezan
// otro frame, y Render lo termina.
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

// Recursos completos para los dos tests de orquestación que ahora atraviesan productores.
// Su mapa0 con pool vacío aisla el gate del dibujo; el caso inicial54 real vive en test_game_scene_wait.
struct ManagerGameResources
{
    Expected<void, std::string> Load(Client::Scenes::WorldState& world, Client::Render::Models::ClothWorld& cloth,
                                     FakeTextureStore& textures, Log& log)
    {
        auto loadedData = Client::Scenes::LoadGameData(dataRoot);
        if (!loadedData.HasValue())
            return Unexpected(loadedData.Error());
        data = std::move(loadedData).Value();
        if (auto loadedModels = models.Load(dataRoot, textures, log); !loadedModels.HasValue())
            return loadedModels;
        world.Runtime().map = 0; // El caller escribe el mundo antes de 61a560, que conserva el observado.
        return Client::Scenes::LoadMap(0, {dataRoot, world, SceneId::Game, 0, cloth.random});
    }
    std::filesystem::path dataRoot = Client::Tests::GoldenDir();
    Client::Scenes::GameData data;
    Client::Scenes::GameModels models;
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

// 06, entrada pasos2..6: el mosaico se retiene con contador>30. Este fixture de orquestación
// ya presta un mapa real: al publicar30 el frame completo se presenta, sin un error de recursos.
TEST(SceneManagerTest, TheMosaicStaysUntilTheCounterIsThirty)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    FakeClock clock;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::WorldState world;
    ManagerGameResources gameResources;
    const auto loaded = gameResources.Load(world, cloth, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    // 0x633f30: la selección ya marcó el mundo cargado.
    world.Runtime().drawingEnabled = true;
    const Client::Scenes::SceneContext context{
        .dataRoot = gameResources.dataRoot, .textures = textures, .log = log, .cloth = &cloth, .world = &world};
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
    // El dibujo de un frame del GameLoop, que puede no tener pasos: la red y el dibujo, si no retiene.
    const auto draw = [&]()
    {
        manager.Poll();
        if (!manager.HoldsTheLastFrame())
        {
            manager.Render(0.0f);
        }
    };
    ASSERT_TRUE(manager.Start(SceneId::WorldLoading).HasValue());
    EXPECT_EQ(world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
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

// El F1 02 con 1 escribe la escena 4 mientras se lee la red (0x4c363f): el Poll toma la orden y el paso que
// sigue ya es el de la selección, que entra antes de su paso. La orden se toma una vez.
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

// Q-C79: el original cambiaría de escena también en la carga; MuRet sigue la tabla, y la orden desde ahí es
// un error en el log y la escena da su paso.
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

// Un Poll que falla no toma la orden: el error es terminal y no hay otro paso.
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

// La selección de la vuelta, sin la lista ni el 54: al entrar marca el mundo cargado (lo que hace 0x633f30 con
// la lista) y, cuando el test elige, pasa a la escena 3 con PrepareSelectedActor, que baja la bandera de la
// carga del mundo (0x633efe).
class ChoosingSelection final : public IScene
{
public:
    ChoosingSelection(Client::Scenes::WorldState& world, Client::Render::Models::CrtRandom& random, const bool& choose)
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
            m_world.PrepareSelectedActor(Client::Protocol::Character::CharacterEntry{}, m_random);
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
    Client::Scenes::WorldState& m_world;
    Client::Render::Models::CrtRandom& m_random;
    const bool& m_choose;
    bool m_chosen = false;
};

// Vuelta a selección y nueva carga: conserva los cambios de escena y la segunda espera.
// La selección visual es un doble explícito; al final ApplyWorldEntry real carga el mapa/actor
// antes de permitir el nuevo frame. No se usa un fallo por terreno ausente como final esperado.
TEST(SceneManagerTest, GameSelectionGameHoldsAndReloadsAgain)
{
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    FakeClock clock;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    ManagerGameResources gameResources;
    const auto loaded = gameResources.Load(world, cloth, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    // 0x633f30: la selección ya marcó el mundo cargado.
    world.Runtime().drawingEnabled = true;
    const Client::Scenes::SceneContext context{.dataRoot = gameResources.dataRoot,
                                               .textures = textures,
                                               .log = log,
                                               .clock = &clock,
                                               .cloth = &cloth,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .selection = &selection,
                                               .world = &world};
    const Client::Scenes::WorldEntryResources resources{.world = world,
                                                        .selection = selection,
                                                        .dataRoot = gameResources.dataRoot,
                                                        .cloth = cloth,
                                                        .camera = camera,
                                                        .cameraInput = cameraInput,
                                                        .models = gameResources.models,
                                                        .items = &gameResources.data.items->table,
                                                        .itemSetTables = &*gameResources.data.itemSets,
                                                        .log = log};
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
                Client::Scenes::ApplyReturnToSelection(resources);
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
    EXPECT_EQ(world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
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

    Client::Protocol::Character::CharacterSelectResult reply;
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
