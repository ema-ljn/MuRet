#pragma once

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstdint>
#include <map>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Actors::Drawing
{
// Globals of WinMain, 0x639470 and 0x609640; they survive map loads and scenes.
struct GameItemRenderState
{
    std::array<std::int32_t, 100> floorAngles{}; // 0x5763360, 0x4bada2..0x4bade0.
    bool initialized = false;
    float glowPulse = 0;                                // 0x7d36ba8, written by 0x639482 before the frame.
    std::map<std::uint16_t, std::int32_t> selections{}; // +0xc0 of each BMD.
    glm::vec2 specialPulse{0.5f, 0.01f};                // 0x6c9de4/0x6c9de8, not the pulse of 0x639470.
};
void InitializeGameItemRenderState(GameItemRenderState& state, Crt::CrtRandom& random);
void UpdateGameItemGlowPulse(GameItemRenderState& state, float worldClock);
} // namespace Client::Actors::Drawing
