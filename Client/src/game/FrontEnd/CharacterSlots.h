#pragma once

#include <glm/ext/vector_float2.hpp>

#include <array>

// The five selection slots, as 0x4c20a0 sets them (06 § Los personajes de la lista): the position in the world and
// the angle in z. The z is 0x634680's, which sets it each frame.
namespace Client::FrontEnd
{
struct CharacterSlot
{
    glm::vec2 position{};
    float angle = 0.0f; // grados, alrededor de z
};

inline constexpr std::array<CharacterSlot, 5> CharacterSlots = {{
    {{12880.0f, 15046.0f}, 100.0f},
    {{12863.0f, 15251.0f}, 75.0f},
    {{12974.0f, 15425.0f}, 50.0f},
    {{13151.0f, 15533.0f}, 25.0f},
    {{13349.6f, 15527.0f}, 0.0f},
}};

inline constexpr float CharacterSlotHeight = 163.0f;
} // namespace Client::FrontEnd
