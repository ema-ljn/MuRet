#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los casos de 0x604ab0 de Atlans (mapa 7, la tabla 0x605c94 por el tipo − 0x16), después del alpha y la
// animación comunes. Contrato: 06 § Los objetos de Atlans.
namespace Client::World
{
struct ObjectStepGlobals;
struct WorldObject;

// El mapa 7, con el reloj del mundo T y el seno en la x87:
// - el 0x16: +0x58 = −2 y la fase (+0x84) más 0,1f, guardada en float; si la suma sin redondear pasa de 10,
//   vuelve a 0; con la fase guardada por encima de 5, una burbuja 0x607/0 en el objeto con su luz (+0xf4) y
//   escala 1;
// - el 0x17: blendMesh 0 y su luz en sin(T · 0,002f) · 0,3f + 0,5;
// - el 0x20 y el 0x22: blendMesh 1 y su luz en (sin(T · 0,004f) + 1) · 0,5;
// - el 0x26: blendMesh 0;
// - el 0x28: blendMesh 0, la velocidad de la animación (+0xd8) en 0,05f y la luz en sin(T · 0,004f) · 0,3f + 0,5.
// Sin el pool de partículas, o con un seno fuera del perfil PC53, corta con su dirección después de lo que ya
// escribió.
[[nodiscard]] Core::Expected<void, std::string> StepAtlansObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                                 const ObjectStepGlobals& globals);
} // namespace Client::World
