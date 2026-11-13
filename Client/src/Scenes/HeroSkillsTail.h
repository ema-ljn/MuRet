#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la cola de 0x57cfc0 con las skills 0x43 a 0x48 (0x5808da..0x599d5d; 06, "La cola de las 0x43 a
// 0x48"), de todas las clases. Llegan el elfo (la tabla de 0x57fa31 y el final de la 0x35 y de la 0x4c), las clases 1
// y 4 al terminar la suya (0x5897b1), el Wizard y el Magic Gladiator desde 0x5925ae y las clases 5 a 7 (0x5897b1).
namespace Client::Scenes
{
struct ActorState;

// 0x5808da(héroe, skill): fuera de 0x43 a 0x48, nada. Si no, con el registro propio abierto hasta 0x599d58 (0x40f370):
// - La cabeza (0x5808ef..0x594002): el costo (0x551c70); sin un jugador (0x20a), nada más. Sin el maná (+0x24), la
//   poción de maná (0x558110(3); con ella, 0x5af520 y el C1 26 son del hito 5); con él y la stamina (+0x38), la espera
//   del lugar del héroe publicado (0x552c50). Después, la energía (0x551f40). Nada de eso frena la skill (Q-C1226).
// - La 0x43 (0x594009..0x594e3d): el rumbo hacia +0x3a8, la posición del blanco (0x5ae300) desde la celda lógica (la Y
//   y la X), la celda de 0x4e9920 y la clave de 0x558eb0 con 0x7758588 (0xffff con −1); sin "webzen", 0x5767948 y el
//   1E con la celda lógica (la X y la Y) y la clave de la magia de 0x57cf90 con un puntero nulo, 0. Después, 0x4f3f80.
// - Las 0x44 a 0x48 (0x594e42, 0x595e25, 0x596dca, 0x597d7e y 0x598d65): sin "webzen" y con el freno de 300 ms, el 19
//   con la clave del elegido (0x6c8e30, +0x1f4) o, sin él, la del jugador (0x57678bc).
// - La acción (0x4ea030) por la montura fuera de la zona segura: 0x52 el caballo oscuro, 0x40 Uniria, 0x41 Dinorant y
//   0x5d Fenrir; sin ella, 0x87 la 0x44 y la 0x47, y 0x3f las demás. +0x378 en 0.
// Sin efectos ni sonidos; el C1 26 de la poción y su sonido (0x593f73..0x593fa1) van con el hito 5.
[[nodiscard]] Core::Expected<void, std::string> UseTheSharedTail(ActorState& hero, std::uint8_t skill,
                                                                 const HeroSkillContext& context);
} // namespace Client::Scenes
