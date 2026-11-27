#include "Kernel/ActorSearch.h"

#include "Kernel/ActorState.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>

namespace Client::Kernel
{
std::size_t ActorWithKey(const WorldState& world, std::uint16_t key)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < SearchedActors; ++index)
    {
        const ActorState& actor = actors[index];
        if (actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key)
            return index;
    }
    return SearchedActors;
}
} // namespace Client::Kernel
