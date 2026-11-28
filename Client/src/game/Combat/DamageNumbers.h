#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Combat
{
// 0x548e40: the first free number of the 100 at 0x7609bd0 (0x78 each) is lit with the number, the position with z +
// 140, the light, the scale, speed 10 (+0x50), the counter at 0 (+0x3c) and whether it rises (+0x4c). It draws no
// rand() and calls nothing; with all 100 alive, it does nothing. Used by the D7 (0x4caae0), the 0x16 and the 0x9C
// (0x4cf200, 0x4cfce0).
void CreateDamageNumber(Kernel::WorldState& world, const glm::vec3& position, std::int32_t amount,
                        const glm::vec3& light, float scale, bool rise);
} // namespace Client::Combat
