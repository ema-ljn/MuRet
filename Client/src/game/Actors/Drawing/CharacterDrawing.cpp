#include "Actors/Drawing/CharacterDrawing.h"
#include "Models/SkinningBuffers.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/TextureSet.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/CharacterClass.h"
#include "Maps/MapId.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Actors/Attachment.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/ItemGlow.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/PassColors.h"
#include "Models/Pose.h"
#include "Models/Shine.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Actors/Drawing/GameItemGlowEffects.h"
#include "Actors/Drawing/GameItemGlowSpecialDrawing.h"
#include "Math/Ftol.h"
#include "Kernel/WorldObject.h"
#include "Actors/Drawing/ScepterGlow.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Actors::Drawing
{
namespace
{
constexpr float PartBoxAlphaThreshold = 0.01f;
using Math::BoneTransform;
using Models::ObjectState;

// The outline of 0x60cf70 in its mode 1: two passes of 0x60cd60, at the scale of +0x94 (1.2, from 0x511290) and at that
// minus 0.02, without light and with 0x40 (0x4ecc30: additive, without texture and without writing depth). The color
// goes by +0x88 (0x60d056..0x60d0fd: with 4, the NPCs').
struct OutlinePass
{
    float boneScale = 1.0f;
    Render::Color color;
};
constexpr float OutlineScale = 1.2f;
constexpr float OutlineStep = 0.02f;
constexpr std::uint8_t NpcKind = 4;
constexpr std::array<OutlinePass, 2> OutlinePasses{{
    {OutlineScale, Render::Color{0.1f, 0.03f, 0.0f, 1.0f}},
    {OutlineScale - OutlineStep, Render::Color{0.7f, 0.2f, 0.0f, 1.0f}},
}};
constexpr std::array<OutlinePass, 2> NpcOutlinePasses{{
    {OutlineScale, Render::Color{0.02f, 0.1f, 0.0f, 1.0f}},
    {OutlineScale - OutlineStep, Render::Color{0.16f, 0.7f, 0.0f, 1.0f}},
}};
// 0x60d09f and 0x60d113: on maps 18 to 23 (0x6c9dd0), for any +0x88, another color and the scale by the object's
// (+0xc): 0.1 / scale + 1 and 0.04 / scale + 1.02 − 0.02, on the x87 (PC53) and stored as float.
constexpr std::uint8_t FirstChaosCastleMap = 18;
constexpr std::uint8_t LastChaosCastleMap = 23;
constexpr float ChaosCastleOutlineSpan = 0.1f;
constexpr float ChaosCastleOutlineBase = 1.0f;
constexpr float ChaosCastleInnerSpan = 0.04f;
constexpr float ChaosCastleInnerBase = 1.02f;
constexpr Render::Color ChaosCastleOutlineColor{0.1f, 0.01f, 0.0f, 1.0f};
constexpr Render::Color ChaosCastleInnerColor{0.7f, 0.07f, 0.0f, 1.0f};

std::array<OutlinePass, 2> OutlinePassesOf(const DrawnCharacter& character)
{
    if (character.map < FirstChaosCastleMap || character.map > LastChaosCastleMap)
        return character.kind == NpcKind ? NpcOutlinePasses : OutlinePasses;
    const double scale = character.object.scale;
    const double outer =
        static_cast<double>(ChaosCastleOutlineSpan) / scale + static_cast<double>(ChaosCastleOutlineBase);
    const double inner = static_cast<double>(ChaosCastleInnerSpan) / scale + static_cast<double>(ChaosCastleInnerBase);
    return {{{static_cast<float>(outer), ChaosCastleOutlineColor},
             {static_cast<float>(inner - static_cast<double>(OutlineStep)), ChaosCastleInnerColor}}};
}
constexpr Render::ModelDrawState OutlineState{std::nullopt, Render::ModelBlend::Additive, true};

// The shadow: 0x60b3d0 with +0x98 at 1 sets the opaque blend and black, and 0x4eff40 turns off the texture and the
// mask.
constexpr Render::ModelDrawState ShadowState{std::nullopt, Render::ModelBlend::Shadow, true};
constexpr Render::Color ShadowColor{0.0f, 0.0f, 0.0f, 1.0f};
// With these pets, 0x50ba30 does not draw the shadow (the ridden ones).
constexpr std::array<std::uint16_t, 2> PetsWithoutShadow{0x1c14, 0x1c15};

// Class 4 in the parts loop of 0x50ba30: (+0x1d4 & 7) at 4.
constexpr Kernel::CharacterClass MaskedClass = Kernel::CharacterClass::DarkLord;

bool IsMaskedClass(std::uint8_t characterClass)
{
    return Kernel::BaseClassOf(characterClass) == MaskedClass;
}

// The model 0x50ba30 draws part `part` with: class 4's helmet, if it has a mask, with it (the model + 0x328b, with the
// same skin).
std::uint16_t DrawnPartModel(Kernel::BodyPart part, std::uint16_t model, std::uint8_t characterClass)
{
    if (part != Kernel::BodyPart::Helm || !IsMaskedClass(characterClass) ||
        std::ranges::find(Tables::HelmsWithMask, model) == Tables::HelmsWithMask.end())
    {
        return model;
    }
    return static_cast<std::uint16_t>(model + Tables::MaskHelmOffset);
}

Render::TextureSlot PassSlot(Models::ShinePass pass)
{
    switch (pass)
    {
    case Models::ShinePass::Shiny:
        return ShinyTextureSlot;
    case Models::ShinePass::Chrome:
        return ChromeTextureSlot;
    case Models::ShinePass::Chrome2:
    case Models::ShinePass::Chrome3:
    case Models::ShinePass::Chrome4:
    case Models::ShinePass::Chrome5:
        break;
    }
    return MetalTextureSlot;
}

// The color of a glow pass, as 0x60b3d0 sets it before its RenderBody: 0x48 and 0x44 with the item's (0x608e00); 0x240
// and 0x1040, the base times the item's factor (0x6093d0); and the set's, that of 0x609560. None goes by the blendMesh
// light: 0x4ecc30 draws them before that branch.
Render::Color ShineColorOf(Models::ShinePass pass, std::uint16_t model, Render::Color base)
{
    switch (pass)
    {
    case Models::ShinePass::Shiny:
    case Models::ShinePass::Chrome:
        return Models::ShineColorOf(model);
    case Models::ShinePass::Chrome2:
    case Models::ShinePass::Chrome4:
    case Models::ShinePass::Chrome5:
        break;
    case Models::ShinePass::Chrome3:
        return Models::SetColorOf(model);
    }
    const glm::vec3 factor = Models::MetalFactorOf(model);
    return Render::Color{base.red * factor.x, base.green * factor.y, base.blue * factor.z, base.alpha};
}

// What a character's models share in a frame: the batch, the library, the character, the clock, the rand() of the
// objects of what hangs (0x607b60) and the buffers they are posed and skinned into.
struct CharacterFrame
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    const DrawnCharacter& character;
    Core::Duration clock{};
    std::minstd_rand& random;
    Models::SkinningBuffers& buffers;
};

// The buffers the draw of `character` uses: those its owner lends or, without them, `own`.
Models::SkinningBuffers& BuffersOf(const DrawnCharacter& character, Models::SkinningBuffers& own)
{
    return character.drawBuffers != nullptr ? *character.drawBuffers : own;
}

// What the load read of mesh `index` (0x4f13d0 and 0x60ff50); a mesh the library has no traits for is drawn plainly.
Models::MeshTraits TraitsOf(std::span<const Models::MeshTraits> traits, std::size_t index)
{
    return index < traits.size() ? traits[index] : Models::MeshTraits{};
}

// The blendMesh 0x4ef0c0 passes 0x4ecc30 with the mesh: its index with R; otherwise, the object's.
std::int16_t BlendMeshOf(const Models::MeshTraits& role, std::size_t index, const ObjectState& object)
{
    return role.blendScript ? static_cast<std::int16_t>(index) : object.blendMesh;
}

// Whether 0x4ecc30 takes the mesh through the blendMesh branch (with its texture, additive and by the light): with a
// blendMesh below −1, or with that of its texture.
bool Blends(const Data::Models::Mesh& mesh, std::int16_t blendMesh)
{
    return blendMesh < Models::NoBlendMesh || mesh.texture == blendMesh;
}

Render::Color Scaled(Render::Color color, float factor)
{
    return Render::Color{color.red * factor, color.green * factor, color.blue * factor, color.alpha};
}

// What a mesh needs to go to the batch: its shape in the world, its role, its texture, its level's color, whether it
// goes through the blendMesh branch and whether its UVs scroll, and the object it is drawn with.
struct MeshDraw
{
    std::uint16_t model = 0;
    std::size_t index = 0;
    const Data::Models::Mesh* mesh = nullptr;
    const Models::SkinnedMesh* skinned = nullptr;
    Models::MeshTraits role;
    std::optional<Render::TextureSlot> texture;
    Render::Color shade;
    bool blends = false;
    // 0x4ecc30 (local_5): the blendMesh's mesh by its index, or the S one.
    bool scrolls = false;
    const ObjectState* object = nullptr;
    Core::Duration frameClock{};
    // RenderBody's alpha and the per-vertex light (+0x44 of the model): those of a world object; a character goes at 1
    // and without it.
    float alpha = 1.0f;
    bool vertexLight = false;
    bool originalClock = false;
};

// A pass's UVs: the mesh's or, with `shine`, those of the glow pass with the normal in the world; both plus `shift`.
struct PassUv
{
    std::optional<Models::ShinePass> shine;
    glm::vec2 shift{};
};

// An item's glow clock with the original's clock: the same at every corner of the pass, so Triangles takes it once and
// not per corner. Without glow or with MuRet's clock, none.
std::optional<ItemShineClock> ItemShineClockOfPass(const MeshDraw& draw, const PassUv& uv)
{
    if (!uv.shine || !draw.originalClock)
        return std::nullopt;
    return ItemShineClockOf(*uv.shine, draw.frameClock);
}

glm::vec2 CornerUv(const MeshDraw& draw, const Data::Models::Triangle& triangle, std::size_t corner, const PassUv& uv,
                   const std::optional<ItemShineClock>& itemClock)
{
    if (uv.shine)
    {
        const auto normal = static_cast<std::size_t>(triangle.normals[corner]);
        if (itemClock)
            return ItemShineTexCoords(*uv.shine, draw.skinned->normals[normal], *itemClock) + uv.shift;
        return Models::ShineTexCoords(*uv.shine, draw.skinned->normals[normal], draw.frameClock) + uv.shift;
    }
    const Data::Models::TexCoord& texCoord = draw.mesh->texCoords[static_cast<std::size_t>(triangle.texCoords[corner])];
    return glm::vec2{texCoord.u, texCoord.v} + uv.shift;
}

// The mesh's triangles with a state, a color and their UVs, each corner at `positionOf(vertex)`; with `normalLight`,
// the color of each corner by the light of its normal (0x4ecc30 with +0x44, in the textured pass).
template <typename PositionOf>
void TrianglesAt(Render::ModelBatch& batch, const MeshDraw& draw, const Render::ModelDrawState& state,
                 Render::Color color, const PassUv& uv, bool normalLight, const PositionOf& positionOf)
{
    batch.SetState(state);
    const std::optional<ItemShineClock> itemClock = ItemShineClockOfPass(draw, uv);
    for (const Data::Models::Triangle& triangle : draw.mesh->triangles)
    {
        std::array<Render::ModelVertex, 3> corners{};
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            const auto vertex = static_cast<std::size_t>(triangle.vertices[corner]);
            Render::Color shaded = color;
            if (normalLight)
            {
                const auto normal = static_cast<std::size_t>(triangle.normals[corner]);
                const float light = Models::NormalLight(draw.skinned->normals[normal]);
                shaded = Render::Color{color.red * light, color.green * light, color.blue * light, color.alpha};
            }
            corners[corner] =
                Render::ModelVertex{positionOf(vertex), CornerUv(draw, triangle, corner, uv, itemClock), shaded};
        }
        batch.Triangle(corners);
    }
}

// TrianglesAt with each corner at its skinned vertex.
void Triangles(Render::ModelBatch& batch, const MeshDraw& draw, const Render::ModelDrawState& state,
               Render::Color color, const PassUv& uv, bool normalLight = false)
{
    TrianglesAt(batch, draw, state, color, uv, normalLight,
                [&draw](std::size_t vertex) { return draw.skinned->positions[vertex]; });
}

// 0x4eff40: the mesh's triangles with each corner flattened on the ground from `origin` with `spread` as it goes to
// glVertex3fv, the skinned vertices left as they are.
void FlatTriangles(Render::ModelBatch& batch, const MeshDraw& draw, const Render::ModelDrawState& state,
                   Render::Color color, const glm::vec3& origin, float spread)
{
    TrianglesAt(batch, draw, state, color, PassUv{}, false, [&draw, &origin, spread](std::size_t vertex)
                { return Models::FlattenOnTheGround(draw.skinned->positions[vertex], origin, spread); });
}

// What is added to the mesh's UVs in a textured pass: the object's U and V if 0x4ecc30 scrolls them, or if it is the
// model+0x88 one (`chromeMesh`, the chrome pass of Spear10).
glm::vec2 TexturedShift(const MeshDraw& draw, bool chromeMesh = false)
{
    return draw.scrolls || chromeMesh ? draw.object->uv : glm::vec2{};
}

bool HasFourComponents(const Data::Models::Mesh& mesh)
{
    return Data::Models::ModelTextureLoaderOf(mesh.textureName) == Data::Textures::TextureLoader::Ozt;
}

// 0x6b8d00: with the alpha below it, 0x4ecc30 blends and sends it in the color.
constexpr float OpaqueAlpha = 0.99f;

