#pragma once

#include <cstddef>
#include <cstdint>

namespace Client::Kernel
{
class WorldState;

// 0x5110d0, 0x512870 and 0x510ff0 walk 400 records, one fewer than the pool: 400 is "none".
inline constexpr std::size_t SearchedActors = 400;

// 0x5110d0: the first of the 400 alive with the key (MOVSX of +0x1f4 against the packet's int: nobody has a key from
// 0x8000 on); with none, the 400.
[[nodiscard]] std::size_t ActorWithKey(const WorldState& world, std::uint16_t key);
} // namespace Client::Kernel
