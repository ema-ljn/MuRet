#include "Scenes/TestScene.h"

#include "Input/InputState.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace Client::Scenes
{
namespace
{
// El ciclo de DAT_07d2d360: (n + 1) mod 32 en cada paso (02, "Qué corre por paso").
constexpr std::uint32_t CycleSteps = 32;
// Los dos extremos del fondo: un azul oscuro al empezar el ciclo y uno claro a la mitad. Son de
// MuRet, elegidos para que el ciclo se vea.
constexpr TestScene::Color Dark{0.08f, 0.10f, 0.18f};
constexpr TestScene::Color Light{0.20f, 0.35f, 0.60f};
} // namespace

TestScene::TestScene(std::function<void(const Color&)> present) : m_present(std::move(present)) {}

void TestScene::Poll() {}

void TestScene::Step(const Input::InputState& /*input*/)
{
    m_cycle = (m_cycle + 1) % CycleSteps;
}

void TestScene::Render(float alpha)
{
    m_present(Background(alpha));
}

bool TestScene::HoldsTheLastFrame() const
{
    return false;
}

bool TestScene::TakeTheSceneChange()
{
    return false;
}

TestScene::Color TestScene::Background(float alpha) const
{
    // La posición en el ciclo, en [0, 1), y una rampa que sube y baja: en 32 vuelve a 0 sin salto,
    // así que la interpolación es continua también ahí. std::lerp da justo los extremos con 0 y 1.
    const float position = (static_cast<float>(m_cycle) + alpha) / static_cast<float>(CycleSteps);
    const float ramp = position < 0.5f ? 2.0f * position : 2.0f - 2.0f * position;
    return {std::lerp(Dark.red, Light.red, ramp), std::lerp(Dark.green, Light.green, ramp),
            std::lerp(Dark.blue, Light.blue, ramp)};
}
} // namespace Client::Scenes
