#include "Scenes/Skill4f5290.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CombatActionSpeeds.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t ClassMask = 7;

// 0x4f52b5..0x4f52c3: la montura de +0x318 fuera de la zona segura (+0x3e2).
bool Riding(const ActorState& actor, std::uint16_t mount)
{
    return actor.pet.model == mount && actor.safeZone == 0;
}
} // namespace

Core::Expected<void, std::string> SetTheActorAction(ActorState& actor, std::uint8_t action,
                                                    const ActorCombatContext& context)
{
    const Render::Models::Skeleton* skeleton = nullptr;
    if (actor.type == Data::Models::PlayerSkeletonModel)
        skeleton = context.playerSkeleton;
    else if (context.models != nullptr)
        skeleton = context.models->SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: falta el esqueleto del modelo {:#x}", actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

Core::Expected<void, std::string> SetTheHitAction(ActorState& actor, const ActorCombatContext& context)
{
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        // 0x4f5334..0x4f5369: IDIV del byte +0x38f por 3.
        const std::uint8_t action = actor.attackCount % 3 == 0 ? std::uint8_t{3} : std::uint8_t{4};
        if (auto acted = SetTheActorAction(actor, action, context); !acted.HasValue())
            return acted;
        ++actor.attackCount;
        return {};
    }
    RefreshCombatActionSpeeds(context.world, context.random);
    if (Riding(actor, UniriaModel) || Riding(actor, DinorantModel))
        return SetTheActorAction(actor, 0x97, context);
    if (Riding(actor, FenrirModel))
        return SetTheActorAction(actor, 0x5d, context);
    if ((actor.characterClass & ClassMask) == ElfClass)
        return SetTheActorAction(actor, 0x92, context);
    // 0x4f5311..0x4f5322: rand() con el resto con signo, que con un rand() de 0 a 0x7fff es el bit 0.
    const int bit = context.random.Next() & 1;
    return SetTheActorAction(actor, static_cast<std::uint8_t>(0x8e + bit), context);
}
} // namespace Client::Scenes
