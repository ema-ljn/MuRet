#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorCombat.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la acción de golpe de las skills (0x4f5290), que llaman las ramas del elfo, de las clases 1, 3 y
// 4 y del Wizard de 0x57cfc0 (0x57f997, 0x58016f, 0x5808cb, 0x58c156, 0x58fcd4, 0x591840 y 0x5925a1) y 0x55cbc0; la
// cola de las 0x43 a 0x48 no (HeroSkillsTail.h). Es la misma que SkillReplies usa con las skills que llegan, con el
// contexto de pegar.
namespace Client::Scenes
{
struct ActorState;

// 0x4ea030(personaje, acción) con el esqueleto de su tipo (+2): Player.bmd para un jugador (0x20a) y el de la
// biblioteca de modelos para los demás. Sin él, corta.
[[nodiscard]] Core::Expected<void, std::string> SetTheActorAction(ActorState& actor, std::uint8_t action,
                                                                  const ActorCombatContext& context);

// 0x4f5290: en un jugador, las velocidades (0x4f3f80) y la acción de golpe: 0x97 montado en Uniria o Dinorant (0x1c14,
// 0x1c15) fuera de la zona segura, 0x5d en Fenrir (0x1c37), 0x92 el elfo y si no 0x8e + (rand() & 1). En otro, 3 o 4
// por +0x38f (cada tres, la 3), que sube.
[[nodiscard]] Core::Expected<void, std::string> SetTheHitAction(ActorState& actor, const ActorCombatContext& context);
} // namespace Client::Scenes
