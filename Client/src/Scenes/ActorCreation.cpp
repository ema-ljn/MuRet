#include "Scenes/ActorCreation.h"

#include "Core/Expected.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/HelperTable.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Scenes
{
namespace
{
bool HasTheKey(const ActorState& actor, std::int32_t key)
{
    return actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key;
}
} // namespace

void ForgetNamedBones(WorldRuntime& runtime, std::size_t actor)
{
    std::erase_if(runtime.namedBones, [actor](const NamedBone& bone) { return bone.actor == actor; });
}

void NameABone(WorldRuntime& runtime, std::size_t actor, std::string_view name, std::uint32_t bone)
{
    std::erase_if(runtime.namedBones,
                  [actor, name](const NamedBone& node) { return node.actor == actor && node.name == name; });
    runtime.namedBones.push_back({actor, std::string(name), bone});
}

Core::Expected<std::size_t, std::string> CreateActor(WorldState& world, std::int32_t key, std::uint16_t type,
                                                     const ActorPlacement& placement,
                                                     const ActorConstructionContext& context,
                                                     Render::Models::ClothWorld& cloth)
{
    const auto actors = world.Actors();
    // 0x512885..0x5128a0, y 0x5128c3..0x5128df.
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        ActorState& actor = actors[slot];
        if (!HasTheKey(actor, key))
            continue;
        if (auto built = ConstructActor(actor, type, placement, context, cloth); !built.HasValue())
            return Core::Unexpected(built.Error());
        actor.flags78 = 0;
        actor.flags7c = 0;
        return slot;
    }
    // 0x5128a2..0x5128b6, y 0x5128e9..0x512926.
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        ActorState& actor = actors[slot];
        if (actor.alive)
            continue;
        ForgetNamedBones(world.Runtime(), slot);
        actor.darkSpirit.reset();
        ReleaseTheLayer(actor);
        if (auto built = ConstructActor(actor, type, placement, context, cloth); !built.HasValue())
            return Core::Unexpected(built.Error());
        actor.key = static_cast<std::uint16_t>(key);
        actor.flags78 = 0;
        actor.flags7c = 0;
        return slot;
    }
    // 0x5128ba: el 400, como está.
    return CreatedActorSearch;
}

void RemoveActor(WorldState& world, std::int32_t key)
{
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        ActorState& actor = actors[slot];
        if (!HasTheKey(actor, key))
            continue;
        actor.alive = false;
        ForgetNamedBones(world.Runtime(), slot);
        TurnOffHelpersOf(world.Helpers(), slot);
        actor.darkSpirit.reset();
        ReleaseTheLayer(actor);
        return;
    }
}
} // namespace Client::Scenes
