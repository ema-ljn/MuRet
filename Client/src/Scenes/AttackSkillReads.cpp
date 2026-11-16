#include "Scenes/AttackSkillReads.h"

#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
std::uint8_t ReadTheSkill(ActorState& actor, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

std::int32_t OwnSkillIndex(const CharacterStepContext& context, std::uint8_t skill)
{
    constexpr std::size_t OwnSkills = 64;
    WorldRuntime& runtime = context.world.Runtime();
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    for (std::size_t index = 0; index < OwnSkills; ++index)
    {
        if (context.world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}
} // namespace Client::Scenes
