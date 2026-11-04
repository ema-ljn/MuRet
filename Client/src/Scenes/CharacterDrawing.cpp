#include "Scenes/CharacterDrawing.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ItemGlow.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Shine.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;

// El esqueleto de los personajes.
constexpr std::uint16_t PlayerModel = 0x20a;

// Los colores de los pases de brillo de las partes Male17 en la traza de la selección: el 141 con
// 1,35 y el 115 y el 120 con (0,75; 0,65; 0,5). Los de otros ítems salen de 0x609640, sin leer
// (P-C14): hasta entonces, estos.
constexpr Render::Color MetalPassColor{1.35f, 1.35f, 1.35f, 1.0f};
constexpr Render::Color ChromePassColor{0.75f, 0.65f, 0.5f, 1.0f};

Render::TextureSlot PassSlot(Render::Models::ShinePass pass)
{
    switch (pass)
    {
    case Render::Models::ShinePass::Shiny:
        return ShinyTextureSlot;
    case Render::Models::ShinePass::Chrome:
        return ChromeTextureSlot;
    case Render::Models::ShinePass::Chrome2:
    case Render::Models::ShinePass::Chrome3:
    case Render::Models::ShinePass::Chrome4:
        break;
    }
    return MetalTextureSlot;
}

Render::Color PassColor(Render::Models::ShinePass pass)
{
    return pass == Render::Models::ShinePass::Chrome4 ? MetalPassColor : ChromePassColor;
}

// Lo que necesita una malla para ir al lote: su forma en el mundo, su estado base y su brillo.
struct MeshDraw
{
    const Data::Models::Mesh* mesh = nullptr;
    const Render::Models::SkinnedMesh* skinned = nullptr;
    Render::ModelDrawState base;
    Render::Color baseColor;
    const Render::Models::ItemGlow* glow = nullptr;
    Core::Duration frameClock{};
};

// Las UV de una esquina: las de la malla, o las de un pase de brillo con la normal en el mundo.
glm::vec2 CornerUv(const MeshDraw& draw, const Data::Models::Triangle& triangle, std::size_t corner,
                   std::optional<Render::Models::ShinePass> shine)
{
    if (shine)
    {
        const auto normal = static_cast<std::size_t>(triangle.normals[corner]);
        return Render::Models::ShineTexCoords(*shine, draw.skinned->normals[normal], draw.frameClock);
    }
    const Data::Models::TexCoord& uv = draw.mesh->texCoords[static_cast<std::size_t>(triangle.texCoords[corner])];
    return {uv.u, uv.v};
}

// Los triángulos de la malla con un estado, un color y las UV de `shine` o las suyas.
void Triangles(Render::ModelBatch& batch, const MeshDraw& draw, const Render::ModelDrawState& state,
               Render::Color color, std::optional<Render::Models::ShinePass> shine)
{
    batch.SetState(state);
    for (const Data::Models::Triangle& triangle : draw.mesh->triangles)
    {
        std::array<Render::ModelVertex, 3> corners{};
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            const auto vertex = static_cast<std::size_t>(triangle.vertices[corner]);
            corners[corner] =
                Render::ModelVertex{draw.skinned->positions[vertex], CornerUv(draw, triangle, corner, shine), color};
        }
        batch.Triangle(corners);
    }
}

// La malla y, detrás, sus pases: los de brillo con su textura y sus UV, y el 0x42 con las suyas.
void DrawMesh(Render::ModelBatch& batch, const MeshDraw& draw)
{
    Triangles(batch, draw, draw.base, draw.baseColor, std::nullopt);
    for (const Render::Models::GlowPass& pass : draw.glow->passes)
    {
        if (pass.shine)
        {
            Triangles(batch, draw, {PassSlot(*pass.shine), Render::ModelBlend::Additive, true}, PassColor(*pass.shine),
                      pass.shine);
        }
        else
        {
            Triangles(batch, draw, {draw.base.texture, Render::ModelBlend::Additive, true}, draw.baseColor,
                      std::nullopt);
        }
    }
}

// El slot que le tocó a una malla: el de la biblioteca o, sin texturas cargadas, el fijo de la piel,
// el pelo o la oculta por su nombre.
std::optional<Render::TextureSlot> AssignedSlot(const Data::Models::Mesh& mesh,
                                                std::span<const Render::TextureSlot> slots, std::size_t index)
{
    if (index < slots.size())
    {
        return slots[index];
    }
    if (const std::optional<std::uint16_t> fixed = Data::Models::SkinSlotOf(mesh.textureName))
    {
        return Render::TextureSlot{*fixed};
    }
    return std::nullopt;
}

