#pragma once

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstdint>
#include <map>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::Scenes
{
// Globales de WinMain/639470/609640; sobreviven a las cargas de mapas y a las escenas.
struct GameItemRenderState
{
    std::array<std::int32_t, 100> floorAngles{}; // 05763360, 4bada2..4bade0.
    bool initialized = false;
    float glowPulse = 0;                                // 07d36ba8, escrito por639482 antes del frame.
    std::map<std::uint16_t, std::int32_t> selections{}; // +c0 de cada BMD.
    glm::vec2 specialPulse{0.5f, 0.01f};                // 006c9de4/006c9de8, distinto del pulso de639470.
};
void InitializeGameItemRenderState(GameItemRenderState& state, Render::Models::CrtRandom& random);
void UpdateGameItemGlowPulse(GameItemRenderState& state, float worldClock);
} // namespace Client::Scenes
