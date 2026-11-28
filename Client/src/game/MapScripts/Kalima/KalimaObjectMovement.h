#pragma once

#include <glm/ext/vector_float3.hpp>
#include <cstdint>

namespace Client::WorldObjects
{
struct WorldObject;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Kalima
{
// 438ad0: sólo acciones unsigned0d..21, sin CRT. El caller consulta altura después.
void MoveKalimaObject(WorldObjects::WorldObject& object, const glm::vec3& heroPosition, std::uint8_t heroAction);
} // namespace Client::MapScripts::Kalima