// Un modelo con las matrices `bones`, cada malla con su textura y el brillo de su nivel.
void DrawModel(Render::ModelBatch& batch, const IModelLibrary& models, std::uint16_t modelId,
               std::span<const BoneTransform> bones, const DrawnCharacter& character,
               const Render::Models::ItemGlow& glow, Core::Duration frameClock)
{
    const Data::Models::Model* model = models.Find(modelId);
    if (model == nullptr)
    {
        return;
    }
    const std::span<const Render::TextureSlot> slots = models.MeshTextures(modelId);
    const float shade = character.light * glow.baseFactor;
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model->meshes[index];
        std::optional<Render::TextureSlot> texture = AssignedSlot(mesh, slots, index);
        if (texture)
        {
            texture = MeshSlotOf(*texture, character.appearance.characterClass);
            if (!texture)
            {
                continue;
            }
        }
        Render::Models::SkinnedMesh skinned;
        if (!Render::Models::SkinMesh(mesh, bones, character.object, skinned))
        {
            continue;
        }
        const bool fourComponents =
            Data::Models::ModelTextureLoaderOf(mesh.textureName) == Data::Textures::TextureLoader::Ozt;
        const Render::ModelDrawState base{
            texture, fourComponents ? Render::ModelBlend::Alpha : Render::ModelBlend::Opaque, true};
        DrawMesh(batch, MeshDraw{&mesh, &skinned, base, Render::Color{shade, shade, shade, 1.0f}, &glow, frameClock});
    }
}

// Lo que cuelga: posado desde su hueso del cuerpo (0x509340) y dibujado como una parte.
void DrawHanging(Render::ModelBatch& batch, const IModelLibrary& models, std::uint16_t modelId,
                 Render::Models::AttachmentSlot slot, const Render::Models::AnimationState& state,
                 std::span<const BoneTransform> body, const DrawnCharacter& character,
                 const Render::Models::ItemGlow& glow, Core::Duration frameClock)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(modelId);
    const Render::Models::Attachment attachment = Render::Models::AttachmentOf(slot);
    if (skeleton == nullptr || static_cast<std::size_t>(attachment.bone) >= body.size())
    {
        return;
    }
    const BoneTransform root =
        Render::Models::AttachmentRoot(body[static_cast<std::size_t>(attachment.bone)], attachment.offset);
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Render::Models::PoseAttached(*skeleton, state, root, bones))
    {
        return;
    }
    DrawModel(batch, models, modelId, bones, character, glow, frameClock);
}
} // namespace

std::optional<Render::TextureSlot> MeshSlotOf(Render::TextureSlot assigned, std::uint8_t characterClass)
{
    const std::uint8_t skin = SkinIndexOf(characterClass);
    switch (assigned.value)
    {
    case Data::Models::HiddenTextureSlot:
        return std::nullopt;
    case Data::Models::SkinTextureSlot:
        return Render::TextureSlot{static_cast<std::uint16_t>(Data::Models::SkinTextureSlot + skin)};
    case Data::Models::HairTextureSlot:
        return Render::TextureSlot{static_cast<std::uint16_t>(Data::Models::SkinTextureSlot + 1 + skin)};
    default:
        return assigned;
    }
}

void DrawCharacter(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnCharacter& character,
                   Core::Duration frameClock)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(PlayerModel);
    if (skeleton == nullptr)
    {
        return;
    }
    std::vector<BoneTransform> body(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Render::Models::PoseSkeleton(*skeleton, character.body, character.object, character.headAngles, body))
    {
        return;
    }
    const GameLogic::CharacterAppearance& appearance = character.appearance;
    for (const GameLogic::WornItem& part : appearance.bodyParts)
    {
        const Render::Models::ItemGlow glow = Render::Models::ItemGlowOf(part.level, part.excellent, part.ancient);
        DrawModel(batch, models, part.model, body, character, glow, frameClock);
    }
    const Render::Models::ItemGlow noGlow{};
    if (appearance.head)
    {
        DrawModel(batch, models, *appearance.head, body, character, noGlow, frameClock);
    }
    using Render::Models::AttachmentSlot;
    for (const auto& [hand, slot] : {std::pair{appearance.rightHand, AttachmentSlot::RightHand},
                                     std::pair{appearance.leftHand, AttachmentSlot::LeftHand}})
    {
        if (hand)
        {
            const Render::Models::ItemGlow glow =
                Render::Models::ItemGlowOf(hand->level, hand->excellent, hand->ancient);
            DrawHanging(batch, models, hand->model, slot, Render::Models::AnimationState{}, body, character, glow,
                        frameClock);
        }
    }
    if (appearance.wings)
    {
        DrawHanging(batch, models, *appearance.wings, AttachmentSlot::Wings, character.wings, body, character, noGlow,
                    frameClock);
    }
    if (appearance.pet && Render::Models::DrawsThePet(*appearance.pet))
    {
        DrawHanging(batch, models, *appearance.pet, AttachmentSlot::Pet, character.pet, body, character, noGlow,
                    frameClock);
    }
}
} // namespace Client::Scenes
