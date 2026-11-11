#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
// Los objetos de Castle Siege (el mapa 30) en el paso, fuera del dibujo (06 § Los objetos de Castle Siege). Lo
// que depende del asedio prendido (el byte 0x7239ec que lee 0x4294b0, que solo escriben los paquetes de 0x4294c0,
// CL9.5) corta con su dirección; apagado, que es como arranca el original, va entero.
namespace Client::World
{
struct ObjectStepGlobals;
struct WorldObject;

// 0x63741d..0x637466, al principio del dibujo del mundo en el 30: el héroe en x de 16.100 a 19.000 y en y de
// 18.900 a 21.700 (0x6ba704, 0x6ba700, 0x6ba6fc y 0x6ba6f8, con los bordes y sin NaN) prende la niebla negra
// de 0x42b0a0; afuera, glDisable(GL_FOG).
[[nodiscard]] bool InTheCastleSiegeFog(const glm::vec3& hero);

// Lo que 0x42b540 lee afuera: el byte del asedio, la hora de la música (0x7239f0), timeGetTime() (0x6b8544) y
// el pedido sonoro 0x41ed10(id, 0, 0).
struct CastleSiegePrelude
{
    bool siegeActive = false;
    float* musicTime = nullptr;
    std::function<std::uint32_t()> systemTime{};
    std::function<void(std::int32_t)> sound{};
};

// 0x42b540, el tercer eslabón de 0x6061c0, una vez por paso en el 30, antes de mirar si hay objetos vistos.
// Con timeGetTime() en T, si (T − 0x7239f0) · 0,001 pasa de 1.800 (más de media hora) o 0x7239f0 es 0, guarda
// T en float en 0x7239f0 y elige la música: sin asedio deja charge.mp3 y toca castle.mp3 (0x42b5c3; MuRet todavía
// no tiene audio, CL10). Después, siempre, el sonido 0x1e2. Sin asedio termina ahí, sin rand(), y 0x6061c0 no
// sigue. Con el asedio (los rand() de los sonidos 0x1e3 a 0x1e7, las piedras 0x12b/0x12c y los 0xdb sobre el
// héroe), corta en 0x42b5f8 después del sonido. Un binding ausente corta donde lo pide el ASM.
[[nodiscard]] Core::Expected<void, std::string> StepCastleSiegePrelude(const CastleSiegePrelude& prelude);

// 0x42c850 (0x605a62, después de 0x438a90, que en el 30 no hace nada), con un objeto visto del 30 que pasó el
// alpha y la animación. Corta la cadena con todos los tipos. Sin asedio:
// - el 0: una luz dinámica de radio 3, L = ((rand() & 3) + 3) · 0,1f y el color (L; L · 0,6f; L · 0,2f), y
//   +0x58 en −2;
// - el 4: +0x58 en −1 y la U en T · 0,0001f;
// - el 0x2a, el 0x34, el 0x35, el 0x36 y el 0x42: +0x58 en −2;
// - el 0x19f (el lancero de 0x42a760): +0x58 en −1.
// Con el asedio, el 0 queda con −2 sin luz, el 0x35 suelta la luz del 0, el 0x19f queda en −2 y el 0x42 corta
// en 0x42c977 (lee 0x7239e4, CL9.5). Sin el byte del asedio en `globals`, corta antes de tocar el objeto.
[[nodiscard]] Core::Expected<void, std::string>
StepCastleSiegeObject(WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

// 0x42c500 (desde 0x6063d0, con 0x4055b0 en 0 fuera de Chaos Castle) con cada objeto visto del 30, después de
// 0x604ab0. El 0x51 y el 0x53: blendMesh 1, la luz del blendMesh en 1 y la V en T · 0,0002f (el 0x51) o en
// T · −0,0004f (el 0x53). Del 0x3d al 0x40, sin asedio, nada; con él corta en 0x42c596 (las puertas de +0x8,
// 0x518cf0 y el sonido 0x1ec, CL9.5). Después, siempre, 0x42b480 (ApplyCastleSiegeObjectState).
[[nodiscard]] Core::Expected<void, std::string> StepCastleSiegeSeenObject(WorldObject& object, bool siegeActive,
                                                                          float worldClock);
} // namespace Client::World
