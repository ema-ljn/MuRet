#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>
#include <string_view>

// CL9 hito 4, fase 2c: lo que comparten las ramas de las clases de 0x57cfc0 y las funciones que mandan (06, "Usar una
// skill"). Todo con el contexto de UseTheHeroSkill; las ramas de cada clase lo llaman con el registro propio abierto,
// como el original.
namespace Client::World
{
struct ItemState;
}

namespace Client::Scenes
{
struct ActorState;

// Lo que 0x551c70(skill, 1, 0, &maná, 0, &stamina) deja en sus punteros: el WORD +0x22 y el WORD +0x24 de la entrada
// de Skill.bmd.
struct SkillCost
{
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
};

// 0x551c70 con el maná y la stamina: abre la entrada de la skill, lee y la cierra (60 rand() si su cuenta vuelve a 0).
// Sin Skill.bmd, corta con `where`.
[[nodiscard]] Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                                 std::string_view where);

// El camino sin maná de las skills (0x559b29, 0x55a398, 0x56205f, 0x57d821, 0x58093e, 0x581b86 y 0x589920): con el
// maná del registro propio (+0x24) debajo del de la skill, 0x558110(3) busca una poción de maná (0x1c04 a 0x1c06) en
// el inventario, sin rand(). Sin ella, nada: el que llama sigue con su camino de "no". Con ella, 0x5af520 mira las
// ventanas y el C1 26 la usa: son del hito 5, y corta con `where`, la dirección de su 0x5af520.
[[nodiscard]] Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where);

// 0x5526f0(skill, c): el alcance (+0x26 de Skill.bmd, con el acceso de la entrada), 2 más con `caster` en el 0x1c16.
[[nodiscard]] Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const ActorState& caster,
                                                              const HeroSkillContext& context);

// Lo que las skills mandan por send() en línea: un C1 (Delivery::Clear, el D3 y el DF) sale sin rand(); un C3
// (Delivery::Encrypted, el 19, el 1E y el 26) saca antes los dos rand() del relleno y del serial (0x5767913), también
// sin la sesión, como el original con el socket en −1.
void SendTheSkillPacket(const HeroSkillContext& context, Protocol::Outgoing packet);

// El freno de 300 ms de los envíos (0x576791c, PassesTheActionBrake) con el GetTickCount del contexto. Sin él, corta
// con `where`.
[[nodiscard]] Core::Expected<bool, std::string> PassesTheSkillBrake(std::int32_t skill, const HeroSkillContext& context,
                                                                    std::string_view where);

// 0x559510(c, héroe, ítem, skill): sin elegido (0x6c8e30 en −1 o menos), 0. Si no, su celda (ftol de la posición ·
// 0,01) en 0x77e1ba8/0x77e1bac, 0x7758580..0x7758588 en 0, `skill` y el elegido, y con la entrada de la skill abierta
// (60 rand() al cerrarla en 0): el alcance (0x5526f0 · 1,2; 1,8 en Blood Castle con las 0x13 a 0x17) contra la
// distancia de `hero` al centro de la celda. En alcance, 0x55b6f0(c, héroe); fuera, el camino de PathFinding2 hasta el
// alcance desde las celdas lógicas de c (+0x420 y +0x41c, 4 rand() cada una) y, si lo hay, +0x378 en 1 y +0x379 en 5
// (la llegada del caso 5 de 0x5650c0). Da 1 con 0x55b6f0 o con el camino.
[[nodiscard]] Core::Expected<bool, std::string>
UseTheSkillOnTheChosen(ActorState& caster, ActorState& hero, std::uint8_t skill, const HeroSkillContext& context);

