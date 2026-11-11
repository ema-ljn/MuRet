#include "Scenes/CharacterDrawing.h"

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ItemGlow.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/PassColors.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Shine.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameItemGlowEffects.h"
#include "Scenes/GameItemGlowSpecialDrawing.h"
#include "World/ObjectModelStepState.h"
#include "World/ScepterGlow.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;
using Render::Models::ObjectState;

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

// La clase 4 en el lazo de las partes de 0x50ba30: (+0x1d4 & 7) en 4.
constexpr std::uint8_t MaskedClass = 4;
constexpr std::uint8_t ClassMask = 7;

bool IsMaskedClass(std::uint8_t characterClass)
{
    return (characterClass & ClassMask) == MaskedClass;
}

// El modelo con que 0x50ba30 dibuja la parte `part`: el casco de la clase 4, si tiene máscara, con
// ella (el modelo + 0x328b, con la misma piel).
std::uint16_t DrawnPartModel(GameLogic::BodyPart part, std::uint16_t model, std::uint8_t characterClass)
{
    if (part != GameLogic::BodyPart::Helm || !IsMaskedClass(characterClass) ||
        std::ranges::find(Data::Models::HelmsWithMask, model) == Data::Models::HelmsWithMask.end())
    {
        return model;
    }
    return static_cast<std::uint16_t>(model + Data::Models::MaskHelmOffset);
}

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

// El color de un pase de brillo, como lo pone 0x60b3d0 antes de su RenderBody: el 0x48 y el 0x44 con
// el del ítem (0x608e00); el 0x240 y el 0x1040, el base por el factor del ítem (0x6093d0); y el de
// set, el de 0x609560. Ninguno va por la luz del blendMesh: 0x4ecc30 los dibuja antes de esa rama.
Render::Color ShineColorOf(Render::Models::ShinePass pass, std::uint16_t model, Render::Color base)
{
    switch (pass)
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

// Lo que comparten los modelos de un personaje en un frame: el lote, la biblioteca, el personaje, el
// reloj y los rand() de los objetos de lo que cuelga (0x607b60).
struct CharacterFrame
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    const DrawnCharacter& character;
    Core::Duration clock{};
    std::minstd_rand& random;
};

// Lo que dicen de una malla el script de su textura (0x4ef0c0 y 0x4ecc30) y el +0 de 0x60ff50.
struct MeshRole
{
    // H: 0x4ef0c0 no la dibuja en ningún pase.
    bool hidden = false;
    // R: 0x4ef0c0 le pasa a 0x4ecc30 su índice como el blendMesh, en vez del del objeto.
    bool blendScript = false;
    // S: 0x4ecc30 le corre las UV con la U y la V del objeto.
    bool scrollScript = false;
    // Sin N ni el +0: 0x4ecc30 le corre los pases de brillo.
    bool shines = true;
};

MeshRole RoleOf(std::uint16_t model, const Data::Models::Mesh& mesh, std::size_t index)
{
    MeshRole role;
    if (const std::optional<Data::Models::TextureScript> script = Data::Models::TextureScriptOf(mesh.textureName))
    {
        role.hidden = script->hidden;
        role.blendScript = script->blendMesh;
        role.scrollScript = script->scrollUv;
        role.shines = !script->withoutShine;
    }
    const Data::Models::ModelMesh marked{model, static_cast<std::uint16_t>(index)};
    if (std::ranges::find(Data::Models::MeshesWithoutShine, marked) != Data::Models::MeshesWithoutShine.end())
    {
        role.shines = false;
    }
    return role;
}

// El blendMesh que 0x4ef0c0 le pasa a 0x4ecc30 con la malla: su índice con R; si no, el del objeto.
std::int16_t BlendMeshOf(const MeshRole& role, std::size_t index, const ObjectState& object)
{
    return role.blendScript ? static_cast<std::int16_t>(index) : object.blendMesh;
}

// Si 0x4ecc30 pasa la malla por la rama del blendMesh (con su textura, aditiva y por la luz): con un
// blendMesh menor que −1, o con el de su textura.
bool Blends(const Data::Models::Mesh& mesh, std::int16_t blendMesh)
{
    return blendMesh < Render::Models::NoBlendMesh || mesh.texture == blendMesh;
}

Render::Color Scaled(Render::Color color, float factor)
{
    return Render::Color{color.red * factor, color.green * factor, color.blue * factor, color.alpha};
}

// Lo que necesita una malla para ir al lote: su forma en el mundo, su papel, su textura, el color de
// su nivel, si va por la rama del blendMesh y si le corre las UV, y el objeto con que se dibuja.
struct MeshDraw
{
    std::uint16_t model = 0;
    std::size_t index = 0;
    const Data::Models::Mesh* mesh = nullptr;
    const Render::Models::SkinnedMesh* skinned = nullptr;
    MeshRole role;
    std::optional<Render::TextureSlot> texture;
    Render::Color shade;
    bool blends = false;
    // 0x4ecc30 (local_5): la malla del blendMesh por su índice, o la de S.
    bool scrolls = false;
    const ObjectState* object = nullptr;
    Core::Duration frameClock{};
    // El alpha de RenderBody y la luz por vértice (+0x44 del modelo): los de un objeto del mundo; un
    // personaje va en 1 y sin ella.
    float alpha = 1.0f;
    bool vertexLight = false;
    // El largo de cada normal girada: la escala de un objeto de 0x5ffdf0, que la lleva en la raíz
    // (0x4eb860 con Translate) y 0x4ebc80 no normaliza; un personaje, 1 (la escala va en cada vértice).
    float normalScale = 1.0f;
    bool originalClock = false;
};

// Las UV de un pase: las de la malla o, con `shine`, las del pase de brillo con la normal en el
// mundo; las dos más `shift`.
struct PassUv
{
    std::optional<Render::Models::ShinePass> shine;
    glm::vec2 shift{};
};

glm::vec2 CornerUv(const MeshDraw& draw, const Data::Models::Triangle& triangle, std::size_t corner, const PassUv& uv)
{
    if (uv.shine)
    {
        const auto normal = static_cast<std::size_t>(triangle.normals[corner]);
        if (draw.originalClock)
            return ItemShineTexCoords(*uv.shine, draw.skinned->normals[normal] * draw.normalScale, draw.frameClock) +
                   uv.shift;
        return Render::Models::ShineTexCoords(*uv.shine, draw.skinned->normals[normal] * draw.normalScale,
                                              draw.frameClock) +
               uv.shift;
    }
    const Data::Models::TexCoord& texCoord = draw.mesh->texCoords[static_cast<std::size_t>(triangle.texCoords[corner])];
    return glm::vec2{texCoord.u, texCoord.v} + uv.shift;
}

