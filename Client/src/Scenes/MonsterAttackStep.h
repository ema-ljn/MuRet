#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

// CL9 hito 3, fase 2e-B: los casos de los monstruos de 0x4f5b70, lo que corre en cada paso de +0x381 después de
// 0x4f97b0. 06, "El paso del ataque" y "Los casos de los monstruos (0x4f5b70)".
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 0x4f5b70 después de su rand() de 0x4f5b97 (que saca StepTheAttack): los seis ayudantes de los mapas (0x43a7a0 en
// Kalima, 0x4436f0 en el 31, 0x42dcf0 en el 30, 0x429110 en el 33 y el 54, 0x42fce0 en el 35 y 0x4324c0 en Crywolf),
// que pueden hacer salir (AL en 1, 0x4f903c); el switch por +0x376 (0x4f5c7e) y la cola con el blanco de +0x3a4 en
// [0, 400): +0x38e con 0x11 (el switch de 0x4f8147) o con 3 (el de 0x4f8a56). Lo portado escribe en el monstruo
// (+0x78, +0x381, +0x17c), pide los sonidos (CL10) y crea las partículas 0x607/0 y 0x5e4 del +0x376 0x2d. Corta, con
// lo que hizo antes y sus rand(), en los efectos (0x518cf0), joints (0x530860) y partículas sin portar (CL11), en la
// flecha (0x53e2b0, la fase de la flecha), en el 0x4ec110 con el offset sin escribir de 0x432acc (Q-C1050) y en la
// habilidad 3 sin caso (0x4f8a39, hito 4). `slot` es el lugar de `actor` en el pool.
[[nodiscard]] Core::Expected<void, std::string> StepTheAttackCases(ActorState& actor, std::size_t slot,
                                                                   const CharacterStepContext& context);
} // namespace Client::Scenes