// The base pass (0x609640 → RenderBody(2) with the object), without the mesh the object hides: the blendMesh's one
// additive and by the light, one whose texture has four components (`fourComponents`) or with the alpha below 0.99 with
// the normal blend (and that alpha), and otherwise opaque; with the per-vertex light, by that of each normal, except
// the blendMesh's.
void DrawBaseWith(Render::ModelBatch& batch, const MeshDraw& draw, bool fourComponents)
{
    if (static_cast<std::int16_t>(draw.index) == draw.object->hiddenMesh)
    {
        return;
    }
    if (draw.blends)
    {
        Triangles(batch, draw, {draw.texture, Render::ModelBlend::Additive, true},
                  Scaled(draw.shade, draw.object->light), PassUv{std::nullopt, TexturedShift(draw)});
        return;
    }
    const bool fading = draw.alpha < OpaqueAlpha;
    const Render::ModelBlend blend = fading || fourComponents ? Render::ModelBlend::Alpha : Render::ModelBlend::Opaque;
    Render::Color color = draw.shade;
    color.alpha = fading ? draw.alpha : 1.0f;
    Triangles(batch, draw, {draw.texture, blend, true}, color, PassUv{std::nullopt, TexturedShift(draw)},
              draw.vertexLight);
}

void DrawBase(Render::ModelBatch& batch, const MeshDraw& draw)
{
    DrawBaseWith(batch, draw, HasFourComponents(*draw.mesh));
}

// The set pass's alpha and what it already multiplied: 0x4ecc30 multiplies the model's color in place, so each mesh the
// pass draws goes through it one more time than the previous one (SetAlphaAt).
struct SetFade
{
    float alpha = 1.0f;
    float factor = 1.0f;
};

// A glow pass with its texture and its UVs, except on a mesh without glow or the one its item hides in that pass; the
// set's, with the alpha that keeps multiplying, and 0x1040 (mode 0x1000 of 0x4ecc30) with the object's U and V.
void DrawShinePass(Render::ModelBatch& batch, const MeshDraw& draw, Models::ShinePass pass, SetFade& fade)
{
    if (!draw.role.shines || Models::PassHiddenMesh(draw.model, pass, draw.object->hiddenMesh) == draw.index)
    {
        return;
    }
    Render::Color color = ShineColorOf(pass, draw.model, draw.shade);
    if (pass == Models::ShinePass::Chrome3)
    {
        fade.factor *= fade.alpha;
        color = Scaled(color, fade.factor);
    }
    const glm::vec2 shift = pass == Models::ShinePass::Chrome4 ? draw.object->uv : glm::vec2{};
    Triangles(batch, draw, {PassSlot(pass), Render::ModelBlend::Additive, true}, color, PassUv{pass, shift});
}

// The excellent 0x42: with its texture and additive, with the pulsing color, by the light if it goes through the
// blendMesh branch.
void DrawExcellentPass(Render::ModelBatch& batch, const MeshDraw& draw)
{
    const Render::Color color = Models::ExcellentColorAt(draw.frameClock);
    Triangles(batch, draw, {draw.texture, Render::ModelBlend::Additive, true},
              draw.blends ? Scaled(color, draw.object->light) : color, PassUv{std::nullopt, TexturedShift(draw)});
}

// The slot a mesh got: the library's or, without textures loaded, the fixed one of the skin, the hair or the hidden one
// by its name.
std::optional<Render::TextureSlot> AssignedSlot(const Data::Models::Mesh& mesh,
                                                std::span<const Render::TextureSlot> slots, std::size_t index)
{
    if (index < slots.size())
    {
        if (slots[index] == Render::UnboundModelTextureSlot)
            return std::nullopt;
        return slots[index];
    }
    if (const std::optional<std::uint16_t> fixed = Data::Models::SkinSlotOf(mesh.textureName))
    {
        return Render::TextureSlot{*fixed};
    }
    return std::nullopt;
}

// The texture a mesh is drawn with, if it is drawn: the hidden one is not.
struct MeshTexture
{
    bool drawn = true;
    std::optional<Render::TextureSlot> slot;
};

std::optional<Render::TextureSlot> SkinSlotOf(Render::TextureSlot assigned, std::uint8_t skin)
{
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

MeshTexture TextureOf(const Data::Models::Mesh& mesh, std::span<const Render::TextureSlot> slots, std::size_t index,
                      std::uint8_t skin)
{
    const std::optional<Render::TextureSlot> assigned = AssignedSlot(mesh, slots, index);
    if (!assigned)
    {
        return {};
    }
    const std::optional<Render::TextureSlot> slot = SkinSlotOf(*assigned, skin);
    return MeshTexture{slot.has_value(), slot};
}

bool InRange(std::int16_t index, std::size_t count)
{
    return index >= 0 && static_cast<std::size_t>(index) < count;
}

// Whether each triangle names, at its three corners, a vertex, a normal and a UV of the mesh. The original does not
// check it and would read outside; in the lab, all of them comply.
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

// An outline pass on a mesh: without texture, or the blendMesh's with its own and by the light (0x4ecc30 with 0x40
// passes the blendMesh before the rest). A four-component mesh that does not go through that branch does not go (0x40
// without 0x2).
void OutlineMeshAround(Render::ModelBatch& batch, const Models::ObjectTransform& transform, const OutlinePass& pass,
                       const MeshDraw& draw, std::span<const BoneTransform> bones)
{
    if (!draw.blends && HasFourComponents(*draw.mesh))
    {
        return;
    }
    Models::SkinnedMesh skinned;
    if (!Models::SkinMeshAroundBones(*draw.mesh, bones, transform, pass.boneScale, skinned))
    {
        return;
    }
    MeshDraw around = draw;
    around.skinned = &skinned;
    const Render::ModelDrawState state =
        draw.blends ? Render::ModelDrawState{draw.texture, Render::ModelBlend::Additive, true} : OutlineState;
    Triangles(batch, around, state, draw.blends ? Scaled(pass.color, draw.object->light) : pass.color, PassUv{});
}

void OutlineMesh(const CharacterFrame& frame, const OutlinePass& pass, const MeshDraw& draw,
                 std::span<const BoneTransform> bones)
{
    OutlineMeshAround(frame.batch, frame.character.object, pass, draw, bones);
}

// The two outline passes, each on all the model's meshes (0x60cd60 → 0x4ef0c0, with the character's object), without
// the one the object hides.
void DrawOutline(const CharacterFrame& frame, std::span<const MeshDraw> draws, std::span<const BoneTransform> bones)
{
    for (const OutlinePass& pass : OutlinePassesOf(frame.character))
    {
        for (const MeshDraw& draw : draws)
        {
            if (static_cast<std::int16_t>(draw.index) != draw.object->hiddenMesh)
            {
                OutlineMesh(frame, pass, draw, bones);
            }
        }
    }
}

// How a model is drawn: its glow, whether it carries the outline, the object's type, which 0x60b3d0 looks at for its
// per-object cases, and what the object passes to RenderBody. In the parts, the object is the character, and the
// outline goes with the light from before 0x60b3d0 writes its own; in what hangs, the one 0x509340 builds with its
// model.
struct ModelUse
{
    const Models::ItemGlow& glow;
    bool outlined = false;
    std::uint16_t objectType = Tables::PlayerSkeletonModel;
    ObjectState object;
    float outlineLight = 1.0f;
    bool hanging = false;
};

// 0x60b3d0 with Spear10's object: before the passes, RenderBody(2) with 0x5c0 forced, the color (0.5, 0.5, 1.5) and
// model+0x88 at 0, with which 0x4ecc30 scrolls mesh 0's UVs. Opaque (Chrome01 has three components), or additive and by
// the light in the blendMesh branch.
constexpr std::uint16_t ChromeFirstObject = 0x81b;
constexpr std::size_t ChromeFirstMesh = 0;
constexpr Render::Color ChromeFirstColor{0.5f, 0.5f, 1.5f, 1.0f};

void DrawChromeFirst(Render::ModelBatch& batch, const MeshDraw& draw)
{
    if (static_cast<std::int16_t>(draw.index) == draw.object->hiddenMesh)
    {
        return;
    }
    const Render::ModelBlend blend = draw.blends ? Render::ModelBlend::Additive : Render::ModelBlend::Opaque;
    Triangles(batch, draw, {ChromeTextureSlot, blend, true},
              draw.blends ? Scaled(ChromeFirstColor, draw.object->light) : ChromeFirstColor,
              PassUv{std::nullopt, TexturedShift(draw, draw.index == ChromeFirstMesh)});
}

// RenderBody's passes on a model's meshes, one after another like 0x60b3d0: Spear10's chrome, the base and those of its
// level's glow, each on all the meshes.
void DrawPasses(const CharacterFrame& frame, std::span<const MeshDraw> draws, const ModelUse& use)
{
    if (use.objectType == ChromeFirstObject)
    {
        for (const MeshDraw& draw : draws)
        {
            DrawChromeFirst(frame.batch, draw);
        }
    }
    for (const MeshDraw& draw : draws)
    {
        DrawBase(frame.batch, draw);
    }
    SetFade fade{Models::SetAlphaAt(frame.clock)};
    for (const Models::GlowPass& pass : use.glow.passes)
    {
        for (const MeshDraw& draw : draws)
        {
            if (pass.shine)
            {
                DrawShinePass(frame.batch, draw, *pass.shine, fade);
            }
            else
            {
                DrawExcellentPass(frame.batch, draw);
            }
        }
    }
}

// The meshes of `modelId` that are drawn, taken to the world with `bones` and `object` in `buffers` (one per mesh):
// each with its role, its texture (skin and hair `skin`, +0x98 of the model), `shade` and `state`. Those of H, the
// hidden one (0x12d), the broken ones and those that cannot be taken to the world do not go.
std::vector<MeshDraw> MeshDrawsOf(const Models::IModelLibrary& models, std::uint16_t modelId,
                                  const Data::Models::Model& model, std::span<const BoneTransform> bones,
                                  const Models::ObjectTransform& object, std::uint8_t skin, Render::Color shade,
                                  const ObjectState& state, Core::Duration clock, Models::SkinnedMeshes& buffers)
{
    const std::span<const Render::TextureSlot> slots = models.MeshTextures(Models::ModelId{modelId});
    const std::span<const Models::MeshTraits> traits = models.MeshTraitsOf(Models::ModelId{modelId});
    const std::span<Models::SkinnedMesh> skinned = buffers.Reset(model.meshes.size());
    std::vector<MeshDraw> draws;
    draws.reserve(model.meshes.size());
    for (std::size_t index = 0; index < model.meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model.meshes[index];
        const Models::MeshTraits role = TraitsOf(traits, index);
        const MeshTexture texture = TextureOf(mesh, slots, index, skin);
        if (role.hidden || !texture.drawn || !IndicesInRange(mesh) ||
            !Models::SkinMesh(mesh, bones, object, skinned[index]))
        {
            continue;
        }
        const std::int16_t blendMesh = BlendMeshOf(role, index, state);
        draws.push_back(MeshDraw{modelId, index, &mesh, &skinned[index], role, texture.slot, shade,
                                 Blends(mesh, blendMesh),
                                 static_cast<std::int16_t>(index) == blendMesh || role.scrollScript, &state, clock});
    }
    return draws;
}

// A model with the `bones` matrices, each mesh with its texture and its level's glow; with the outline, the outline
// first.
void DrawModel(const CharacterFrame& frame, std::uint16_t modelId, std::span<const BoneTransform> bones,
               const ModelUse& use)
{
    const DrawnCharacter& character = frame.character;
    if (!use.hanging && !(character.alpha > PartBoxAlphaThreshold))
        return;
    const auto publishBox = [&]
    {
        if (!use.hanging && character.alpha > PartBoxAlphaThreshold && character.onPartBox)
            character.onPartBox(character.object.position);
    };
    const Data::Models::Model* model = frame.models.Find(Models::ModelId{modelId});
    if (model == nullptr)
    {
        publishBox();
        return;
    }
    // The parts use the body's bones and are not posed again: they publish nothing. What hangs was already published in
    // DrawHangingFrom, with its root.
    const glm::vec3 light =
        use.hanging ? frame.character.hangingLight.value_or(frame.character.light) : frame.character.light;
    const glm::vec3 shade = light * use.glow.baseFactor;
    std::vector<MeshDraw> draws =
        MeshDrawsOf(frame.models, modelId, *model, bones, frame.character.object,
                    SkinIndexOf(frame.character.appearance.characterClass),
                    Render::Color{shade.x, shade.y, shade.z, 1.0f}, use.object, frame.clock, frame.buffers.meshes);
    for (MeshDraw& draw : draws)
    {
        draw.alpha = frame.character.alpha;
    }
    if (use.outlined)
    {
        ObjectState outlineObject = use.object;
        outlineObject.light = use.outlineLight;
        std::vector<MeshDraw> outlined = draws;
        for (MeshDraw& draw : outlined)
        {
            draw.object = &outlineObject;
        }
        DrawOutline(frame, outlined, bones);
    }
    publishBox();
    DrawPasses(frame, draws, use);
}

// The hands loop of 0x50ba30 skips the bolts, the arrows and the raven: the first two go on the back (DrawQuiver), and
// the raven is a separate object, 0xc0 (0x420d60 creates it in the login), drawn on its own.
constexpr std::uint16_t Bolts = 0xa19;
constexpr std::uint16_t Arrows = 0xa21;
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::array<std::uint16_t, 3> NotInTheHand{Bolts, Arrows, Raven};

bool SkippedInTheHand(std::uint16_t model)
{
    return std::ranges::find(NotInTheHand, model) != NotInTheHand.end();
}

} // namespace

bool DrawsTheShadow(const DrawnCharacter& character)
{
    if (!character.castsShadow)
        return false;
    const std::optional<std::uint16_t>& pet = character.appearance.pet;
    return character.safeZone || !pet || std::ranges::find(PetsWithoutShadow, *pet) == PetsWithoutShadow.end();
}

