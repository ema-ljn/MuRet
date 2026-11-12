#pragma once

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los daños del personaje propio que muestran la ventana del personaje (0x5ee2e0) y el tooltip de la skill (0x5b90b0):
// el del ataque, 0x5b8b80, el de la magia, 0x5513d0, y el de la skill, 0x5516e0, con sus accesos a la tabla de
// protección y sus rand(). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La ventana del personaje" y "El
// cuerpo de la skill".
namespace Client::Scenes
{
struct ItemSetOptionList;
struct OwnCharacterRecord;
struct OwnEquipmentOverride;

// Lo que 0x5b8b80 deja en sus dos punteros y lo que devuelve.
struct OwnAttackDamage
{
    std::uint32_t min = 0;
    std::uint32_t max = 0;
    // El byte que devuelve: la ventana pinta el renglón del daño en naranja con 1. Lo prenden las dos manos con un
    // tipo de 0 a 0xbff en las clases 1 y 3, y en la 2, con las dos de 0x800 a 0x9ff, el 0x807 de la izquierda o el
    // 0x80f de la derecha con nivel (el +4 & 0x78 en 8 o más). Qué es (las dos armas, las flechas o los virotes con
    // nivel) es *inferido*.
    bool highlighted = false;
};

// 0x5b8b80(&min, &max), cdecl: abre el registro propio (sin gastar) y, al cerrarlo, si queda en 0, saca 3.848
// rand(). Las manos son los lugares 0 (+0xb6c) y 1 (+0xbb4), o el ítem del cursor (0x7879f80) en el lugar de
// [0x78a6758] (0 o 1) con [0x7879fbc] > 0 y la grilla del inventario. Con la derecha de 0x808 a 0x80e, el 0x810 o de
// 0x812 a 0x9ff, el par +0x50 y +0x52; si no, con la izquierda de 0x800 a 0x806, el 0x811, el 0x814 o el 0x815, la
// derecha en −1 o de 0xa00 a 0xbff, el par +0x54 y +0x56; si no, +0x50 y +0x52. Las clases 1 y 3 con las dos manos
// de 0 a 0xbff suman el 55 % de cada par (+0x50 y +0x54, +0x52 y +0x56, sin signo y truncando); con el bit 0x02 de
// +0x3c, 15 más a los dos. `heroClass` es el +0x1d4 del héroe & 7.
[[nodiscard]] OwnAttackDamage AttackDamageOf(const OwnCharacterRecord& own, std::uint32_t heroClass,
                                             const OwnEquipmentOverride& held, std::uint8_t& ownRecordDepth,
                                             Render::Models::CrtRandom& random);

// Lo que 0x5513d0 deja en sus dos punteros.
struct OwnMagicDamage
{
    std::int32_t min = 0;
    std::int32_t max = 0;
};

// 0x5513d0(skill, &min, &max), thiscall sobre el registro propio: abre la entrada `skill` de Skill.bmd (60 rand() al
// cerrarla en 0, `skillDepth`), y con s, el byte +0x21 de la entrada, min = +0x5a + s y max = +0x5c + s + s / 2.
// Pone en 0 las 16 marcas de la lista de [0x6eed3c] (+0x937c), suma a un p de 32 bits en 0 las entradas del byte
// +0x30 de la skill (0x40d8c0) y a sus 16 bits de abajo las del selector 0x9f (0x40d690), y suma p a los dos. Sin
// Skill.bmd o sin la skill, error antes de abrir; con más de 16 entradas en la lista, el de 0x40d8c0 o 0x40d690.
[[nodiscard]] Core::Expected<OwnMagicDamage, std::string>
MagicDamageOf(const OwnCharacterRecord& own, std::uint8_t skill, const Data::Tables::SkillTable* skills,
              std::uint8_t& skillDepth, Render::Models::CrtRandom& random, ItemSetOptionList& setOptions);

// Lo que 0x5516e0 deja en sus dos punteros.
struct OwnSkillDamage
{
    std::int32_t min = 0;
    std::int32_t max = 0;
};

// 0x5516e0(skill, &min, &max), thiscall sobre el registro propio, que no lee: 0x5513d0 sin los dos words del registro.
// Abre la entrada `skill` de Skill.bmd (60 rand() al cerrarla en 0, `skillDepth`) y, con s, su byte +0x21, min = s y
// max = s + s / 2; pone en 0 las 16 marcas de +0x937c y suma a los dos el mismo p de los conjuntos (0x40d8c0 y
// 0x40d690). Sin Skill.bmd o sin la skill, error antes de abrir.
[[nodiscard]] Core::Expected<OwnSkillDamage, std::string>
SkillDamageOf(std::uint8_t skill, const Data::Tables::SkillTable* skills, std::uint8_t& skillDepth,
              Render::Models::CrtRandom& random, ItemSetOptionList& setOptions);
} // namespace Client::Scenes
