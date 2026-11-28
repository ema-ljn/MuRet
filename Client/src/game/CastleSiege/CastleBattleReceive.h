#pragma once

#include "Core/Expected.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "CastleSiege/CastleSiegeReceive.h"

#include <cstdint>
#include <string>

namespace Client::CastleSiege
{
struct CastleBattleState;
}

// The incoming castle battle packets (CL9 milestone 13; 04 § Castle Siege, la batalla): what 0x4e2b10 and its
// functions do with B9 01/02/03/05, BA, BB and B2 20 on WorldRuntime::castleSiege, the characters and the panels.
// None looks at the map, the scene or the encryption flag, and none takes rand() beyond what the panel registry opens.
namespace Client::CastleSiege
{
// 0x4e2b10 with a battle packet: B9 01 (0x4e0750), 02 (0x4e0790), 03 (0x4e07f0) and 05 (0x4e0860); BA (0x4cf1b0),
// BB (0x4e0f30) and B2 20 (0x4e04e0). What belongs to another wave or milestone stops, with its address.
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleBattlePacket(const Protocol::CastleSiege::CastleBattlePacket& packet, const CastleReceiveContext& context);

// 0x4ca799..0x4ca7cd: what 0x14 (0x4ca780, the one that removes characters from the viewport) does for each key with
// the object of 0x5767924, before 0x510ff0: with the whole key (without the AND 0x7fff) at 0x13d it clears +4, +5 and
// +6; with another, +0x1f, +0x20 and +0x21. Without an object, nothing.
void ForgetTheSwitchHolder(Kernel::WorldRuntime& runtime, std::uint16_t key);
} // namespace Client::CastleSiege
