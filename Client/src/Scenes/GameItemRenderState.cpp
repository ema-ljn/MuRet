#include "Scenes/GameItemRenderState.h"

#include "Render/Models/Cloth.h"

#include <cmath>

namespace Client::Scenes
{
void InitializeGameItemRenderState(GameItemRenderState& state, Render::Models::CrtRandom& random)
{
    if (state.initialized)
        return;
    // 4badc6..4badd9: cien llamadas, una por entrada, antes de construir los actores.
    constexpr int FullTurn = 360;
    for (auto& angle : state.floorAngles)
        angle = random.Next() % FullTurn;
    state.initialized = true;
}

void UpdateGameItemGlowPulse(GameItemRenderState& state, float worldClock)
{
    // Literales float de6b89e8/6b8ae8/6b8a20;63948e/639499 guardan sin sacar ST0.
    constexpr float Frequency = 0.004f;
    constexpr float Amplitude = 0.15f;
    constexpr float Base = 0.6f;
    state.glowPulse = static_cast<float>(std::sin(static_cast<double>(worldClock) * Frequency) * Amplitude + Base);
}
} // namespace Client::Scenes
