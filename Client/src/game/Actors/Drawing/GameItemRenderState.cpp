#include "Actors/Drawing/GameItemRenderState.h"

#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/RoundedMath.h"

namespace Client::Actors::Drawing
{
void InitializeGameItemRenderState(GameItemRenderState& state, Crt::CrtRandom& random)
{
    if (state.initialized)
        return;
    // 0x4badc6..0x4badd9: a hundred calls, one per entry, before the actors are built.
    constexpr int FullTurn = Math::FullTurn;
    for (auto& angle : state.floorAngles)
        angle = random.Next() % FullTurn;
    state.initialized = true;
}

void UpdateGameItemGlowPulse(GameItemRenderState& state, float worldClock)
{
    // Float literals of 0x6b89e8, 0x6b8ae8 and 0x6b8a20; 0x63948e and 0x639499 store without popping ST0.
    constexpr float Frequency = 0.004f;
    constexpr float Amplitude = 0.15f;
    constexpr float Base = 0.6f;
    state.glowPulse =
        static_cast<float>(Math::RoundedSin(static_cast<double>(worldClock) * Frequency) * Amplitude + Base);
}
} // namespace Client::Actors::Drawing
