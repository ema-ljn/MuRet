#include "Scenes/CharacterSelectScene.h"

#include "Input/InputState.h"
#include "Scenes/SceneCanvas.h"
#include "support/RecordingFrameTarget.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

// Client::Scenes::CharacterSelectScene: la escena 4 de la CL5, adonde llega el login aceptado. No
// sube nada, no dibuja nada (el frame queda en el negro con que empieza) y no pide otra: la
// selección de verdad, con el F3 00, es de la CL6.3.
namespace
{
using Client::Input::InputState;
using Client::Scenes::CharacterSelectScene;
using Client::Tests::RecordingFrameTarget;

TEST(CharacterSelectSceneTest, EntersWithNothingToLoad)
{
    CharacterSelectScene scene;

    const auto entered = scene.Enter();

    EXPECT_TRUE(entered.HasValue());
}

TEST(CharacterSelectSceneTest, DrawsNothing)
{
    CharacterSelectScene scene;
    ASSERT_TRUE(scene.Enter().HasValue());
    RecordingFrameTarget target;

    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();

    EXPECT_TRUE(target.frames.back().events.empty());
}

TEST(CharacterSelectSceneTest, StaysInTheSelection)
{
    CharacterSelectScene scene;
    ASSERT_TRUE(scene.Enter().HasValue());
    const InputState input;

    for (int step = 0; step < 100; ++step)
    {
        scene.Step(input);
    }
    scene.Leave();

    EXPECT_EQ(scene.Next(), std::nullopt);
}
} // namespace
