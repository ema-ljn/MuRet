#include "Actors/Drawing/WeaponTrail.h"

#include "Core/Expected.h"
#include "Math/X87.h"

#include "Tables/PlayerModels.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Kernel/DrawTailState.h"
#include "Actors/Drawing/GameRibbonCreation.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Kernel/WorldState.h"
#include "WorldObjects/WorldModelPose.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Actors::Drawing
{
namespace
{
using Result = Core::Expected<void, std::string>;
// 0x4fd482..0x4fd5bb: ten samples; each increment is published as a float.
constexpr int TrailSamples = 10;
constexpr float SampleStep = 0.1f;
constexpr std::uint8_t AlwaysTrailed = 0x8d;
constexpr glm::vec3 White{1.0f, 1.0f, 1.0f};

struct Trail
{
    int mode = 0;
    std::int32_t type = 0;
    int model = 0;
    std::uint8_t level = 0;
    std::size_t firstHand = 0;
    std::size_t secondHand = 0;
};

using Math::F32;

Trail PlayerTrail(const Kernel::ActorState& actor, Trail trail)
{
    const std::uint8_t action = actor.animation.action;
    const int model = trail.model;
    trail.mode = 1;
    if (action == 0x84 || action == 0x85 || action == 0x4c)
        trail.type = 2;
    else if (action >= 0x39 && action <= 0x3b)
        trail.type = model == 0x220 || model == 0x227 || model == 0x231 ? 1 : 2;
    else if (action == 0x3c)
        trail.type = model == 0x617 ? 1 : 2;
    else if (model >= 0x212 && model < 0x412 && ((action >= 0x23 && action <= 0x29) || action == AlwaysTrailed))
        trail.type = model == 0x223 ? 6 : (action == 0x29 || action == AlwaysTrailed ? 1 : 0);
    else if ((model == 0x415 || (model >= 0x417 && model < 0x812)) && action >= 0x38 && action <= 0x3c)
        trail.type = 2;
    else if (model >= 0x812 && model < 0xa12 && action >= 0x2a && action <= 0x2d)
    {
        trail.mode = model == 0x81c ? 1 : 3;
        trail.type = model != 0x81c && action == 0x2d ? 1 : 0;
    }
    else
        trail.mode = 0;
    return trail;
}

Trail MonsterTrail(const Kernel::ActorState& actor, Trail trail)
{
    const bool swing = actor.animation.action == 3 || actor.animation.action == 4;
    if (!swing)
        return trail;
    trail.mode = 1;
    if (actor.word376 == 0x47 || actor.word376 == 0x4a || actor.word376 == 0x12d)
        trail.type = 6;
    else if (actor.type == 0x194)
    {
        // 0x4fd13f jumps to the helpers: action 4 does not fall back to the generic weapon.
        if (actor.animation.action != 3)
        {
            trail.mode = 0;
            return trail;
        }
        trail.mode = 5;
        trail.type = 2;
        trail.secondHand = 1;
    }
    else if (actor.type == 0x195)
    {
        trail.mode = 4;
        trail.level = 99;
    }
    else if (actor.type == 0x197)
    {
        if (actor.subtype == 9)
            trail.type = 2;
        else
        {
            trail.level = 99;
            trail.model = 0;
        }
    }
    else if (trail.model < 0x212 || trail.model >= 0x412)
        trail.mode = 0;
    return trail;
}

Trail TrailOf(const Kernel::ActorState& actor)
{
    const bool left = actor.animation.action == 0x25 || actor.animation.action == 0x26;
    const std::size_t hand = actor.type == Tables::PlayerSkeletonModel && left ? 1 : 0;
    Trail trail{.model = static_cast<std::int16_t>(actor.hands[hand].model),
                .level = actor.hands[hand].level,
                .firstHand = hand,
                .secondHand = hand};
    return actor.type == Tables::PlayerSkeletonModel ? PlayerTrail(actor, trail) : MonsterTrail(actor, trail);
}

// 0x4fd2e4..0x4fd3a4: the red weapon prevails over level 99 and over the ribbon type.
glm::vec3 LightOf(const Trail& trail)
{
    if (trail.model == 0x21f || trail.model == 0x618 || trail.model == 0x81b)
        return {1.0f, 0.2f, 0.2f};
    if (trail.level == 99)
        return {0.3f, 0.2f, 1.0f};
    if (trail.type != 0)
        return White;
    if (trail.level < 3)
        return {0.8f, 0.8f, 0.8f};
    if (trail.level < 5)
        return {1.0f, 0.2f, 0.2f};
    if (trail.level < 7)
        return {0.2f, 0.4f, 1.0f};
    return {1.0f, 0.6f, 0.2f};
}

std::array<glm::vec3, 2> OffsetsOf(int mode)
{
    switch (mode)
    {
    case 3:
        return {glm::vec3{0, -100, 0}, glm::vec3{0, -120, 0}};
    case 4:
        return {glm::vec3{0, 0, 0}, glm::vec3{0, -200, 0}};
    case 5:
        return {glm::vec3{0, 0, 0}, glm::vec3{0, -20, 0}};
    default:
        return {glm::vec3{0, -20, 0}, glm::vec3{0, -120, 0}};
    }
}

Math::BoneTransform BoneOf(std::span<const Math::BoneTransform> bones, std::size_t index)
{
    // Global or own matrices never posed: the .bss zeros (Q-C318), not the identity.
    return index < bones.size() ? bones[index] : Math::BoneTransform(0.0f);
}

Result Append(Kernel::ActorState& actor, const Trail& trail, const glm::vec3& first, const glm::vec3& second,
              Kernel::WorldState& world, bool shortTrail)
{
    const auto created = CreateRibbon(world.DrawTail().ribbons, {.ownerContext = &actor,
                                                                 .ownerEquipment = &actor.level,
                                                                 .first = first,
                                                                 .second = second,
                                                                 .light = LightOf(trail),
                                                                 .type = trail.type,
                                                                 .shortTrail = shortTrail,
                                                                 .key = 0});
    if (!created.HasValue())
        return Core::Unexpected("0x4fd46f/0x4fd5a4 → " + created.Error());
    return {};
}

bool SingleSample(const Kernel::ActorState& actor, int model)
{
    if (actor.type != Tables::PlayerSkeletonModel)
        return actor.type != 0x194 && actor.type != 0x195 && actor.type != 0x197;
    return model == 0x215 || model == 0x218 || model == 0x21b || model == 0x21d || model == 0x816;
}

Result SampleParticles(Kernel::ActorState& actor, const Trail& trail, const glm::vec3& point,
                       const CharacterStepContext& context, int sample)
{
    if (actor.type != 0x194 || sample % 2 == 0)
        return {};
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x4fd585: faltan las partículas de la estela del 0x194"));
    const auto created =
        context.particles->CreateChecked(Particles::BlueTrailFireParticle, point, actor.angles, LightOf(trail),
                                         Particles::BlueTrailFireSubtype, 1.0f, context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::string("0x4fd585: falló el alta de la partícula 0x5dc/12 de la estela"));
    return {};
}

Result SampleRibbon(Kernel::ActorState& actor, const Trail& trail, const Models::Skeleton& skeleton,
                    const CharacterStepContext& context)
{
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(actor.type);
    if (placement.action >= skeleton.actions.size())
        return Core::Unexpected(std::string("0x4fd487: la acción compartida de la estela pasa su tabla"));
    const auto& speeds = context.actionSpeeds != nullptr ? *context.actionSpeeds : context.world.ActionSpeeds();
    const float base = skeleton.actions[placement.action].speed;
    const float speed = actor.type == Tables::PlayerSkeletonModel ? speeds.Speed(placement.action, base) : base;
    float frame = F32(static_cast<double>(actor.animation.frame) - speed);
    const float step = F32(static_cast<double>(speed) * SampleStep);
    const auto offsets = OffsetsOf(trail.mode);
    for (int sample = 0; sample < TrailSamples; ++sample)
    {
        if (auto posed =
                WorldObjects::PoseWorldModel(context.world, context.faunaModels, actor, skeleton, frame, actor.angles);
            !posed.HasValue())
            return posed;
        const auto& bones = context.world.PoseCarry().bones;
        const auto first = Math::OriginalVectorTransform(
            offsets[0], Math::OriginalMatrixOf(BoneOf(bones, actor.hands[trail.firstHand].bone)));
        const auto second = Math::OriginalVectorTransform(
            offsets[1], Math::OriginalMatrixOf(BoneOf(bones, actor.hands[trail.secondHand].bone)));
        if (auto emitted = SampleParticles(actor, trail, second, context, sample); !emitted.HasValue())
            return emitted;
        if (auto added = Append(actor, trail, first, second, context.world, true); !added.HasValue())
            return added;
        frame = F32(static_cast<double>(frame) + step);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> EmitWeaponTrail(Kernel::ActorState& actor, const Models::Skeleton* skeleton,
                                                  const CharacterStepContext& context)
{
    const Trail trail = TrailOf(actor);
    if (trail.mode == 0)
        return {};
    if (!SingleSample(actor, trail.model))
    {
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4fd482: falta el modelo de la estela"));
        return SampleRibbon(actor, trail, *skeleton, context);
    }
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(actor.type);
    const auto offsets = OffsetsOf(trail.mode);
    const auto first = Math::OriginalTransformPosition(
        Math::OriginalMatrixOf(BoneOf(actor.bodyPose, actor.hands[trail.firstHand].bone)), offsets[0], placement.scale,
        placement.origin);
    const auto second = Math::OriginalTransformPosition(
        Math::OriginalMatrixOf(BoneOf(actor.bodyPose, actor.hands[trail.secondHand].bone)), offsets[1], placement.scale,
        placement.origin);
    return Append(actor, trail, first, second, context.world, false);
}
} // namespace Client::Actors::Drawing
