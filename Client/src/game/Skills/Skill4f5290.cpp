#include "Skills/Skill4f5290.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Skills
{
namespace
{
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t FenrirModel = 0x1c37;

// 0x4f52b5..0x4f52c3: the mount of +0x318 outside the safe zone (+0x3e2).
bool Riding(const Kernel::ActorState& actor, std::uint16_t mount)
{
    return actor.pet.model == mount && actor.safeZone == 0;
}
} // namespace

Core::Expected<void, std::string> SetTheActorAction(Kernel::ActorState& actor, std::uint8_t action,
                                                    const Combat::ActorCombatContext& context)
{
    const Models::Skeleton* skeleton = nullptr;
    if (actor.type == Tables::PlayerSkeletonModel)
        skeleton = context.playerSkeleton;
    else if (context.models != nullptr)
        skeleton = context.models->SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: falta el esqueleto del modelo {:#x}", actor.type));
    Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

Core::Expected<void, std::string> SetTheHitAction(Kernel::ActorState& actor, const Combat::ActorCombatContext& context)
{
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        // 0x4f5334..0x4f5369: IDIV of byte +0x38f by 3.
        const std::uint8_t action = actor.attackCount % 3 == 0 ? std::uint8_t{3} : std::uint8_t{4};
        if (auto acted = SetTheActorAction(actor, action, context); !acted.HasValue())
            return acted;
        ++actor.attackCount;
        return {};
    }
    Combat::RefreshCombatActionSpeeds(context.world, context.random);
    if (Riding(actor, UniriaModel) || Riding(actor, DinorantModel))
        return SetTheActorAction(actor, 0x97, context);
    if (Riding(actor, FenrirModel))
        return SetTheActorAction(actor, 0x5d, context);
    if (Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf)
        return SetTheActorAction(actor, 0x92, context);
    // 0x4f5311..0x4f5322: rand() with the signed remainder, which with a rand() from 0 to 0x7fff is bit 0.
    const int bit = context.random.Next() & 1;
    return SetTheActorAction(actor, static_cast<std::uint8_t>(0x8e + bit), context);
}
} // namespace Client::Skills