// Los triángulos de la malla con un estado, un color y sus UV; con `normalLight`, el color de cada
// esquina por la luz de su normal (0x4ecc30 con +0x44, en el pase con textura).
void Triangles(Render::ModelBatch& batch, const MeshDraw& draw, const Render::ModelDrawState& state,
               Render::Color color, const PassUv& uv, bool normalLight = false)
{
    batch.SetState(state);
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
                const float light = Render::Models::NormalLight(draw.skinned->normals[normal] * draw.normalScale);
                shaded = Render::Color{color.red * light, color.green * light, color.blue * light, color.alpha};
            }
            corners[corner] =
                Render::ModelVertex{draw.skinned->positions[vertex], CornerUv(draw, triangle, corner, uv), shaded};
        }
        batch.Triangle(corners);
    }
}

// Lo que se le suma a las UV de la malla en un pase con textura: la U y la V del objeto si 0x4ecc30 se
// las corre, o si es la del model+0x88 (`chromeMesh`, el pase del chrome de Spear10).
glm::vec2 TexturedShift(const MeshDraw& draw, bool chromeMesh = false)
{
    return draw.scrolls || chromeMesh ? draw.object->uv : glm::vec2{};
}

bool HasFourComponents(const Data::Models::Mesh& mesh)
{
    return Data::Models::ModelTextureLoaderOf(mesh.textureName) == Data::Textures::TextureLoader::Ozt;
}

// 0x6b8d00: con el alpha por debajo, 0x4ecc30 mezcla y lo manda en el color.
constexpr float OpaqueAlpha = 0.99f;

// El pase base (0x609640 → RenderBody(2) con el objeto), sin la malla que el objeto oculta: la del
// blendMesh aditiva y por la luz, una de cuatro componentes o con el alpha por debajo de 0,99 con la
// mezcla normal (y ese alpha), y si no opaca; con la luz por vértice, por la de cada normal, salvo la
// del blendMesh.
void DrawBase(Render::ModelBatch& batch, const MeshDraw& draw)
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
    const Render::ModelBlend blend =
        fading || HasFourComponents(*draw.mesh) ? Render::ModelBlend::Alpha : Render::ModelBlend::Opaque;
    Render::Color color = draw.shade;
    color.alpha = fading ? draw.alpha : 1.0f;
    Triangles(batch, draw, {draw.texture, blend, true}, color, PassUv{std::nullopt, TexturedShift(draw)},
              draw.vertexLight);
}

// El alpha del pase de set y lo que ya multiplicó: 0x4ecc30 multiplica el color del modelo en el lugar,
// así que cada malla que dibuja el pase va por una vez más que la anterior (SetAlphaAt).
struct SetFade
{
    float alpha = 1.0f;
    float factor = 1.0f;
};

// Un pase de brillo con su textura y sus UV, salvo en una malla sin brillo o en la que su ítem oculta
// en ese pase; el de set, con el alpha que se va multiplicando, y el 0x1040 (el modo 0x1000 de
// 0x4ecc30) con la U y la V del objeto.
void DrawShinePass(Render::ModelBatch& batch, const MeshDraw& draw, Render::Models::ShinePass pass, SetFade& fade)
{
    if (!draw.role.shines || Render::Models::PassHiddenMesh(draw.model, pass, draw.object->hiddenMesh) == draw.index)
    {
        return;
    }
    Render::Color color = ShineColorOf(pass, draw.model, draw.shade);
    if (pass == Render::Models::ShinePass::Chrome3)
    {
        fade.factor *= fade.alpha;
        color = Scaled(color, fade.factor);
    }
    const glm::vec2 shift = pass == Render::Models::ShinePass::Chrome4 ? draw.object->uv : glm::vec2{};
    Triangles(batch, draw, {PassSlot(pass), Render::ModelBlend::Additive, true}, color, PassUv{pass, shift});
}

