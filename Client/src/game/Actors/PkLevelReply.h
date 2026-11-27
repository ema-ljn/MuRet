#pragma once

#include "Core/Expected.h"
#include "Protocol/Player/PkLevel.h"
#include "Session/WorldEntry.h"

#include <string>

// 0x4d3b50, the F3 08: a character's PK state. 0x4e30cd calls it with the packet.
namespace Client::Actors
{
// The character with the key (0x5110d0, without looking at the result: 400 if there is nobody) keeps the state in
// +0x375 and, in +0x1d6 (the word the list and F3 03 write as the level), 1 from 6 (JC) and 0 otherwise. From 1 to 6,
// a notice to the chat (0x555190) with the character's name (+0x1d9, from another call to 0x5110d0): 487 with 1 and 2
// and 488 with 3, in type 1; 489 with 4, and "%s %d%s" (0x6c843c and 0x6c8444) with 490, 1 or 2 and 491 with 5 and
// 6, in type 2. That sprintf goes to a 0x20-byte stack buffer: if it does not fit, it stops. A text that is not there
// stops.
[[nodiscard]] Core::Expected<void, std::string> ApplyPkLevel(const Protocol::Player::PkLevel& packet,
                                                             const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
