#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

// CL9 hito 4, fase 6b: los casos de habilidad del paso del ataque (0x4fd5f0) que el hito 3 dejó cortados (08, P-C258):
// el switch de la cabeza (0x4fdeeb), el primero con blanco (0x504372) y las acciones 0x38 a 0x3c (0x5042ce, 0x52e740).
// 06, "El paso del ataque". Cada caso corre hasta donde crea un efecto con modelo, un joint o una partícula que MuRet
// no tiene (la CL11), con los rand() de antes; los que crean lo que World ya sabe crear (el terremoto 0xfa, el calco
// 0x61e/1 y la flecha) siguen hasta el final.
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 0x4fdc17..0x4fdeeb y sus casos, con `skill` la lectura de +0x38e de la cabeza: 5, 8, 9, 0xa, 0xc..0xe, 0x1e..0x24,
// 0x26..0x2a, 0x2c, 0x30, 0x31, 0x35, 0x37, 0x38, 0x3f..0x41, 0x43..0x48, 0x4b y 0x4c (la tabla de bytes de 0x505530 y
// la de saltos de 0x5054b4). Los demás, como el 0, no hacen nada (0x502aff). `actor` es c y o; `slot`, su lugar en el
// pool. Todos los casos siguen en 0x502aff.
[[nodiscard]] Core::Expected<void, std::string>
StepTheHeadSkill(ActorState& actor, std::size_t slot, std::uint8_t skill, const CharacterStepContext& context);

// 0x504372 y sus casos, con `skill` la lectura de +0x38e de 0x50432a y `victim` el blanco de +0x3a4: 1, 2, 4, 7, 0xb,
// 0x10, 0x11, 0x18, 0x1a..0x1c, 0x2d, 0x2e, 0x33, 0x34, 0x3c, 0x3d, 0x49 y 0x4a (la tabla de bytes de 0x5055c8 y la de
// saltos de 0x505578). El 3 y los demás no hacen nada. Todos siguen en 0x505069.
[[nodiscard]] Core::Expected<void, std::string> StepTheTargetSkill(ActorState& actor, ActorState& victim,
                                                                   std::size_t slot, std::uint8_t skill,
                                                                   const CharacterStepContext& context);

// 0x5042ce: 0x52e740(0, blanco, blanco + 0x10, o + 0x1c), con las acciones 0x38 a 0x3c de o.
[[nodiscard]] Core::Expected<void, std::string> SparkTheTarget(const ActorState& actor, const ActorState& victim,
                                                               std::size_t slot);
} // namespace Client::Scenes
