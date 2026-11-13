#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la rama de 0x57cfc0 de las clases 1, 3 y 4 (Dark Knight, Magic Gladiator y Dark Lord: +0x1d4 & 7
// del héroe, 0x58187c..0x58188e), 0x581894..0x5897a7. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las
// skills del caballero".
namespace Client::Scenes
{
struct ActorState;
struct HeroSkillContext;

// Con el registro propio abierto de punta a punta (sus 3.848 rand() al cerrarlo, 0x5854be..0x5897a2): el maná y la
// stamina de la skill (0x551c70); nada si el héroe no es un jugador (0x20a) o si el héroe publicado no tiene arma
// derecha o tiene un escudo (0xc12 a 0xe11) en una mano, salvo con las 0x30, 0x3e, 0x3f, 0x40, 0x41, 0x4b y 0x4c. La
// party (0x3f), Fenrir (0x4c), la durabilidad del caballo (0x3e) y Chaos Castle dicen si se puede; sin el maná,
// 0x558110(3) busca la poción, de la parte compartida, y corta (0x581b88); la stamina, la espera (0x552c50), la
// energía (0x551f40) y, el Dark Lord, el liderazgo (0x5521f0, que sin él termina). Después, por la skill (0x58238c):
// las 0x29, 0x2a, 0x37, 0x38, 0x3e, 0x41 y 0x4c mandan el C1 1E y las 0x30, 0x3f, 0x40 y 0x4b el C1 19, los dos como
// C3 (dos rand() antes de mirar el socket) y sin "webzen" en el nombre del héroe publicado; las celdas lógicas del
// héroe cuestan cuatro rand() cada una. Las demás, nada. No dibuja: los efectos los ponen las respuestas. El Magic
// Gladiator sigue después en la rama del Wizard (0x5897b7), que corta quien llama.
[[nodiscard]] Core::Expected<void, std::string> UseTheKnightSkill(ActorState& hero, std::uint8_t heroClass,
                                                                  std::uint8_t skill, const HeroSkillContext& context);
} // namespace Client::Scenes