// 0x55b6f0(c, héroe): la skill de 0x7758584 (con 0x7758580, la del lugar en el registro propio) sobre el personaje de
// 0x7758588. El D3 de girar con la celda lógica del héroe publicado (+0x41c y +0x420) y su rumbo, por el socket; +0x378
// de c en 0; la acción (con un jugador, 0x4f3f80 y la de la tabla 0x55cb60; si no, 0x4f4a10 sobre c); salvo las 0x3c,
// 0x3d y 0x4a, el brillo 0x5fe en el pool de partículas (0x55be23, con el dueño `hero`) y el sonido rand() % 2 + 0x3c
// (un rand(), al último sonido); el blanco en +0x3a8 de c y el rumbo hacia él en
// +0x24 de `hero`; con el nombre sin "webzen" y el freno pasado, el C3 19; +0x381 de c en 1 y, salvo con 0x20 en +0x78
// de `hero` o c en el 0x1c16, la celda del blanco (corrida por el rumbo en Blood Castle, con las 0x3c, 0x39 y 0x2c o
// con los monstruos 0x115, 0x11b, 0x116 y 0x120) y, si no es pared (4 u 8 en el atributo) ni una de las skills del
// arma, el DF con esa celda.
[[nodiscard]] Core::Expected<void, std::string> CastTheSkillAtTheTarget(ActorState& caster, ActorState& hero,
                                                                        const HeroSkillContext& context);

// 0x559afc..0x55a2e5: las skills de la lista de 0x559890 (0x31, 0x3c, 0x3d, 0x2c, 0x4a y la 0x2b, 0x2f y 0x39 con su
// arma). El costo (0x551c70); sin maná, la poción (UseAManaPotion, 0x559b41) y 0; con la stamina de más que +0x38, 0;
// sin 0x552c50 (la espera del lugar 0x7758584), 0; si 0x558bb0 no deja atacar al elegido, las opciones del arma
// (UseTheWeaponOptions); si deja, 0x559510(héroe, héroe, ítem, skill).
[[nodiscard]] Core::Expected<bool, std::string> UseTheListedWeaponSkill(ActorState& hero, const World::ItemState& item,
                                                                        std::uint8_t skill,
                                                                        const HeroSkillContext& context);

// 0x559a21..0x55b4b4: con el registro propio abierto, cada opción del arma (+0x2b...) igual a la skill del lugar +0x425
// del héroe publicado (0x55a368): su costo; sin maná, la poción (0x55a3b0) o la que sigue; con la espera (0x552c50) la
// opción 0x12 pone la acción 0x98 (0x4f4a10 si no es jugador) y, con el nombre sin "webzen" y el freno pasado, manda
// el C3 19 con la clave del héroe publicado; las 0x13 a 0x17 y la 0x31, si 0x558bb0 deja atacar, 0x559510. Sigue con
// las demás opciones; el resultado es el de la última que lo puso.
[[nodiscard]] Core::Expected<bool, std::string> UseTheWeaponOptions(ActorState& hero, const World::ItemState& item,
                                                                    const HeroSkillContext& context);

// 0x561f60(héroe, ítem) desde 0x562024: con el registro propio abierto, cada opción del arma igual a la skill del lugar
// +0x425 del héroe publicado: su costo; sin maná, la poción (0x562075) o la que sigue; con la stamina de más que +0x38
// o sin la espera (0x552c50), 0 enseguida. El alcance (0x5526f0) y, con la 0x18 o la 0x36: sin flechas (0x561c50) se
// terminan las opciones; con la celda del elegido (0x77e1ba8) en alcance, el rumbo de c hacia +0x3a8, la clave de
// 0x558eb0 con 0x7758588 y, con el nombre sin "webzen" (sin el freno), 0x5767948 y el 1E (la 0x36 con la celda lógica
// de c hacia el blanco y la espalda); después 0x4f4a10 y, si c no es un jugador, la flecha (ShootTheArrow, sin portar).
// Sigue con las demás opciones; da 1 si alguna tiró.
[[nodiscard]] Core::Expected<bool, std::string> UseTheElfWeaponOptions(ActorState& hero, const World::ItemState& item,
                                                                       const HeroSkillContext& context);
} // namespace Client::Scenes