namespace
{
using Maps::CastleSiegeMap;
constexpr float DefaultShadowSpread = 2000.0f;
constexpr float CastleSiegeShadowSpread = 2500.0f;

// 0x4eff8d..0x4eff9f (DAT_006c9dd0)
constexpr float ShadowSpreadOf(std::int32_t map)
{
    return map == CastleSiegeMap ? CastleSiegeShadowSpread : DefaultShadowSpread;
}

// The shadow of 0x50ba30 (0x20b with the body's matrices), before the parts: each mesh, taken to the body's vertices
// and flattened on the floor (0x4eff40). None with one of the ridden pets.
void DrawShadow(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const DrawnCharacter& character = frame.character;
    if (!DrawsTheShadow(character))
    {
        return;
    }
    const Data::Models::Model* model = frame.models.Find(Models::ModelId{Tables::PlayerShadowModel});
    if (model == nullptr)
    {
        return;
    }
    const std::span<Models::SkinnedMesh> meshes = frame.buffers.shadowMeshes.Reset(model->meshes.size());
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        // 0x4eff40 does not look at the texture: Shadow01's mesh is the hidden one (hide_m) and goes anyway.
        const Data::Models::Mesh& mesh = model->meshes[index];
        if (!IndicesInRange(mesh) || !Models::SkinMesh(mesh, body, character.object, meshes[index]))
        {
            continue;
        }
        MeshDraw draw;
        draw.mesh = &mesh;
        draw.skinned = &meshes[index];
        FlatTriangles(frame.batch, draw, ShadowState, ShadowColor, character.object.position,
                      ShadowSpreadOf(character.map));
    }
}

// A part of the loop of 0x50ba30, with the model it goes with and its glow; the helmet, marked, because class 4 writes
// the light there.
struct DrawnPart
{
    std::uint16_t model = 0;
    Models::ItemGlow glow;
    bool helm = false;
};

// The parts in the loop's order, from 5 to 0 (+0x1f8 + k · 0x20): the boots, the gloves, the pants, the armor, the
// helmet and the head.
std::vector<DrawnPart> PartsInOrder(const Kernel::CharacterAppearance& appearance)
{
    std::vector<DrawnPart> parts;
    for (std::size_t index = appearance.bodyParts.size(); index-- > 0;)
    {
        const Kernel::WornItem& part = appearance.bodyParts[index];
        const auto bodyPart = static_cast<Kernel::BodyPart>(index);
        parts.push_back(DrawnPart{DrawnPartModel(bodyPart, part.model, appearance.characterClass),
                                  Models::ItemGlowOf(part.level, part.excellent, part.ancient),
                                  bodyPart == Kernel::BodyPart::Helm});
    }
    if (appearance.head)
    {
        parts.push_back(DrawnPart{*appearance.head, Models::ItemGlow{}, false});
    }
    return parts;
}

// The light of the character's object before the outline of a part (0x50ba30 writes it on class 4's helmet) and after
// (0x60b3d0, by the model), from `light`.
struct PartLight
{
    float outline = 1.0f;
    float body = 1.0f;
};

PartLight LightOf(const DrawnPart& part, float light, std::uint8_t characterClass, Core::Duration clock)
{
    PartLight result{light, light};
    if (part.helm && IsMaskedClass(characterClass))
    {
        result = {Models::MaskedClassLightAt(clock), Models::MaskedClassLightAt(clock)};
    }
    if (const std::optional<float> written = Models::ModelLightOf(part.model, clock))
    {
        result.body = *written;
    }
    return result;
}

// The parts with the light of the character's object, which carries over from one part to the next and from one frame
// to another: it starts at what the last part that writes it left (the previous frame's, with this one's clock), or at
// 1 (0x511290).
void DrawParts(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const std::uint8_t characterClass = frame.character.appearance.characterClass;
    const std::vector<DrawnPart> parts = PartsInOrder(frame.character.appearance);
    float light = 1.0f;
    for (const DrawnPart& part : parts)
    {
        light = LightOf(part, light, characterClass, frame.clock).body;
    }
    for (const DrawnPart& part : parts)
    {
        const PartLight partLight = LightOf(part, light, characterClass, frame.clock);
        light = partLight.body;
        DrawModel(frame, part.model, body,
                  ModelUse{part.glow, frame.character.outlined, Tables::PlayerSkeletonModel,
                           ObjectState{Models::NoBlendMesh, light, {}, Models::NoHiddenMesh}, partLight.outline});
    }
}

// The two rand() of 0x607b60, from 0 to 32767 like MSVC's.
Models::ObjectRandoms RandomsOf(std::minstd_rand& random)
{
    constexpr std::uint_fast32_t RandRange = 32768;
    const auto first = static_cast<int>(random() % RandRange);
    const auto second = static_cast<int>(random() % RandRange);
    return {first, second};
}

Models::ObjectRandoms RandomsOf(const CharacterFrame& frame, std::uint16_t model)
{
    if (frame.character.originalRandom == nullptr)
    {
        return RandomsOf(frame.random);
    }
    Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(model); ++index)
    {
        randoms[index] = frame.character.originalRandom->Next();
    }
    return randoms;
}

// What hangs from the root `root`, posed (0x4eb860 with its seventh argument at 1) and drawn with the object 0x509340
// builds with its item's type. Then, its model's case that asks for sprites: the sceptre's, with bone 0 of that pose
// and the item's body, the character's scale (0x509397) and `bodyOrigin` (+0x6c of the model).
void DrawHangingFrom(const CharacterFrame& frame, std::uint16_t modelId, const BoneTransform& root,
                     const glm::vec3& bodyOrigin, const Models::AnimationState& state, const Models::ItemGlow& glow)
{
    constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;
    if (!Actors::HangsAsAModel(modelId) || (frame.character.objectFlags & HiddenAttachmentFlag) != 0)
    {
        return;
    }
    // 0x5093bb initializes the object before trying the item's pose.
    const std::optional<Models::ObjectRandoms> originalRandoms =
        frame.character.originalRandom != nullptr ? std::optional{RandomsOf(frame, modelId)} : std::nullopt;
    const Models::Skeleton* skeleton = frame.models.SkeletonOf(Models::ModelId{modelId});
    if (skeleton == nullptr)
    {
        return;
    }
    std::vector<BoneTransform>& bones = frame.buffers.attachedBones;
    bones.assign(skeleton->bones.size(), BoneTransform(frame.character.poseBones ? 0.0f : 1.0f));
    if (frame.character.poseBones)
    {
        const auto retained = frame.character.poseBones(modelId);
        std::copy_n(retained.begin(), std::min(retained.size(), bones.size()), bones.begin());
    }
    if (!Models::PoseAttached(*skeleton, state, root, bones))
    {
        return;
    }
    if (frame.character.onPose)
        frame.character.onPose(modelId, bones, frame.character.object, false);
    const ObjectState object = Models::HangingObjectState(
        modelId, frame.clock, originalRandoms.has_value() ? *originalRandoms : RandomsOf(frame, modelId));
    DrawModel(frame, modelId, bones, ModelUse{glow, false, modelId, object, object.light, true});
    const DrawnCharacter& character = frame.character;
    if (modelId == Actors::Drawing::HdkMaceModel && character.sprites != nullptr && character.clothWorld != nullptr &&
        !bones.empty())
    {
        const Math::OriginalMatrix bone = Math::OriginalMatrixOf(bones.front());
        if (character.scepter != nullptr)
        {
            character.scepter->Create(*character.sprites, bone, character.object.scale, bodyOrigin,
                                      character.clothWorld->random, frame.clock, character.pace);
        }
        else
        {
            Actors::Drawing::CreateScepterGlow(*character.sprites, bone, character.object.scale, bodyOrigin,
                                               character.clothWorld->random);
        }
    }
}

// 0x4ec190 and 0x50966b: the body origin of what hangs from its slot, the offset turned by the bone and by the
// character's scale, stored as float, plus its position. With the hands' offset, at 0, it is the position.
glm::vec3 HangingOrigin(const BoneTransform& parent, const glm::vec3& offset, const Models::ObjectTransform& object)
{
    const glm::vec3 turned = Math::OriginalVectorRotate(offset, Math::OriginalMatrixOf(parent));
    glm::vec3 origin{};
    for (int k = 0; k < 3; ++k)
    {
        const auto scaled = static_cast<float>(static_cast<double>(object.scale) * static_cast<double>(turned[k]));
        origin[k] = static_cast<float>(static_cast<double>(scaled) + static_cast<double>(object.position[k]));
    }
    return origin;
}

// What hangs from its slot: from its body bone with the offset (0x509340 with the flag at 0).
void DrawHanging(const CharacterFrame& frame, std::uint16_t modelId, Actors::AttachmentSlot slot,
                 const Models::AnimationState& state, std::span<const BoneTransform> body, const Models::ItemGlow& glow)
{
    const Actors::Attachment attachment = Actors::AttachmentOf(slot);
    if (static_cast<std::size_t>(attachment.bone) >= body.size())
    {
        return;
    }
    const BoneTransform& parent = body[static_cast<std::size_t>(attachment.bone)];
    DrawHangingFrom(frame, modelId, Actors::AttachmentRoot(parent, attachment.offset),
                    HangingOrigin(parent, attachment.offset, frame.character.object), state, glow);
}

// The quiver of 0x50ba30 with the weapons in the hand (local_20 at 0): the bolts of the left hand or, otherwise, the
// arrows of the right.
std::optional<Kernel::WornItem> QuiverOf(const Kernel::CharacterAppearance& appearance)
{
    if (appearance.leftHand && appearance.leftHand->model == Bolts)
    {
        return appearance.leftHand;
    }
    if (appearance.rightHand && appearance.rightHand->model == Arrows)
    {
        return appearance.rightHand;
    }
    return std::nullopt;
}

// The quiver on the back: from the wings' bone with QuiverPlacement (0x509340 with its flag), with the level of
// 0x60b3d0 and the hand's excellent bits, without the set. Arrows01 and 02 have a one-key action: the pose does not
// depend on the frame of the wings' slot, with which the original poses it.
void DrawQuiver(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const std::optional<Kernel::WornItem> quiver = QuiverOf(frame.character.appearance);
    const auto bone = static_cast<std::size_t>(Actors::AttachmentOf(Actors::AttachmentSlot::Wings).bone);
    if (!quiver || bone >= body.size())
    {
        return;
    }
    const Models::ItemGlow glow = Models::ItemGlowOf(Models::QuiverGlowLevel(quiver->level), quiver->excellent, false);
    // Along that path, the body origin is the character's position (0x509607).
    DrawHangingFrom(frame, quiver->model, Actors::PlacedRoot(body[bone], Actors::QuiverPlacement),
                    frame.character.object.position, Models::AnimationState{}, glow);
}

// flare01 (0x5ac) and blend 0 of 0x5491e0, the additive one.
constexpr std::uint16_t ClothFlareTexture = 0x5ac;
constexpr std::int32_t AdditiveSprite = 0;

// 0x50d58b..0x50db57: the cloths, after the parts loop (0x50d3dc) and before what hangs, with this frame's body bones
// and its clock: one frame of the block for each original frame that passed (CharacterCloth::Advance).
void DrawTheCloth(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const DrawnCharacter& character = frame.character;
    if (character.cloth == nullptr || character.clothWorld == nullptr)
    {
        return;
    }
    const Kernel::CharacterAppearance& appearance = character.appearance;
    ClothWearer wearer;
    wearer.characterClass = appearance.characterClass;
    wearer.armor = appearance.bodyParts[static_cast<std::size_t>(Kernel::BodyPart::Armor)].model;
    wearer.wings = appearance.wings;
    wearer.objectFlags = character.objectFlags;
    wearer.monster = character.monster;
    const Models::ClothBody clothBody{.bones = body,
                                      .angles = character.object.angles,
                                      .scale = character.object.scale,
                                      .position = character.object.position};
    const bool wears =
        character.wearsCloth.value_or(WearsACloth(appearance.characterClass, character.map) ||
                                      (character.monster && WearsTheMonsterCloth(*character.monster, character.map)));
    if (character.advanceCloth)
    {
        character.cloth->Advance(frame.batch, wearer, clothBody, *character.clothWorld, character.map, frame.clock,
                                 wears, character.clothColor, character.pace);
    }
    else
    {
        character.cloth->DrawCurrent(frame.batch, wearer, clothBody, frame.clock, wears, character.clothColor);
    }
    // The flares of the white helmet (0x455930), additive, in the frame's table.
    if (character.sprites != nullptr)
    {
        for (const Models::ClothFlare& flare : character.cloth->Flares())
        {
            (void)character.sprites->Create(ClothFlareTexture, flare.position, flare.scale, glm::vec3(flare.light),
                                            0.0f, AdditiveSprite);
        }
    }
}

// The original's boundaries let what was already drawn stay if what follows is missing: shadow (0x50bcec), parts
// (0x50d4e0) and cloth (0x50d58b).
bool DrawTheBodyPass(const CharacterFrame& frame, std::span<const BoneTransform> body, CharacterDrawPass pass)
{
    const bool fullBody = pass == CharacterDrawPass::All || pass == CharacterDrawPass::Body;
    if (fullBody || pass == CharacterDrawPass::Shadow)
        DrawShadow(frame, body);
    if (fullBody || pass == CharacterDrawPass::Parts)
        DrawParts(frame, body);
    if (fullBody || pass == CharacterDrawPass::Cloth)
        DrawTheCloth(frame, body);
    return pass == CharacterDrawPass::All || pass == CharacterDrawPass::Hanging;
}

