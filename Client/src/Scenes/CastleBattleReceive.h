#pragma once

#include "Core/Expected.h"
#include "Protocol/CastleBattle.h"
#include "Scenes/CastleSiegeReceive.h"

#include <cstdint>
#include <string>

namespace Client::UI
{
struct CastleBattleState;
}

// Los paquetes de la batalla del castillo que llegan (CL9 hito 13; 04 § Castle Siege, la batalla): lo que hacen
// 0x4e2b10 y sus funciones con el B9 01/02/03/05, el BA, el BB y el B2 20 sobre WorldRuntime::castleSiege, los
// personajes y los paneles. Ninguno mira el mapa, la escena ni el flag de cifrado, y ninguno saca rand() fuera de
// lo que abra el registro de los paneles.
namespace Client::Scenes
{
// 0x4e2b10 con un paquete de la batalla: B9 01 (0x4e0750), 02 (0x4e0790), 03 (0x4e07f0) y 05 (0x4e0860); BA
// (0x4cf1b0), BB (0x4e0f30) y B2 20 (0x4e04e0). Corta, con su dirección, lo que es de otra ola o de otro hito.
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleBattlePacket(const Protocol::CastleBattle::CastleBattlePacket& packet, const CastleReceiveContext& context);

// 0x4ca799..0x4ca7cd: lo que el 0x14 (0x4ca780, el que saca personajes del viewport) hace por cada clave con el
// objeto de 0x5767924, antes de 0x510ff0: con la clave entera (sin el AND 0x7fff) en 0x13d apaga +4, +5 y +6; con
// otra, +0x1f, +0x20 y +0x21. Sin objeto, nada. Lo llama el port del 0x14 (hito 1b/2 de la CL9).
void ForgetTheSwitchHolder(UI::CastleBattleState& battle, std::uint16_t key);
} // namespace Client::Scenes