// El 0x42 del excelente: con su textura y aditivo, con el color que late, por la luz si va por la
// rama del blendMesh.
void DrawExcellentPass(Render::ModelBatch& batch, const MeshDraw& draw)
{
    const Render::Color color = Render::Models::ExcellentColorAt(draw.frameClock);
    Triangles(batch, draw, {draw.texture, Render::ModelBlend::Additive, true},
              draw.blends ? Scaled(color, draw.object->light) : color, PassUv{std::nullopt, TexturedShift(draw)});
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

// Un pase del contorno sobre una malla: sin textura, o la del blendMesh con la suya y por la luz
// (0x4ecc30 con 0x40 pasa el blendMesh antes que lo demás). Una malla de cuatro componentes que no va
// por esa rama no va (0x40 sin 0x2).
void OutlineMesh(const CharacterFrame& frame, const OutlinePass& pass, const MeshDraw& draw,
                 std::span<const BoneTransform> bones)
{
    if (!draw.blends && HasFourComponents(*draw.mesh))
    {
        return;
    }
    Render::Models::SkinnedMesh skinned;
    if (!Render::Models::SkinMeshAroundBones(*draw.mesh, bones, frame.character.object, pass.boneScale, skinned))
    {
        return;
    }
    MeshDraw around = draw;
    around.skinned = &skinned;
    const Render::ModelDrawState state =
        draw.blends ? Render::ModelDrawState{draw.texture, Render::ModelBlend::Additive, true} : OutlineState;
    Triangles(frame.batch, around, state, draw.blends ? Scaled(pass.color, draw.object->light) : pass.color, PassUv{});
}

// Los dos pases del contorno, cada uno sobre todas las mallas del modelo (0x60cd60 → 0x4ef0c0, con el
// objeto del personaje), sin la que el objeto oculta.
void DrawOutline(const CharacterFrame& frame, std::span<const MeshDraw> draws, std::span<const BoneTransform> bones)
{
    for (const OutlinePass& pass : OutlinePasses)
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

// Cómo se dibuja un modelo: su brillo, si lleva el contorno, el tipo del objeto, que 0x60b3d0 mira
// para sus casos por objeto, y lo que el objeto le pasa a RenderBody. En las partes, el objeto es el
// personaje, y el contorno va con la luz de antes de que 0x60b3d0 escriba la suya; en lo que cuelga,
// el que 0x509340 le arma con su modelo.
struct ModelUse
{
    const Render::Models::ItemGlow& glow;
    bool outlined = false;
    std::uint16_t objectType = Data::Models::PlayerSkeletonModel;
    ObjectState object;
    float outlineLight = 1.0f;
    bool hanging = false;
};

// 0x60b3d0 con el objeto de Spear10: antes de los pases, RenderBody(2) con el 0x5c0 forzado, el color
// (0,5; 0,5; 1,5) y el model+0x88 en 0, con que 0x4ecc30 le corre las UV a la malla 0. Opaco (Chrome01
// es de tres componentes), o aditivo y por la luz en la rama del blendMesh.
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

// Los pases de RenderBody sobre las mallas de un modelo, uno detrás de otro como 0x60b3d0: el del
// chrome de Spear10, el base y los de brillo de su nivel, cada uno sobre todas las mallas.
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
    SetFade fade{Render::Models::SetAlphaAt(frame.clock)};
    for (const Render::Models::GlowPass& pass : use.glow.passes)
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

// Las mallas de `modelId` que se dibujan, pasadas al mundo con `bones` y `object` en `skinned` (una
// por malla): cada una con su papel, su textura (la piel y el pelo de `characterClass`), `shade` y
// `state`. Las de H, la oculta (0x12d), las rotas y las que no se pueden pasar al mundo no van.
std::vector<MeshDraw> MeshDrawsOf(const IModelLibrary& models, std::uint16_t modelId, const Data::Models::Model& model,
                                  std::span<const BoneTransform> bones, const Render::Models::ObjectTransform& object,
                                  std::uint8_t characterClass, Render::Color shade, const ObjectState& state,
                                  Core::Duration clock, std::vector<Render::Models::SkinnedMesh>& skinned)
{
    const std::span<const Render::TextureSlot> slots = models.MeshTextures(modelId);
    skinned.assign(model.meshes.size(), Render::Models::SkinnedMesh{});
    std::vector<MeshDraw> draws;
    draws.reserve(model.meshes.size());
    for (std::size_t index = 0; index < model.meshes.size(); ++index)
    {
        const Data::Models::Mesh& mesh = model.meshes[index];
        const MeshRole role = RoleOf(modelId, mesh, index);
        const MeshTexture texture = TextureOf(mesh, slots, index, characterClass);
        if (role.hidden || !texture.drawn || !IndicesInRange(mesh) ||
            !Render::Models::SkinMesh(mesh, bones, object, skinned[index]))
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

// Un modelo con las matrices `bones`, cada malla con su textura y el brillo de su nivel; con el
// contorno, antes el contorno.
void DrawModel(const CharacterFrame& frame, std::uint16_t modelId, std::span<const BoneTransform> bones,
               const ModelUse& use)
{
    const Data::Models::Model* model = frame.models.Find(modelId);
    if (model == nullptr)
    {
        return;
    }
    if (frame.character.onPose)
        frame.character.onPose(modelId, bones, frame.character.object, false);
    const glm::vec3 light =
        use.hanging ? frame.character.hangingLight.value_or(frame.character.light) : frame.character.light;
    const glm::vec3 shade = light * use.glow.baseFactor;
    std::vector<Render::Models::SkinnedMesh> skinned;
    std::vector<MeshDraw> draws = MeshDrawsOf(
        frame.models, modelId, *model, bones, frame.character.object, frame.character.appearance.characterClass,
        Render::Color{shade.x, shade.y, shade.z, 1.0f}, use.object, frame.clock, skinned);
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
    DrawPasses(frame, draws, use);
}

// El lazo de las manos de 0x50ba30 saltea las saetas, las flechas y el cuervo: las dos primeras van
// a la espalda (DrawQuiver), y el cuervo es un objeto aparte, el 0xc0 (en el login lo crea 0x420d60),
// que todavía no se dibuja (CL7.6 C).
constexpr std::uint16_t Bolts = 0xa19;
constexpr std::uint16_t Arrows = 0xa21;
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::array<std::uint16_t, 3> NotInTheHand{Bolts, Arrows, Raven};

bool SkippedInTheHand(std::uint16_t model)
{
    return std::ranges::find(NotInTheHand, model) != NotInTheHand.end();
}

// La sombra de 0x50ba30 (0x20b con las matrices del cuerpo), antes de las partes: cada malla, pasada
// a los vértices del cuerpo y aplastada sobre el piso (0x4eff40). Sin ella con una de las mascotas
// que se montan.
void DrawShadow(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const DrawnCharacter& character = frame.character;
    if (!character.castsShadow)
    {
        return;
    }
    const std::optional<std::uint16_t>& pet = character.appearance.pet;
    if (!character.safeZone && pet && std::ranges::find(PetsWithoutShadow, *pet) != PetsWithoutShadow.end())
    {
        return;
    }
    const Data::Models::Model* model = frame.models.Find(Data::Models::PlayerShadowModel);
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
        MeshDraw draw;
        draw.mesh = &mesh;
        draw.skinned = &skinned;
        Triangles(frame.batch, draw, ShadowState, ShadowColor, PassUv{});
    }
}

// Una parte del lazo de 0x50ba30, con el modelo con que va y su brillo; el casco, marcado, porque ahí
// escribe la luz la clase 4.
struct DrawnPart
{
    std::uint16_t model = 0;
    Render::Models::ItemGlow glow;
    bool helm = false;
};

// Las partes en el orden del lazo, de la 5 a la 0 (+0x1f8 + k · 0x20): las botas, los guantes, el
// pantalón, la armadura, el casco y la cabeza.
std::vector<DrawnPart> PartsInOrder(const GameLogic::CharacterAppearance& appearance)
{
    std::vector<DrawnPart> parts;
    for (std::size_t index = appearance.bodyParts.size(); index-- > 0;)
    {
        const GameLogic::WornItem& part = appearance.bodyParts[index];
        const auto bodyPart = static_cast<GameLogic::BodyPart>(index);
        parts.push_back(DrawnPart{DrawnPartModel(bodyPart, part.model, appearance.characterClass),
                                  Render::Models::ItemGlowOf(part.level, part.excellent, part.ancient),
                                  bodyPart == GameLogic::BodyPart::Helm});
    }
    if (appearance.head)
    {
        parts.push_back(DrawnPart{*appearance.head, Render::Models::ItemGlow{}, false});
    }
    return parts;
}

// La luz del objeto del personaje antes del contorno de una parte (0x50ba30 la escribe en el casco de
// la clase 4) y después (0x60b3d0, por el modelo), desde `light`.
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
        result = {Render::Models::MaskedClassLightAt(clock), Render::Models::MaskedClassLightAt(clock)};
    }
    if (const std::optional<float> written = Render::Models::ModelLightOf(part.model, clock))
    {
        result.body = *written;
    }
    return result;
}

// Las partes con la luz del objeto del personaje, que queda de una parte a la siguiente y de un frame
// al otro: empieza en la que dejó la última que la escribe (la del frame anterior, con el reloj de
// este), o en 1 (0x511290).
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
                  ModelUse{part.glow, frame.character.outlined, Data::Models::PlayerSkeletonModel,
                           ObjectState{Render::Models::NoBlendMesh, light, {}, Render::Models::NoHiddenMesh},
                           partLight.outline});
    }
}

