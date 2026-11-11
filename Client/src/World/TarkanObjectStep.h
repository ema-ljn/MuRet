#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Los casos de 0x604ab0 de Tarkan (mapa 8, la tabla 0x605cc0 por el tipo − 2), después del alpha y la
// animación comunes. Contrato: 06 § Los objetos de Tarkan.
namespace Client::World
{
struct ObjectModelStepState;
struct ObjectStepGlobals;
struct WorldObject;

// 0x61a145..0x61a18f, al final de la carga de los modelos del mundo (0x616ec0): en el mapa 8, el model+0x88
// de 11, 12, 13, 73, 75 y 79 en 0, con que su malla 0 va sin luz y con la U y la V de su paso. Los demás
// mapas no la tocan.
void SeedTarkanModelState(std::uint8_t map, ObjectModelStepState& state);

// 0x604c70..0x604c8e: en el mapa 8, el tipo 8 anima con la velocidad (+0xd8) por 4 (0x6b8a44) en una copia;
// el +0xd8 no cambia. Los demás, con la suya.
[[nodiscard]] float TarkanAnimationSpeed(std::int16_t type, float speed);

// El mapa 8, con el reloj del mundo T y L = sin(T · 0,002f) · 0,35f + 0,65f (el seno en la x87):
// - el 2: blendMesh 0 y la U en (−ftol(T) % 1.000) · 0,001f;
// - el 4: blendMesh 0, la luz del blendMesh en L, la V en (−ftol(T) % 10.000) · 0,0001f y una luz dinámica
//   (L; L; L) de radio 3;
// - el 7: blendMesh 0 y la luz del blendMesh en L con T + ángulo z · 100, y una luz (L; L · 0,6f; L · 0,2f) de
//   radio 3;
// - el 0xb, el 0xd, el 0x49, el 0x4b y el 0x4f: la V en (−ftol(T) % 10.000) · 0,0002f; el 0x48, además, el
//   blendMesh en 0;
// - el 0xc: la U y la V en (−ftol(T) % 50.000) · 0,00005f;
// - el 0x3d, el 0x41 y el 0x42: blendMesh 1, la V en (−ftol(T) % 1.000) · 0,001f y una luz
//   (L; L · 0,6f; L · 0,2f) de radio 2, sin tocar la del blendMesh;
// - el 0x3f y el 0x40: +0x58 = −2;
// - el 0x52: blendMesh 0 y la luz del objeto (+0xf4) en (1; 1; 1).
// Un reloj sin ftol acreditado o un destino de luz ausente corta con su dirección después de lo que ya
// escribió.
[[nodiscard]] Core::Expected<void, std::string> StepTarkanObject(WorldObject& object, const ObjectStepGlobals& globals);
} // namespace Client::World
