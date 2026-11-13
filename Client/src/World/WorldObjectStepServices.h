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
// 0x52f360 con otro objeto que uno del mundo (el 0xf1 desde 0x505425): lo que lee del objeto, los ángulos (+0x1c), la
// posición (+0x10) y la luz (+0xf4, la del humo de las clases 1 y 2). Solo usa worldClock, dynamicLighting y particles
// de `globals`.
[[nodiscard]] Core::Expected<void, std::string>
EmitObjectFire(std::int32_t fireClass, const glm::vec3& angles, const glm::vec3& position, const glm::vec3& light,
               const glm::vec3& offset, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

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

// 0x43ffc0 (0x605a62), con el mapa 31 (0x43ff10): después de 0x438a90 y 0x42c850, que en el 31 no hacen nada.
// El 1, el 0x2c y el 0x2d sueltan la mariposa 0xae /0, /1 y /2 con timeGetTime() & 0x3ff debajo de 10; el
// 0x1b sube y baja la z con el reloj del mundo y su fase (+0x84); el 0x2a suma a la luz dinámica
// ((rand() & 3) + 3) · 0,1f por (0,9; 0,2; 0,1), radio 3; el 1, el 3, el 0x2a, el 0x2c, el 0x2d y el 0x35
// quedan con +0x58 en −2. Con cualquier tipo, el ambiente 0x1ef si pasaron más de 300.000 ms del sistema
// desde el anterior (0x723c4c). Un binding ausente corta donde lo pide el ASM, después de lo ya hecho.
[[nodiscard]] Core::Expected<void, std::string>
StepLandOfTrialsObject(WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

// 0x4303e0 (0x605a62), con el mapa 34 (0x4303c0): después de 0x438a90, 0x42c850, 0x43ffc0, 0x42ee40 y 0x4250f0, que
// en el 34 no hacen nada. El 0x29 suma a la luz dinámica (0,2; 0,7; 0,5) con radio 2; el 0x39 y el 0x47,
// ((rand() & 3) + 3) · 0,1f por (1; 0,6; 0,2) con radio 3; el 0x51 queda con alpha 0,2 y el 0x52, el 0x53 y el 0x54
// con +0x58 en −2. Sin sonido. Sin el buffer dinámico, la luz corta después de su rand().
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfObject(WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);
} // namespace Client::World