// The hands loop of 0x50ba30 (0x50e6af), after the back, the wings and the pet.
void DrawHands(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const Kernel::CharacterAppearance& appearance = frame.character.appearance;
    using Actors::AttachmentSlot;
    for (const auto& [hand, slot] : {std::pair{appearance.rightHand, AttachmentSlot::RightHand},
                                     std::pair{appearance.leftHand, AttachmentSlot::LeftHand}})
    {
        if (hand && !SkippedInTheHand(hand->model))
        {
            // 0x509340 passes 0x60b3d0 the set at 0: what hangs does not carry its pass.
            const Models::ItemGlow glow = Models::ItemGlowOf(hand->level, hand->excellent, false);
            DrawHanging(frame, hand->model, slot, Models::AnimationState{}, body, glow);
        }
    }
}

// The seed of a frame's rand(): the clock in ms. The original uses the whole client's rand(), which cannot be repeated.
std::uint_fast32_t RandomSeedOf(Core::Duration clock)
{
    return static_cast<std::uint_fast32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
}
} // namespace

std::optional<Render::TextureSlot> MeshSlotOf(Render::TextureSlot assigned, std::uint8_t characterClass)
{
    return SkinSlotOf(assigned, SkinIndexOf(characterClass));
}

// 0x607b60 draws no rand outside these four cases: one for Sword15 and Staff08, two for Staff06 and Shield15. The
// legacy local draw does not belong to the global flow.
std::size_t HangingRandomCount(std::uint16_t model)
{
    constexpr std::uint16_t Sword15 = 0x220;
    constexpr std::uint16_t Staff08 = 0xc19;
    constexpr std::uint16_t Staff06 = 0xc17;
    constexpr std::uint16_t Shield15 = 0xe20;
    if (model == Sword15 || model == Staff08)
        return 1;
    if (model == Staff06 || model == Shield15)
        return 2;
    return 0;
}

namespace
{
// PoseCharacterBody into `body`, which keeps its capacity: empty without the skeleton or if the pose fails.
std::span<const BoneTransform> PoseCharacterBodyInto(std::vector<BoneTransform>& body,
                                                     const Models::IModelLibrary& models,
                                                     const DrawnCharacter& character)
{
    body.clear();
    const Models::Skeleton* skeleton = models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (skeleton == nullptr)
    {
        return {};
    }
    body.assign(skeleton->bones.size(), BoneTransform(character.poseBones ? 0.0f : 1.0f));
    if (character.poseBones)
    {
        const auto retained = character.poseBones(Tables::PlayerSkeletonModel);
        std::copy_n(retained.begin(), std::min(retained.size(), body.size()), body.begin());
    }
    if (!Models::PoseSkeleton(*skeleton, character.body, character.object, character.headAngles, body))
    {
        body.clear();
    }
    return body;
}
} // namespace

std::vector<BoneTransform> PoseCharacterBody(const Models::IModelLibrary& models, const DrawnCharacter& character)
{
    std::vector<BoneTransform> body;
    PoseCharacterBodyInto(body, models, character);
    return body;
}

void DrawCharacter(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnCharacter& character,
                   Core::Duration frameClock, CharacterDrawPass pass)
{
    Models::SkinningBuffers own;
    Models::SkinningBuffers& buffers = BuffersOf(character, own);
    const std::span<const BoneTransform> body = PoseCharacterBodyInto(buffers.bones, models, character);
    if (body.empty())
    {
        return;
    }
    if (character.onPose)
        character.onPose(Tables::PlayerSkeletonModel, body, character.object, false);
    const Kernel::CharacterAppearance& appearance = character.appearance;
    std::minstd_rand random(RandomSeedOf(frameClock));
    const CharacterFrame frame{batch, models, character, frameClock, random, buffers};
    if (!DrawTheBodyPass(frame, body, pass))
    {
        return;
    }
    DrawQuiver(frame, body);
    const Models::ItemGlow noGlow{};
    using Actors::AttachmentSlot;
    if (appearance.wings)
    {
        DrawHanging(frame, *appearance.wings, AttachmentSlot::Wings, character.wings, body, noGlow);
    }
    if (appearance.pet && Actors::DrawsThePet(*appearance.pet))
    {
        DrawHanging(frame, *appearance.pet, AttachmentSlot::Pet, character.pet, body, noGlow);
    }
    DrawHands(frame, body);
}

namespace
{
// The skin the Angel's skin_ mesh goes with: 0 (+0x98 of 0x1f7 at 0).
constexpr std::uint8_t ChaosCastleBodySkin = 0;
} // namespace

Core::Expected<void, std::string> DrawChaosCastleBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      const Models::IModelLibrary& angelModels,
                                                      const DrawnCharacter& character, Core::Duration frameClock,
                                                      std::uint32_t flags)
{
    if (!(character.alpha > PartBoxAlphaThreshold))
        return {};
    Models::SkinningBuffers own;
    Models::SkinningBuffers& buffers = BuffersOf(character, own);
    const std::span<const BoneTransform> body = PoseCharacterBodyInto(buffers.bones, models, character);
    if (body.empty())
        return {};
    if (character.onPose)
        character.onPose(Tables::PlayerSkeletonModel, body, character.object, false);
    const Data::Models::Model* model = angelModels.Find(Models::ModelId{ChaosCastleBodyModel});
    if (model == nullptr)
    {
        if (character.alpha > PartBoxAlphaThreshold && character.onPartBox)
            character.onPartBox(character.object.position);
        return {};
    }
    if (character.onPose)
        character.onPose(ChaosCastleBodyModel, body, character.object, false);
    // The character's object: without blendMesh or hidden mesh and with the +0x68 light at 1, as 0x511290 leaves it. On
    // these maps the parts loop, which changes it on class 4's helmet, does not run for a player.
    const ObjectState state{Models::NoBlendMesh, 1.0f, {}, Models::NoHiddenMesh};
    if (character.outlined)
    {
        // 0x60d04d: the outline, before 0x5ffce0 and 0x60b3d0.
        std::minstd_rand random(RandomSeedOf(frameClock));
        const CharacterFrame frame{batch, angelModels, character, frameClock, random, buffers};
        std::vector<MeshDraw> draws =
            MeshDrawsOf(angelModels, ChaosCastleBodyModel, *model, body, character.object, ChaosCastleBodySkin,
                        Render::Color{character.light.x, character.light.y, character.light.z, 1.0f}, state, frameClock,
                        buffers.meshes);
        for (MeshDraw& draw : draws)
            draw.alpha = character.alpha;
        DrawOutline(frame, draws, body);
    }
    if (character.alpha > PartBoxAlphaThreshold && character.onPartBox)
        character.onPartBox(character.object.position);
    const std::span<Models::SkinnedMesh> meshes = buffers.posedMeshes.Reset(model->meshes.size());
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        if (!IndicesInRange(model->meshes[index]) ||
            !Models::SkinMesh(model->meshes[index], body, character.object, meshes[index]))
        {
            return Core::Unexpected(
                std::string("0x60cf70: una malla del Angel (0x1f7) no se puede pasar a los huesos del cuerpo"));
        }
    }
    // 0x60b3d0 with level 0: the model's color is the light (0x60cb0f), and 0x609640 does the rest.
    DrawnObject object;
    object.model = ChaosCastleBodyModel;
    object.animation = character.body;
    object.transform = character.object;
    object.state = state;
    object.color = character.light;
    object.alpha = character.alpha;
    ItemSpecialDrawContext special;
    special.flags = flags;
    return DrawSpecialItemMeshes(batch, angelModels, object, frameClock, meshes, special);
}

namespace
{
MeshDraw RetainedMeshDraw(const DrawnObject& object, Core::Duration frameClock, const Data::Models::Mesh& mesh,
                          const Models::SkinnedMesh& skinned, std::size_t index, const Models::MeshTraits& role,
                          std::optional<Render::TextureSlot> texture, bool originalClock)
{
    const auto blendMesh = BlendMeshOf(role, index, object.state);
    MeshDraw draw{object.model,
                  index,
                  &mesh,
                  &skinned,
                  role,
                  texture,
                  Render::Color{object.color.x, object.color.y, object.color.z, 1},
                  Blends(mesh, blendMesh),
                  static_cast<std::int16_t>(index) == blendMesh || role.scrollScript,
                  &object.state,
                  frameClock};
    draw.alpha = object.alpha;
    const bool unlit = object.unlitMesh && static_cast<std::int16_t>(index) == *object.unlitMesh;
    draw.vertexLight = object.vertexLight && !unlit;
    draw.scrolls = draw.scrolls || unlit;
    draw.originalClock = originalClock;
    return draw;
}

// Each mesh of `object`'s model drawn from `meshes`, in its order, to `visit`: none without the model or, unless
// `forceVisible`, with the alpha below 0.01; not the hidden one (unless `rawMesh`), those not drawn, the broken ones
// and those `meshes` does not cover.
template <typename Visit>
void ForEachRetainedMeshDraw(const Visit& visit, const Models::IModelLibrary& models, const DrawnObject& object,
                             Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes,
                             bool forceVisible = false, bool rawMesh = false, bool originalClock = false)
{
    const auto* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr || (!forceVisible && object.alpha < 0.01f))
        return;
    const auto slots = models.MeshTextures(Models::ModelId{object.model});
    const auto traits = models.MeshTraitsOf(Models::ModelId{object.model});
    for (std::size_t index = 0; index < model->meshes.size() && index < meshes.size(); ++index)
    {
        const auto& mesh = model->meshes[index];
        const auto role = TraitsOf(traits, index);
        const auto texture = TextureOf(mesh, slots, index, object.skin);
        if ((!rawMesh && role.hidden) || !texture.drawn || !IndicesInRange(mesh) ||
            meshes[index].positions.size() < mesh.vertices.size() || meshes[index].normals.size() < mesh.normals.size())
            continue;
        visit(RetainedMeshDraw(object, frameClock, mesh, meshes[index], index, role, texture.slot, originalClock));
    }
}

// ForEachRetainedMeshDraw's draws, for the passes that go over them more than once.
std::vector<MeshDraw> RetainedObjectMeshDraws(const Models::IModelLibrary& models, const DrawnObject& object,
                                              Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes,
                                              bool forceVisible = false, bool rawMesh = false,
                                              bool originalClock = false)
{
    std::vector<MeshDraw> draws;
    ForEachRetainedMeshDraw([&draws](const MeshDraw& draw) { draws.push_back(draw); }, models, object, frameClock,
                            meshes, forceVisible, rawMesh, originalClock);
    return draws;
}
} // namespace

std::vector<Models::SkinnedMesh> WorldScratchOf(const Models::IModelLibrary& models, const DrawnObject& object)
{
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{object.model});
    if (model == nullptr || skeleton == nullptr)
        return {};
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Models::PoseWorldSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
        return {};
    std::vector<Models::SkinnedMesh> skinned(model->meshes.size());
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        if (IndicesInRange(model->meshes[index]))
            static_cast<void>(Models::SkinMeshInWorld(model->meshes[index], bones, skinned[index]));
    }
    return skinned;
}

