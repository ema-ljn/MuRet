#pragma once

#include "Core/Expected.h"

#include <string>

// La respuesta del F3 06 (0x4d3200, desde 0x4e30cd en el sub-dispatcher del F3): el punto de subir de nivel que se
// pidió con los botones de la ventana del personaje (Protocol/Character/LevelUpPoint, 0x5e468e). Contrato:
// Client/docs/research/04-protocolo.md, "El F3 06 que llega".
namespace Client::Protocol::Player
{
struct LevelUpPointResult;
}

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Player
{

// 0x4d3200: con el nibble alto de pkt[4] (0x4d320c), con el registro propio abierto (en línea, como 0x40b240, o el alta
// de 0x40ad90 sin la entrada): los puntos para repartir (+0x68, un word) menos 1, antes del switch (0x4d32d8); la
// estadística del nibble bajo más 1, por la tabla de 0x4d34f4 (0 la fuerza, 1 la agilidad, 2 la vitalidad con la vida
// máxima de pkt[6..7], 3 la energía con el maná máximo, 4 el comando; de 5 a 15, ninguna, por el JA de 0x4d32df); y el
// BP máximo (+0x3a) de pkt[8..9] (0x4d3356). Lo cierra (como 0x40b4d0: 3.848 rand() si queda sin accesos). Después,
// siempre (0x4d341a..0x4d34ed), lo abre, corre 0x550d10 (RecalculateOwnStats) y lo cierra.
[[nodiscard]] Core::Expected<void, std::string>
ApplyLevelUpPointResult(const Protocol::Player::LevelUpPointResult& reply,
                        const Scenes::WorldEntryResources& resources);
} // namespace Client::Player
