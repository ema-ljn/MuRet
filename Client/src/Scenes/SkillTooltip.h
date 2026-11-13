#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// El tooltip de una skill de la barra (06 § Los tooltips, "El cuerpo de la skill", CL8 hito 5): 0x5b90b0, que
// 0x5f7bc0 llama con el lugar de la skill del héroe (0x6c9320), y la ayuda de los comandos del cuervo (0x421c10), que
// lo reemplaza con los lugares 0x78 a 0x7b del Dark Lord. Los dos escriben en HudState::tooltip sin limpiarlo, sin
// tocar 0x78abb30 ni 0x78abb34, y dibujan la caja (0x5af090) con el modo 2 y el marco.
namespace Client::Scenes
{
struct GameHudContext;

// 0x5b90b0(x, y, lugar, skill de asedio), cdecl. Con la ayuda de los comandos (0x421c10) sale sin abrir nada. Si no,
// abre el registro propio y escribe, con la skill del byte +0x6c + lugar del registro:
//   - "\n", el nombre de 0x551c70 (el 1200 con la 0x3c y la opción 0x42 en la derecha) en azul y negrita, "\n";
//   - el daño según la clase: la magia de 0x5513d0 (el 170) para el Wizard, salvo que la skill de asedio sea 0x43,
//     0x44 o 0x45, y los tres de la 0x10; el de la skill (0x5516e0 más el ataque de 0x5b8b80, el 879) para las clases
//     1 a 4, o el 1237 con la 0x3e, salvo las skills de la tabla de 0x5ba73c;
//   - el daño de los stats de la 0x4c (0x552470), los tres de la elfa (0x1a, 0x1b y 0x1c), el alcance, el maná y la
//     stamina de la entrada, lo del Dark Knight (0x2f y el combo), el atributo (0x5519f0, el 1080 + byte), lo que dice
//     +0x31 de la entrada (de quién viene la skill o la guerra de guilds), lo del Dark Lord (0x3f sin party) y las dos
//     notas de la 0x4c;
//   - un "\n" al final.
// Cierra el registro propio (3.848 rand() si queda en 0) y pone la caja arriba de y: ((N − 3) · cy + 3 · cy / 2) / ry.
// Cada acceso a una entrada de Skill.bmd saca 60 rand() al cerrarse en 0. Lo que MuRet no tiene corta con su dirección.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSkillTooltip(GameHudContext& context, std::int32_t x,
                                                                    std::int32_t y, std::int32_t slot,
                                                                    std::int32_t siegeSkill);
} // namespace Client::Scenes
