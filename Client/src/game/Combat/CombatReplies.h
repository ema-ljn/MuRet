#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Combat
{
struct KillExperience;
struct Death;
} // namespace Client::Protocol::Combat
namespace Client::Scenes
{
struct ActorCombatContext;
struct ChatReceiveContext;

// 0x4cf200 (0x16) y 0x4cfce0 (0x9C), con el flag de descifrado en 1 (con 0 manda el F1 03 la sesión): el muerto de
// 0x5110d0 (el lugar 400 si nadie tiene la clave, como con el 0x9C de Blood Castle: Q-C1160); con el bit 15, 0x4f5660
// sobre él; sin él, el héroe anota el muerto (+0x380 en 2, el daño en +0x382 y el lugar en +0x3a4). Después el número
// del daño (0x548e40 con la luz 1, 0,6, 0 y la escala 15), +0x389 en 1 y +0x378 en 0, la experiencia sumada en el
// registro propio (+0x10, 3848 rand() al cerrarlo) y, si no es 0, el texto 486 de Text.bmd con ella al chat (0x555190
// de tipo 1, con el nombre de 0x576798c, o 0x5767990 el 0x9C: vacíos, P-C252 d).
[[nodiscard]] Core::Expected<void, std::string> ApplyKillExperience(const Protocol::Combat::KillExperience& reply,
                                                                    const ActorCombatContext& combat,
                                                                    const ChatReceiveContext& chat);

// 0x4d0a30 (0x17): el que muere (0x5110d0, sin máscara) queda con +0x389 en 1, +0x378 en 0 y la skill en +0x391. En
// Blood Castle y Chaos Castle (mapas 11 a 23), 0x4d07c0 con la celda del TerrainWall; en los demás, la skill 0x28 o
// 0x3b lo empuja (dos rand()) y lo gira hacia el que lo mató.
[[nodiscard]] Core::Expected<void, std::string> ApplyDeath(const Protocol::Combat::Death& reply,
                                                           const ActorCombatContext& combat);
} // namespace Client::Scenes
