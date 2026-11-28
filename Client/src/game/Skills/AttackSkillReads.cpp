#include "Skills/AttackSkillReads.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>

namespace Client::Skills
{
std::uint8_t ReadTheSkill(Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

std::int32_t OwnSkillIndex(const Actors::CharacterStepContext& context, std::uint8_t skill)
{
    constexpr std::size_t OwnSkills = 64;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    for (std::size_t index = 0; index < OwnSkills; ++index)
    {
        if (context.world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}
} // namespace Client::Skills
