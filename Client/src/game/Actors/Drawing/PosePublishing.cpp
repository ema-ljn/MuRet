#include "Actors/Drawing/PosePublishing.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/FaunaModels.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Kernel/ActorState.h"
#include "Kernel/PoseBuffer.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Actors::Drawing
{
namespace
{
constexpr float VisibleAlpha = .01f;

// 0x4ebc80 on the scratch (0x5786f9c and 0x68c53bc): without `world`, by the scale and plus the object's origin
// (Translate at 1), the normals without the scale; with it, `bones` already in the world (Translate at 0), the normals
// with the scale the bones carry.
void SkinTheScratch(Kernel::PoseBuffer& buffer, const Data::Models::Model& model,
                    std::span<const Math::BoneTransform> bones, const Models::ObjectTransform& transform, bool world)
{
    if (buffer.meshes.size() < model.meshes.size())
        buffer.meshes.resize(model.meshes.size());
    for (std::size_t i = 0; i < model.meshes.size(); ++i)
    {
        Models::SkinnedMesh& mesh = buffer.skinned;
        auto& output = buffer.meshes[i];
        const bool skinned = world ? Models::SkinMeshInWorld(model.meshes[i], bones, mesh)
                                   : Models::SkinMesh(model.meshes[i], bones, transform, mesh);
        if (!skinned)
        {
            // Q-C347: a vertex or a normal on a bone the mesh does not have (mesh 1 of Crywolf's Object36.bmd names
            // −8888) makes 0x4ebc80 read outside the matrices. As MeshDrawsOf does with broken meshes, that one is not
            // posed and not drawn.
            output.positions.clear();
            output.normals.clear();
            continue;
        }
        if (output.positions.size() < mesh.positions.size())
            output.positions.resize(mesh.positions.size());
        std::copy(mesh.positions.begin(), mesh.positions.end(), output.positions.begin());
        if (output.normals.size() < mesh.normals.size())
            output.normals.resize(mesh.normals.size());
        std::copy(mesh.normals.begin(), mesh.normals.end(), output.normals.begin());
    }
}
} // namespace

Core::Expected<void, std::string> PublishPose(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models,
                                              const DrawnObject& object, bool translated)
{
    if (object.alpha < VisibleAlpha)
        return {};
    const auto* model = models.Find(Models::ModelId{object.model});
    const auto* skeleton = models.SkeletonOf(Models::ModelId{object.model});
    if (model == nullptr || skeleton == nullptr)
        return {};
    buffer.models[object.model] = object.transform;
    const auto count = skeleton->bones.size();
    if (buffer.bones.size() < count)
        buffer.bones.resize(count, Math::BoneTransform(0));
    auto bones = std::span(buffer.bones).first(count);
    // 0x5ffdf0 with its second argument at 1 (without `translated`): 0x4eb860 with the flag at 0 (the sete of 0x5fff22
    // or 0x5fff50) leaves the bones in the model and 0x4ebc80 runs with Translate at 1. With the second argument at 0:
    // 0x4eb860 with the flag at 1 (0x4ebbda..0x4ebc14) puts the scale (R · s, in float) and the origin in the root and
    // builds the bones in the world, and 0x4ebc80 runs with Translate at 0: bone · v and bone · n. The Translate of
    // both calls to 0x4ebc80 is the same `ebx` (the second argument, 0x5fff19) that 0x600154 pushes: 0x600177 with
    // +0x11c at 1 poses in the own buffer ([esi+0x120]) and 0x6001a1 with +0x11c at 0, in the global 0x68b181c. Both
    // poses skip the dummies (0x4eb973), which keep what the buffer had.
    const bool posed =
        translated ? Models::PoseWorldSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones)
                   : Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones);
    if (!posed)
        return Core::Unexpected(std::string("5ffdf0: pose de fauna inválida"));
    return PublishPoseRaw(buffer, models, object.model, bones, object.transform, translated);
}

Core::Expected<void, std::string> PublishFloorPose(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models,
                                                   const DrawnObject& object)
{
    const auto* skeleton = models.SkeletonOf(Models::ModelId{object.model});
    if (skeleton == nullptr)
        return {};
    const auto count = skeleton->bones.size();
    if (buffer.bones.size() < count)
        buffer.bones.resize(count, Math::BoneTransform(0));
    auto bones = std::span(buffer.bones).first(count);
    if (!Models::PoseSkeleton(*skeleton, object.animation, object.transform, {}, bones))
        return Core::Unexpected(std::string("608b0a: pose de ítem del piso inválida"));
    return PublishPoseRaw(buffer, models, object.model, bones, object.transform, false, object.alpha > VisibleAlpha);
}

Core::Expected<void, std::string> TransformPose(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models,
                                                const DrawnObject& object)
{
    return PublishPoseRaw(buffer, models, object.model, buffer.bones, object.transform, false);
}

Core::Expected<void, std::string> PublishPoseRaw(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models,
                                                 std::uint16_t modelId, std::span<const Math::BoneTransform> bones,
                                                 const Models::ObjectTransform& transform, bool translated, bool skin)
{
    const auto* model = models.Find(Models::ModelId{modelId});
    const auto* skeleton = models.SkeletonOf(Models::ModelId{modelId});
    const auto count = bones.size();
    buffer.models[modelId] = transform;
    if (buffer.bones.size() < count)
        buffer.bones.resize(count, Math::BoneTransform(0));
    for (std::size_t i = 0; i < count; ++i)
        if (skeleton == nullptr || i >= skeleton->bones.size() || !skeleton->bones[i].dummy)
            buffer.bones[i] = bones[i];
    if (model == nullptr)
        return {};
    bones = std::span(buffer.bones).first(count);
    if (skin)
        SkinTheScratch(buffer, *model, bones, transform, translated);
    return {};
}

Core::Expected<void, std::string> PoseWorldModel(Kernel::WorldState& world, Assets::FaunaModels* faunaModels,
                                                 const Kernel::ActorState& actor, const Models::Skeleton& skeleton,
                                                 float frame, const glm::vec3& angles)
{
    // 0x4eb86d: without actions it publishes neither metadata nor scratch, even if the model has bones.
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
} // namespace Client::Actors::Drawing