// Los dos rand() de 0x607b60, de 0 a 32767 como el de MSVC.
Render::Models::ObjectRandoms RandomsOf(std::minstd_rand& random)
{
    constexpr std::uint_fast32_t RandRange = 32768;
    const auto first = static_cast<int>(random() % RandRange);
    const auto second = static_cast<int>(random() % RandRange);
    return {first, second};
}

// 0x607b60 no saca rand fuera de estos cuatro casos: uno para Sword15 y Staff08,
// dos para Staff06 y Shield15. El sorteo local legacy no pertenece al flujo global.
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

Render::Models::ObjectRandoms RandomsOf(const CharacterFrame& frame, std::uint16_t model)
{
    if (frame.character.originalRandom == nullptr)
    {
        return RandomsOf(frame.random);
    }
    Render::Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(model); ++index)
    {
        randoms[index] = frame.character.originalRandom->Next();
    }
    return randoms;
}

// Lo que cuelga desde la raíz `root`, posado (0x4eb860 con su séptimo argumento en 1) y dibujado con
// el objeto que 0x509340 le arma con el tipo de su ítem. Después, el caso de su modelo que pide sprites:
// el del cetro, con el hueso 0 de esa pose y el cuerpo del ítem, la escala del personaje (0x509397) y
// `bodyOrigin` (+0x6c del modelo).
void DrawHangingFrom(const CharacterFrame& frame, std::uint16_t modelId, const BoneTransform& root,
                     const glm::vec3& bodyOrigin, const Render::Models::AnimationState& state,
                     const Render::Models::ItemGlow& glow)
{
    constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;
    if (!Render::Models::HangsAsAModel(modelId) || (frame.character.objectFlags & HiddenAttachmentFlag) != 0)
    {
        return;
    }
    // 0x5093bb inicializa el objeto antes de intentar la pose del ítem.
    const std::optional<Render::Models::ObjectRandoms> originalRandoms =
        frame.character.originalRandom != nullptr ? std::optional{RandomsOf(frame, modelId)} : std::nullopt;
    const Render::Models::Skeleton* skeleton = frame.models.SkeletonOf(modelId);
    if (skeleton == nullptr)
    {
        return;
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(frame.character.poseBones ? 0.0f : 1.0f));
    if (frame.character.poseBones)
    {
        const auto retained = frame.character.poseBones(modelId);
        std::copy_n(retained.begin(), std::min(retained.size(), bones.size()), bones.begin());
    }
    if (!Render::Models::PoseAttached(*skeleton, state, root, bones))
    {
        return;
    }
    if (frame.character.onPose)
        frame.character.onPose(modelId, bones, frame.character.object, false);
    const ObjectState object = Render::Models::HangingObjectState(
        modelId, frame.clock, originalRandoms.has_value() ? *originalRandoms : RandomsOf(frame, modelId));
    DrawModel(frame, modelId, bones, ModelUse{glow, false, modelId, object, object.light, true});
    const DrawnCharacter& character = frame.character;
    if (modelId == World::HdkMaceModel && character.sprites != nullptr && character.clothWorld != nullptr &&
        !bones.empty())
    {
        const Render::Models::OriginalMatrix bone = Render::Models::OriginalMatrixOf(bones.front());
        if (character.scepter != nullptr)
        {
            character.scepter->Create(*character.sprites, bone, character.object.scale, bodyOrigin,
                                      character.clothWorld->random, frame.clock, character.pace);
        }
        else
        {
            World::CreateScepterGlow(*character.sprites, bone, character.object.scale, bodyOrigin,
                                     character.clothWorld->random);
        }
    }
}

// 0x4ec190 y 0x50966b: el origen del cuerpo de lo que cuelga de su lugar, el desplazamiento girado por el
// hueso y por la escala del personaje, guardado en float, más su posición. Con el de las manos, en 0, es
// la posición.
glm::vec3 HangingOrigin(const BoneTransform& parent, const glm::vec3& offset,
                        const Render::Models::ObjectTransform& object)
{
    const glm::vec3 turned = Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalMatrixOf(parent));
    glm::vec3 origin{};
    for (int k = 0; k < 3; ++k)
    {
        const auto scaled = static_cast<float>(static_cast<double>(object.scale) * static_cast<double>(turned[k]));
        origin[k] = static_cast<float>(static_cast<double>(scaled) + static_cast<double>(object.position[k]));
    }
    return origin;
}

// Lo que cuelga de su lugar: desde su hueso del cuerpo con el desplazamiento (0x509340 con la bandera
// en 0).
void DrawHanging(const CharacterFrame& frame, std::uint16_t modelId, Render::Models::AttachmentSlot slot,
                 const Render::Models::AnimationState& state, std::span<const BoneTransform> body,
                 const Render::Models::ItemGlow& glow)
{
    const Render::Models::Attachment attachment = Render::Models::AttachmentOf(slot);
    if (static_cast<std::size_t>(attachment.bone) >= body.size())
    {
        return;
    }
    const BoneTransform& parent = body[static_cast<std::size_t>(attachment.bone)];
    DrawHangingFrom(frame, modelId, Render::Models::AttachmentRoot(parent, attachment.offset),
                    HangingOrigin(parent, attachment.offset, frame.character.object), state, glow);
}

