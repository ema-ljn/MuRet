#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Protocol::Movement
{
struct Walk;
struct Damage;
} // namespace Client::Protocol::Movement
namespace Client::Scenes
{
struct WorldEntryResources;
// 06, «Lo que llega»: D3 y D7 conservan las coordenadas protegidas y el orden de la CRT.
[[nodiscard]] Core::Expected<void, std::string>
ApplyWalkReply(const Protocol::Movement::Walk& reply, std::uint16_t playerKey, const WorldEntryResources& resources);
[[nodiscard]] Core::Expected<void, std::string> ApplyDamageReply(const Protocol::Movement::Damage& reply,
                                                                 std::uint16_t playerKey,
                                                                 const WorldEntryResources& resources);
} // namespace Client::Scenes
