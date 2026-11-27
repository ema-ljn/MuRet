#pragma once

#include "Core/Id.h"

#include <cstdint>

namespace Client::Kernel
{
// The key of the player's own character, from the F1 00 (App::ServerSession::PlayerIndex).
using PlayerKey = Core::Id<struct PlayerKeyTag, std::uint16_t>;
} // namespace Client::Kernel
