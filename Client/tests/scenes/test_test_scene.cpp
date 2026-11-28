#include "Scenes/TestScene.h"

#include "Input/InputState.h"

#include <gtest/gtest.h>

#include <vector>

// Client::Scenes::TestScene: the 32-step cycle and the background that rises and falls with it.
namespace
{
using Client::Input::InputState;
using Client::Scenes::TestScene;
using Color = TestScene::Color;

// TestScene.cpp's extremes: the dark blue at the start of the cycle and the light one at the middle.
constexpr Color Dark{0.08f, 0.10f, 0.18f};
constexpr Color Light{0.20f, 0.35f, 0.60f};

void ExpectColor(const Color& actual, const Color& expected)
{
    EXPECT_FLOAT_EQ(actual.red, expected.red);
    EXPECT_FLOAT_EQ(actual.green, expected.green);
    EXPECT_FLOAT_EQ(actual.blue, expected.blue);
}

Color Midpoint(const Color& from, const Color& to)
{
    return {(from.red + to.red) / 2, (from.green + to.green) / 2, (from.blue + to.blue) / 2};
}

void Steps(TestScene& scene, int count)
{
    const InputState input;
    for (int step = 0; step < count; ++step)
    {
        scene.Step(input);
    }
}

TEST(TestSceneTest, StartsDark)
{
    const TestScene scene{[](const Color&) {}};

    ExpectColor(scene.Background(0.0f), Dark);
}

TEST(TestSceneTest, IsLightHalfwayAndDarkAgainAfterACycle)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Light);
    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Dark);
}

TEST(TestSceneTest, TheAlphaIsBetweenThisStepAndTheNext)
{
    TestScene scene{[](const Color&) {}};
    Steps(scene, 3);
    const Color now = scene.Background(0.0f);
    const Color halfway = scene.Background(0.5f);
    Steps(scene, 1);
    const Color next = scene.Background(0.0f);

    ExpectColor(halfway, Midpoint(now, next));
}

TEST(TestSceneTest, GoesUpInTheFirstHalfAndBackDownInTheSecond)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 8);
    ExpectColor(scene.Background(0.0f), Midpoint(Dark, Light));
    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Midpoint(Dark, Light));
}

TEST(TestSceneTest, TheCycleWrapsWithoutAJump)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 31);
    ExpectColor(scene.Background(1.0f), Dark);
}

TEST(TestSceneTest, RenderPresentsTheBackground)
{
    std::vector<Color> presented;
    TestScene scene{[&presented](const Color& color) { presented.push_back(color); }};
    Steps(scene, 5);

    scene.Render(0.25f);

    ASSERT_EQ(presented.size(), 1u);
    ExpectColor(presented[0], scene.Background(0.25f));
}
// The test scene has no network.
TEST(TestSceneTest, PollDoesNothing)
{
    std::vector<Color> presented;
    TestScene scene{[&presented](const Color& color) { presented.push_back(color); }};
    Steps(scene, 5);
    const Color before = scene.Background(0.0f);

    scene.Poll();

    ExpectColor(scene.Background(0.0f), before);
    EXPECT_TRUE(presented.empty());
}
} // namespace
