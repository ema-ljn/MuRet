#include "Scenes/CharacterDrawing.h"

#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ItemGlow.h"
#include "Render/Models/PassColors.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Shine.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
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

// El contorno de 0x60cf70 con su modo 1, para un jugador fuera de los mapas 18 a 23: dos pases de
// 0x60cd60, a la escala de +0x94 (1,2, de 0x511290) y a ella menos 0,02, sin luz y con 0x40 (0x4ecc30:
// aditivo, sin textura y sin escribir la profundidad).
struct OutlinePass
{
    float boneScale = 1.0f;
    Render::Color color;
};
constexpr float OutlineScale = 1.2f;
constexpr float OutlineStep = 0.02f;
constexpr std::array<OutlinePass, 2> OutlinePasses{{
    {OutlineScale, Render::Color{0.1f, 0.03f, 0.0f, 1.0f}},
    {OutlineScale - OutlineStep, Render::Color{0.7f, 0.2f, 0.0f, 1.0f}},
}};
constexpr Render::ModelDrawState OutlineState{std::nullopt, Render::ModelBlend::Additive, true};

// La sombra: 0x60b3d0 con +0x98 en 1 pone la opaca y el negro, y 0x4eff40 apaga la textura y la
// máscara.
constexpr Render::ModelDrawState ShadowState{std::nullopt, Render::ModelBlend::Shadow, true};
constexpr Render::Color ShadowColor{0.0f, 0.0f, 0.0f, 1.0f};
// Con estas mascotas, 0x50ba30 no dibuja la sombra (las que se montan).
constexpr std::array<std::uint16_t, 2> PetsWithoutShadow{0x1c14, 0x1c15};

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

// El color de un pase, como lo pone 0x60b3d0 antes de su RenderBody: el 0x44 y el 0x48 con el del
// ítem (0x608e00); el 0x240 y el 0x1040, el base por el factor del ítem (0x6093d0); el de set, el de
// 0x609560; y el 0x42 del excelente, el que late.
Render::Color PassColor(const Render::Models::GlowPass& pass, std::uint16_t model, Render::Color base,
                        Core::Duration frameClock)
{
    if (!pass.shine)
    {
        return Render::Models::ExcellentColorAt(frameClock);
    }
    switch (*pass.shine)
    {
    case Render::Models::ShinePass::Shiny:
    case Render::Models::ShinePass::Chrome:
        return Render::Models::ShineColorOf(model);
    case Render::Models::ShinePass::Chrome2:
    case Render::Models::ShinePass::Chrome4:
        break;
    case Render::Models::ShinePass::Chrome3:
        return Render::Models::SetColorOf(model);
    }
    const glm::vec3 factor = Render::Models::MetalFactorOf(model);
    return Render::Color{base.red * factor.x, base.green * factor.y, base.blue * factor.z, base.alpha};
}

// Lo que necesita una malla para ir al lote: su forma en el mundo, su estado base y su brillo.
struct MeshDraw
{
    std::uint16_t model = 0;
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
        const Render::Color color = PassColor(pass, draw.model, draw.baseColor, draw.frameClock);
        if (pass.shine)
        {
            Triangles(batch, draw, {PassSlot(*pass.shine), Render::ModelBlend::Additive, true}, color, pass.shine);
        }
        else
        {
            Triangles(batch, draw, {draw.base.texture, Render::ModelBlend::Additive, true}, color, std::nullopt);
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

// La textura con la que se dibuja una malla, si se dibuja: la oculta no.
struct MeshTexture
{
    bool drawn = true;
    std::optional<Render::TextureSlot> slot;
};

MeshTexture TextureOf(const Data::Models::Mesh& mesh, std::span<const Render::TextureSlot> slots, std::size_t index,
                      std::uint8_t characterClass)
{
    const std::optional<Render::TextureSlot> assigned = AssignedSlot(mesh, slots, index);
    if (!assigned)
    {
        return {};
    }
    const std::optional<Render::TextureSlot> slot = MeshSlotOf(*assigned, characterClass);
    return MeshTexture{slot.has_value(), slot};
}

bool InRange(std::int16_t index, std::size_t count)
{
    return index >= 0 && static_cast<std::size_t>(index) < count;
}

// Si cada triángulo nombra, en sus tres esquinas, un vértice, una normal y una UV de la malla. El
// original no lo mira y leería fuera; en el laboratorio, todas lo cumplen.
bool IndicesInRange(const Data::Models::Mesh& mesh)
{
    for (const Data::Models::Triangle& triangle : mesh.triangles)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            if (!InRange(triangle.vertices[corner], mesh.vertices.size()) ||
                !InRange(triangle.normals[corner], mesh.normals.size()) ||
                !InRange(triangle.texCoords[corner], mesh.texCoords.size()))
            {
                return false;
            }
        }
    }
    return true;
}

bool HasFourComponents(const Data::Models::Mesh& mesh)
{
    return Data::Models::ModelTextureLoaderOf(mesh.textureName) == Data::Textures::TextureLoader::Ozt;
}

