#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los casos de 0x604ab0 del Dungeon (mapa 1, la tabla 0x605bc0), de Lost Tower (mapa 4, la tabla 0x605c50) y
// de la Arena (mapa 6), después del alpha y la animación comunes. Contrato: 06 § Los objetos del Dungeon y de
// Lost Tower, y § Los objetos de la Arena.
namespace Client::World
{
struct ObjectStepGlobals;
struct WorldObject;

// El mapa 1: el 0x16, el 0x17 y el 0x18 ponen el model+0x88 de su tipo en 1 y su V en el resto firmado de
// ftol(reloj) por 1.000 por −0,001f (0x6ba1a4, sin negar el ftol); el 0x27, el 0x28 y el 0x33 se ocultan
// (+0x58 = −2); el 0x29 y el 0x2a son fuegos de clase 0 de 0x52f360 a (0; −30; 240) y (0; 0; 190); el 0x34
// suelta la piedra 0xda con rand() % 3 == 0 y siempre se oculta. Un destino prestado ausente corta con su
// dirección después de lo que ya escribió.
[[nodiscard]] Core::Expected<void, std::string>
StepDungeonObject(WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

// El mapa 4: el 3 y el 4 corren la U como el agua del 0 (−ftol % 1.000 por 0,001f); el 0x12 y el 0x17
// llevan el blendMesh a 1, y el 0x13 y el 0x14 a 4 con la U que corre; el 0x18 se oculta y con rand() & 63
// en 0 suelta el chorro 0x5de; el 0x19 se oculta, y el 0x26 y el 0x27 van por la patada de 0x52f550.
[[nodiscard]] Core::Expected<void, std::string>
StepLostTowerObject(WorldObject& object, Render::Models::CrtRandom& random, const ObjectStepGlobals& globals);

// El mapa 6 (el caso 6 de 0x604ab0, 0x6054fd, sin tabla): el 0x15 lleva el blendMesh a 3 y corre la V como la
// U del 3 y el 4 del mapa 4 (−ftol % 1.000 por 0,001f); el 0x26 se oculta. Sin rand() ni destinos prestados.
[[nodiscard]] Core::Expected<void, std::string> StepArenaObject(WorldObject& object, const ObjectStepGlobals& globals);

// 0x52f550: con la acción del héroe entre 0xd y 0x21 y la velocidad x del objeto por debajo de 0,1 (o NaN),
// a menos de 50 del héroe (la distancia con la y guardada en float en uno de los factores), la velocidad
// (dx · −0,4; dy · −0,4; 0), el giro (dy · −4; dx · −4) en +0x28/+0x2c y el sonido 0x7c. Siempre después:
// velocidad y giro por 0,6, y sumados a la posición y a los ángulos (la x con el producto sin redondear).
[[nodiscard]] Core::Expected<void, std::string> KickObject(WorldObject& object, const ObjectStepGlobals& globals);
} // namespace Client::World
