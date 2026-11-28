#pragma once

#include "Core/Expected.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "CastleSiege/CastleSiegeReceive.h"

#include <string>

namespace Client::CastleSiege
{
// The dispatcher's B2 11..19/1D/1E, with their messages, windows and effects on the same world.
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleBattleCommand(const Protocol::CastleSiege::CastleBattlePacket& packet, const CastleReceiveContext& context);
} // namespace Client::CastleSiege
