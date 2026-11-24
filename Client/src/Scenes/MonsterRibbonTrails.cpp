#include "Scenes/MonsterRibbonTrails.h"

#include "Core/Expected.h"
#include "Math/X87.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameRibbonCreation.h"
#include "Scenes/WorldModelPose.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr int TrailSamples = 10;
constexpr float SampleStep = 0.1f;
constexpr std::int32_t MonsterRibbonType = 3;
constexpr bool ShortTrail = true;
constexpr std::uint8_t ActionThree = 3;
constexpr std::uint8_t ActionFour = 4;
constexpr std::uint16_t Monster1a7 = 0x1a7;
constexpr std::uint16_t Monster1b1 = 0x1b1;
constexpr std::uint16_t Monster1b3 = 0x1b3;
constexpr std::uint16_t Monster1b8 = 0x1b8;
constexpr std::uint16_t Monster1b9 = 0x1b9;
constexpr std::uint16_t Monster1ba = 0x1ba;
constexpr float Monster1b8FrameLimit = 5.06f;
constexpr glm::vec3 White{1.0f, 1.0f, 1.0f};
constexpr glm::vec3 Olive{0.5f, 0.5f, 0.0f};
constexpr glm::vec3 Gray{0.3f, 0.3f, 0.3f};

struct RibbonLeg
{
    std::size_t firstBone;
    std::size_t lastBone;
    glm::vec3 firstOffset;
    glm::vec3 lastOffset;
    std::int32_t key;
    glm::vec3 light;
};

Render::Models::BoneTransform BoneAt(const WorldState& world, std::size_t index)
{
    const auto& bones = world.PoseCarry().bones;
    return index < bones.size() ? bones[index] : Render::Models::BoneTransform(0.0f);
}

using Math::F32;

template <std::size_t LegCount>
Core::Expected<void, std::string> EmitSamples(ActorState& actor, const Render::Models::Skeleton& skeleton,
                                              const CharacterStepContext& context, std::uint8_t speedAction,
                                              const std::array<RibbonLeg, LegCount>& legs)
{
    if (static_cast<std::size_t>(speedAction) >= skeleton.actions.size())
        return Core::Unexpected(std::format("estela del monstruo: no existe la acción {}", speedAction));
    const float speed = skeleton.actions[speedAction].speed;
    float frame = F32(static_cast<double>(actor.animation.frame) - speed);
    const float frameStep = F32(static_cast<double>(speed) * SampleStep);
    for (int sample = 0; sample < TrailSamples; ++sample)
    {
        if (auto posed = PoseWorldModel(context.world, actor, skeleton, frame, actor.angles); !posed.HasValue())
            return posed;
        for (std::size_t legIndex = 0; legIndex < legs.size(); ++legIndex)
        {
            const auto& leg = legs[legIndex];
            const glm::vec3 first = Render::Models::OriginalVectorTransform(
                leg.firstOffset, Render::Models::OriginalMatrixOf(BoneAt(context.world, leg.firstBone)));
            const glm::vec3 second = Render::Models::OriginalVectorTransform(
                leg.lastOffset, Render::Models::OriginalMatrixOf(BoneAt(context.world, leg.lastBone)));
            const auto created = CreateRibbon(context.world.DrawTail().ribbons, {.ownerContext = &actor,
                                                                                 .ownerEquipment = &actor.level,
                                                                                 .first = first,
                                                                                 .second = second,
                                                                                 .light = leg.light,
                                                                                 .type = MonsterRibbonType,
                                                                                 .shortTrail = ShortTrail,
                                                                                 .key = leg.key});
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
        }
        frame = F32(static_cast<double>(frame) + frameStep);
    }
    return {};
}

Core::Expected<void, std::string> EmitEarly1a7(ActorState& actor, const Render::Models::Skeleton& skeleton,
                                               const CharacterStepContext& context)
{
    constexpr std::array legs{
        RibbonLeg{23, 23, {0, -10, -80}, {30, -30, -230}, 23, White},
        RibbonLeg{34, 34, {30, 10, 80}, {30, -65, 230}, 34, White},
    };
    return EmitSamples(actor, skeleton, context, actor.animation.action, legs);
}