// Los dos pases del contorno, cada uno sobre todas las mallas del modelo (0x60cd60 → 0x4ef0c0). Una
// malla de cuatro componentes no va (0x4ecc30 con 0x40 sin 0x2).
void DrawOutline(Render::ModelBatch& batch, const Data::Models::Model& model,
                 std::span<const Render::TextureSlot> slots, std::span<const BoneTransform> bones,
                 const DrawnCharacter& character)
{
    for (const OutlinePass& pass : OutlinePasses)
    {
        for (std::size_t index = 0; index < model.meshes.size(); ++index)
        {
            const Data::Models::Mesh& mesh = model.meshes[index];
            if (!TextureOf(mesh, slots, index, character.appearance.characterClass).drawn || HasFourComponents(mesh) ||
                !IndicesInRange(mesh))
            {
                continue;
            }
            Render::Models::SkinnedMesh skinned;
            if (!Render::Models::SkinMeshAroundBones(mesh, bones, character.object, pass.boneScale, skinned))
            {
                continue;
            }
            Triangles(batch, MeshDraw{0, &mesh, &skinned, OutlineState, pass.color, nullptr, {}}, OutlineState,
                      pass.color, std::nullopt);
        }
    }
}

// Un modelo con las matrices `bones`, cada malla con su textura y el brillo de su nivel; con
// `outlined`, antes el contorno.
void DrawModel(Render::ModelBatch& batch, const IModelLibrary& models, std::uint16_t modelId,
               std::span<const BoneTransform> bones, const DrawnCharacter& character,
               const Render::Models::ItemGlow& glow, Core::Duration frameClock, bool outlined)
{
    const Data::Models::Model* model = models.Find(modelId);
    if (model == nullptr)
    {
        return;
    }
    const std::span<const Render::TextureSlot> slots = models.MeshTextures(modelId);
    if (outlined)
    {
        DrawOutline(batch, *model, slots, bones, character);
    }
    const float shade = character.light * glow.baseFactor;
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model->meshes[index];
        const MeshTexture texture = TextureOf(mesh, slots, index, character.appearance.characterClass);
        if (!texture.drawn || !IndicesInRange(mesh))
        {
            continue;
        }
        Render::Models::SkinnedMesh skinned;
        if (!Render::Models::SkinMesh(mesh, bones, character.object, skinned))
        {
            continue;
        }
        const Render::ModelDrawState base{
            texture.slot, HasFourComponents(mesh) ? Render::ModelBlend::Alpha : Render::ModelBlend::Opaque, true};
        DrawMesh(batch,
                 MeshDraw{modelId, &mesh, &skinned, base, Render::Color{shade, shade, shade, 1.0f}, &glow, frameClock});
    }
}

// El lazo de las manos de 0x50ba30 saltea las saetas, las flechas y el cuervo: las dos primeras van
// a la espalda, con el camino de 0x509340 con su bandera, y el cuervo es un objeto aparte (0x420cf0);
// ninguno de los dos está todavía (PR F).
constexpr std::array<std::uint16_t, 3> NotInTheHand{0xa19, 0xa21, 0x1c17};

bool SkippedInTheHand(std::uint16_t model)
{
    return std::ranges::find(NotInTheHand, model) != NotInTheHand.end();
}

// La sombra de 0x50ba30 (0x20b con las matrices del cuerpo), antes de las partes: cada malla, pasada
// a los vértices del cuerpo y aplastada sobre el piso (0x4eff40). Sin ella con una de las mascotas
// que se montan.
void DrawShadow(Render::ModelBatch& batch, const IModelLibrary& models, std::span<const BoneTransform> body,
                const DrawnCharacter& character)
{
    const std::optional<std::uint16_t>& pet = character.appearance.pet;
    if (pet && std::ranges::find(PetsWithoutShadow, *pet) != PetsWithoutShadow.end())
    {
        return;
    }
    const Data::Models::Model* model = models.Find(Data::Models::PlayerShadowModel);
    if (model == nullptr)
    {
        return;
    }
    for (const Data::Models::Mesh& mesh : model->meshes)
    {
        // 0x4eff40 no mira la textura: la malla de Shadow01 es la oculta (hide_m) y va igual.
        Render::Models::SkinnedMesh skinned;
        if (!IndicesInRange(mesh) || !Render::Models::SkinMesh(mesh, body, character.object, skinned))
        {
            continue;
        }
        for (glm::vec3& position : skinned.positions)
        {
            position = Render::Models::FlattenOnTheGround(position, character.object.position);
        }
        Triangles(batch,
                  MeshDraw{Data::Models::PlayerShadowModel, &mesh, &skinned, ShadowState, ShadowColor, nullptr, {}},
                  ShadowState, ShadowColor, std::nullopt);
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
    DrawModel(batch, models, modelId, bones, character, glow, frameClock, false);
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
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
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
    DrawShadow(batch, models, body, character);
    for (const GameLogic::WornItem& part : appearance.bodyParts)
    {
        const Render::Models::ItemGlow glow = Render::Models::ItemGlowOf(part.level, part.excellent, part.ancient);
        DrawModel(batch, models, part.model, body, character, glow, frameClock, character.outlined);
    }
    const Render::Models::ItemGlow noGlow{};
    if (appearance.head)
    {
        DrawModel(batch, models, *appearance.head, body, character, noGlow, frameClock, character.outlined);
    }
    using Render::Models::AttachmentSlot;
    for (const auto& [hand, slot] : {std::pair{appearance.rightHand, AttachmentSlot::RightHand},
                                     std::pair{appearance.leftHand, AttachmentSlot::LeftHand}})
    {
        if (hand && !SkippedInTheHand(hand->model))
        {
            // 0x509340 le pasa a 0x60b3d0 el set en 0: lo que cuelga no lleva su pase.
            const Render::Models::ItemGlow glow = Render::Models::ItemGlowOf(hand->level, hand->excellent, false);
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
