#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la rama del Wizard de 0x57cfc0 (0x5897aa..0x593874; 06, "Usar una skill"). La usan el Wizard (0)
// y el Magic Gladiator (3), que llega desde el final de la rama de las clases 1, 3 y 4 (0x5854c1 → 0x5897a7). Sigue
// en 0x5808da (las skills 0x43 a 0x48, de todas las clases: HeroSkillsTail.h).
namespace Client::Scenes
{
struct ActorState;

// 0x5897a7..0x599d5d con `skill`, la del lugar (0x28 ya cargada como 0x3a por el derecho) y el alcance de
// GameMovementState::selectedSkillRange. Con otra clase que 0 o 3, directo a 0x5808da.
// - La cabeza (0x5897b7..0x58a13a), con el registro propio abierto: el maná y la stamina (0x551c70, la de la 0x28 para
//   la 0x3a y la 0x28, y sin stamina la 0x28), la energía (0x551f40) contra +0x1e + +0x30, el maná contra +0x24 (sin
//   él, la poción de maná: HeroSkillCasts), la stamina contra +0x38 (sin ella, la 0x3a y la 0x28 sueltan el derecho)
//   y la espera (0x552c50). Cerrado el registro, el blanco (0x57ce80).
// - Por la skill: 0x3a y 0x28 (el 19 sin blanco y la acción 0x44 o 0x45), 0x10 (Soul Barrier, con la party), 0xa y 0xe
//   (el 1E de Hellfire e Inferno), 0x4c (con 0x4e9920 y la clave mágica) y las demás por el alcance (0x58c15e): fuera
//   de él, el camino hasta el elegido (+0x379 en 5); dentro y con la línea de vista, 0x55cbc0 con un elegido atacable
//   y, con el blanco, el ángulo y la 5 (Flame), la 8 y la 9, la 0xc (Aqua Beam) y la 0x26 y la 0x27 (el 1E), y del
//   Wizard la 6 (Teleport, el 1C) y la 0xf (Teleport Ally, el B0). Las 0x12 a 0x17 y la 0x2f, nada.
// Los envíos van por HeroSkillContext::send; los C3 sacan sus dos rand() antes. Lo que es de otro hito corta con su
// dirección: los nombres de la party (0x59e680 y 0x59272c, hito 7) y la poción (0x589936, hito 5).
[[nodiscard]] Core::Expected<void, std::string> UseTheWizardSkill(ActorState& hero, std::uint8_t skill,
                                                                  const HeroSkillContext& context);
} // namespace Client::Scenes
