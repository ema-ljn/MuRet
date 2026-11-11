#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Lo que 0x604ab0 hace con los objetos de Devil Square (mapa 9) y de los Blood Castle (mapas 11 a 17), fuera de
// lo común. Contrato: 06 § Los objetos de Devil Square y de Blood Castle.
namespace Client::World
{
class Particles;
struct ObjectStepGlobals;
struct WorldObject;

// 0x604ac0..0x604ba0, al principio de 0x604ab0 con el mapa 9, antes de los techos y del alpha, con cada objeto
// que llega: si ftol(reloj) % 4.000 (con signo) queda por debajo de 1.000 y rand() % 100 da 0, una luz dinámica
// de radio 12 con L = (rand() % 12 + 4) · 0,1f y el color (L · 0,2f; L · 0,3f; L · 0,5), en (x del héroe +
// rand() % 1.200 − 600; y del héroe + rand() % 1.200 − 600), la y antes que la x. Después, siempre, el sonido 1
// en bucle (0x41ed10(1, 0, 1)). Un reloj sin ftol acreditado, el héroe, la luz o el sonido ausentes cortan con su
// dirección después de lo ya hecho.
[[nodiscard]] Core::Expected<void, std::string> StepDevilSquarePrelude(Render::Models::CrtRandom& random,
                                                                       const ObjectStepGlobals& globals);

// 0x6062e6..0x6063c3, la parte de los mapas 11 a 17 de 0x6061c0, una vez por paso y antes de mirar si hay
// objetos vistos: un punto en (x del héroe + rand() % 900 − 300; y del héroe + rand() % 900 − 300; z del héroe +
// rand() % 50 + 250), cada suma en la x87 y guardada en float, y con rand() & 3 en 0 la órbita 0x60f/3 ahí, con
// ángulos en 0, luz (1; 1; 1) y escala 0,19f. Sin el pool de partículas, o si la órbita falla, corta en
// 0x6063be después de los rand().
[[nodiscard]] Core::Expected<void, std::string> StepBloodCastlePrelude(Render::Models::CrtRandom& random,
                                                                       const glm::vec3& hero, Particles* particles,
                                                                       const float& worldClock);

// 0x605991, en los mapas 11 a 17 después del alpha y la animación: el 9 y el 10 quedan con +0x58 en −2, salvo
// con +0x8a en 4 (la puerta que abrió 0x5ff960). Los demás tipos, nada.
void StepBloodCastleObject(WorldObject& object);
} // namespace Client::World