namespace
{
// The meshes of a world object posed like 0x5ffdf0 with the second argument at 0, with its alpha and its per-vertex
// light, to `visit`, or none if 0x5ffdf0 does not draw it (the alpha below 0.01, 0x6b88d4) or its model is missing.
// Without the published scratch, WorldScratchOf builds it in `skinned`.
template <typename Visit>
void ForEachObjectMeshDraw(const Visit& visit, const Models::IModelLibrary& models, const DrawnObject& object,
                           Core::Duration clock, std::vector<Models::SkinnedMesh>& skinned)
{
    if (!object.retainedMeshes.empty())
    {
        ForEachRetainedMeshDraw(visit, models, object, clock, object.retainedMeshes);
        return;
    }
    constexpr float VisibleAlpha = 0.01f;
    if (object.alpha < VisibleAlpha)
        return;
    skinned = WorldScratchOf(models, object);
    ForEachRetainedMeshDraw(visit, models, object, clock, skinned);
}

// ForEachObjectMeshDraw's draws, for the passes that look them up or go over them more than once.
std::vector<MeshDraw> ObjectMeshDraws(const Models::IModelLibrary& models, const DrawnObject& object,
                                      Core::Duration clock, std::vector<Models::SkinnedMesh>& skinned)
{
    std::vector<MeshDraw> draws;
    ForEachObjectMeshDraw([&draws](const MeshDraw& draw) { draws.push_back(draw); }, models, object, clock, skinned);
    return draws;
}

// The last of the four meshes of Lorencia's fountain (0x600ad6), which is also its blendMesh.
constexpr std::int16_t WaterspoutLastMesh = 3;

// What 0x6002e0 adds to the horse after its RenderBody (0x6002e0, case 0xbf).
constexpr std::size_t DarkHorseFirstChromeMesh = 12;
constexpr std::size_t DarkHorseLastChromeMesh = 15;
constexpr Render::Color DarkHorseChromeColor{1.0f, 0.8f, 0.3f, 1.0f};
constexpr std::size_t DarkHorseShadowMesh = 8;
constexpr Render::Color DarkHorseShadowColor{0.0f, 0.0f, 0.0f, 0.7f};
// 0x60073c..0x600787: no shadow on maps 10, 24 to 29 and 36; on 7, glColor4f(0, 0, 0, 0.2).
constexpr float DarkHorseShadowAlphaOfMapSeven = 0.2f;
constexpr std::int32_t DarkHorseFaintShadowMap = 7;
constexpr std::int32_t DarkHorseNoShadowMap = 10;
constexpr std::int32_t DarkHorseFirstKalimaMap = 24;
constexpr std::int32_t DarkHorseLastKalimaMap = 29;
constexpr std::int32_t DarkHorseSeventhKalimaMap = 36;
constexpr Render::ModelDrawState BlendedShadowState{std::nullopt, Render::ModelBlend::BlendedShadow, true};
// Case 0xc0: the meshes it draws with the pet's level at 0, in its order, and its shadow (glColor4f(0, 0, 0, 1),
// 0x6009eb).
constexpr std::array<std::size_t, 2> DarkSpiritMeshes{3, 2};
constexpr Render::Color DarkSpiritShadowColor{0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

void SkinTheDrawnObject(const Models::IModelLibrary& models, const DrawnObject& object, Core::Duration clock,
                        std::vector<Models::SkinnedMesh>& skinned)
{
    static_cast<void>(ObjectMeshDraws(models, object, clock, skinned));
}

void DrawObject(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                Core::Duration frameClock)
{
    std::vector<Models::SkinnedMesh> skinned;
    ForEachObjectMeshDraw([&batch](const MeshDraw& draw) { DrawBase(batch, draw); }, models, object, frameClock,
                          skinned);
}

void DrawObjectWithTexture(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                           Core::Duration frameClock, Render::TextureSlot texture)
{
    std::vector<Models::SkinnedMesh> skinned;
    ForEachObjectMeshDraw(
        [&batch, texture](MeshDraw draw)
        {
            draw.texture = texture;
            DrawBaseWith(batch, draw, false);
        },
        models, object, frameClock, skinned);
}

void DrawObjectFromMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                          Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes,
                          std::optional<Models::ShinePass> shine)
{
    SetFade fade{object.alpha, 1.0f};
    const auto drawMesh = [&](const MeshDraw& draw)
    {
        if (!shine)
        {
            DrawBase(batch, draw);
            return;
        }
        if (!draw.role.shines || Models::PassHiddenMesh(draw.model, *shine, draw.object->hiddenMesh) == draw.index)
            return;
        if (*shine == Models::ShinePass::Chrome || *shine == Models::ShinePass::Shiny)
        {
            // 0x60b130 → 0x608e00 gets alpha and scale before RenderBody; the color includes that product.
            const auto color = Scaled(Models::ShineColorOf(object.model), object.alpha);
            Triangles(batch, draw, {PassSlot(*shine), Render::ModelBlend::Additive, true}, color, PassUv{*shine, {}});
        }
        else
            DrawShinePass(batch, draw, *shine, fade);
    };
    ForEachRetainedMeshDraw(drawMesh, models, object, frameClock, meshes, shine.has_value());
}

namespace
{
// SkinObjectMeshes into `skinned`, one empty mesh per mesh of `model`.
void SkinEachMesh(const Data::Models::Model& model, const DrawnObject& object, std::span<const BoneTransform> bones,
                  float boneScale, std::span<Models::SkinnedMesh> skinned)
{
    for (std::size_t index = 0; index < model.meshes.size(); ++index)
    {
        if (IndicesInRange(model.meshes[index]))
            static_cast<void>(
                Models::SkinMeshAroundBones(model.meshes[index], bones, object.transform, boneScale, skinned[index]));
    }
}
} // namespace

std::vector<Models::SkinnedMesh> SkinObjectMeshes(const Models::IModelLibrary& models, const DrawnObject& object,
                                                  std::span<const BoneTransform> bones, float boneScale)
{
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return {};
    std::vector<Models::SkinnedMesh> skinned(model->meshes.size());
    SkinEachMesh(*model, object, bones, boneScale, skinned);
    return skinned;
}

std::span<Models::SkinnedMesh> SkinObjectMeshesInto(Models::SkinnedMeshes& buffers, const Models::IModelLibrary& models,
                                                    const DrawnObject& object, std::span<const BoneTransform> bones,
                                                    float boneScale)
{
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return buffers.Reset(0);
    const std::span<Models::SkinnedMesh> skinned = buffers.Reset(model->meshes.size());
    SkinEachMesh(*model, object, bones, boneScale, skinned);
    return skinned;
}

void DrawObjectOutline(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                       Core::Duration frameClock, std::span<const BoneTransform> bones,
                       std::span<const ObjectOutlinePass> passes)
{
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return;
    Models::SkinnedMeshes skinned;
    const std::vector<MeshDraw> draws = MeshDrawsOf(models, object.model, *model, bones, object.transform, object.skin,
                                                    Render::Color{object.color.x, object.color.y, object.color.z, 1.0f},
                                                    object.state, frameClock, skinned);
    for (const ObjectOutlinePass& pass : passes)
    {
        const OutlinePass around{pass.boneScale, pass.color};
        for (const MeshDraw& draw : draws)
        {
            if (static_cast<std::int16_t>(draw.index) != object.state.hiddenMesh)
                OutlineMeshAround(batch, object.transform, around, draw, bones);
        }
    }
}

void DrawObjectOutlineOfTheFirstMesh(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                     const DrawnObject& object, Core::Duration frameClock,
                                     std::span<const BoneTransform> bones, std::span<const ObjectOutlinePass> passes)
{
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr || model->meshes.empty())
        return;
    // 0x60ce4e: 0x4ecc30(0, …) without 0x4ef0c0, which is the one that looks at the mesh's script (hidden or its own
    // blendMesh).
    const Data::Models::Mesh& mesh = model->meshes.front();
    Models::MeshTraits role = TraitsOf(models.MeshTraitsOf(Models::ModelId{object.model}), 0);
    role.hidden = false;
    role.blendScript = false;
    const MeshTexture texture = TextureOf(mesh, models.MeshTextures(Models::ModelId{object.model}), 0, object.skin);
    Models::SkinnedMesh skinned;
    if (!texture.drawn || !IndicesInRange(mesh) || !Models::SkinMesh(mesh, bones, object.transform, skinned))
        return;
    const std::int16_t blendMesh = BlendMeshOf(role, 0, object.state);
    const MeshDraw draw{object.model,
                        0,
                        &mesh,
                        &skinned,
                        role,
                        texture.slot,
                        Render::Color{object.color.x, object.color.y, object.color.z, 1.0f},
                        Blends(mesh, blendMesh),
                        blendMesh == 0 || role.scrollScript,
                        &object.state,
                        frameClock};
    for (const ObjectOutlinePass& pass : passes)
        OutlineMeshAround(batch, object.transform, OutlinePass{pass.boneScale, pass.color}, draw, bones);
}

void DrawObjectShadow(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                      std::span<const Models::SkinnedMesh> meshes, std::int32_t map)
{
    const bool kalima =
        (map >= DarkHorseFirstKalimaMap && map <= DarkHorseLastKalimaMap) || map == DarkHorseSeventhKalimaMap;
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr || map == DarkHorseNoShadowMap || kalima)
        return;
    const bool faint = map == DarkHorseFaintShadowMap;
    Render::Color color = DarkSpiritShadowColor;
    if (faint)
        color.alpha = DarkHorseShadowAlphaOfMapSeven;
    const float spread = ShadowSpreadOf(map);
    for (std::size_t index = 0; index < model->meshes.size() && index < meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model->meshes[index];
        if (static_cast<std::int16_t>(index) == object.state.hiddenMesh || mesh.triangles.empty() ||
            mesh.texture == object.state.blendMesh || !IndicesInRange(mesh) ||
            meshes[index].positions.size() < mesh.vertices.size())
            continue;
        MeshDraw draw;
        draw.mesh = &mesh;
        draw.skinned = &meshes[index];
        FlatTriangles(batch, draw, faint ? BlendedShadowState : ShadowState, faint ? color : ShadowColor,
                      object.transform.position, spread);
    }
}

namespace
{
std::optional<Models::ShinePass> ItemShine(std::uint32_t flags)
{
    using Models::ShinePass;
    if ((flags & 0x4) != 0)
        return ShinePass::Chrome;
    if ((flags & 0x8) != 0)
        return ShinePass::Shiny;
    if ((flags & 0x200) != 0)
        return ShinePass::Chrome2;
    if ((flags & 0x800) != 0)
        return ShinePass::Chrome3;
    if ((flags & 0x1000) != 0)
        return ShinePass::Chrome4;
    if ((flags & 0x4000) != 0)
        return ShinePass::Chrome5;
    return {};
}

Core::Expected<void, std::string> ValidateItemMesh(const Data::Models::Mesh& mesh,
                                                   std::span<const Models::SkinnedMesh> meshes, std::size_t index,
                                                   std::size_t textures, bool textureOverride)
{
    if (!textureOverride && index >= textures)
        return Core::Unexpected(std::string("4ecc30: falta la textura de una malla del ítem"));
    if (index >= meshes.size())
        return Core::Unexpected(
            std::string("4ecc30: el scratch retenido no cubre los vértices/normales de la malla del ítem"));
    if (meshes[index].positions.size() < mesh.vertices.size() || meshes[index].normals.size() < mesh.normals.size() ||
        !IndicesInRange(mesh))
        return Core::Unexpected(std::string("4ecc30: la malla del ítem no tiene vértices/normales válidos"));
    return {};
}

Render::ModelBlend ItemPassBlend(const MeshDraw& draw, std::uint32_t flags, std::optional<Models::ShinePass> shine)
{
    using Render::ModelBlend;
    // 0x4ecf22..0x4ecf34 → 0x4ed014: the blendMesh's mesh (or all, with the blendMesh at −2 or below) only looks at
    // 0x80: ZERO / ONE_MINUS_SRC_COLOR (0x620b10) with it, additive (0x620a90) without it. 0x40 belongs to the other
    // branch (0x4ecf57).
    if (!shine && draw.blends)
        return (flags & 0x80) != 0 ? ModelBlend::Subtractive : ModelBlend::Additive;
    if ((flags & 0x40) != 0 || shine == Models::ShinePass::Chrome3 || shine == Models::ShinePass::Chrome4 ||
        shine == Models::ShinePass::Chrome5)
        return ModelBlend::Additive;
    if ((flags & 0x80) != 0)
        return ModelBlend::Subtractive;
    if (shine && (flags & 0x10) != 0)
        return ModelBlend::Multiplicative;
    if (draw.alpha < OpaqueAlpha || ((flags & 0x2) != 0 && HasFourComponents(*draw.mesh)))
        return ModelBlend::Alpha;
    return ModelBlend::Opaque;
}

void DrawItemPassMesh(Render::ModelBatch& batch, const MeshDraw& draw, std::uint32_t flags,
                      std::optional<Models::ShinePass> shine, std::optional<Render::TextureSlot> texture,
                      glm::vec3& currentColor, glm::vec3* storedColor)
{
    if (shine && !draw.role.shines)
        return;
    if (!shine && ((!draw.blends && (flags & 0x43) == 0) || ((flags & 0x3) == 0 && HasFourComponents(*draw.mesh))))
        return;
    // 0x4ed4bd: 0x4000 without a forced texture goes with the mesh's slot, not with one of the pass.
    const bool ownSlot = !shine || *shine == Models::ShinePass::Chrome5;
    auto slot = texture ? texture : (ownSlot ? draw.texture : std::optional(PassSlot(*shine)));
    const auto blend = ItemPassBlend(draw, flags, shine);
    Render::Color color{currentColor.r, currentColor.g, currentColor.b, 1};
    if (blend == Render::ModelBlend::Alpha)
        color.alpha = draw.alpha;
    if ((flags & 0x1) != 0)
        slot.reset();
    if (!shine && draw.blends)
        color = Scaled(color, draw.object->light);
    if (shine && blend == Render::ModelBlend::Additive && draw.alpha < OpaqueAlpha)
    {
        color = Scaled(color, draw.alpha);
        currentColor = {color.red, color.green, color.blue}; // 0x4ed3fd..0x4ed415: FSTP RGB per mesh.
        if (storedColor)
            *storedColor = currentColor;
    }
    const auto shift = shine == Models::ShinePass::Chrome4 || (!shine && draw.scrolls) ? draw.object->uv : glm::vec2{};
    Triangles(batch, draw, {slot, blend, true}, color, PassUv{shine, shift},
              !shine && draw.vertexLight && !draw.blends);
}

Models::SkinnedMesh FlattenedItemMesh(const Models::SkinnedMesh& mesh, const glm::vec3& origin)
{
    // 0x4ed6f6..0x4ed76d: X/Z in PC53 up to the store; Y has an intermediate spillF32.
    constexpr double Spread = 2000.0;
    constexpr double Horizon = 4000.0;
    constexpr double Lift = 5.0;
    auto flattened = mesh;
    for (auto& point : flattened.positions)
    {
        const double x = static_cast<double>(point.x) - origin.x;
        const double z = static_cast<double>(point.z) - origin.z;
        const float y = static_cast<float>(static_cast<double>(point.y) - origin.y);
        point = {static_cast<float>(((x + Spread) * z) / (z - Horizon) + x + origin.x),
                 static_cast<float>(static_cast<double>(y) + origin.y),
                 static_cast<float>(static_cast<double>(origin.z) + Lift)};
    }
    return flattened;
}

// 0x4ed775..0x4ed818: the wave of flag 0x400 (without 0x20, which goes before): each corner comes from its vertex in
// the world (0x5786f9c) shifted by the corner's turned normal (0x68c53bc) by sin((931 · v + ftol(T)) · 0.007f) · 28,
// with v the vertex index. The sine stays on the x87 and each product and the sum round to double (PC53). Since the
// shift depends on the corner's normal, each corner goes with its own vertex.
struct WavedMesh
{
    Data::Models::Mesh mesh;
    Models::SkinnedMesh vertices;
};

Core::Expected<WavedMesh, std::string> WavedItemMesh(const Data::Models::Mesh& input,
                                                     const Models::SkinnedMesh& skinned, float worldClock)
{
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3be56042u)); // 0.007f (0x6ba144)
    constexpr double Reach = 28.0;                                                  // 0x6b880c
    const auto clock = static_cast<std::uint32_t>(Math::FtolLow(static_cast<double>(worldClock)));
    WavedMesh waved;
    waved.mesh = input;
    waved.mesh.vertices.clear();
    waved.vertices.normals = skinned.normals;
    for (Data::Models::Triangle& triangle : waved.mesh.triangles)
    {
        const std::size_t corners = std::min<std::size_t>(triangle.polygon, triangle.vertices.size());
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
            const std::int16_t vertex = triangle.vertices[corner];
            const glm::vec3& origin = skinned.positions[static_cast<std::size_t>(vertex)];
            const glm::vec3& normal = skinned.normals[static_cast<std::size_t>(triangle.normals[corner])];
            const auto phase =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(vertex)) * 931u + clock);
            const double argument = static_cast<double>(phase) * Rate;
            glm::vec3 point{};
            for (int axis = 0; axis < 3; ++axis)
            {
                // 0x4ed7f4: FMUL of the held sine by the f32 normal with a single R53. With the normal at ±0 the
                // product is a zero with the sign of sine XOR normal (the sine's R53 keeps the sign).
                const float factor = normal[axis];
                const float magnitude = factor == 0.0f ? 1.0f : std::fabs(factor);
                const auto sine = Math::Detail::SineProductPC53(argument, magnitude);
                if (!sine)
                    return Core::Unexpected(std::string("4ed7c0: la onda no cabe en el perfil PC53"));
                const double product =
                    factor == 0.0f ? *sine * static_cast<double>(factor) : (factor < 0.0f ? -*sine : *sine);
                point[axis] = static_cast<float>(product * Reach + static_cast<double>(origin[axis]));
            }
            if (waved.mesh.vertices.size() >= 0x8000)
                return Core::Unexpected(std::string("4ed775: la malla de la onda tiene más esquinas que un int16"));
            triangle.vertices[corner] = static_cast<std::int16_t>(waved.mesh.vertices.size());
            waved.mesh.vertices.push_back(input.vertices[static_cast<std::size_t>(vertex)]);
            waved.vertices.positions.push_back(point);
        }
    }
    return waved;
}
} // namespace

