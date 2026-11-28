#include "Actors/ActorCreation.h"

#include "Core/Expected.h"
#include "Actors/ActorConstruction.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Kernel/HelperTable.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Actors
{
namespace
{
bool HasTheKey(const Kernel::ActorState& actor, std::int32_t key)
{
    return actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key;
}
} // namespace

void ForgetNamedBones(Kernel::WorldRuntime& runtime, std::size_t actor)
{
    std::erase_if(runtime.namedBones, [actor](const Kernel::NamedBone& bone) { return bone.actor == actor; });
}

void NameABone(Kernel::WorldRuntime& runtime, std::size_t actor, std::string_view name, std::uint32_t bone)
{
    std::erase_if(runtime.namedBones,
                  [actor, name](const Kernel::NamedBone& node) { return node.actor == actor && node.name == name; });
    runtime.namedBones.push_back({actor, std::string(name), bone});
}

Core::Expected<std::size_t, std::string> CreateActor(Kernel::WorldState& world, std::int32_t key, std::uint16_t type,
                                                     const ActorPlacement& placement,
                                                     const ActorConstructionContext& context, Models::ClothWorld& cloth)
{
    const auto actors = world.Actors();
    // 0x512885..0x5128a0, and 0x5128c3..0x5128df.
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        if (!HasTheKey(actor, key))
            continue;
        if (auto built = ConstructActor(actor, type, placement, context, cloth); !built.HasValue())
            return Core::Unexpected(built.Error());
        actor.flags78 = 0;
        actor.flags7c = 0;
        return slot;
    }
    // 0x5128a2..0x5128b6, and 0x5128e9..0x512926.
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        if (actor.alive)
            continue;
        ForgetNamedBones(world.Runtime(), slot);
        actor.darkSpirit.reset();
        Kernel::ReleaseTheLayer(actor);
        world.ActorCloths()[slot] = {};
        if (auto built = ConstructActor(actor, type, placement, context, cloth); !built.HasValue())
            return Core::Unexpected(built.Error());
        actor.key = static_cast<std::uint16_t>(key);
        actor.flags78 = 0;
        actor.flags7c = 0;
        return slot;
    }
    // 0x5128ba: 400, as it is.
    return CreatedActorSearch;
}

void RemoveActor(Kernel::WorldState& world, std::int32_t key)
{
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < CreatedActorSearch; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        if (!HasTheKey(actor, key))
            continue;
        actor.alive = false;
        ForgetNamedBones(world.Runtime(), slot);
        Kernel::TurnOffHelpersOf(world.Helpers(), slot);
        actor.darkSpirit.reset();
        Kernel::ReleaseTheLayer(actor);
        world.ActorCloths()[slot] = {};
        return;
    }
}

void RemoveActorAtSlot(Kernel::WorldState& world, Kernel::ActorState& actor, std::size_t slot)
{
    actor.alive = false;
    if (slot < CreatedActorSearch)
    {
        ForgetNamedBones(world.Runtime(), slot);
        Kernel::TurnOffHelpersOf(world.Helpers(), slot);
    }
    actor.darkSpirit.reset();
    if (slot < CreatedActorSearch)
        world.ActorCloths()[slot].ReleaseResources();
    Kernel::ReleaseTheLayer(actor);
}
} // namespace Client::Actors
