#include "Scenes/TestScene.h"

#include "Input/InputState.h"

#include <gtest/gtest.h>

#include <vector>

// Client::Scenes::TestScene: el ciclo de 32 pasos y el fondo que sube y baja con él.
namespace
{
using Client::Input::InputState;
using Client::Scenes::TestScene;
using Color = TestScene::Color;

// Los extremos de TestScene.cpp: el azul oscuro del principio del ciclo y el claro de la mitad.
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

    ExpectColor(halfway, Midpoint(now, next));
}

// La segunda mitad baja: a los 8 pasos (un cuarto) y a los 24 (tres cuartos) está en el medio de
// los dos extremos, a la ida y a la vuelta.
TEST(TestSceneTest, GoesUpInTheFirstHalfAndBackDownInTheSecond)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 8);
    ExpectColor(scene.Background(0.0f), Midpoint(Dark, Light));
    Steps(scene, 16);
    ExpectColor(scene.Background(0.0f), Midpoint(Dark, Light));
}

// En el paso 31, el alfa que llega a 1 da el oscuro del paso 0: el ciclo vuelve sin salto.
TEST(TestSceneTest, TheCycleWrapsWithoutAJump)
{
    TestScene scene{[](const Color&) {}};

    Steps(scene, 31);
    ExpectColor(scene.Background(1.0f), Dark);
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
// La escena de prueba no tiene red: Poll no avanza el ciclo ni presenta.
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
