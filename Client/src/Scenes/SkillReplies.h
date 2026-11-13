#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Skill
{
struct MagicAttack;
struct DurationMagic;
struct MagicCancel;
struct ActorState;
struct ActorEffect;
struct SkillTarget;
struct SkillList;
struct SummonLife;
} // namespace Client::Protocol::Skill

// Las skills que llegan (CL9 hito 4), ya cifradas: lo que los handlers de 0x4e2b10 hacen con el mundo. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Las skills que llegan". Lo que MuRet no tiene corta con error
// después de lo que el original hace antes: la acción de ataque de 0x4f4a10 (hito 3), las banderas de 0x4e6380
// (hito 1), los efectos de 0x518cf0 de tipos que MuRet no crea y los joints de 0x530860 (CL11). Los sonidos quedan en
// WorldRuntime::lastSoundId (CL10). El rand() del cierre de +0x38e y de +0x42e, y los de 0x4f2250 y 0x4f5290, en su
// orden.
namespace Client::Scenes
{
struct WorldEntryResources;

// 0x4cc530: la skill de la fuente sobre el blanco (sin el blanco, nada). Salvo la 0x3b: el rumbo de la fuente hacia
// el blanco (si no es el héroe, ni las skills 6 y 0xf, y el blanco se ve), el blanco en +0x3a4 (y en +0x434 con la
// 0x4c), +0x38d y la skill en +0x38e, protegido. Después, la rama de la skill (06, la tabla de 0x4cc530).
[[nodiscard]] Core::Expected<void, std::string> ApplyMagicAttack(const Protocol::Skill::MagicAttack& attack,
                                                                 const WorldEntryResources& resources);

// 0x4ce000: la skill con duración. Sobre el personaje de la clave (el 400 si no está, como el original, Q-C1204): la
// skill en +0x38e, el rumbo (rumbo · 1/255 · 360) y la acción, con la de ataque si no es un jugador y la de la skill si
// es otro jugador; después la celda (+0x39c, +0x39d), +0x381 en 1, +0x3a4 en 0xffff y +0x380 en 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyDurationMagic(const Protocol::Skill::DurationMagic& magic,
                                                                   const WorldEntryResources& resources);

// 0x4cbe90: el fin de una skill con duración: la bandera que le toca (0x4e6910), con 0x439d40 antes en las 0xc9 y
// 0xca. Sobre el 400 si la clave no está.
void ApplyMagicCancel(const Protocol::Skill::MagicCancel& cancel, const WorldEntryResources& resources);

// 0x4cbe00: con el tipo 1 y la máscara sin prender del todo en +0x78, 0x4e6380 (hito 1, corta); con el tipo 1 y la
// máscara prendida, +0x7c queda en 0x200000 si la tenía y en 0 si no; con otro tipo y la máscara prendida, 0x4e6910.
[[nodiscard]] Core::Expected<void, std::string> ApplyActorState(const Protocol::Skill::ActorState& state,
                                                                const WorldEntryResources& resources);

// 0x4cc290: sin el blanco, nada. Si no, la fuente en 0x5767964, la skill en +0x42e (protegido), el blanco en +0x3a4 y
// su bit 15 en +0x38d, y con las skills 1 a 0xd, la acción de ataque (0x4f4a10, hito 3).
[[nodiscard]] Core::Expected<void, std::string> ApplySkillTarget(const Protocol::Skill::SkillTarget& target,
                                                                 const WorldEntryResources& resources);

// 0x4dbf50: con un personaje de la clave que es un jugador (+0x88 en 1) y el tipo 0x10, quince joints 0x60f (CL11,
// corta), el efecto 0x61e y el sonido 0x98.
[[nodiscard]] Core::Expected<void, std::string> ApplyActorEffect(const Protocol::Skill::ActorEffect& effect,
                                                                 const WorldEntryResources& resources);

// 0x4c5070: las skills del registro propio, abierto (3.848 rand() si lo cierra en 0); después lo abre otra vez y
// cuenta +0x6a y +0x6b con +0x31 de Skill.bmd de cada una de las 64 primeras (60 rand() por cierre en 0); arregla
// +0x425 del héroe y lo cierra.
[[nodiscard]] Core::Expected<void, std::string> ApplySkillList(const Protocol::Skill::SkillList& list,
                                                               const WorldEntryResources& resources);

// 0x4e31e1: la vida del invocado, 0x5767960 (HudState::summonLife).
void ApplySummonLife(const Protocol::Skill::SummonLife& summon, const WorldEntryResources& resources);
} // namespace Client::Scenes