Core::Expected<void, std::string> DrawItemMeshPass(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   const DrawnObject& object, Core::Duration frameClock,
                                                   std::span<const Models::SkinnedMesh> meshes, std::int16_t mesh,
                                                   std::uint32_t flags, std::optional<Render::TextureSlot> texture,
                                                   glm::vec3* storedColor, std::optional<float> worldClock)
{
    const auto* model = models.Find(Models::ModelId{object.model});
    if (!model)
        return Core::Unexpected(std::string("4ecc30: falta el modelo del pase de ítem"));
    if (mesh >= 0 && static_cast<std::size_t>(mesh) >= model->meshes.size())
        return {}; // 0x4ecc3d..0x4ecc4f: the native one skips a mesh outside the counter.
    constexpr std::uint32_t Supported =
        0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x4000;
    if ((flags & ~Supported) != 0)
        return Core::Unexpected(std::string("4ecc30: banderas del pase de ítem fuera del contrato portable"));
    constexpr std::uint32_t Wave = 0x400;
    const bool waving = (flags & 0x20) == 0 && (flags & Wave) != 0;
    if (waving && !worldClock)
        return Core::Unexpected(std::string("4ed79d: la onda de 0x4ecc30 sin el reloj del mundo (0x5767b88)"));
    const auto slots = models.MeshTextures(Models::ModelId{object.model});
    const auto traits = models.MeshTraitsOf(Models::ModelId{object.model});
    const auto shine = ItemShine(flags);
    auto currentColor = object.color;
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        if (mesh >= 0 && index != static_cast<std::size_t>(mesh))
            continue;
        const auto& input = model->meshes[index];
        const auto role = TraitsOf(traits, index);
        if (mesh < 0 && (role.hidden || static_cast<std::int16_t>(index) == object.state.hiddenMesh))
            continue;
        // 0x4ecc5b: without triangles it leaves before everything, without touching the model's color.
        if (input.triangles.empty())
            continue;
        if (auto valid = ValidateItemMesh(input, meshes, index, slots.size(), texture.has_value()); !valid.HasValue())
            return valid;
        const auto assigned = TextureOf(input, slots, index, object.skin);
        if (!assigned.drawn)
            continue;
        Models::SkinnedMesh flattened;
        const Models::SkinnedMesh* vertices = &meshes[index];
        const Data::Models::Mesh* corners = &input;
        WavedMesh waved;
        if ((flags & 0x20) != 0)
        {
            flattened = FlattenedItemMesh(meshes[index], object.transform.position);
            vertices = &flattened;
        }
        else if (waving)
        {
            auto made = WavedItemMesh(input, meshes[index], *worldClock);
            if (!made.HasValue())
                return Core::Unexpected(made.Error());
            waved = std::move(made.Value());
            vertices = &waved.vertices;
            corners = &waved.mesh;
        }
        const auto draw = RetainedMeshDraw(object, frameClock, *corners, *vertices, index, role, assigned.slot, true);
        DrawItemPassMesh(batch, draw, flags, shine, texture, currentColor, storedColor);
    }
    return {};
}

