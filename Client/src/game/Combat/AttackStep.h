#pragma once

#include "Core/Expected.h"
#include "Skills/AttackSkillCases.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// CL9 hito 3, fase 2b: lo que el cuerpo de un personaje (0x4fd5f0) hace después de pegar. 06, "El paso del ataque".
namespace Client::Scenes
{

// 0x4fdb9a..0x50539e, la habilidad del paso del cuerpo. Con +0x381 distinto de 0, 0x4f97b0 (los frames del ataque,
// con +0x38e y el registro propio), 0x4f5b70 (el rand() y los ayudantes de monstruos) y +0x381 += 1. Desde
// [0x6c88ac] (WorldRuntime::skillThreshold): +0x381 en 0, o+0x8a con 0x558eb0 (ChosenKeyOf), +0x38e por la tabla de
// protección, lo de después del switch con el blanco de +0x3a4 y sin él, la escritura de +0x38e en 0 y +0x382 y
// +0x380 en 0. La flecha (0x53e2b0, después de 0x59fb80) es FireTheArrow (ArrowEffects.h). Corta, con lo que hizo
// antes y sus rand(), en los casos de habilidad (hito 4), en los monstruos y transformados (la fase 2c), en los cortes
// de la flecha y en los efectos sin portar.
// `slot` es el lugar de `actor` en el pool y `light`, la luz local de 0x4fd5f0 (Scenes::BodyLight o la de la partícula
// 0x5ac, 0x4fdb3f) que leen los casos de habilidad.
[[nodiscard]] Core::Expected<void, std::string> StepTheAttack(Kernel::ActorState& actor, std::size_t slot,
                                                              const Actors::CharacterStepContext& context,
                                                              const glm::vec3& light = BodyLight);
} // namespace Client::Scenes
