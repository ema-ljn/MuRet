#include "Scenes/SkinShells.h"

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
#include "Scenes/ActorPickingBox.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/VertexEffects.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/SkinShellEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr Render::TextureSlot ChromeTexture{0x5c0}; // Chrome01, el 0x5c0 de 0x6005c0 y 0x60056e
constexpr std::uint32_t AdditiveBodyFlags = 0x42;   // 0x6005c7
constexpr float VisibleAlpha = 0.01f;               // 0x6b88d4, el de 0x5ffdf0
} // namespace

Core::Expected<void, std::string> ShedTheSkinShells(ActorState& owner, std::int32_t subtype,
                                                    const Models::IModelLibrary& models, WorldState& world,
                                                    World::ModelEffects& effects, Crt::CrtRandom& random)
{
    constexpr std::uint16_t Shadow = Tables::PlayerShadowModel;
    const Data::Models::Model* model = models.Find(Shadow);
    const Models::Skeleton* skeleton = models.SkeletonOf(Shadow);
    if (model == nullptr || skeleton == nullptr)
        return Core::Unexpected(std::string("0x53c1eb: falta el modelo 0x20b (Player\\Shadow01) del joint 0x632"));
    // 0x53c1c6..0x53c1eb: el cuadro, el anterior y la acción anterior en 0; la acción, la escala y el origen, los del
    // modelo (+0xa0, +0x68 y +0x6c); +0x84 queda en 0 (sólo 0x6001b0 lo escribe, y nunca en el 0x20b).
    const SharedModelPlacement placement = world.ModelPlacement(Shadow);
    const Models::AnimationState animation{
        .action = placement.action, .frame = 0.0f, .previousAction = 0, .previousKey = 0};
    const Models::ObjectTransform transform{
        .position = placement.origin, .angles = owner.angles, .scale = placement.scale, .lockedRootHeight = 0.0f};
    std::vector<Math::BoneTransform> bones(skeleton->bones.size(), Math::BoneTransform(0.0f));
    if (!Models::PoseWorldSkeleton(*skeleton, animation, transform, owner.headTurn.angles, bones))
        return Core::Unexpected(std::string("0x53c1eb: la pose del 0x20b (Player\\Shadow01) del joint 0x632 falla"));
    // 0x53c1f0..0x53c210: con el último argumento en 0 y la escala de los huesos (0x6c881c) en 1 (0x4ebdb0), cada
    // vértice sólo por la matriz de su hueso (0x5fccc0). Vale 1 porque 0x5ffdf0 y 0x60cf70 la ponen en 1 antes de
    // mirar el modo (0x5fff82, 0x60d01e) y los únicos modos 2 y 3 (0x50c20b, 0x50c217) van seguidos de 0x50c248
    // (Q-C1730; en Chaos Castle cada 0x5ffdf0 resta 0,15f al alpha y no salta en 0x5ffe3b por debajo de 0,01f:
    // quedaría en 1,4f si 0x50c248 no pasa, o en 1,2f si tampoco 0x50c217, P-C521).
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
    // 0x4ec04d..0x4ec0f6 (sin el modo del editor, 0x77f3b18): la caja del dueño, de su +0x124..+0x138, desde el origen
    // del modelo.
    PublishPickingBox(owner, placement.origin, owner.boundsMin, {owner.boundsMaxX, owner.boundsMaxY, owner.boundsTop});
    // 0x53c215..0x53c246: 0x4ee770 con el /0 (0x53c227) o el /1 (0x53c241); el joint sólo llega acá con esos dos.
    DrawnObject shadow;
    shadow.model = Shadow;
    return EmitVertexEffects(models, shadow, meshes, 0, World::SkinShellEffect, subtype, effects, random);
}

Core::Expected<bool, std::string> DrawTheSkinShellBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       const World::ModelEffect& effect, DrawnObject& object,
                                                       Core::Duration frameClock)
{
    if (effect.subtype == 0)
    {
        // 0x600568..0x600588 → 0x600b37.
        object.color = effect.light;
        DrawObjectWithTexture(batch, models, object, frameClock, ChromeTexture);
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
        // Sin el scratch publicado, el de 0x5ffdf0 con el segundo argumento en 0 (0x52da99 → 0x602e90(efecto, 0, …)):
        // WorldScratchOf, la normal girada por R · s. Nada sin el modelo, su esqueleto o la pose.
        if (object.alpha < VisibleAlpha)
            return true;
        posed = WorldScratchOf(models, object);
        if (posed.empty())
            return true;
        meshes = posed;
    }
    if (auto drawn = DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, AdditiveBodyFlags, ChromeTexture);
        !drawn.HasValue())
        return Core::Unexpected("0x6005cb: " + drawn.Error());
    return true;
}
} // namespace Client::Scenes
