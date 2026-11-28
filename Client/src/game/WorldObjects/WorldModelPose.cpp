#include "WorldObjects/WorldModelPose.h"

#include "Core/Expected.h"

#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <string>

namespace Client::WorldObjects
{
Core::Expected<void, std::string> PoseWorldModel(Kernel::WorldState& world, Assets::FaunaModels* faunaModels,
                                                 const Kernel::ActorState& actor, const Models::Skeleton& skeleton,
                                                 float frame, const glm::vec3& angles)
{
    // 4eb86d: sin acciones no publica ni metadatos ni scratch, aunque el modelo tenga huesos.
    if (skeleton.actions.empty())
        return {};
    if (faunaModels == nullptr)
        return Core::Unexpected(std::string("0x4eb860: no fauna models (0x5769ad8)"));
    Kernel::SharedModelPlacement placement = world.ModelPlacement(actor.type);
    const std::uint8_t action = placement.action < skeleton.actions.size() ? placement.action : 0;
    if (placement.action != action)
    {
        placement.action = action;
        world.PlaceModel(actor.type, placement);
    }
    const Models::AnimationState animation{.action = action,
                                           .frame = frame,
                                           .previousAction = actor.animation.previousAction,
                                           .previousKey = actor.animation.previousKey};
    auto& carry = world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), Math::BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton.bones.size());
    const Models::ObjectTransform transform{.position = placement.origin, .angles = angles, .scale = placement.scale};
    if (!Models::PoseWorldSkeleton(skeleton, animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::string("0x4eb860: no se pudo posar el modelo de la estela"));
    carry.models[actor.type] = transform;
    const auto whole = Math::OriginalFtolLow(static_cast<double>(frame));
    if (!whole)
        return Core::Unexpected(std::string("0x4eb8bf: el frame de la estela no tiene conversión nativa observada"));
    std::int16_t key = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(*whole));
    if (key < 0 || key >= skeleton.actions[action].keyCount)
        key = 0;
    faunaModels->RecordAnimation(actor.type, frame, key);
    return {};
}

} // namespace Client::WorldObjects
