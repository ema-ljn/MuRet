#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Scenes
{
class WorldState;
// 0x548e40: el primer número libre de los 100 de 0x7609bd0 (de a 0x78) se prende con el número, la posición con
// z + 140, la luz, la escala, la velocidad 10 (+0x50), el contador en 0 (+0x3c) y si sube (+0x4c). No saca rand() ni
// llama a nadie; con los 100 vivos, no hace nada. Lo usan el D7 (0x4caae0), el 0x16 y el 0x9C (0x4cf200, 0x4cfce0).
void CreateDamageNumber(WorldState& world, const glm::vec3& position, std::int32_t amount, const glm::vec3& light,
                        float scale, bool rise);
} // namespace Client::Scenes
