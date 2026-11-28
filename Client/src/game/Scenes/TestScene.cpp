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
// The cycle of DAT_07d2d360: (n + 1) mod 32 on each step (02, "Qué corre por paso").
constexpr std::uint32_t CycleSteps = 32;
// The background's two ends: a dark blue at the cycle's start and a light one at the middle. They are MuRet's,
// chosen so the cycle shows.
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
    // The position in the cycle, in [0, 1), and a ramp that goes up and down: at 32 it returns to 0 without a jump, so
    // the interpolation is continuous there too. std::lerp gives exactly the ends with 0 and 1.
    const float position = (static_cast<float>(m_cycle) + alpha) / static_cast<float>(CycleSteps);
    const float ramp = position < 0.5f ? 2.0f * position : 2.0f - 2.0f * position;
    return {std::lerp(Dark.red, Light.red, ramp), std::lerp(Dark.green, Light.green, ramp),
            std::lerp(Dark.blue, Light.blue, ramp)};
}
} // namespace Client::Scenes
