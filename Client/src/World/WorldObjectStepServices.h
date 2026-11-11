#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct ObjectStepGlobals;
struct TerrainLighting;
struct WorldObject;

// 0x5f9780(x, y, color, radio, 0x7b4ccc4) sobre la luz dinámica durable. Sin destino, o con un centro que no
// cabe en int32 (NaN incluido), corta con `address` sin tocar nada.
[[nodiscard]] Core::Expected<void, std::string> AddDynamicLight(TerrainLighting* destination, const glm::vec3& position,
                                                                const glm::vec3& color, std::int32_t radius,
                                                                std::string_view address);

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

// 0x4250f0 (0x605a97), con el mapa 33 o el 54 (0x425070): después de la cadena de 0x605a62, que en esos
// mapas no toca nada. La V del 0x19/0x1c, la luz dinámica del 0x1e/0x47 (un rand(), radio 2), el alpha del
// 0x29, el −2 de +0x58 de los que emiten en el dibujo (0x38..0x3b, 0x3e, 0x3f, 0x43 y 0x46) y la velocidad
// del 0x40. Siempre termina con el pedido sonoro 0x246; sin emisor corta después de los stores.
[[nodiscard]] Core::Expected<void, std::string> StepAidaObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                               const ObjectStepGlobals& globals);
} // namespace Client::World
