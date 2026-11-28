#pragma once

#include "WorldObjects/WorldObjects.h"
#include "Kernel/WorldObject.h"

#include <cstdint>
#include <optional>

namespace Client::WorldObjects
{
// 0x606660's default chain: each handler keeps its guard/return and reads the map on entry. Unsupported is an
// integration gate; it does not vouch for the missing transitive behavior.
[[nodiscard]] std::optional<WorldObjectCreationError>
ApplyDefaultWorldObjectCreation(Kernel::WorldObject& object, WorldObjectCreationContext& context);

// 0x42b480, of map 30, at creation (0x42c6b0) and on each step of a seen object (0x42c500): with byte 0x7239ec that
// 0x4294b0 reads into `siegeState`, the 4, 0xb, 0x25, 0x29, 0x2b to 0x30, 0x43 to 0x45, 0x47 to 0x4c and 0x58 end with
// +0x58 at −1 if it is nonzero and at −2 if it is 0; the 0x10, 0x26 to 0x28, 0x32, 0x3a, 0x3b, 0x4e and 0x50 to 0x57,
// the reverse. The other types, nothing.
void ApplyCastleSiegeObjectState(Kernel::WorldObject& object, std::uint8_t siegeState);
} // namespace Client::WorldObjects
