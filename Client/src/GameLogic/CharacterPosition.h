#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::GameLogic
{
struct CharacterPositionState
{
    glm::vec3& position;
    glm::vec3& angles;
    float& phase;
    const std::uint16_t& type;
    const std::uint16_t& item;
    const std::uint8_t& safeZone;
    const std::int32_t& map;
};
// 0x508a80. Miembros ordinarios disjuntos; callbacks síncronos no vacíos. El servicio de velocidad
// recibe el actor vivo por captura; terreno observa también Z provisional. No copia el actor.
void UpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                             const std::function<double(float, float)>& height);
} // namespace Client::GameLogic