// El carcaj de 0x50ba30 con las armas en la mano (local_20 en 0): las saetas de la mano izquierda o,
// si no, las flechas de la derecha.
std::optional<GameLogic::WornItem> QuiverOf(const GameLogic::CharacterAppearance& appearance)
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

// El carcaj a la espalda: del hueso de las alas con QuiverPlacement (0x509340 con su bandera), con el
// nivel de 0x60b3d0 y el excelente de la mano, sin el set. Arrows01 y 02 tienen una acción de una key:
// la pose no depende del frame del lugar de las alas, con que el original lo posa.
void DrawQuiver(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const std::optional<GameLogic::WornItem> quiver = QuiverOf(frame.character.appearance);
    const auto bone =
        static_cast<std::size_t>(Render::Models::AttachmentOf(Render::Models::AttachmentSlot::Wings).bone);
    if (!quiver || bone >= body.size())
    {
        return;
    }
    const Render::Models::ItemGlow glow =
        Render::Models::ItemGlowOf(Render::Models::QuiverGlowLevel(quiver->level), quiver->excellent, false);
    // Por ese camino, el origen del cuerpo es la posición del personaje (0x509607).
    DrawHangingFrom(frame, quiver->model, Render::Models::PlacedRoot(body[bone], Render::Models::QuiverPlacement),
                    frame.character.object.position, Render::Models::AnimationState{}, glow);
}

// flare01 (0x5ac) y la mezcla 0 de 0x5491e0, la aditiva.
constexpr std::uint16_t ClothFlareTexture = 0x5ac;
constexpr std::int32_t AdditiveSprite = 0;

// 0x50d58b..0x50db57: las telas, después del lazo de las partes (0x50d3dc) y antes de lo que cuelga,
// con los huesos del cuerpo de este frame y su reloj: un frame del bloque por cada uno del original que
// pasó (CharacterCloth::Advance).
void DrawTheCloth(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const DrawnCharacter& character = frame.character;
    if (character.cloth == nullptr || character.clothWorld == nullptr)
    {
        return;
    }
    const GameLogic::CharacterAppearance& appearance = character.appearance;
    ClothWearer wearer;
    wearer.characterClass = appearance.characterClass;
    wearer.armor = appearance.bodyParts[static_cast<std::size_t>(GameLogic::BodyPart::Armor)].model;
    wearer.wings = appearance.wings;
    wearer.objectFlags = character.objectFlags;
    const Render::Models::ClothBody clothBody{.bones = body,
                                              .angles = character.object.angles,
                                              .scale = character.object.scale,
                                              .position = character.object.position};
    const bool wears = WearsACloth(appearance.characterClass, character.map);
    if (character.advanceCloth)
    {
        character.cloth->Advance(frame.batch, wearer, clothBody, *character.clothWorld, character.map, frame.clock,
                                 wears, PlainClothColor, character.pace);
    }
    else
    {
        character.cloth->DrawCurrent(frame.batch, wearer, frame.clock, wears);
    }
    // Los destellos del casco blanco (0x455930), aditivos, en la tabla del frame.
    if (character.sprites != nullptr)
    {
        for (const Render::Models::ClothFlare& flare : character.cloth->Flares())
        {
            (void)character.sprites->Create(ClothFlareTexture, flare.position, flare.scale, glm::vec3(flare.light),
                                            0.0f, AdditiveSprite);
        }
    }
}

// Las fronteras del original permiten conservar lo ya dibujado si falta lo que sigue:
// sombra (0x50bcec), partes (0x50d4e0) y tela (0x50d58b).
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

// El lazo de las manos de 0x50ba30 (0x50e6af), después de la espalda, las alas y la mascota.
void DrawHands(const CharacterFrame& frame, std::span<const BoneTransform> body)
{
    const GameLogic::CharacterAppearance& appearance = frame.character.appearance;
    using Render::Models::AttachmentSlot;
    for (const auto& [hand, slot] : {std::pair{appearance.rightHand, AttachmentSlot::RightHand},
                                     std::pair{appearance.leftHand, AttachmentSlot::LeftHand}})
    {
        if (hand && !SkippedInTheHand(hand->model))
        {
            // 0x509340 le pasa a 0x60b3d0 el set en 0: lo que cuelga no lleva su pase.
            const Render::Models::ItemGlow glow = Render::Models::ItemGlowOf(hand->level, hand->excellent, false);
            DrawHanging(frame, hand->model, slot, Render::Models::AnimationState{}, body, glow);
        }
    }
}

// Un objeto del mundo no tiene la piel de una clase: sus mallas no van a los slots de la piel ni del
// pelo (ninguna se llama así).
constexpr std::uint8_t ObjectClass = 0;

// La semilla de los rand() de un frame: el reloj en ms. El original usa el rand() de todo el cliente,
// que no se puede repetir.
std::uint_fast32_t RandomSeedOf(Core::Duration clock)
{
    return static_cast<std::uint_fast32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
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
                   Core::Duration frameClock, CharacterDrawPass pass)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton == nullptr)
    {
        return;
    }
    std::vector<BoneTransform> body(skeleton->bones.size(), BoneTransform(character.poseBones ? 0.0f : 1.0f));
    if (character.poseBones)
    {
        const auto retained = character.poseBones(Data::Models::PlayerSkeletonModel);
        std::copy_n(retained.begin(), std::min(retained.size(), body.size()), body.begin());
    }
    if (!Render::Models::PoseSkeleton(*skeleton, character.body, character.object, character.headAngles, body))
    {
        return;
    }
    if (character.onPose)
        character.onPose(Data::Models::PlayerSkeletonModel, body, character.object, false);
    const GameLogic::CharacterAppearance& appearance = character.appearance;
    std::minstd_rand random(RandomSeedOf(frameClock));
    const CharacterFrame frame{batch, models, character, frameClock, random};
    if (!DrawTheBodyPass(frame, body, pass))
    {
        return;
    }
    DrawQuiver(frame, body);
    const Render::Models::ItemGlow noGlow{};
    using Render::Models::AttachmentSlot;
    if (appearance.wings)
    {
        DrawHanging(frame, *appearance.wings, AttachmentSlot::Wings, character.wings, body, noGlow);
    }
    if (appearance.pet && Render::Models::DrawsThePet(*appearance.pet))
    {
        DrawHanging(frame, *appearance.pet, AttachmentSlot::Pet, character.pet, body, noGlow);
    }
    DrawHands(frame, body);
}

