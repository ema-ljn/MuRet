#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

// CL9 hito 4, fase 6a: los casos de habilidad de 0x4f97b0, los frames del ataque (P-C258). 06, "Las habilidades de
// los frames del ataque".
namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;

// 0x4f9899..0x4fab5c: el switch de 0x4f97b0 por la habilidad − 0x28 (la tabla de 0x4fb10c), con `skill` ya leída de
// +0x38e y [0x6c88ac] en 15 (0x4f987d). false: la habilidad no tiene caso y 0x4f97b0 sigue en 0x4fab5d. true: el caso
// corrió y 0x4f97b0 sale (cada caso termina en un RET), con lo que escribió: +0x381 en 15 al terminar la acción (en 10
// la 0x41 que no la terminó), el frame de la 0x34 y [0x6c88ac] en 5 (0x4f9ee2, WorldRuntime::skillThreshold), o+0x8a
// de la 0x37 (0x558eb0), o+0x194 de la 0x2b, y los cuatro joints 0x617/0 y las cuatro 0x5d9/2 de la 0x2c. Corta, con
// lo que hizo antes y sus rand(), en los efectos, joints y partículas que MuRet no tiene (CL11) y sin el esqueleto del
// modelo de la 0x2b. `slot` es el lugar de `actor` en el pool.
[[nodiscard]] Core::Expected<bool, std::string>
StepTheFrameSkill(ActorState& actor, std::uint8_t skill, std::size_t slot, const CharacterStepContext& context);
} // namespace Client::Scenes
