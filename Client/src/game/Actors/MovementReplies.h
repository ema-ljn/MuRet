#pragma once

#include "Core/Expected.h"
#include "Kernel/PlayerKey.h"

#include <string>

namespace Client::Protocol::Movement
{
struct Walk;
struct Damage;
} // namespace Client::Protocol::Movement
namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Actors
{
// 06, "Lo que llega": D3 and D7 keep the protected coordinates and the CRT's order.
[[nodiscard]] Core::Expected<void, std::string> ApplyWalkReply(const Protocol::Movement::Walk& reply,
                                                               Kernel::PlayerKey player,
                                                               const Scenes::WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> ApplyDamageReply(const Protocol::Movement::Damage& reply,
                                                                 Kernel::PlayerKey player,
                                                                 const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
