#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorCombat.h"

#include <string>

// CL9 hito 4, fase 2c: la acción por las manos (0x4e9c50), que llaman las ramas del elfo, de las clases 1, 3 y 4 y del
// Wizard de 0x57cfc0 (0x5817ce, 0x5896d2 y 0x58eb7a). Es la misma que SkillReplies usa con las skills que llegan.
namespace Client::Scenes
{
struct ActorState;

// 0x4e9c50(por, en): la acción de `on` (0x4ea030) por las manos de `by` (+0x2b8 la derecha y +0x2d8 la izquierda, el
// WORD contra 0xffff): las dos, 0x63; solo la derecha, 0x64; solo la izquierda, 0x65; ninguna, 0x62.
[[nodiscard]] Core::Expected<void, std::string> ActByTheHands(const ActorState& by, ActorState& on,
                                                              const ActorCombatContext& context);
} // namespace Client::Scenes
