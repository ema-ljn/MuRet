#include "Effects/SkinShells.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorPickingBox.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Kernel/WorldState.h"
#include "Effects/ActorEffectHooks.h"
#include "Effects/ModelEffects.h"
#include "Effects/SkinShellEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Client::Effects
{
namespace
{
constexpr Render::TextureSlot ChromeTexture{0x5c0}; // Chrome01, the 0x5c0 of 0x6005c0 and 0x60056e
constexpr std::uint32_t AdditiveBodyFlags = 0x42;   // 0x6005c7
constexpr float VisibleAlpha = 0.01f;               // 0x6b88d4, 0x5ffdf0's
} // namespace

Core::Expected<void, std::string> ShedTheSkinShells(Kernel::ActorState& owner, std::int32_t subtype,
                                                    const Models::IModelLibrary& models, Kernel::WorldState& world,
                                                    Effects::ModelEffects& effects, Crt::CrtRandom& random)
{
    constexpr std::uint16_t Shadow = Tables::PlayerShadowModel;
    const Data::Models::Model* model = models.Find(Models::ModelId{Shadow});
    const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{Shadow});
    if (model == nullptr || skeleton == nullptr)
        return Core::Unexpected(std::string("0x53c1eb: falta el modelo 0x20b (Player\\Shadow01) del joint 0x632"));
    // 0x53c1c6..0x53c1eb: the frame, the previous one and the previous action at 0; the action, scale and origin, the
    // model's (+0xa0, +0x68 and +0x6c); +0x84 stays at 0 (only 0x6001b0 writes it, and never on the 0x20b).
    const Kernel::SharedModelPlacement placement = world.ModelPlacement(Shadow);
    const Models::AnimationState animation{
        .action = placement.action, .frame = 0.0f, .previousAction = 0, .previousKey = 0};
    const Models::ObjectTransform transform{
        .position = placement.origin, .angles = owner.angles, .scale = placement.scale, .lockedRootHeight = 0.0f};
    std::vector<Math::BoneTransform> bones(skeleton->bones.size(), Math::BoneTransform(0.0f));
    if (!Models::PoseWorldSkeleton(*skeleton, animation, transform, owner.headTurn.angles, bones))
        return Core::Unexpected(std::string("0x53c1eb: la pose del 0x20b (Player\\Shadow01) del joint 0x632 falla"));
    // 0x53c1f0..0x53c210: with the last argument at 0 and the bones' scale (0x6c881c) at 1 (0x4ebdb0), each vertex only
    // by its bone's matrix (0x5fccc0). It is 1 because 0x5ffdf0 and 0x60cf70 set it to 1 before checking the mode
    // (0x5fff82, 0x60d01e) and the only modes 2 and 3 (0x50c20b, 0x50c217) are followed by 0x50c248 (Q-C1730; in Chaos
    // Castle each 0x5ffdf0 subtracts 0.15f from the alpha and does not jump at 0x5ffe3b below 0.01f: it would stay at
    // 1.4f if 0x50c248 does not run, or at 1.2f if 0x50c217 does not either, P-C521).
    std::vector<Models::SkinnedMesh> meshes(model->meshes.size());
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        for (const Data::Models::Vertex& vertex : model->meshes[index].vertices)
        {
            const auto bone = static_cast<std::size_t>(vertex.bone);
            if (vertex.bone < 0 || bone >= bones.size())
                return Core::Unexpected(std::string("0x53c210: un vértice del 0x20b nombra un hueso que no está"));
            const glm::vec3 local{vertex.position[0], vertex.position[1], vertex.position[2]};
            meshes[index].positions.push_back(
                Math::OriginalVectorTransform(local, Math::OriginalMatrixOf(bones[bone])));
        }
    }
    // 0x4ec04d..0x4ec0f6 (without the editor mode, 0x77f3b18): the owner's box, from its +0x124..+0x138, from the
    // model's origin.
    Actors::PublishPickingBox(owner, placement.origin, owner.boundsMin,
                              {owner.boundsMaxX, owner.boundsMaxY, owner.boundsTop});
    // 0x53c215..0x53c246: 0x4ee770 with the /0 (0x53c227) or the /1 (0x53c241); the joint only gets here with those
    // two.
    Actors::Drawing::DrawnObject shadow;
    shadow.model = Shadow;
    const ActorEffectHooks hooks;
    return Actors::Drawing::EmitVertexEffects(models, shadow, meshes, 0, Effects::SkinShellEffect, subtype, effects,
                                              &hooks, random);
}

Core::Expected<bool, std::string> DrawTheSkinShellBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       const Effects::ModelEffect& effect,
                                                       Actors::Drawing::DrawnObject& object, Core::Duration frameClock)
{
    if (effect.subtype == 0)
    {
        // 0x600568..0x600588 → 0x600b37.
        object.color = effect.light;
        Actors::Drawing::DrawObjectWithTexture(batch, models, object, frameClock, ChromeTexture);
        return true;
    }
    if (effect.subtype != 1)
        return true; // 0x600590 → 0x602e7d
    // 0x600596..0x6005cb: (0x3dcccccd, 0x3f000000, edi = 0x3f800000 de 0x60043c).
    object.color = {std::bit_cast<float>(0x3dcccccdu), 0.5f, 1.0f};
    std::vector<Models::SkinnedMesh> posed;
    std::span<const Models::SkinnedMesh> meshes = object.retainedMeshes;
    if (meshes.empty())
    {
        // Without the published scratch, 0x5ffdf0's with the second argument at 0 (0x52da99 → 0x602e90(effect, 0, …)):
        // WorldScratchOf, the normal rotated by R · s. Nothing without the model, its skeleton or the pose.
        if (object.alpha < VisibleAlpha)
            return true;
        posed = Actors::Drawing::WorldScratchOf(models, object);
        if (posed.empty())
            return true;
        meshes = posed;
    }
    if (auto drawn = Actors::Drawing::DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, AdditiveBodyFlags,
                                                       ChromeTexture);
        !drawn.HasValue())
        return Core::Unexpected("0x6005cb: " + drawn.Error());
    return true;
}
} // namespace Client::Effects
