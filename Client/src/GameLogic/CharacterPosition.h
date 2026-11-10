#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::GameLogic
{
// Metadatos prestados de lvalues del tipo exacto: el wrapper rechaza temporales/conversiones.
struct CharacterPositionState
{
    glm::vec3& position;
    glm::vec3& angles;
    float& phase;
    std::reference_wrapper<const std::uint16_t> type;
    std::reference_wrapper<const std::uint16_t> item;
    std::reference_wrapper<const std::uint8_t> safeZone;
    std::reference_wrapper<const std::int32_t> map;
};
// 0x508a80. Referentes disjuntos y vivos, actor/buffers sin reubicar durante la llamada.
// Callbacks síncronos no vacíos al invocarlos; no reasignar el callback que se está ejecutando.
// Velocidad recibe el actor vivo por captura; terreno observa Z provisional. No copia el actor.
// Perfil numérico y límite std::sin/FSIN: ADR0012.
void UpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                             const std::function<double(float, float)>& height);
// Variante del adapter: sin altura conserva el prefijo XYZ/velocidad y devuelve false antes
// del store de altura y del avance de fase. El callback de velocidad sigue siendo obligatorio.
[[nodiscard]] bool TryUpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                                              const std::function<double(float, float)>& height);
} // namespace Client::GameLogic