Core::Expected<void, std::string> EmitEarly42fa40(ActorState& actor, const Render::Models::Skeleton& skeleton,
                                                  const CharacterStepContext& context)
{
    if (actor.type == Monster1b3 && actor.animation.action == ActionThree)
    {
        constexpr std::array legs{RibbonLeg{25, 25, {0, 0, 120}, {}, 25, White}};
        return EmitSamples(actor, skeleton, context, ActionThree, legs);
    }
    if (actor.type == Monster1b1 && actor.animation.action == ActionFour)
    {
        constexpr std::array legs{
            RibbonLeg{80, 80, {0, 0, -90}, {}, 80, White},
            RibbonLeg{82, 82, {0, 0, 90}, {}, 84, White},
        };
        return EmitSamples(actor, skeleton, context, ActionFour, legs);
    }
    return {};
}

Core::Expected<void, std::string> EmitEarly(ActorState& actor, const Render::Models::Skeleton& skeleton,
                                            const CharacterStepContext& context)
{
    if (actor.type == Monster1a7 && (actor.animation.action == ActionThree || actor.animation.action == ActionFour))
        return EmitEarly1a7(actor, skeleton, context);
    return EmitEarly42fa40(actor, skeleton, context);
}

Core::Expected<void, std::string> EmitLate(ActorState& actor, const Render::Models::Skeleton& skeleton,
                                           const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    const bool swing = action == ActionThree || action == ActionFour;
    if (actor.type == Monster1b8 && swing)
    {
        if (actor.animation.frame > Monster1b8FrameLimit)
            return {};
        constexpr std::array legs{
            RibbonLeg{18, 20, {20, 0, 0}, {100, 0, 0}, 23, White},
            RibbonLeg{25, 26, {20, 0, 0}, {100, 0, 0}, 24, White},
        };
        return EmitSamples(actor, skeleton, context, action, legs);
    }
    if (actor.type == Monster1b9 && swing)
    {
        constexpr std::array legs{
            RibbonLeg{35, 37, {20, 0, 0}, {60, 0, 0}, 23, Olive},
            RibbonLeg{39, 41, {20, 0, 0}, {60, 0, 0}, 24, Olive},
            RibbonLeg{43, 45, {20, 0, 0}, {60, 0, 0}, 25, Olive},
        };
        return EmitSamples(actor, skeleton, context, action, legs);
    }
    if (actor.type == Monster1ba && action == ActionThree)
    {
        constexpr std::array legs{RibbonLeg{26, 27, {0, 0, 20}, {0, 0, 70}, 24, Gray}};
        return EmitSamples(actor, skeleton, context, ActionThree, legs);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> EmitEarlyMonsterRibbonTrails(ActorState& actor,
                                                               const Render::Models::Skeleton* skeleton,
                                                               const CharacterStepContext& context)
{
    const bool eligible =
        (actor.type == Monster1a7 && (actor.animation.action == ActionThree || actor.animation.action == ActionFour)) ||
        (actor.type == Monster1b3 && actor.animation.action == ActionThree) ||
        (actor.type == Monster1b1 && actor.animation.action == ActionFour);
    if (!eligible)
        return {};
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("estela del monstruo: falta el esqueleto"));
    return EmitEarly(actor, *skeleton, context);
}

Core::Expected<void, std::string> EmitLateMonsterRibbonTrails(ActorState& actor,
                                                              const Render::Models::Skeleton* skeleton,
                                                              const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    const bool eligible = (actor.type == Monster1b8 && (action == ActionThree || action == ActionFour)) ||
                          (actor.type == Monster1b9 && (action == ActionThree || action == ActionFour)) ||
                          (actor.type == Monster1ba && action == ActionThree);
    if (!eligible || (actor.type == Monster1b8 && actor.animation.frame > Monster1b8FrameLimit))
        return {};
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("estela del monstruo: falta el esqueleto"));
    return EmitLate(actor, *skeleton, context);
}
} // namespace Client::Scenes
