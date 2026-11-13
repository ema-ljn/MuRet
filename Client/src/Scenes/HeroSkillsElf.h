#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la rama del elfo de 0x57cfc0 (0x57d790..0x58187c; 06, "La rama del elfo"). La usa el elfo
// (+0x1d4 & 7 en 2); lo que sigue de 0x5808da (las skills 0x43 a 0x48, de todas las clases) corta con su dirección.
namespace Client::Scenes
{
struct ActorState;

// 0x57d790..0x58187c con `skill`, la del lugar, y el alcance de GameMovementState::selectedSkillRange ([ebp−0x50]).
// - La cabeza, con el registro propio abierto: el maná y la stamina (0x551c70; sin el maná, la poción de maná de
//   HeroSkillCasts, 0x57d837), la stamina contra +0x38, la espera (0x552c50) y la energía (0x551f40) contra +0x1e +
//   +0x30. Cerrado el registro, el blanco (0x57ce80), sin mirar su resultado.
// - 0x1e a 0x24 (las invocaciones, 0x57f252): en el mapa 10 o en Chaos Castle, nada; si no, sin "webzen" y con el
//   freno, el C1 06 19 con la clave del jugador (0x57678bc); 0x4f5290 y nada más.
// - 0x34 (0x57e14d): un jugador con el arco o la ballesta del héroe publicado, flechas (0x561c50) y la celda del blanco
//   al alcance: el rumbo hacia +0x3a8, la clave de 0x558eb0 con 0x7758588 (0xffff sin él), sin "webzen" 0x5767948 y
//   el C1 0C 1E con la celda lógica, y 0x4f4a10. Sigue como las demás.
// - Las demás (0x57f008): con un elegido, 0x7758580..88; fuera de alcance, el camino hasta el elegido (un jugador, con
//   las 0x1a a 0x1c o atacable, +0x379 en 5) y, salvo 0x43 a 0x47 y 0x4c, nada más. Con la línea de vista y un
//   elegido, 0x55faa0 (las 0x1a a 0x1c sobre un jugador, y nada más; las 0x33 y 0x2e con el arma, y sigue). Por la
//   skill: 0x1a a 0x1c y 0x35, el 19 con la clave del jugador y 0x4f5290 (la 0x35 con +0x378 en 0); 0x4c, con
//   0x4e9920 y la clave mágica, como la del Wizard; las demás, a 0x5808da.
// Los envíos van por HeroSkillContext::send; los C3 sacan sus dos rand() antes. Lo que es de otro hito corta con su
// dirección: la poción (0x57d837, hito 5) y 0x5808da (0x5808ef).
[[nodiscard]] Core::Expected<void, std::string> UseTheElfSkill(ActorState& hero, std::uint8_t skill,
                                                               const HeroSkillContext& context);
} // namespace Client::Scenes
