#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct ObjectStepGlobals;
struct WorldObject;

// 0x52f360: clase 0 llama/luz, 1 humo/0 y 2 humo/2. El offset gira sin aplicar escala.
// Conserva el prefijo RNG y las emisiones anteriores si falta un destino prestado.
[[nodiscard]] Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const WorldObject& object,
                                                               const glm::vec3& offset,
                                                               Render::Models::CrtRandom& random,
                                                               const ObjectStepGlobals& globals);

// 0x604ee9/0x604f5c: lámparas 0x5a/0x96 del mapa 0, sobre la luz dinámica durable.
[[nodiscard]] Core::Expected<void, std::string>
StepObjectLamp(const WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

// 0x6050d4: puertas del mapa 2; cero RNG. La petición sonora sigue a los stores de apertura.
[[nodiscard]] Core::Expected<void, std::string> StepDeviasObjectDoor(WorldObject& object,
                                                                     const ObjectStepGlobals& globals);
} // namespace Client::World
