#include "Scenes/GameScene.h"

#include "App/SessionState.h"
#include "Input/InputState.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"

#include "support/AppPrinters.h"
#include "support/FakeTextureStore.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

// Client::Scenes::GameScene: la escena 5 mínima, que manda el F3 03 en su primer paso (0x636290).
namespace
{
using Client::App::SessionState;
using Client::Scenes::GameScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Tests::FakeTextureStore;
using Client::Tests::LoggedInSession;

// Con el login, la lista y el lugar 3 elegido: la sesión espera la entrada.
void ChooseSlotThree(LoggedInSession& online)
{
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}, {3, "char0002"}});
    online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
}

TEST(GameSceneTest, TheFirstStepSendsF303)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    FakeTextureStore store;
    GameScene scene(SceneContext{{}, store, online.log}, &online.session);
    ASSERT_TRUE(scene.Enter().HasValue());
    const Client::Input::InputState input;

    scene.Step(input);
    scene.Step(input);

    EXPECT_EQ(online.session.State(), SessionState::EnteringWorld);
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("se ignora la entrada al mundo") != std::string::npos; }));
}

// Q-C70: en el juego (el modo 5), el FD_CLOSE no abre un aviso y el juego sigue sin conexión: el
// texto 3 en el área de mensajes es de la CL9. La escena no vuelve al login.
TEST(GameSceneTest, AFailedSessionKeepsTheGame)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    FakeTextureStore store;
    GameScene scene(SceneContext{{}, store, online.log}, &online.session);
    const Client::Input::InputState input;
    scene.Step(input);

    online.socket.peerClosed = true;
    static_cast<void>(online.session.Poll());
    scene.Step(input);

    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(scene.Next(), std::nullopt);
    EXPECT_FALSE(scene.WantsToQuit());
}

// El original loguea el lugar y el nombre; MuRet, el lugar (el nombre es del jugador).
TEST(GameSceneTest, TheLogHasTheSlotNotTheName)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    FakeTextureStore store;
    GameScene scene(SceneContext{{}, store, online.log}, &online.session);
    const Client::Input::InputState input;

    scene.Step(input);

    EXPECT_TRUE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                    { return line.find("del lugar 3") != std::string::npos; }));
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}
} // namespace