namespace
{
// 0x60b4aa..0x60b6d5 and the three tables 0x60ccfc/0x60cd1c/0x60cd30.
int ItemLevel(std::uint16_t model, int level)
{
    if ((model >= 0x1a12 && model <= 0x1a18) || model == 0x1a1d || model == 0x1c30 || model == 0x223e ||
        model == 0x2241 || model == 0x2242 || model == 0x2246 || model == 0x2247 || model == 0x224a || model == 0x1e21)
        return 0;
    if (model == 0x1a19 || (model >= 0x1a1e && model <= 0x1a20) || (model >= 0x1a22 && model <= 0x1a25) ||
        model == 0x1e26 || model == 0x223f)
        return 9;
    if (model == 0x1a21 || model == 0x1a30 || model == 0x1a31 || model == 0x1e1f || model == 0x1e20 ||
        model == 0x1e22 || model == 0x1e28 || model == 0x1c15 || model == 0x1c1c || model == 0x1e2b ||
        model == 0x1e2c || model == 0x1e31 || model == 0x223a || model == 0x223b || model == 0x2243 || model == 0x2249)
        return 8;
    if (model == 0x1e23 || model == 0x1e24 || model == 0x1e25)
        return level * 2;
    if (model == 0x2244)
        return level * 2 - 15;
    if (model == 0x2245)
        return level - 1;
    if (model == 0x2248)
        return level + 7;
    if (model == 0x2240)
        return level == 13 ? 13 : 9;
    if (model == 0xa19 || model == 0xa21)
        return level == 0 ? 0 : level * 2 + 1;
    return level;
}

void PublishItemStore(const DrawnObject& object, const ItemDrawContext& context)
{
    if (context.special && context.special->publishedObject)
        *context.special->publishedObject = object;
    if (context.modelState && object.unlitMesh)
        context.modelState->unlitMeshes[static_cast<std::int16_t>(object.model)] =
            static_cast<std::int8_t>(*object.unlitMesh);
}

Core::Expected<Core::Duration, std::string> ItemFrameClock(Core::Duration clock, const ItemDrawContext& context)
{
    if (!context.special || !context.special->worldClock)
        return clock;
    const long double nanos = static_cast<long double>(*context.special->worldClock) * 1000000;
    if (!std::isfinite(nanos) || nanos >= 0x1p63L || nanos < -0x1p63L)
        return Core::Unexpected(std::string("60b3d0: reloj fuera del perfil portable"));
    return Core::Duration(static_cast<std::int64_t>(nanos));
}

Core::Expected<void, std::string> DrawItemOptionPasses(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       const DrawnObject& base, Core::Duration frameClock,
                                                       std::span<const Models::SkinnedMesh> meshes,
                                                       const Models::ItemGlow& glow, const ItemDrawContext& context)
{
    const auto draws = RetainedObjectMeshDraws(models, base, frameClock, meshes, false, false, true);
    // 0x60cbff..0x60cc1c: T is the float of the world clock; without it, the frame clock. The alpha is the pulse in a
    // float (FST) and the rand() is decided by the x87's pulse, unrounded (FCOMP).
    const double milliseconds = context.special && context.special->worldClock
                                    ? static_cast<double>(*context.special->worldClock)
                                    : std::chrono::duration<double, std::milli>(frameClock).count();
    const double pulse = Models::SetPulseAt(milliseconds);
    SetFade fade{static_cast<float>(pulse)};
    for (const auto& pass : glow.passes)
    {
        if (pass.shine == Models::ShinePass::Chrome3 && Models::SetPulseIsOff(pulse) && context.originalFrame)
        {
            if (!context.special || !context.special->random)
                return Core::Unexpected(std::string("0x60cc29: falta el CRT del brillo ancient"));
            const auto selected = context.special->random->Next() % 100;
            if (!context.selections)
                return Core::Unexpected(std::string("0x60cc36: falta el estado por modelo del brillo ancient"));
            (*context.selections)[base.model] = selected;
        }
        for (const auto& draw : draws)
        {
            if (pass.shine)
                DrawShinePass(batch, draw, *pass.shine, fade);
            else
                DrawExcellentPass(batch, draw);
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawItemFromMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                     const DrawnObject& object, Core::Duration frameClock,
                                                     std::span<const Models::SkinnedMesh> meshes,
                                                     std::uint8_t packedLevel, std::uint8_t excellent,
                                                     std::uint8_t ancient, const ItemDrawContext& context)
{
    const std::uint8_t detail = context.glowDetail;
    const auto clock = ItemFrameClock(frameClock, context);
    if (!clock.HasValue())
        return Core::Unexpected(clock.Error());
    frameClock = clock.Value();
    auto level = ItemLevel(object.model, packedLevel);
    if (detail < Models::DefaultGlowDetail)
        level = std::min(level, 2 * static_cast<int>(detail) + 5);
    if (object.model >= 0x1e12 && object.model <= 0x1e18 && level > 0)
        level = 7;
    const bool hasExcellent =
        (excellent & 0x3f) != 0 && !(object.model >= 0x1a12 && object.model <= 0x1a18) && object.model != 0x1c30;
    const bool hasAncient = (ancient & 3) == 1 || (ancient & 3) == 2;
    const auto glow = Models::ItemGlowOf(static_cast<std::uint8_t>(std::max(0, level)), hasExcellent && detail != 0,
                                         hasAncient && detail != 0, Models::DefaultGlowDetail);
    auto base = object;
    if (context.modelState)
    {
        const auto selected = context.modelState->unlitMeshes.find(static_cast<std::int16_t>(base.model));
        if (selected != context.modelState->unlitMeshes.end())
            base.unlitMesh = selected->second;
    }
    if (!base.vertexLight)
        base.color *= 0.1f; // 0x5ffce0 before the early RETs.
    const auto early = DrawEarlyItemGlow(batch, models, base, frameClock, meshes, level, packedLevel, context);
    if (!early.HasValue() || early.Value())
    {
        PublishItemStore(base, context);
        if (!early.HasValue())
            return Core::Unexpected(early.Error());
        return {};
    }
    base.color = object.color;
    std::optional<float> storedAlpha;
    const auto prepared =
        PrepareItemGlowEffects(base, frameClock, static_cast<std::uint8_t>(std::max(0, level)), context, storedAlpha);
    if (storedAlpha && context.storedAlpha)
        *context.storedAlpha = *storedAlpha;
    if (!prepared.HasValue())
        return prepared;
    base.color *= glow.baseFactor;
    if (level >= 3 && level <= 4)
        base.color *= glm::vec3(context.glowPulse, context.glowPulse * 0.6f, context.glowPulse * 0.6f);
    else if (level >= 5 && level <= 6)
        base.color *= glm::vec3(context.glowPulse * 0.5f, context.glowPulse * 0.7f, context.glowPulse);
    auto special = context.special ? *context.special : ItemSpecialDrawContext{};
    special.publishedObject = &base;
    if (object.model == ChromeFirstObject)
        for (const auto& draw : RetainedObjectMeshDraws(models, base, frameClock, meshes, false, false, true))
            DrawChromeFirst(batch, draw);
    const auto rendered = DrawSpecialItemMeshes(batch, models, base, frameClock, meshes, special);
    if (storedAlpha)
        base.alpha = *storedAlpha;
    PublishItemStore(base, context);
    if (!rendered.HasValue())
        return rendered;
    return DrawItemOptionPasses(batch, models, base, frameClock, meshes, glow, context);
}

Core::Expected<void, std::string> DrawSkeletonBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   const DrawnCharacter& character, Core::Duration frameClock,
                                                   std::uint16_t model, std::uint8_t level,
                                                   const Models::ObjectState& state, bool vertexLight,
                                                   const ItemDrawContext& context)
{
    // 0x60cf99: with the alpha at 0.01 or below (or NaN), nothing.
    if (!(character.alpha > PartBoxAlphaThreshold))
        return {};
    Models::SkinningBuffers own;
    Models::SkinningBuffers& buffers = BuffersOf(character, own);
    const std::span<const BoneTransform> body = PoseCharacterBodyInto(buffers.bones, models, character);
    if (body.empty())
        return {};
    if (character.onPose)
        character.onPose(Tables::PlayerSkeletonModel, body, character.object, false);
    const Data::Models::Model* found = models.Find(Models::ModelId{model});
    if (found == nullptr)
    {
        // The zeroed record of 0x60dbe0: no meshes to draw; 0x4ebc80 publishes the picking box anyway.
        if (character.onPartBox)
            character.onPartBox(character.object.position);
        return {};
    }
    // Nobody writes the skeleton's skin (+0x98 of the model): 0x60dbe0 leaves it at 0, like the Angel's.
    constexpr std::uint8_t SkeletonBodySkin = 0;
    if (character.outlined)
    {
        // 0x60d04d: the outline, with the object's hidden mesh +0x58 (0x60ce68), before 0x5ffce0 and 0x60b3d0.
        std::minstd_rand random(RandomSeedOf(frameClock));
        const CharacterFrame frame{batch, models, character, frameClock, random, buffers};
        std::vector<MeshDraw> draws =
            MeshDrawsOf(models, model, *found, body, character.object, SkeletonBodySkin,
                        Render::Color{character.light.x, character.light.y, character.light.z, 1.0f}, state, frameClock,
                        buffers.meshes);
        for (MeshDraw& draw : draws)
            draw.alpha = character.alpha;
        DrawOutline(frame, draws, body);
    }
    if (character.onPartBox)
        character.onPartBox(character.object.position);
    const std::span<Models::SkinnedMesh> meshes = buffers.posedMeshes.Reset(found->meshes.size());
    for (std::size_t index = 0; index < found->meshes.size(); ++index)
    {
        if (!IndicesInRange(found->meshes[index]) ||
            !Models::SkinMesh(found->meshes[index], body, character.object, meshes[index]))
            return Core::Unexpected(
                std::format("0x60cf70: una malla del cuerpo del esqueleto {:#x} no se puede pasar a "
                            "los huesos del cuerpo",
                            model));
    }
    // 0x60cffa..0x60d003: the body without a hidden mesh, with +0x44 of the model times +0xe8 of the object.
    DrawnObject object;
    object.model = model;
    object.animation = character.body;
    object.transform = character.object;
    object.state = state;
    object.state.hiddenMesh = Models::NoHiddenMesh;
    object.color = character.light;
    object.alpha = character.alpha;
    object.vertexLight = vertexLight;
    // 0x60d42a..0x60d44b: 0x60b3d0 with the level, without excellent or set, and the last argument at 2.
    constexpr std::uint32_t SkeletonBodyFlags = 2;
    ItemDrawContext resources = context;
    ItemSpecialDrawContext special = context.special != nullptr ? *context.special : ItemSpecialDrawContext{};
    special.flags = SkeletonBodyFlags;
    special.publishedObject = &object;
    resources.special = &special;
    return DrawItemFromMeshes(batch, models, object, frameClock, meshes, level, 0, 0, resources);
}

void DrawSubtractiveMesh(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                         Core::Duration frameClock)
{
    if (object.state.blendMesh != Models::EveryMeshBlends)
    {
        return;
    }
    DrawnObject white = object;
    white.color = glm::vec3(1.0f);
    white.alpha = 1.0f;
    std::vector<Models::SkinnedMesh> skinned;
    for (const MeshDraw& draw : ObjectMeshDraws(models, white, frameClock, skinned))
    {
        if (draw.index != 0)
        {
            continue;
        }
        Triangles(batch, draw, {draw.texture, Render::ModelBlend::Subtractive, true},
                  Render::Color{1.0f, 1.0f, 1.0f, 1.0f}, PassUv{std::nullopt, object.state.uv});
    }
}

void DrawWaterspout(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock)
{
    // 0x600a62..0x600abd: meshes 0 to 2, with +0x64, +0x68 and U = V = 0.
    ObjectState plain = object.state;
    plain.uv = glm::vec2{};
    plain.hiddenMesh = Models::NoHiddenMesh;
    // 0x600ac2..0x600adf: mesh 3, with the blendMesh fixed at 3, +0x68, +0x6c and +0x70.
    ObjectState last = object.state;
    last.blendMesh = WaterspoutLastMesh;
    last.hiddenMesh = Models::NoHiddenMesh;
    std::vector<Models::SkinnedMesh> skinned;
    for (MeshDraw draw : ObjectMeshDraws(models, object, frameClock, skinned))
    {
        const auto index = static_cast<std::int16_t>(draw.index);
        if (index > WaterspoutLastMesh)
        {
            continue;
        }
        draw.object = index == WaterspoutLastMesh ? &last : &plain;
        draw.blends = Blends(*draw.mesh, draw.object->blendMesh);
        draw.scrolls = index == draw.object->blendMesh || draw.role.scrollScript;
        DrawBase(batch, draw);
    }
}

} // namespace Client::Actors::Drawing

namespace Client::Actors::Drawing
{

void DrawTarkanChrome(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                      Core::Duration frameClock)
{
    ObjectState state = object.state;
    state.hiddenMesh = Models::NoHiddenMesh;
    std::vector<Models::SkinnedMesh> skinned;
    for (MeshDraw draw : ObjectMeshDraws(models, object, frameClock, skinned))
    {
        if (draw.index != 0)
        {
            continue;
        }
        draw.object = &state;
        DrawBase(batch, draw);
        if (!draw.role.shines)
        {
            return;
        }
        Render::Color chromeColor = draw.shade;
        if (draw.alpha < OpaqueAlpha)
        {
            chromeColor = Scaled(chromeColor, draw.alpha);
        }
        Triangles(batch, draw, {PassSlot(Models::ShinePass::Chrome), Render::ModelBlend::Additive, true}, chromeColor,
                  PassUv{Models::ShinePass::Chrome, glm::vec2{}});
        return;
    }
}

Core::Expected<void, std::string> DrawAidaMeshPass(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   const DrawnObject& object, std::int16_t type, float worldClock,
                                                   Core::Duration frameClock)
{
    constexpr std::int16_t GlowingType = 0x29;
    const bool glowing = type == GlowingType;
    if (!glowing && type != 0x41 && type != 0x42)
        return {};
    if (models.Find(Models::ModelId{object.model}) == nullptr)
        return {};
    constexpr int Period = 100000;
    constexpr std::uint32_t GlowFlags = 0x42, BaseFlags = 0x2;
    constexpr double GlowStep = static_cast<double>(std::bit_cast<float>(0x3727c5acu));
    constexpr double BaseStep = static_cast<double>(std::bit_cast<float>(0x3851b717u));
    constexpr float GlowColor = std::bit_cast<float>(0x3f051eb8u);
    // 0x425623..0x425645: ftol of the clock, neg, cdq and idiv by 100,000 (the remainder carries the dividend's sign).
    const std::int32_t clock = Math::FtolLow(static_cast<double>(worldClock));
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(clock));
    const auto remainder = static_cast<double>(negated % Period);
    DrawnObject pass = object;
    pass.unlitMesh = std::int16_t{0};
    pass.state.uv.x = static_cast<float>(remainder * (glowing ? GlowStep : BaseStep));
    if (glowing)
        pass.color = glm::vec3(GlowColor);
    std::vector<Models::SkinnedMesh> skinned;
    std::span<const Models::SkinnedMesh> meshes = object.retainedMeshes;
    if (meshes.empty())
    {
        static_cast<void>(ObjectMeshDraws(models, pass, frameClock, skinned));
        meshes = skinned;
    }
    // Without the pose (the alpha below 0.01 or without a skeleton), 0x5ffdf0 left no vertices to repeat.
    if (meshes.empty())
        return {};
    return DrawItemMeshPass(batch, models, pass, frameClock, meshes, 0, glowing ? GlowFlags : BaseFlags);
}

void DrawDarkHorse(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& horse,
                   std::int32_t map, Core::Duration frameClock)
{
    DrawnObject white = horse;
    white.color = glm::vec3(1.0f);
    std::vector<Models::SkinnedMesh> skinned;
    const std::vector<MeshDraw> draws = ObjectMeshDraws(models, white, frameClock, skinned);
    for (const MeshDraw& draw : draws)
    {
        DrawBase(batch, draw);
    }
    // 0x4ecc30(12..15, 0x44, …) directly, without RenderBody: the additive chrome of each one that exists (the stock
    // models: only 12, Q-C113), without the U and the V (at 0) and, with the alpha below 0.99, with the color each one
    // multiplies in place by it (Q-C95). Except with N in the script. The meshes come from MeshDrawsOf, which leaves
    // out those of H and the broken ones; the stock ones have no script.
    Render::Color chromeColor = DarkHorseChromeColor;
    for (const MeshDraw& chrome : draws)
    {
        if (chrome.index < DarkHorseFirstChromeMesh || chrome.index > DarkHorseLastChromeMesh || !chrome.role.shines)
        {
            continue;
        }
        if (chrome.alpha < OpaqueAlpha)
        {
            chromeColor = Scaled(chromeColor, chrome.alpha);
        }
        Triangles(batch, chrome, {PassSlot(Models::ShinePass::Chrome), Render::ModelBlend::Additive, true}, chromeColor,
                  PassUv{Models::ShinePass::Chrome, glm::vec2{}});
    }
    // 0x60073c..0x60075b: on maps 10, 24 to 29 and 36 there is no shadow.
    const bool kalima =
        (map >= DarkHorseFirstKalimaMap && map <= DarkHorseLastKalimaMap) || map == DarkHorseSeventhKalimaMap;
    if (map == DarkHorseNoShadowMap || kalima)
    {
        return;
    }
    Render::Color shadowColor = DarkHorseShadowColor;
    if (map == DarkHorseFaintShadowMap)
    {
        shadowColor.alpha = DarkHorseShadowAlphaOfMapSeven;
    }
    // 0x4eff40(−1, −1, 8, 9): mesh 8. It comes from MeshDrawsOf, like the chrome.
    if (const auto shadow = std::ranges::find(draws, DarkHorseShadowMesh, &MeshDraw::index); shadow != draws.end())
    {
        FlatTriangles(batch, *shadow, BlendedShadowState, shadowColor, horse.transform.position, ShadowSpreadOf(map));
    }
}

void DrawDarkSpirit(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& spirit,
                    float groundHeight, Core::Duration frameClock, std::int32_t map)
{
    std::vector<Models::SkinnedMesh> skinned;
    const std::vector<MeshDraw> draws = ObjectMeshDraws(models, spirit, frameClock, skinned);
    for (const std::size_t mesh : DarkSpiritMeshes)
    {
        if (const auto draw = std::ranges::find(draws, mesh, &MeshDraw::index); draw != draws.end())
        {
            DrawBase(batch, *draw);
        }
    }
    // 0x6009a9..0x6009c3: on maps 10, 24 to 29 and 36 there is no shadow; 0x6009d8: on 7, with alpha 0.2.
    const bool kalima =
        (map >= DarkHorseFirstKalimaMap && map <= DarkHorseLastKalimaMap) || map == DarkHorseSeventhKalimaMap;
    if (map == DarkHorseNoShadowMap || kalima)
        return;
    Render::Color shadowColor = DarkSpiritShadowColor;
    if (map == DarkHorseFaintShadowMap)
        shadowColor.alpha = DarkHorseShadowAlphaOfMapSeven;
    // 0x600a0b: 0x4eff40 flattens from (x, y, the terrain height there), which it writes to +0x6c..+0x74, with the
    // map's spread (0x4eff8d: 2500 on 30).
    const glm::vec3 ground(spirit.transform.position.x, spirit.transform.position.y, groundHeight);
    const float spread = ShadowSpreadOf(map);
    for (const MeshDraw& draw : draws)
        FlatTriangles(batch, draw, BlendedShadowState, shadowColor, ground, spread);
}

void DrawBloodCastleShadow(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                           float groundHeight, Core::Duration frameClock)
{
    // 0x4eff40(+0x64, 2, −1, −1): without mesh 2 or those with the blendMesh's texture (+2 of the mesh).
    constexpr std::size_t SkippedMesh = 2;
    std::vector<Models::SkinnedMesh> skinned;
    const std::vector<MeshDraw> draws = ObjectMeshDraws(models, object, frameClock, skinned);
    const glm::vec3 ground(object.transform.position.x, object.transform.position.y, groundHeight);
    for (const MeshDraw& draw : draws)
    {
        if (draw.index == SkippedMesh || draw.mesh->texture == object.state.blendMesh || draw.mesh->triangles.empty())
            continue;
        FlatTriangles(batch, draw, BlendedShadowState, DarkSpiritShadowColor, ground, DefaultShadowSpread);
    }
}

std::span<const Models::SkinnedMesh> PosedMeshesOf(const Models::IModelLibrary& models, const DrawnObject& object,
                                                   std::vector<Models::SkinnedMesh>& skinned)
{
    if (!object.retainedMeshes.empty())
        return object.retainedMeshes;
    skinned = WorldScratchOf(models, object);
    return skinned;
}

namespace
{
// 0x42d019..0x42d038: 0x620980, glColor3f(0, 0, 0) and 0x4eff40(+0x64, +0x58, −1, −1). The shadow skips the +0x58 mesh,
// those without triangles and those with the blendMesh's texture (+2 of the mesh), and flattens each vertex from
// model+0x6c..+0x74, which 0x5ffdf0 left at the object's position, with map 30's spread.
void DrawCastleSiegeShadow(Render::ModelBatch& batch, const Data::Models::Model& model, const DrawnObject& object,
                           std::span<const Models::SkinnedMesh> meshes)
{
    const float spread = ShadowSpreadOf(CastleSiegeMap);
    for (std::size_t index = 0; index < model.meshes.size() && index < meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model.meshes[index];
        if (static_cast<std::int16_t>(index) == object.state.hiddenMesh || mesh.triangles.empty() ||
            mesh.texture == object.state.blendMesh || !IndicesInRange(mesh) ||
            meshes[index].positions.size() < mesh.vertices.size())
        {
            continue;
        }
        MeshDraw draw;
        draw.mesh = &mesh;
        draw.skinned = &meshes[index];
        FlatTriangles(batch, draw, ShadowState, ShadowColor, object.transform.position, spread);
    }
}

// (ftol(T) % 10,000) · 0.0002f (0x6b8a64): cdq and idiv leave the remainder with the dividend's sign, fild loads it
// exactly, the product rounds to double and the fstp to float.
float CastleSiegeScroll(std::int32_t clock)
{
    constexpr std::int32_t Period = 10000;
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3951b717u));
    return static_cast<float>(static_cast<double>(clock % Period) * Step);
}
} // namespace

