#include "Scenes/TestScene.h"

#include "Input/InputState.h"

#include <gtest/gtest.h>

#include <vector>

// Client::Scenes::TestScene: el ciclo de 32 pasos y el fondo que sube y baja con él.
namespace
{
using Client::Input::InputState;
using Client::Scenes::Color;
using Client::Scenes::TestScene;

// Los extremos de TestScene.cpp: el azul oscuro del principio del ciclo y el claro de la mitad.
constexpr Color Dark{0.08f, 0.10f, 0.18f};
constexpr Color Light{0.20f, 0.35f, 0.60f};

void ExpectColor(const Color& actual, const Color& expected)
{
    EXPECT_FLOAT_EQ(actual.red, expected.red);
    EXPECT_FLOAT_EQ(actual.green, expected.green);
    EXPECT_FLOAT_EQ(actual.blue, expected.blue);
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

// A los 16 pasos, la mitad del ciclo, está en el claro; a los 32 vuelve al oscuro.
TEST(TestSceneTest, IsLightHalfwayAndDarkAgainAfterACycle)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Light);
    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Dark);
}

// El alfa interpola entre el paso actual y el próximo: con 0,5 entre el paso 3 y el 4 queda en el
// medio de los dos.
TEST(TestSceneTest, TheAlphaIsBetweenThisStepAndTheNext)
{
    TestScene scene{[](const Color&) {}};
    Steps(scene, 3);
    const Color now = scene.Background(0.0f);
    const Color halfway = scene.Background(0.5f);
    Steps(scene, 1);
    const Color next = scene.Background(0.0f);

    ExpectColor(halfway, Color{(now.red + next.red) / 2, (now.green + next.green) / 2, (now.blue + next.blue) / 2});
}

// Render presenta el fondo de su alfa, una vez por llamada.
TEST(TestSceneTest, RenderPresentsTheBackground)
{
    std::vector<Color> presented;
    TestScene scene{[&presented](const Color& color) { presented.push_back(color); }};
    Steps(scene, 5);

    scene.Render(0.25f);

    ASSERT_EQ(presented.size(), 1u);
    ExpectColor(presented[0], scene.Background(0.25f));
}
} // namespace
