#pragma once

#include "Core/Expected.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Scenes/CastleSiegeReceive.h"

#include <string>

namespace Client::Scenes
{
// Los B2 11..19/1D/1E del dispatcher, con sus mensajes, ventanas y efectos sobre el mismo mundo.
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleBattleCommand(const Protocol::CastleSiege::CastleBattlePacket& packet, const CastleReceiveContext& context);
} // namespace Client::Scenes