Core::Expected<void, std::string> DrawChromeShell(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                  const DrawnObject& object, Core::Duration frameClock)
{
    // 0x6003d1..0x600401: 0x4ecc30(mesh, 0x4040, 0.5f (0x3f000000), +0x64, +0x68, +0x6c · 8f (0x6b86c4), +0x70 · 2,
    // 0x620), with the fstp of the U and the V.
    constexpr std::uint32_t ShellFlags = 0x4040;
    constexpr float ShellAlpha = 0.5f;
    constexpr double ShellUScale = 8.0;
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return {};
    // 0x60039d..0x6003a8: the model's color at 1. The pass's alpha is the 0.5, not the object's +0x174, and 0x4000 does
    // not look at the per-vertex light.
    DrawnObject shell = object;
    shell.color = glm::vec3(1.0f);
    shell.alpha = ShellAlpha;
    shell.vertexLight = false;
    shell.state.uv = {static_cast<float>(static_cast<double>(object.state.uv.x) * ShellUScale),
                      static_cast<float>(static_cast<double>(object.state.uv.y) + object.state.uv.y)};
    std::vector<Models::SkinnedMesh> skinned;
    const auto meshes = PosedMeshesOf(models, shell, skinned);
    if (meshes.empty())
        return {};
    // 0x600406..0x600423: from 0 to the model's +0x24; each 0x4ecc30 multiplies the color in place (Q-C95).
    for (std::size_t mesh = 0; mesh < model->meshes.size(); ++mesh)
    {
        if (auto drawn = DrawItemMeshPass(batch, models, shell, frameClock, meshes, static_cast<std::int16_t>(mesh),
                                          ShellFlags, MetalTextureSlot, &shell.color);
            !drawn.HasValue())
            return drawn;
    }
    return {};
}

Core::Expected<void, std::string> DrawFenrirBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                 const DrawnObject& fenrir, std::int32_t map, bool charging,
                                                 Core::Duration frameClock)
{
    constexpr std::uint32_t BaseFlags = 0x2, ChargeFlags = 0x46;
    constexpr Render::TextureSlot ChargeTexture{0x68f}; // Effect\eff_lighting.jpg.
    const Data::Models::Model* model = models.Find(Models::ModelId{fenrir.model});
    if (model == nullptr)
        return {};
    // 0x60279a..0x6027a3: the model's color at 1 and +0x88 at 0, without the per-vertex light.
    DrawnObject white = fenrir;
    white.color = glm::vec3(1.0f);
    white.vertexLight = false;
    std::vector<Models::SkinnedMesh> skinned;
    const auto meshes = PosedMeshesOf(models, white, skinned);
    if (meshes.empty())
        return {};
    for (const std::int16_t mesh : {std::int16_t{0}, std::int16_t{1}})
        if (auto drawn = DrawItemMeshPass(batch, models, white, frameClock, meshes, mesh, BaseFlags); !drawn.HasValue())
            return drawn;
    if (charging)
        if (auto drawn = DrawItemMeshPass(batch, models, white, frameClock, meshes, 1, ChargeFlags, ChargeTexture);
            !drawn.HasValue())
            return drawn;
    // 0x6028dc..0x6028f3: on maps 10, 24 to 29 and 36 there is no shadow; 0x602904: on 7, with alpha 0.2.
    const bool kalima =
        (map >= DarkHorseFirstKalimaMap && map <= DarkHorseLastKalimaMap) || map == DarkHorseSeventhKalimaMap;
    if (map == DarkHorseNoShadowMap || kalima)
        return {};
    Render::Color shadowColor = DarkHorseShadowColor;
    if (map == DarkHorseFaintShadowMap)
        shadowColor.alpha = DarkHorseShadowAlphaOfMapSeven;
    // 0x60292b: 0x4eff40(−1, −1, −1, −1), without blendMesh or hidden mesh: every mesh with triangles, flattened from
    // model+0x6c..+0x74, which 0x5ffdf0 left at the Fenrir's place, with the map's spread.
    const float spread = ShadowSpreadOf(map);
    for (std::size_t index = 0; index < model->meshes.size() && index < meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model->meshes[index];
        if (mesh.triangles.empty() || !IndicesInRange(mesh) || meshes[index].positions.size() < mesh.vertices.size())
            continue;
        MeshDraw draw;
        draw.mesh = &mesh;
        draw.skinned = &meshes[index];
        FlatTriangles(batch, draw, BlendedShadowState, shadowColor, fenrir.transform.position, spread);
    }
    return {};
}

Core::Expected<bool, std::string> DrawCastleSiegeBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      const DrawnObject& object, const CastleSiegeBodyPass& pass,
                                                      Core::Duration frameClock)
{
    constexpr std::int16_t Banner = 0xc, Scrolled = 0x11, Glowing = 0x13, Flowing = 0x33, Chromed = 0x42;
    constexpr std::int16_t FirstGate = 0x3d, LastGate = 0x40;
    const std::int16_t type = pass.type;
    const bool shadowed = type == 0x32 || type == 0x53 || type == 0x54 || type == 0x55;
    const bool gate = type >= FirstGate && type <= LastGate;
    if (type != Banner && type != Scrolled && type != Glowing && type != Flowing && type != Chromed && !shadowed &&
        !gate)
    {
        // 0x42cfd6: 0x42e3d0 only has cases for monster types (0x19b, 0x19c, 0x1a8, 0x1f8 and 0x1ff).
        return false;
    }
    const Data::Models::Model* model = models.Find(Models::ModelId{object.model});
    if (model == nullptr)
        return true;
    constexpr std::uint32_t BaseFlags = 0x2, AdditiveFlags = 0x42, ChromeFlags = 0x44, ShinyFlags = 0x48;
    std::vector<Models::SkinnedMesh> skinned;
    if (gate && pass.field04 != 0)
    {
        if (model->meshes.empty())
            return true;
        if (!pass.emitGateVertices)
            return Core::Unexpected(std::string("0x42cfcb: falta el emisor de vértices12f de la puerta"));
        const auto meshes = PosedMeshesOf(models, object, skinned);
        if (auto emitted = pass.emitGateVertices(object, meshes); !emitted.HasValue())
            return Core::Unexpected(emitted.Error());
        return true;
    }
    if (type == Banner)
    {
        // 0x42cc32..0x42cc89: without RenderBody or 0x4ecc10; the model's color at 1.
        constexpr Render::TextureSlot FlagTexture{0x69c};
        DrawnObject banner = object;
        banner.color = glm::vec3(1.0f);
        const auto meshes = PosedMeshesOf(models, banner, skinned);
        if (meshes.empty())
            return true;
        if (auto first = DrawItemMeshPass(batch, models, banner, frameClock, meshes, 0, BaseFlags); !first.HasValue())
            return Core::Unexpected(first.Error());
        if (auto flag = DrawItemMeshPass(batch, models, banner, frameClock, meshes, 1, BaseFlags, FlagTexture);
            !flag.HasValue())
            return Core::Unexpected(flag.Error());
        return true;
    }
    if (type == Scrolled)
    {
        // 0x42cc9a..0x42ccf7: mesh 0 without light and with the U and the V scrolled, at 0.45 (0x3ee66666).
        DrawnObject scrolled = object;
        scrolled.color = glm::vec3(std::bit_cast<float>(0x3ee66666u));
        scrolled.unlitMesh = std::int16_t{0};
        scrolled.state.uv.y = CastleSiegeScroll(Math::FtolLow(static_cast<double>(pass.worldClock)));
        DrawObject(batch, models, scrolled, frameClock);
        return true;
    }
    if (type == Flowing)
    {
        // 0x42cd0a..0x42cd5b: neg before the cdq; the color, the one 0x5ffce0 left.
        DrawnObject flowing = object;
        flowing.unlitMesh = std::int16_t{0};
        const std::int32_t clock = Math::FtolLow(static_cast<double>(pass.worldClock));
        flowing.state.uv.x = CastleSiegeScroll(static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(clock)));
        const auto meshes = PosedMeshesOf(models, flowing, skinned);
        if (meshes.empty())
            return true;
        if (auto drawn = DrawItemMeshPass(batch, models, flowing, frameClock, meshes, -1, AdditiveFlags);
            !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return true;
    }
    if (type == Chromed)
    {
        // 0x42cd6e..0x42cdbf: the clock times 0.001f (0x6b89c8), FSIN, times 0.2f (0x6b8854) and plus 0.3f (0x6b89dc)
        // on the x87 up to the fstp of the light.
        constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
        constexpr float Swing = std::bit_cast<float>(0x3e4ccccdu);
        constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3e99999au));
        const auto pulse = Math::Detail::SineProductPC53(static_cast<double>(pass.worldClock) * Rate, Swing);
        if (!pulse)
            return Core::Unexpected(std::string("0x42cd9f: la luz del 0x42 del 30 no cabe en el perfil PC53"));
        DrawnObject chromed = object;
        chromed.color = glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3ecccccdu),
                                  std::bit_cast<float>(0x3f19999au));
        chromed.state.light = static_cast<float>(*pulse + Base);
        const auto meshes = PosedMeshesOf(models, chromed, skinned);
        if (meshes.empty())
            return true;
        if (auto drawn = DrawItemMeshPass(batch, models, chromed, frameClock, meshes, -1, ChromeFlags);
            !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return true;
    }
    if (type == Glowing && pass.siegeActive)
    {
        // 0x42ce09..0x42cec4: the war one draws only mesh 3; it keeps the object's blendMesh in the three passes.
        DrawnObject glow = object;
        const auto meshes = PosedMeshesOf(models, glow, skinned);
        if (meshes.empty())
            return true;
        if (auto base = DrawItemMeshPass(batch, models, glow, frameClock, meshes, 3, BaseFlags); !base.HasValue())
            return Core::Unexpected(base.Error());
        glow.color = glm::vec3(.3f);
        if (auto chrome = DrawItemMeshPass(batch, models, glow, frameClock, meshes, 3, ChromeFlags, ChromeTextureSlot);
            !chrome.HasValue())
            return Core::Unexpected(chrome.Error());
        glow.color = {.2f, .2f, .5f};
        if (auto shiny = DrawItemMeshPass(batch, models, glow, frameClock, meshes, 3, ShinyFlags, ChromeTextureSlot);
            !shiny.HasValue())
            return Core::Unexpected(shiny.Error());
        return true;
    }
    // RenderBody(2, alpha, blendMesh, light, U, V, +0x58, −1): 0x13 (0x42ced0), the gates (0x42cf8e) and the shadow
    // ones (0x42cfe7).
    DrawObject(batch, models, object, frameClock);
    if (type == Glowing)
    {
        // 0x42cef9..0x42cf62: the two passes with Chrome01 and the blendMesh at 0, at 0.3 and at (0.2, 0.2, 0.5).
        DrawnObject glow = object;
        glow.state.blendMesh = 0;
        glow.color = glm::vec3(std::bit_cast<float>(0x3e99999au));
        const auto meshes = PosedMeshesOf(models, glow, skinned);
        if (meshes.empty())
            return true;
        if (auto chrome = DrawItemMeshPass(batch, models, glow, frameClock, meshes, -1, ChromeFlags, ChromeTextureSlot);
            !chrome.HasValue())
            return Core::Unexpected(chrome.Error());
        constexpr float Dim = std::bit_cast<float>(0x3e4ccccdu);
        glow.color = glm::vec3(Dim, Dim, 0.5f);
        if (auto shiny = DrawItemMeshPass(batch, models, glow, frameClock, meshes, -1, ShinyFlags, ChromeTextureSlot);
            !shiny.HasValue())
            return Core::Unexpected(shiny.Error());
        return true;
    }
    if (shadowed && !pass.siegeActive)
    {
        const auto meshes = PosedMeshesOf(models, object, skinned);
        DrawCastleSiegeShadow(batch, *model, object, meshes);
    }
    return true;
}

std::vector<Math::OriginalMatrix> PosedWorldBones(const Models::IModelLibrary& models, const DrawnObject& object)
{
    const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{object.model});
    if (skeleton == nullptr)
    {
        return {};
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
    {
        return {};
    }
    constexpr std::size_t Rows = 3;
    constexpr std::size_t Columns = 4;
    std::vector<Math::OriginalMatrix> world;
    world.reserve(bones.size());
    for (const BoneTransform& bone : bones)
    {
        Math::OriginalMatrix matrix = Math::OriginalMatrixOf(bone);
        for (std::size_t row = 0; row < Rows; ++row)
        {
            for (std::size_t column = 0; column + 1 < Columns; ++column)
            {
                matrix[(row * Columns) + column] *= object.transform.scale;
            }
            float& translation = matrix[(row * Columns) + Columns - 1];
            translation = (translation * object.transform.scale) + object.transform.position[static_cast<int>(row)];
        }
        world.push_back(matrix);
    }
    return world;
}

std::optional<glm::vec3> PosedBonePoint(const Models::IModelLibrary& models, const DrawnObject& object,
                                        std::size_t bone, const glm::vec3& offset)
{
    const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{object.model});
    // 0x4eb973: the pose skips the dummies without writing them; the global buffer keeps the earlier one.
    if (skeleton == nullptr || bone >= skeleton->bones.size() || skeleton->bones[bone].dummy)
    {
        return std::nullopt;
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
    {
        return std::nullopt;
    }
    return (Math::VectorTransform(offset, bones[bone]) * object.transform.scale) + object.transform.position;
}

std::optional<glm::vec3> LastPosedBonePoint(std::span<const PosedInTheBuffer> posed, std::size_t bone,
                                            const glm::vec3& offset)
{
    for (auto it = posed.rbegin(); it != posed.rend(); ++it)
    {
        if (it->models == nullptr)
        {
            continue;
        }
        if (const std::optional<glm::vec3> point = PosedBonePoint(*it->models, it->object, bone, offset))
        {
            return point;
        }
    }
    return std::nullopt;
}
} // namespace Client::Actors::Drawing
