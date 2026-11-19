#include "Scenes/BlastEffectCalls.h"

#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SkillHits.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/BlastModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
World::ModelEffectOwner BlastOwnerOf(const ActorState& actor)
{
    return {.type = &actor.type,
            .effect = nullptr,
            .angles = &actor.angles,
            .position = &actor.position,
            .alive = &actor.alive,
            .seen = &actor.seen};
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffect(const World::ModelEffectArguments& arguments, ActorState* owner, const CharacterStepContext& context)
{
    WorldState& world = context.world;
    const World::BlastEffectServices services{
        .ownRecordDepth = &world.Runtime().ownRecordDepth,
        .ownRecordCount = &world.OwnCharacter().bytef04,
        .hero = &world.Actors()[world.Runtime().hero],
        .skillHits = [&](const World::ModelEffect& effect, float radius) -> Core::Expected<void, std::string>
        {
            const SkillHitsRequest request{.skillIndex = effect.byte89,
                                           .point = effect.transform.position,
                                           .radius = radius,
                                           .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                           .key = effect.disturbanceState,
                                           .extra = 0};
            const auto hit = HitTheTargetsAround(request, world, context.random, context.send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        },
        .joint = [&](const World::FishEffectRequest& request, bool effectOwner) -> Core::Expected<void, std::string>
        {
            if (context.joints.empty())
                return Core::Unexpected(std::string("0x530860: el paso no tiene la tabla de joints (0x74a8d78)"));
            const auto made = CreateFaunaJoint(context.joints, request, context.random,
                                               effectOwner ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
            return {};
        },
        // 0x41ed10(id, 0, 0): el pedido de sonido del paso (WorldRuntime::RequestSound en el juego).
        .sound =
            [&](std::int32_t id)
        {
            if (context.soundRequest)
                context.soundRequest(id);
        }};
    World::ModelEffectArguments placed = arguments;
    placed.owner = owner;
    const World::ModelEffectOwner live = owner != nullptr ? BlastOwnerOf(*owner) : World::ModelEffectOwner{};
    auto made = World::CreateBlastModelEffect(world.Map().effects, placed, live, context.random, services);
    if (made.HasValue() && made.Value().has_value() && owner != nullptr)
    {
        World::ModelEffect& effect = world.Map().effects.Effects()[*made.Value()];
        effect.itemType = &owner->word8c;
        effect.itemLevel = &owner->byte8e;
    }
    return made;
}
} // namespace Client::Scenes
