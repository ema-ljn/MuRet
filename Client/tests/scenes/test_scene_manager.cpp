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
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTransitions.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
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
    // La escena cuyo Enter falla.
    std::optional<SceneId> failingEnter;
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
    }

    void Render(SceneCanvas& canvas, float alpha) override
    {
        Note("render");
        m_stage.draws.push_back(SceneDraw{
            .scene = m_id, .clock = canvas.frameClock, .alpha = alpha, .frameOpen = m_stage.target.FrameOpen()});
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

    [[nodiscard]] bool HoldsTheLastFrame() const override
    {
        return m_stage.holding.contains(m_id);
    }

private:
    void Note(const char* what)
    {
        m_stage.journal.push_back(std::string(what) + " " + std::string(SceneName(m_id)));
    }

    SceneId m_id;
    Stage& m_stage;
};

// El SceneManager con las escenas de prueba, el reloj falso, el lienzo que anota y un log que
// guarda. `polls` cuenta las veces que corrió pollNetwork.
struct Harness
{
    Harness() : log(sink, LogLevel::Debug), manager(Factory(), stage.target, clock, [this] { ++polls; }, log) {}

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
    SceneManager manager;
    InputState input;
};

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
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
} // namespace
