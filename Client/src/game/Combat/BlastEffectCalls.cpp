#include "Combat/BlastEffectCalls.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Kernel/ModelEffectArguments.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Actors/SkillHits.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"
#include "Effects/BlastModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Combat
{
Effects::ModelEffectOwner BlastOwnerOf(const Kernel::ActorState& actor)
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
// The creation with the owner in +0x108 (what the step reads live) and its +0x8c and +0x8e.
Core::Expected<std::optional<std::size_t>, std::string> PlaceTheBlast(const Kernel::ModelEffectArguments& arguments,
                                                                      Kernel::ActorState* owner,
                                                                      Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                      const Effects::BlastEffectServices& services)
{
    Kernel::ModelEffectArguments placed = arguments;
    placed.owner = owner;
    const Effects::ModelEffectOwner live = owner != nullptr ? BlastOwnerOf(*owner) : Effects::ModelEffectOwner{};
    auto made = Effects::CreateBlastModelEffect(world.Map().effects, placed, live, random, services);
    if (made.HasValue() && made.Value().has_value() && owner != nullptr)
    {
        Effects::ModelEffect& effect = world.Map().effects.Effects()[*made.Value()];
        effect.itemType = &owner->word8c;
        effect.itemLevel = &owner->byte8e;
    }
    return made;
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string> CreateBlastEffect(const Kernel::ModelEffectArguments& arguments,
                                                                          Kernel::ActorState* owner,
                                                                          const Actors::CharacterStepContext& context)
{
    Kernel::WorldState& world = context.world;
    const Effects::BlastEffectServices services{
        .ownRecordDepth = &world.Runtime().ownRecordDepth,
        .ownRecordCount = &world.OwnCharacter().bytef04,
        .hero = &world.Hero(),
        .skillHits = [&](const Effects::ModelEffect& effect, float radius) -> Core::Expected<void, std::string>
        {
            const Actors::SkillHitsRequest request{.skillIndex = effect.byte89,
                                                   .point = effect.transform.position,
                                                   .radius = radius,
                                                   .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                                   .key = effect.disturbanceState,
                                                   .extra = 0};
            const auto hit = Actors::HitTheTargetsAround(request, world, Terrain::TerrainOrEmpty(context.terrain),
                                                         context.random, context.send);
            if (!hit.HasValue())
                return Core::Unexpected(hit.Error());
            return {};
        },
        .joint = [&](const Kernel::JointRequest& request, bool effectOwner) -> Core::Expected<void, std::string>
        {
            if (context.joints.empty())
                return Core::Unexpected(std::string("0x530860: el paso no tiene la tabla de joints (0x74a8d78)"));
            const auto made = Joints::CreateFaunaJoint(context.joints, request, context.random,
                                                       effectOwner ? Kernel::JointOwnerKind::ModelEffect
                                                                   : Kernel::JointOwnerKind::ByType);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
            return {};
        },
        // 0x41ed10(id, 0, 0): the step's sound request (WorldRuntime::RequestSound in the game).
        .sound =
            [&](Audio::SoundId id)
        {
            if (context.soundRequest)
                context.soundRequest(id);
        }};
    return PlaceTheBlast(arguments, owner, world, context.random, services);
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffectOutsideTheStep(const Kernel::ModelEffectArguments& arguments, Kernel::ActorState* owner,
                                Kernel::WorldState& world, Crt::CrtRandom& random)
{
    // 0x530860: the joint table is the global one (0x74a8d78), also outside the step (the 0x112's sixty 0x5ac/0).
    const Effects::BlastEffectServices services{
        .joint = [&world, &random](const Kernel::JointRequest& request,
                                   bool effectOwner) -> Core::Expected<void, std::string>
        {
            const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints, request, random,
                                                       effectOwner ? Kernel::JointOwnerKind::ModelEffect
                                                                   : Kernel::JointOwnerKind::ByType);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
            return {};
        }};
    return PlaceTheBlast(arguments, owner, world, random, services);
}
} // namespace Client::Combat
