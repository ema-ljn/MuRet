#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <string>

// CL9 hito 4, fase 2c: la skill del arma sobre su blanco al alcance (0x55cbc0). La llaman la rama del Wizard de
// 0x57cfc0 (0x58eecc, con el héroe dos veces) y la llegada de 0x5650c0 (0x5663b2, hito 3).
namespace Client::Scenes
{
struct ActorState;

// 0x55cbc0(p1, p2): la skill del lugar de la skill del arma (0x7758584, en el registro propio abierto y cerrado). Con
// la 0x2b, la 0x2f o la 0x49 con la derecha del héroe publicado (+0x2b8, MOVSX) fuera de 0xc12 a 0xe11, nada. Si no: la
// posición del blanco (0x7758588, sin mirar el −1) en +0x3a8..+0x3b0 de p1 y el ángulo de p2 hacia él (0x4e93f0); por
// la skill − 1 (la tabla 0x55fa54):
// - 1, 2, 3, 4, 7, 0xb, 0x11 y 0x2d: sin "webzen" y con el freno, el C1 06 19 con la clave del blanco; 0x4f5290(p1) y
//   el D3 de girar con la celda (la X y la Y) y el ángulo del héroe publicado.
// - 0x49: lo mismo, con 0x4ea030(p2, 0x86) y 0x4f3f80 en lugar de 0x4f5290.
// - 0xd: sin "webzen" ni freno, 0x5767948 y el C1 0C 1E con la celda de +0x3a8 de p1, el rumbo de p2 y la clave del
//   blanco por 0x558eb0 (0xffff sin él); 0x4f5290(p1) y el D3.
// - Las demás, nada.
[[nodiscard]] Core::Expected<void, std::string> UseTheSkillOnItsTarget(ActorState& user, ActorState& turned,
                                                                       const HeroSkillContext& context);
} // namespace Client::Scenes
