#include "Scenes/BlastEffectCalls.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SkillHits.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "World/BlastModelEffects.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
World::ModelEffectOwner BlastOwnerOf(const Kernel::ActorState& actor)
{
    return {.type = &actor.type,
            .effect = nullptr,
            .angles = &actor.angles,
            .position = &actor.position,
            .alive = &actor.alive,
            .seen = &actor.seen};
}

namespace
{
// El alta con el dueño en +0x108 (lo que el paso lee en vivo) y sus +0x8c y +0x8e.
Core::Expected<std::optional<std::size_t>, std::string> PlaceTheBlast(const World::ModelEffectArguments& arguments,
                                                                      Kernel::ActorState* owner,
                                                                      Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                      const World::BlastEffectServices& services)
{
    World::ModelEffectArguments placed = arguments;
    placed.owner = owner;
    const World::ModelEffectOwner live = owner != nullptr ? BlastOwnerOf(*owner) : World::ModelEffectOwner{};
    auto made = World::CreateBlastModelEffect(world.Map().effects, placed, live, random, services);
    if (made.HasValue() && made.Value().has_value() && owner != nullptr)
    {
        World::ModelEffect& effect = world.Map().effects.Effects()[*made.Value()];
        effect.itemType = &owner->word8c;
        effect.itemLevel = &owner->byte8e;
    }
    return made;
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string> CreateBlastEffect(const World::ModelEffectArguments& arguments,
                                                                          Kernel::ActorState* owner,
                                                                          const Actors::CharacterStepContext& context)
{
    Kernel::WorldState& world = context.world;
    const World::BlastEffectServices services{
        .ownRecordDepth = &world.Runtime().ownRecordDepth,
        .ownRecordCount = &world.OwnCharacter().bytef04,
        .hero = &world.Hero(),
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
    return PlaceTheBlast(arguments, owner, world, context.random, services);
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffectOutsideTheStep(const World::ModelEffectArguments& arguments, Kernel::ActorState* owner,
                                Kernel::WorldState& world, Crt::CrtRandom& random)
{
    // 0x530860: la tabla de joints es la global (0x74a8d78), también fuera del paso (los sesenta 0x5ac/0 del 0x112).
    const World::BlastEffectServices services{
        .joint = [&world, &random](const World::FishEffectRequest& request,
                                   bool effectOwner) -> Core::Expected<void, std::string>
        {
            const auto made = CreateFaunaJoint(world.DrawTail().joints, request, random,
                                               effectOwner ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
            return {};
        }};
    return PlaceTheBlast(arguments, owner, world, random, services);
}
} // namespace Client::Scenes
