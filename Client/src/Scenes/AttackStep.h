#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

// CL9 hito 3, fase 2b: lo que el cuerpo de un personaje (0x4fd5f0) hace después de pegar. 06, "El paso del ataque".
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 0x4fdb9a..0x50539e, la habilidad del paso del cuerpo. Con +0x381 distinto de 0, 0x4f97b0 (los frames del ataque,
// con +0x38e y el registro propio), 0x4f5b70 (el rand() y los ayudantes de monstruos) y +0x381 += 1. Desde
// [0x6c88ac] (WorldRuntime::skillThreshold): +0x381 en 0, o+0x8a con 0x558eb0 (ChosenKeyOf), +0x38e por la tabla de
// protección, lo de después del switch con el blanco de +0x3a4 y sin él, la escritura de +0x38e en 0 y +0x382 y
// +0x380 en 0. Corta, con lo que hizo antes y sus rand(), en los casos de habilidad (hito 4), en los monstruos y
// transformados (la fase 2c), en la flecha (0x59fb80 y 0x53e2b0) y en las partículas, efectos y joints sin portar.
// `slot` es el lugar de `actor` en el pool.
[[nodiscard]] Core::Expected<void, std::string> StepTheAttack(ActorState& actor, std::size_t slot,
                                                              const CharacterStepContext& context);
} // namespace Client::Scenes
