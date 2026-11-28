#pragma once

#include <glm/ext/vector_float3.hpp>
#include <cstdint>

namespace Client::WorldObjects
{
struct WorldObject;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Kalima
{
// 0x438ad0: only unsigned actions 0x0d..0x21, no CRT. The caller queries the height afterwards.
void MoveKalimaObject(WorldObjects::WorldObject& object, const glm::vec3& heroPosition, std::uint8_t heroAction);
} // namespace Client::MapScripts::Kalima
