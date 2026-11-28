#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Movement
{
struct HeroMovementContext;
} // namespace Client::Movement

// Hablarle a un NPC (CL9 hito 2, P-C178): el clic de MoveHero sobre el NPC elegido (0x59da75..0x59dc59, en
// GameMovement.cpp) lleva al héroe hasta su celda con +0x379 en 2, y la llegada (0x5650c0 con ese 2,
// 0x5674c8..0x5695b3) le manda el C1 30 con su clave. Lo que abre cada NPC lo dice la respuesta del GS (0x4d1e00,
// hito 6); el diálogo de Dialog.bmd que lleva de una respuesta a otra es el 0x8d (GameDialogSteps.cpp).
namespace Client::Npc
{

// 0x5674c8..0x5695b3, el caso 2 de 0x5650c0: el máximo de MouseUpdateTime en 12; fuera de Crywolf activo, el héroe
// parado (0x4f2250) y +0x378 en 0; con el NPC de 0x6c8e54 (+0x376 de 0xcd o más), el aviso de nivel del 0xee, las
// banderas de los paneles, la ventana de las misiones cerrada, el C3 A0 si las misiones no tienen la clase, el aviso
// del 0x101 y el C3 30; en Crywolf activo, antes del C3 30, el clic en un altar (0x567b00..0x567c23,
// Events::Crywolf::ClickTheCrywolfAltar, CL9 hito 11), que con una elfa muestra el estado del altar en vez de hablar.
// Al final, 0x6eeda8 y 0x6c8e54 en −1.
[[nodiscard]] Core::Expected<void, std::string> TalkToTheNpc(Kernel::ActorState& hero,
                                                             const Movement::HeroMovementContext& context);
} // namespace Client::Npc
