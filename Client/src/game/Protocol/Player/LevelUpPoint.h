#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

// Adding a level-up point, F3 06, from the character window's buttons (0x5e468e..0x5e4a12): the original builds the
// request on the stack and sends it with send(), past the queue (04, "F3 06").
namespace Client::Protocol::Player
{
// The buttons' statistics, top to bottom; the command only for the Dark Lord (class 4).
inline constexpr std::uint8_t StrengthPoint = 0;
inline constexpr std::uint8_t AgilityPoint = 1;
inline constexpr std::uint8_t VitalityPoint = 2;
inline constexpr std::uint8_t EnergyPoint = 3;
inline constexpr std::uint8_t CommandPoint = 4;

// C1 05 F3 06 and the statistic, with the XOR32 from byte 3.
[[nodiscard]] Outgoing WriteLevelUpPoint(std::uint8_t stat);

// The reply, C1 0A F3 06 (0x4d3200, from 0x4e30cd; PMSG_LVPOINTADDRESULT of CGLevelUpPointAdd, 0x417b50, in the
// rebuilt GameServer 0.97): pkt[4] is 0x10 plus the statistic if the server added the point and 0 if not, pkt[6..7]
// the maximum life (vitality) or mana (energy) and pkt[8..9] the maximum BP, little-endian words.
struct LevelUpPointResult
{
    std::uint8_t result = 0;
    std::uint16_t maxLifeOrMana = 0;
    std::uint16_t maxBp = 0;
};

// 0x4d320c: with pkt[4]'s high nibble at 0 it reads nothing else; with it, pkt[8..9] always (0x4d334d) and pkt[6..7]
// for the vitality or the energy (0x4d332d and 0x4d333b): without the ten bytes, TooShort.
[[nodiscard]] Core::Expected<LevelUpPointResult, Network::ProtocolError>
ReadLevelUpPointResult(const Network::Packet& packet);
} // namespace Client::Protocol::Player
