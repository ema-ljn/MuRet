#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Network/ProtocolError.h"

#include <cstdint>

// El pedido de poner un punto de los de subir de nivel (F3 06, C->S), desde los botones de la ventana del personaje
// (0x5e468e..0x5e4a12, CL8 hito 5): C1 05 F3 06 y la estadística, con el XOR32 desde el byte 3, como lo arma el
// PacketWriter del original en la pila y lo manda con send() sin pasar por la cola (04-protocolo, "F3 06").
namespace Client::Protocol::Character
{
// Las estadísticas de los botones, de arriba hacia abajo: la fuerza, la agilidad, la vitalidad, la energía y, solo
// el Dark Lord (la clase 4), el comando.
inline constexpr std::uint8_t StrengthPoint = 0;
inline constexpr std::uint8_t AgilityPoint = 1;
inline constexpr std::uint8_t VitalityPoint = 2;
inline constexpr std::uint8_t EnergyPoint = 3;
inline constexpr std::uint8_t CommandPoint = 4;

[[nodiscard]] Outgoing WriteLevelUpPoint(std::uint8_t stat);

// La respuesta, C1 0A F3 06 (0x4d3200, desde 0x4e30cd; PMSG_LVPOINTADDRESULT de CGLevelUpPointAdd, 0x417b50, en el
// GameServer 0.97 reconstruido, de 10 bytes): pkt[4] es 0x10 + la estadística si el servidor sumó el punto y 0 si no;
// pkt[5] el relleno; pkt[6..7] la vida máxima (con la vitalidad) o el maná máximo (con la energía), y pkt[8..9] el BP
// máximo, words en little endian.
struct LevelUpPointResult
{
    std::uint8_t result = 0;
    std::uint16_t maxLifeOrMana = 0;
    std::uint16_t maxBp = 0;
};

// 0x4d320c: con el nibble alto de pkt[4] en 0 no lee nada más; con él, lee pkt[8..9] siempre (0x4d334d) y pkt[6..7]
// con la vitalidad o la energía (0x4d332d y 0x4d333b): sin los 10 bytes, TooShort.
[[nodiscard]] Core::Expected<LevelUpPointResult, Network::ProtocolError>
ReadLevelUpPointResult(const Network::Packet& packet);
} // namespace Client::Protocol::Character