namespace
{
MeshDraw RetainedMeshDraw(const DrawnObject& object, Core::Duration frameClock, const Data::Models::Mesh& mesh,
                          const Render::Models::SkinnedMesh& skinned, std::size_t index, const MeshRole& role,
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
    draw.normalScale = object.transform.scale;
    draw.originalClock = originalClock;
    return draw;
}

std::vector<MeshDraw> RetainedObjectMeshDraws(const IModelLibrary& models, const DrawnObject& object,
                                              Core::Duration frameClock,
                                              std::span<const Render::Models::SkinnedMesh> meshes,
                                              bool forceVisible = false, bool rawMesh = false,
                                              bool originalClock = false)
{
    const auto* model = models.Find(object.model);
    if (model == nullptr || (!forceVisible && object.alpha < 0.01f))
        return {};
    const auto slots = models.MeshTextures(object.model);
    std::vector<MeshDraw> draws;
    for (std::size_t index = 0; index < model->meshes.size() && index < meshes.size(); ++index)
    {
        const auto& mesh = model->meshes[index];
        const auto role = RoleOf(object.model, mesh, index);
        const auto texture = TextureOf(mesh, slots, index, ObjectClass);
        if ((!rawMesh && role.hidden) || !texture.drawn || !IndicesInRange(mesh) ||
            meshes[index].positions.size() < mesh.vertices.size() || meshes[index].normals.size() < mesh.normals.size())
            continue;
        draws.push_back(
            RetainedMeshDraw(object, frameClock, mesh, meshes[index], index, role, texture.slot, originalClock));
    }
    return draws;
}
// Las mallas de un objeto del mundo posado como 0x5ffdf0, con su alpha y su luz por vértice, o
// ninguna si 0x5ffdf0 no lo dibuja (el alpha por debajo de 0,01, 0x6b88d4) o falta su modelo.
std::vector<MeshDraw> ObjectMeshDraws(const IModelLibrary& models, const DrawnObject& object, Core::Duration clock,
                                      std::vector<Render::Models::SkinnedMesh>& skinned)
{
    if (!object.retainedMeshes.empty())
        return RetainedObjectMeshDraws(models, object, clock, object.retainedMeshes);
    constexpr float VisibleAlpha = 0.01f;
    const Data::Models::Model* model = models.Find(object.model);
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(object.model);
    if (object.alpha < VisibleAlpha || model == nullptr || skeleton == nullptr)
    {
        return {};
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Render::Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
    {
        return {};
    }
    std::vector<MeshDraw> draws =
        MeshDrawsOf(models, object.model, *model, bones, object.transform, ObjectClass,
                    Render::Color{object.color.x, object.color.y, object.color.z, 1.0f}, object.state, clock, skinned);
    for (MeshDraw& draw : draws)
    {
        draw.alpha = object.alpha;
        const bool unlit = object.unlitMesh && static_cast<std::int16_t>(draw.index) == *object.unlitMesh;
        draw.vertexLight = object.vertexLight && !unlit;
        draw.scrolls = draw.scrolls || unlit;
        draw.normalScale = object.transform.scale;
    }
    return draws;
}

// La última de las cuatro mallas de la fuente de Lorencia (0x600ad6), que también es su blendMesh.
constexpr std::int16_t WaterspoutLastMesh = 3;

// Lo que 0x6002e0 le agrega al caballo después de su RenderBody (0x6002e0, el caso 0xbf).
constexpr std::size_t DarkHorseFirstChromeMesh = 12;
constexpr std::size_t DarkHorseLastChromeMesh = 15;
constexpr Render::Color DarkHorseChromeColor{1.0f, 0.8f, 0.3f, 1.0f};
constexpr std::size_t DarkHorseShadowMesh = 8;
constexpr Render::Color DarkHorseShadowColor{0.0f, 0.0f, 0.0f, 0.7f};
// 0x60073c..0x600787: sin sombra en los mapas 10, 24 a 29 y 36; en el 7, glColor4f(0, 0, 0, 0,2).
constexpr float DarkHorseShadowAlphaOfMapSeven = 0.2f;
constexpr std::int32_t DarkHorseFaintShadowMap = 7;
constexpr std::int32_t DarkHorseNoShadowMap = 10;
constexpr std::int32_t DarkHorseFirstKalimaMap = 24;
constexpr std::int32_t DarkHorseLastKalimaMap = 29;
constexpr std::int32_t DarkHorseSeventhKalimaMap = 36;
constexpr Render::ModelDrawState BlendedShadowState{std::nullopt, Render::ModelBlend::BlendedShadow, true};
// El caso 0xc0: las mallas que dibuja con el nivel de la mascota en 0, en su orden, y su sombra
// (glColor4f(0, 0, 0, 1), 0x6009eb).
constexpr std::array<std::size_t, 2> DarkSpiritMeshes{3, 2};
constexpr Render::Color DarkSpiritShadowColor{0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

void DrawObject(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                Core::Duration frameClock)
{
    std::vector<Render::Models::SkinnedMesh> skinned;
    for (const MeshDraw& draw : ObjectMeshDraws(models, object, frameClock, skinned))
    {
        DrawBase(batch, draw);
    }
}

void DrawObjectFromMeshes(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                          Core::Duration frameClock, std::span<const Render::Models::SkinnedMesh> meshes,
                          std::optional<Render::Models::ShinePass> shine)
{
    SetFade fade{object.alpha, 1.0f};
    for (const auto& draw : RetainedObjectMeshDraws(models, object, frameClock, meshes, shine.has_value()))
    {
        if (!shine)
        {
            DrawBase(batch, draw);
            continue;
        }
        if (!draw.role.shines ||
            Render::Models::PassHiddenMesh(draw.model, *shine, draw.object->hiddenMesh) == draw.index)
            continue;
        if (*shine == Render::Models::ShinePass::Chrome || *shine == Render::Models::ShinePass::Shiny)
        {
            // 60b130→608e00 recibe alpha y escala antes de RenderBody; el color incluye ese producto.
            const auto color = Scaled(Render::Models::ShineColorOf(object.model), object.alpha);
            Triangles(batch, draw, {PassSlot(*shine), Render::ModelBlend::Additive, true}, color, PassUv{*shine, {}});
        }
        else
            DrawShinePass(batch, draw, *shine, fade);
    }
}

namespace
{
std::optional<Render::Models::ShinePass> ItemShine(std::uint32_t flags)
{
    using Render::Models::ShinePass;
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
    return {};
}

Core::Expected<void, std::string> ValidateItemMesh(const Data::Models::Mesh& mesh,
                                                   std::span<const Render::Models::SkinnedMesh> meshes,
                                                   std::size_t index, std::size_t textures, bool textureOverride)
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

Render::ModelBlend ItemPassBlend(const MeshDraw& draw, std::uint32_t flags,
                                 std::optional<Render::Models::ShinePass> shine)
{
    using Render::ModelBlend;
    if ((flags & 0x40) != 0 || (!shine && draw.blends) || shine == Render::Models::ShinePass::Chrome3 ||
        shine == Render::Models::ShinePass::Chrome4)
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
                      std::optional<Render::Models::ShinePass> shine, std::optional<Render::TextureSlot> texture,
                      glm::vec3& currentColor, glm::vec3* storedColor)
{
    if (shine && !draw.role.shines)
        return;
    if (!shine && ((!draw.blends && (flags & 0x43) == 0) || ((flags & 0x3) == 0 && HasFourComponents(*draw.mesh))))
        return;
    auto slot = texture ? texture : (shine ? std::optional(PassSlot(*shine)) : draw.texture);
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
        currentColor = {color.red, color.green, color.blue}; // 4ed3fd..4ed415: FSTP RGB por malla.
        if (storedColor)
            *storedColor = currentColor;
    }
    const auto shift =
        shine == Render::Models::ShinePass::Chrome4 || (!shine && draw.scrolls) ? draw.object->uv : glm::vec2{};
    Triangles(batch, draw, {slot, blend, true}, color, PassUv{shine, shift},
              !shine && draw.vertexLight && !draw.blends);
}

Render::Models::SkinnedMesh FlattenedItemMesh(const Render::Models::SkinnedMesh& mesh, const glm::vec3& origin)
{
    // 4ed6f6..4ed76d: X/Z en PC53 hasta el store; Y tiene un spillF32 intermedio.
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
} // namespace

Core::Expected<void, std::string> DrawItemMeshPass(Render::ModelBatch& batch, const IModelLibrary& models,
                                                   const DrawnObject& object, Core::Duration frameClock,
                                                   std::span<const Render::Models::SkinnedMesh> meshes,
                                                   std::int16_t mesh, std::uint32_t flags,
                                                   std::optional<Render::TextureSlot> texture, glm::vec3* storedColor)
{
    const auto* model = models.Find(object.model);
    if (!model)
        return Core::Unexpected(std::string("4ecc30: falta el modelo del pase de ítem"));
    if (mesh >= 0 && static_cast<std::size_t>(mesh) >= model->meshes.size())
        return {}; // 4ecc3d..4ecc4f: el nativo omite una malla fuera del contador.
    constexpr std::uint32_t Supported = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x200 | 0x800 | 0x1000;
    if ((flags & ~Supported) != 0)
        return Core::Unexpected(std::string("4ecc30: banderas del pase de ítem fuera del contrato portable"));
    const auto slots = models.MeshTextures(object.model);
    const auto shine = ItemShine(flags);
    auto currentColor = object.color;
    for (std::size_t index = 0; index < model->meshes.size(); ++index)
    {
        if (mesh >= 0 && index != static_cast<std::size_t>(mesh))
            continue;
        const auto& input = model->meshes[index];
        const auto role = RoleOf(object.model, input, index);
        if (mesh < 0 && (role.hidden || static_cast<std::int16_t>(index) == object.state.hiddenMesh))
            continue;
        if (auto valid = ValidateItemMesh(input, meshes, index, slots.size(), texture.has_value()); !valid.HasValue())
            return valid;
        const auto assigned = TextureOf(input, slots, index, ObjectClass);
        if (!assigned.drawn)
            continue;
        Render::Models::SkinnedMesh flattened;
        const Render::Models::SkinnedMesh* vertices = &meshes[index];
        if ((flags & 0x20) != 0)
        {
            flattened = FlattenedItemMesh(meshes[index], object.transform.position);
            vertices = &flattened;
        }
        const auto draw = RetainedMeshDraw(object, frameClock, input, *vertices, index, role, assigned.slot, true);
        DrawItemPassMesh(batch, draw, flags, shine, texture, currentColor, storedColor);
    }
    return {};
}

namespace
{
// 60b4aa..60b6d5 y las tres tablas60ccfc/60cd1c/60cd30.
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

Core::Expected<void, std::string> DrawItemOptionPasses(Render::ModelBatch& batch, const IModelLibrary& models,
                                                       const DrawnObject& base, Core::Duration frameClock,
                                                       std::span<const Render::Models::SkinnedMesh> meshes,
                                                       const Render::Models::ItemGlow& glow,
                                                       const ItemDrawContext& context)
{
    const auto draws = RetainedObjectMeshDraws(models, base, frameClock, meshes, false, false, true);
    SetFade fade{Render::Models::SetAlphaAt(frameClock)};
    for (const auto& pass : glow.passes)
    {
        if (pass.shine == Render::Models::ShinePass::Chrome3 && fade.alpha <= 0.01f && context.originalFrame)
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

Core::Expected<void, std::string> DrawItemFromMeshes(Render::ModelBatch& batch, const IModelLibrary& models,
                                                     const DrawnObject& object, Core::Duration frameClock,
                                                     std::span<const Render::Models::SkinnedMesh> meshes,
                                                     std::uint8_t packedLevel, std::uint8_t excellent,
                                                     std::uint8_t ancient, std::uint8_t detail,
                                                     const ItemDrawContext& context)
{
    const auto clock = ItemFrameClock(frameClock, context);
    if (!clock.HasValue())
        return Core::Unexpected(clock.Error());
    frameClock = clock.Value();
    auto level = ItemLevel(object.model, packedLevel);
    if (detail < Render::Models::DefaultGlowDetail)
        level = std::min(level, 2 * static_cast<int>(detail) + 5);
    if (object.model >= 0x1e12 && object.model <= 0x1e18 && level > 0)
        level = 7;
    const bool hasExcellent =
        (excellent & 0x3f) != 0 && !(object.model >= 0x1a12 && object.model <= 0x1a18) && object.model != 0x1c30;
    const bool hasAncient = (ancient & 3) == 1 || (ancient & 3) == 2;
    const auto glow =
        Render::Models::ItemGlowOf(static_cast<std::uint8_t>(std::max(0, level)), hasExcellent && detail != 0,
                                   hasAncient && detail != 0, Render::Models::DefaultGlowDetail);
    auto base = object;
    if (context.modelState)
    {
        const auto selected = context.modelState->unlitMeshes.find(static_cast<std::int16_t>(base.model));
        if (selected != context.modelState->unlitMeshes.end())
            base.unlitMesh = selected->second;
    }
    if (!base.vertexLight)
        base.color *= 0.1f; // 5ffce0 antes de los RET tempranos.
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

void DrawSubtractiveMesh(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                         Core::Duration frameClock)
{
    if (object.state.blendMesh != Render::Models::EveryMeshBlends)
    {
        return;
    }
    DrawnObject white = object;
    white.color = glm::vec3(1.0f);
    white.alpha = 1.0f;
    std::vector<Render::Models::SkinnedMesh> skinned;
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

void DrawWaterspout(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock)
{
    // 0x600a62..0x600abd: las mallas 0 a 2, con +0x64, +0x68 y U = V = 0.
    ObjectState plain = object.state;
    plain.uv = glm::vec2{};
    plain.hiddenMesh = Render::Models::NoHiddenMesh;
    // 0x600ac2..0x600adf: la 3, con el blendMesh 3 fijo, +0x68, +0x6c y +0x70.
    ObjectState last = object.state;
    last.blendMesh = WaterspoutLastMesh;
    last.hiddenMesh = Render::Models::NoHiddenMesh;
    std::vector<Render::Models::SkinnedMesh> skinned;
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

void DrawDarkHorse(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& horse, std::int32_t map,
                   Core::Duration frameClock)
{
    DrawnObject white = horse;
    white.color = glm::vec3(1.0f);
    std::vector<Render::Models::SkinnedMesh> skinned;
    const std::vector<MeshDraw> draws = ObjectMeshDraws(models, white, frameClock, skinned);
    for (const MeshDraw& draw : draws)
    {
        DrawBase(batch, draw);
    }
    // 0x4ecc30(12..15, 0x44, …) directo, sin RenderBody: el cromo aditivo de cada una que existe (las de
    // serie: solo la 12, Q-C113), sin la U y la V (en 0) y, con el alpha por debajo de 0,99, con el color
    // que cada una multiplica en el lugar por él (Q-C95). Salvo con N en el script. Las mallas salen de
    // MeshDrawsOf, que deja afuera las de H y las rotas; las de serie no tienen script.
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
        Triangles(batch, chrome, {PassSlot(Render::Models::ShinePass::Chrome), Render::ModelBlend::Additive, true},
                  chromeColor, PassUv{Render::Models::ShinePass::Chrome, glm::vec2{}});
    }
    // 0x60073c..0x60075b: en los mapas 10, 24 a 29 y 36 no hay sombra.
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
    // 0x4eff40(−1, −1, 8, 9): la malla 8. Sale de MeshDrawsOf, como el cromo.
    if (const auto shadow = std::ranges::find(draws, DarkHorseShadowMesh, &MeshDraw::index); shadow != draws.end())
    {
        Render::Models::SkinnedMesh flat = *shadow->skinned;
        for (glm::vec3& position : flat.positions)
        {
            position = Render::Models::FlattenOnTheGround(position, horse.transform.position);
        }
        MeshDraw draw = *shadow;
        draw.skinned = &flat;
        Triangles(batch, draw, BlendedShadowState, shadowColor, PassUv{});
    }
}

void DrawDarkSpirit(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& spirit,
                    float groundHeight, Core::Duration frameClock)
{
    std::vector<Render::Models::SkinnedMesh> skinned;
    const std::vector<MeshDraw> draws = ObjectMeshDraws(models, spirit, frameClock, skinned);
    for (const std::size_t mesh : DarkSpiritMeshes)
    {
        if (const auto draw = std::ranges::find(draws, mesh, &MeshDraw::index); draw != draws.end())
        {
            DrawBase(batch, *draw);
        }
    }
    // 0x600a0b: 0x4eff40 aplasta desde (x, y, la altura del terreno ahí), que le escribe a +0x6c..+0x74.
    const glm::vec3 ground(spirit.transform.position.x, spirit.transform.position.y, groundHeight);
    for (const MeshDraw& draw : draws)
    {
        Render::Models::SkinnedMesh flat = *draw.skinned;
        for (glm::vec3& position : flat.positions)
        {
            position = Render::Models::FlattenOnTheGround(position, ground);
        }
        MeshDraw shadow = draw;
        shadow.skinned = &flat;
        Triangles(batch, shadow, BlendedShadowState, DarkSpiritShadowColor, PassUv{});
    }
}

std::vector<Render::Models::OriginalMatrix> PosedWorldBones(const IModelLibrary& models, const DrawnObject& object)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(object.model);
    if (skeleton == nullptr)
    {
        return {};
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Render::Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
    {
        return {};
    }
    constexpr std::size_t Rows = 3;
    constexpr std::size_t Columns = 4;
    std::vector<Render::Models::OriginalMatrix> world;
    world.reserve(bones.size());
    for (const BoneTransform& bone : bones)
    {
        Render::Models::OriginalMatrix matrix = Render::Models::OriginalMatrixOf(bone);
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

std::optional<glm::vec3> PosedBonePoint(const IModelLibrary& models, const DrawnObject& object, std::size_t bone,
                                        const glm::vec3& offset)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(object.model);
    // 0x4eb973: la pose saltea los dummy sin escribirlos; el buffer global queda con el de antes.
    if (skeleton == nullptr || bone >= skeleton->bones.size() || skeleton->bones[bone].dummy)
    {
        return std::nullopt;
    }
    std::vector<BoneTransform> bones(skeleton->bones.size(), BoneTransform(1.0f));
    if (!Render::Models::PoseSkeleton(*skeleton, object.animation, object.transform, glm::vec2{}, bones))
    {
        return std::nullopt;
    }
    return (Render::Models::VectorTransform(offset, bones[bone]) * object.transform.scale) + object.transform.position;
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
} // namespace Client::Scenes
