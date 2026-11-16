#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <string>

// CL9 hito 4, fase 2c: las skills del elfo sobre su blanco al alcance (0x55faa0). La llaman la rama del elfo de
// 0x57cfc0 (0x57f239 y 0x57fa20, con el héroe dos veces) y la llegada de 0x5650c0 (0x56654a y 0x5666db, el caso 5).
namespace Client::Scenes
{
struct ActorState;

// 0x55faa0(p1, p2): primero el D3 de girar con la celda lógica del héroe publicado (la X, +0x41c, y la Y, +0x420,
// cada una abierta y cerrada) y su ángulo de ese momento. Después, la skill del lugar 0x7758584 (sin mirar
// 0x7758580) en el registro propio abierto y cerrado; la posición del blanco (0x7758588, sin mirar el −1) en
// +0x3a8..+0x3b0 de p1 y el ángulo de p2 hacia él (0x4e93f0); por la skill − 0x1a (la tabla 0x56181c):
// - 0x1a, 0x1b y 0x1c: sin "webzen" y con el freno, el C1 06 19 con la clave del blanco (+0x1f4); 0x4f5290(p1).
// - 0x33: sin flechas (0x561c50), nada; si no, el 19 como arriba y 0x4f4a10(p1).
// - 0x2e: sin flechas, nada; si no, el 19 y 0x4f48e0(p1), la acción de tirar por las manos.
// - Las demás, nada.
[[nodiscard]] Core::Expected<void, std::string> UseTheElfSkillOnItsTarget(ActorState& user, ActorState& turned,
                                                                          const HeroSkillContext& context);
} // namespace Client::Scenes
