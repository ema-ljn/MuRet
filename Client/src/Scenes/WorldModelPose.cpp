#include "Scenes/WorldModelPose.h"

#include "Core/Expected.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> PoseWorldModel(WorldState& world, const ActorState& actor,
                                                 const Render::Models::Skeleton& skeleton, float frame,
                                                 const glm::vec3& angles)
{
    // 4eb86d: sin acciones no publica ni metadatos ni scratch, aunque el modelo tenga huesos.
    if (skeleton.actions.empty())
        return {};
    SharedModelPlacement placement = world.ModelPlacement(actor.type);
    const std::uint8_t action = placement.action < skeleton.actions.size() ? placement.action : 0;
    if (placement.action != action)
    {
        placement.action = action;
        world.PlaceModel(actor.type, placement);
    }
    const Render::Models::AnimationState animation{.action = action,
                                                   .frame = frame,
                                                   .previousAction = actor.animation.previousAction,
                                                   .previousKey = actor.animation.previousKey};
    auto& carry = world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), Render::Models::BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton.bones.size());
    const Render::Models::ObjectTransform transform{
        .position = placement.origin, .angles = angles, .scale = placement.scale};
    if (!Render::Models::PoseWorldSkeleton(skeleton, animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::string("0x4eb860: no se pudo posar el modelo de la estela"));
    carry.models[actor.type] = transform;
    const auto whole = World::OriginalFtolLow(static_cast<double>(frame));
    if (!whole)
        return Core::Unexpected(std::string("0x4eb8bf: el frame de la estela no tiene conversión nativa observada"));
    std::int16_t key = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(*whole));
    if (key < 0 || key >= skeleton.actions[action].keyCount)
        key = 0;
    world.FaunaResources().RecordAnimation(actor.type, frame, key);
    return {};
}

} // namespace Client::Scenes
