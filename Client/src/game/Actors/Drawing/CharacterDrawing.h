#pragma once

#include "Core/IClock.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/ItemGlow.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skinning.h"
#include "Models/Shine.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterCloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Client::Kernel
{
struct JointRequest;
} // namespace Client::Kernel

namespace Client::Actors::Drawing
{
class ScepterGlowCarry;
} // namespace Client::Actors::Drawing

namespace Client::WorldObjects
{
struct ObjectModelStepState;
} // namespace Client::WorldObjects

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

namespace Client::Models
{
struct SkinningBuffers;
} // namespace Client::Models

// A character's drawing in the ModelBatch, like 0x50ba30 in the selection: the posed Player.bmd body, the parts with
// that pose's matrices (0x60cf70), what hangs from its bone (0x509340, Actors/Attachment.h) and each part's glow passes
// by its level (0x60b3d0, Models/ItemGlow.h), each mesh with its texture and the blend of 0x4ecc30. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario" and "La selección de personaje". Without
// the per-vertex light (in the selection the character goes with one color per mesh), without the color of 0x609640
// (P-C14) and with the weapons always in the hand.
namespace Client::Actors::Drawing
{
struct DrawnCharacter
{
    Kernel::CharacterAppearance appearance;
    Models::AnimationState body;
    Models::ObjectTransform object;
    glm::vec2 headAngles{};
    // The pet and the wings advance by time (Models/Animation.h).
    Models::AnimationState pet;
    Models::AnimationState wings;
    // The character's light, channel by channel: the base of 0x50ba30 plus +0xf4 (0.4, and 1.4 for the one chosen in
    // the selection; in the login, the terrain's at its place plus 0.3).
    glm::vec3 light{0.4f, 0.4f, 0.4f};
    // 0x50db94 resets the RGB before the weapons, the wings and the pet: the hero does not keep there the color from
    // the own record's bits it used for its parts.
    std::optional<glm::vec3> hangingLight{};
    // +0x174 and the shadow guards of 0x50bc05..0x50bcbd (CastsShadow, GameCharacters.h). `object.position` already has
    // the z of the shadow's floor (0x50bc77..0x50bcbd, ShadowGroundOf in GameCharacters.h).
    float alpha = 1.0f;
    bool castsShadow = true;
    bool safeZone = false;
    // +0x78: the cloth is neither simulated nor drawn with 0x4000; with 0x1000 the dt is 0.
    std::uint32_t objectFlags = 0;
    // In the game, the rand the case of 0x607b60 asks for belong to the client's global flow.
    Crt::CrtRandom* originalRandom = nullptr;
    // The one under the mouse (DAT_006c8e30 in 0x510d00): its parts carry the outline.
    bool outlined = false;
    // +0x88 of the object: with 4 (an NPC), the outline of 0x60cf70 goes with other colors; with 1 (a player), in Chaos
    // Castle it wears the Angel (DrawChaosCastleBody).
    std::uint8_t kind = 0;
    // The cloth of class 3 or 4 (0x50d58b..0x50db57): this character's, which lives between frames, the global state it
    // reads (the wind, the counter and the rand()) and the scene's map. Without both, it goes without a cloth.
    CharacterCloth* cloth = nullptr;
    Models::ClothWorld* clothWorld = nullptr;
    // +0x376: the monster number (0xffff for a player). With 0x37, that monster's cloth (0x50c951,
    // WearsTheMonsterCloth).
    std::optional<std::uint16_t> monster;
    // false between original frames: draws the current cloth without creating or simulating it.
    bool advanceCloth = true;
    std::uint8_t map = 0;
    // bVar23 and the color of 0x50ba30 (0x50c929..0x50cc29) when the drawing knows them (the game, with the duel);
    // without them, WearsACloth with the class and the map, in white.
    std::optional<bool> wearsCloth{};
    glm::vec3 clothColor = PlainClothColor;
    // The frame's sprite table (0x5491e0, World/Sprites.h): the cloth's 30 flares, when drawing it, and the sceptre's
    // five (0x509340, case 0x620, World/ScepterGlow.h) with the rand() of `clothWorld`, in that order. Without it, they
    // are not requested.
    Particles::FrameSprites* sprites = nullptr;
    // The draw of the sceptre's five between MuRet frames (Actors::Drawing::ScepterGlowCarry): with it, once per
    // original frame; without it, on each DrawCharacter.
    Actors::Drawing::ScepterGlowCarry* scepter = nullptr;
    // How the cloth and the sceptre count the original's frames (Models::OriginalFrameCarry): by the frame's clock, or
    // one per draw in the faithful mode (SceneContext::pacing).
    Models::OriginalFramePace pace = Models::OriginalFramePace::ByTheClock;
    // Publishes matrices already computed: the body with the head (also Chaos Castle's) and what hangs, with its real
    // root. The body goes to the character's own buffer (+0x120, 0x511bf7), not to the global one: whoever receives it
    // decides where to keep it.
    std::function<void(std::uint16_t, std::span<const Math::BoneTransform>, const Models::ObjectTransform&, bool)>
        onPose{};
    std::function<std::span<const Math::BoneTransform>(std::uint16_t)> poseBones{};
    // Publishes the box after drawing the parts and outlines, before the character's body.
    std::function<void(const glm::vec3&)> onPartBox{};
    // What the draw poses and skins into, kept by whoever draws the character from frame to frame. Without them, each
    // draw uses buffers of its own.
    Models::SkinningBuffers* drawBuffers = nullptr;
};

// 0x50db57 separates the parts and the cloth from what hangs with a rand and a new light. Both paths reuse the same
// model render; All keeps the login's and the selection's contract.
enum class CharacterDrawPass : std::uint8_t
{
    All,
    Body,
    Hanging,
    Shadow,
    Parts,
    Cloth,
};

// The texture slots of the glow passes (06): 0x5fc for 0x8, 0x5c0 for 0x4 and 0x620 for 0x200, 0x800 and 0x1000.
inline constexpr Render::TextureSlot ShinyTextureSlot{0x5fc};
inline constexpr Render::TextureSlot ChromeTextureSlot{0x5c0};
inline constexpr Render::TextureSlot MetalTextureSlot{0x620};

// A class's skin, `model + 0x98` (0x50d41c..0x50d431): 2 · (class & 7) + (class >> 3).
[[nodiscard]] constexpr std::uint8_t SkinIndexOf(std::uint8_t characterClass)
{
    return static_cast<std::uint8_t>(2 * (characterClass & 7) + (characterClass >> 3));
}

// The slot a mesh uses: the skin's (0x12e) goes to 0x12e + the skin, the hair's (0x137) to 0x12f + the skin, and the
// hidden one (0x12d) is not drawn (06 § El modelo en el binario).
[[nodiscard]] std::optional<Render::TextureSlot> MeshSlotOf(Render::TextureSlot assigned, std::uint8_t characterClass);

// 0x50ba30: the body posed with `body` and the head; the shadow; the five parts from the boots to the helmet and the
// head; the back, the wings and the pet that is drawn (Q-C98); and the hands. Each model goes pass by pass
// (RenderBody): each mesh with its texture, opaque or, with an OZT (four components), with the normal blend, and the
// light's color times its level's factor; then its glow passes, additive. With `outlined`, each of the six parts
// carries the outline of 0x60cf70 first; what hangs does not. A model the library does not have is not drawn. What the
// object passes to RenderBody (Models/ObjectState.h): in the parts, the character's, with the light class 4 writes on
// the helmet and 0x60b3d0 by the model; in what hangs, the one 0x509340 builds with 0x607b60. The script of each mesh's
// texture (0x4ef0c0 and 0x4ecc30): with H it is not drawn; with R it passes its index as the blendMesh, and the mesh
// whose texture is the blendMesh (all of them, with −2) goes additive with its texture and by the light; with N, or if
// 0x60ff50 marks it, it does not glow; S and the blendMesh scroll the UVs with the object's U and V. Class 4 wears its
// masked helmets with each one's MaskHelmMale. The per-model cases of 0x60b3d0 a character reaches: the mesh some items
// hide in their passes, Spear10's chrome first and the set that fades mesh by mesh. The bolts of the left hand or the
// arrows of the right go on the back, from the wings' bone.
void DrawCharacter(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnCharacter& character,
                   Core::Duration frameClock, CharacterDrawPass pass = CharacterDrawPass::All);

// The shadow guards of 0x50ba30 (0x50bc05..0x50bc71: castsShadow and, outside the safe zone, without the ridden pets):
// with them, in Blood Castle the one that dies pushed goes down to the terrain (0x50bc77..0x50bcbd) and 0x50bcec calls
// 0x60cf70 with 0x20b, which copies the character's scale and position to it even if it does not draw it
// (WorldState::PlaceShadowModel).
[[nodiscard]] bool DrawsTheShadow(const DrawnCharacter& character);

// 0x50d374..0x50d3d7: in Chaos Castle (18 to 23), a player (+0x88 at 1) does not wear its six parts but the Angel
// (0x1f7, Player\Angel), on the posed body, with 0x60cf70: those maps' outline if it is marked, and 0x60b3d0 with level
// 0, which sets its light (+0x3b4) and calls 0x609640 (case 0x1f7) with `flags`: 2, or 6 for the hero, with mesh 0's
// chrome. Nobody writes 0x1f7's skin (+0x98 of the model: only the parts loop sets it on its own parts, and 0x60dbe0
// leaves it at 0), so its skin_ mesh goes with skin 0. Without the model it draws nothing, like a part. `models` poses
// the body (0x20a) and `angelModels` has 0x1f7, with the textures of its load.
inline constexpr std::uint16_t ChaosCastleBodyModel = 0x1f7;
[[nodiscard]] Core::Expected<void, std::string> DrawChaosCastleBody(Render::ModelBatch& batch,
                                                                    const Models::IModelLibrary& models,
                                                                    const Models::IModelLibrary& angelModels,
                                                                    const DrawnCharacter& character,
                                                                    Core::Duration frameClock, std::uint32_t flags);

// The body bones of `character` as DrawCharacter poses them (0x4eb860 with the head): the character's own buffer,
// +0x120. Without the 0x20a skeleton or if the pose fails, empty.
[[nodiscard]] std::vector<Math::BoneTransform> PoseCharacterBody(const Models::IModelLibrary& models,
                                                                 const DrawnCharacter& character);

// How many rand() 0x607b60 draws when building the object of what hangs with type `model`: one with Sword15 (0x220) and
// Staff08 (0xc19), two with Staff06 (0xc17) and Shield15 (0xe20), and none with the others.
[[nodiscard]] std::size_t HangingRandomCount(std::uint16_t model);

// A world object (0x606660): the model, its animation (+0x111 to +0x118), where it goes, what it passes to RenderBody
// and the model's color (+0x48..+0x50, the one 0x5ffce0 sets before posing).
struct DrawnObject
{
    std::uint16_t model = 0;
    Models::AnimationState animation;
    Models::ObjectTransform transform;
    Models::ObjectState state;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    // +0x174, the alpha passed to RenderBody.
    float alpha = 1.0f;
    // +0xe8: the per-vertex light, which 0x5ffce0 copies to +0x44 of the model.
    bool vertexLight = false;
    // +0x98 of the model (0x50d431): the skin mesh goes on 0x12e + skin and the hair mesh on 0x12f + skin.
    std::uint8_t skin = 0;
    // model+0x88: the shared selection the objects' step publishes, without light and with scrolled UVs.
    std::optional<std::int16_t> unlitMesh{};
    // Scratch published by the caller's pose; the specialized renderers do not pose again either.
    std::span<const Models::SkinnedMesh> retainedMeshes{};
};

// 0x602e90 with an object without per-type cases: 0x5ffdf0 does not draw it with the alpha below 0.01, and otherwise
// poses it with its scale, its angles and its position, without a head; 0x6002e0 calls RenderBody(2): the base pass of
// each mesh, without glow and without outline, with its texture's script and the object's blendMesh, light, U, V and
// hidden mesh. With the alpha below 0.99 each mesh goes with the normal blend and that alpha (0x4ecc30); with the
// per-vertex light, each corner goes with the color by the light of its normal (Models::NormalLight), except the
// blendMesh's. A model the library does not have is not drawn.
void DrawObject(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                Core::Duration frameClock);

// RenderBody(2, …, `texture`) (0x4ef0c0) with a world object: DrawObject's base pass with `texture` forced on each
// mesh, which has three components (like Chrome01): opaque, or with the normal blend and the alpha below 0.99, or
// additive and by the light in the blendMesh branch.
void DrawObjectWithTexture(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                           Core::Duration frameClock, Render::TextureSlot texture);

// RenderBody on the scratch 0x4ebc80 already transformed: keeps the dummies and the held vertices when another family
// does not pose again. shine picks an extra pass of 0x60b130.
void DrawObjectFromMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                          Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes,
                          std::optional<Models::ShinePass> shine = std::nullopt);

// 0x4ebc80 on `bones`, the bones 0x5ffdf0 posed in the own buffer (+0x120): one per mesh of `object`'s model, in its
// order, with its scale and its position; also those without texture, which 0x4eff40 does not look at. A mesh with
// indices out of range is left empty. Without the model, none. `boneScale` is the 0x6c881c that 0x5ffdf0 leaves by its
// mode: 1, or 1.2 with 2 and 1.4 with 3 (0x5fff82..0x5fffaf, Models::SkinMeshAroundBones).
[[nodiscard]] std::vector<Models::SkinnedMesh> SkinObjectMeshes(const Models::IModelLibrary& models,
                                                                const DrawnObject& object,
                                                                std::span<const Math::BoneTransform> bones,
                                                                float boneScale = 1.0f);

// SkinObjectMeshes into `buffers`, which keep their capacity from one model to the next: the meshes are valid until the
// next use of `buffers`.
[[nodiscard]] std::span<Models::SkinnedMesh>
SkinObjectMeshesInto(Models::SkinnedMeshes& buffers, const Models::IModelLibrary& models, const DrawnObject& object,
                     std::span<const Math::BoneTransform> bones, float boneScale = 1.0f);

// The scratch of 0x5ffdf0 with the second argument at 0 (0x602e90 of a world object at 0x604a47, of an effect at
// 0x52da99, of a helper at 0x44532e or of the raven at 0x41834e) when nobody published it: 0x4eb860 with the flag at 1
// (PoseWorldSkeleton, the dummies with the identity) and 0x4ebc80 with Translate at 0 (SkinMeshInWorld, 0x6001a1), all
// the model's meshes, the normals with the scale inside (R · s). It does not look at the alpha. One that cannot be
// posed is left empty. Without the model, the skeleton or the pose, none.
[[nodiscard]] std::vector<Models::SkinnedMesh> WorldScratchOf(const Models::IModelLibrary& models,
                                                              const DrawnObject& object);

// The meshes 0x5ffdf0 left: the scratch the pose published or, without it, all the model's meshes posed as DrawObject
// poses them (WorldScratchOf, kept in `skinned`), also those without texture (a bare 0x4ecc30 and 0x4eff40 do not look
// at the script). Without the model, the skeleton or the pose, none.
[[nodiscard]] std::span<const Models::SkinnedMesh> PosedMeshesOf(const Models::IModelLibrary& models,
                                                                 const DrawnObject& object,
                                                                 std::vector<Models::SkinnedMesh>& skinned);

// A pass of 0x60cd60: the scale 0x4ebc80 reads from 0x6c881c and the model's color (+0x48..+0x50).
struct ObjectOutlinePass
{
    float boneScale = 1.0f;
    Render::Color color;
};

// 0x60cd60(model, object, 0x40, …, scale) once per pass of `passes`, with `object` posed in `bones` (the outline of
// 0x5ffdf0 on a monster or an NPC): each mesh taken to the world around its bones at the pass's scale and
// RenderBody(0x40) without light, without texture, additive and without writing depth, except the blendMesh's, with its
// texture and by its light (like the outline of DrawCharacter's parts); without the hidden mesh. The caller checks
// +0x78 & 0x4000 (0x60cd74) and the 0xc5 case (0x60cdd9) first.
void DrawObjectOutline(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                       Core::Duration frameClock, std::span<const Math::BoneTransform> bones,
                       std::span<const ObjectOutlinePass> passes);

// The outline of 0xc5 (0x60cdd9..0x60ce55): like DrawObjectOutline, but only mesh 0 with 0x4ecc30 (0x60ce4e), without
// its texture's script (hidden, or its own blendMesh, which 0x4ef0c0 looks at) and without the hidden mesh: the +0x58
// goes as the texture, which the outline does not use except in the blendMesh branch. The caller stops with +0x58 other
// than −1.
void DrawObjectOutlineOfTheFirstMesh(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                     const DrawnObject& object, Core::Duration frameClock,
                                     std::span<const Math::BoneTransform> bones,
                                     std::span<const ObjectOutlinePass> passes);

// The shadow of 0x60b3d0 with +0x98 at 1 (0x60b41a..0x60ccee), on `meshes` (SkinObjectMeshes): no shadow on maps 10, 24
// to 29 and 36; on 7, 0x620a00(1) and glColor4f(0, 0, 0, 0.2), and on the others 0x620980 and glColor3f(0, 0, 0).
// 0x4eff40(+0x64, +0x58, −1, −1): without the hidden mesh, those without triangles and those with the blendMesh's
// texture (+2 of the mesh), each vertex flattened from the object's position (model+0x6c..+0x74, which 0x60cf70 copied)
// with the map's spread. The caller checks +0x1bc, +0x78 & 0x4000 and +0x92 (0x60b46f..0x60b49f).
void DrawObjectShadow(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                      std::span<const Models::SkinnedMesh> meshes, std::int32_t map);

struct ItemSpecialDrawContext;
// Globals of 0x60b3d0 lent by the frame's owner and its projected scratch.
struct ItemDrawContext
{
    const ItemSpecialDrawContext* special = nullptr;
    float glowPulse = 0;
    std::map<std::uint16_t, std::int32_t>* selections = nullptr;
    bool originalFrame = true;
    WorldObjects::ObjectModelStepState* modelState = nullptr;
    std::int32_t map = 0;
    void* ownerContext = nullptr;
    std::function<std::span<const Math::BoneTransform>()> poseBones{};
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> createJoint{};
    std::function<Core::Expected<void, std::string>(std::uint16_t, std::int32_t, const DrawnObject&)> createEffect{};
    // Gets only the native alpha stores, separate from the temporary drawing alpha.
    float* storedAlpha = nullptr;
    // 0x6c9dcc, the options' glow detail.
    std::uint8_t glowDetail = Models::DefaultGlowDetail;
};

// 0x60b3d0: body and options on the bones and meshes the caller published, without posing again.
[[nodiscard]] Core::Expected<void, std::string>
DrawItemFromMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                   Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes, std::uint8_t level,
                   std::uint8_t excellent, std::uint8_t ancient, const ItemDrawContext& context = {});

// 0x50d2aa..0x50d35e: a 0x20a with subtype 0xd1..0xd3 does not wear its six parts but the skeleton's whole body
// (`model`, the subtype: Skill\Skeleton01..03, which 0x615e36 loads), on the posed body, with 0x60cf70(c, subtype,
// NULL, &c+0x3b4, c+0x174, `level` · 8, 0, 0, 0, 0, 1, mode, 2). The NULL part skips the pants' cloth (0x60d18b); the
// ninth argument at 0 poses on the own bones (+0x120). As in a part: it leaves with the alpha at 0.01 or below
// (0x60cf99); the outline if it is marked, with `state` (the hidden mesh +0x58 only there, 0x60ce68); the picking box
// (0x4ebc80) even if the model is not loaded; and 0x60b3d0 with `level` (the level already extracted, >> 3 & 0xf),
// without excellent or set, and 2. `vertexLight` is +0xe8 of the object (0x60cffa).
[[nodiscard]] Core::Expected<void, std::string>
DrawSkeletonBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnCharacter& character,
                 Core::Duration frameClock, std::uint16_t model, std::uint8_t level, const Models::ObjectState& state,
                 bool vertexLight, const ItemDrawContext& context);

// 0x4ecc30/0x4ef0c0: mesh < 0 walks RenderBody; mesh >= 0 asks for that mesh directly. The special passes lend the same
// meshes and compute no other pose. storedColor gets only the RGB FSTPs, also before a later error. Bit 0x20 projects
// to the floor with spread 2000 (0x4ed6f6, Kalima); it does not stand for the siege's spread 2500. 0x400 without 0x20
// is the wave of 0x4ed775: each corner shifted by its turned normal by sin((931 · vertex + ftol(T)) · 0.007f) · 28,
// with T the world clock (`worldClock`, 0x5767b88); without it, it stops.
[[nodiscard]] Core::Expected<void, std::string>
DrawItemMeshPass(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                 Core::Duration frameClock, std::span<const Models::SkinnedMesh> meshes, std::int16_t mesh,
                 std::uint32_t flags, std::optional<Render::TextureSlot> texture = std::nullopt,
                 glm::vec3* storedColor = nullptr, std::optional<float> worldClock = std::nullopt);

// The point `offset` of bone `bone` of `object` posed as DrawObject poses it, in the world (times the scale and plus
// the object's position, like the vertices). It is what 0x4ec110 reads, without translating, from the pose's global
// buffer (0x68b181c): 0x4eb860 leaves it in the world with its eighth argument at 1. Without the model, with fewer
// bones or with that bone a dummy (0x4eb973 does not write it), nullopt.
[[nodiscard]] std::optional<glm::vec3> PosedBonePoint(const Models::IModelLibrary& models, const DrawnObject& object,
                                                      std::size_t bone, const glm::vec3& offset);

// A model a drawing posed in the pose's global buffer (without its own buffer, +0x11c at 0), with the library it comes
// from.
struct PosedInTheBuffer
{
    const Models::IModelLibrary* models = nullptr;
    DrawnObject object;
};

// The point `offset` of bone `bone` as the drawings that posed `posed`, in that order, left it in the global buffer:
// that of the last one that has that bone without it being a dummy (the others do not write it). If none, nullopt.
[[nodiscard]] std::optional<glm::vec3> LastPosedBonePoint(std::span<const PosedInTheBuffer> posed, std::size_t bone,
                                                          const glm::vec3& offset);

// 0x602e90 with the case of map 55's whirlwind 90 in 0x6002e0: 0x620b10 and 0x4ecc30(0, 0x80, 1, blendMesh, 1, U, V,
// −1), mesh 0 alone, in white and without per-vertex light, with ModelBlend::Subtractive and the object's U and V.
// Without the blendMesh (before its first step), 0x4ecc30 binds no texture and does not change the blend (Q-C135): it
// does not draw it. A model the library does not have is not drawn.
void DrawSubtractiveMesh(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                         Core::Duration frameClock);

// 0x602e90 with the case of Lorencia's 0x69 in 0x6002e0 (0x600a4a..0x600ae6): between 0x4ecc10 and 0x4ecc20
// (glPushMatrix and glPopMatrix), four 0x4ecc30(mesh, 2, alpha, blendMesh, light, U, V, −1) without RenderBody: meshes
// 0 to 2 with the object's blendMesh and light and U = V = 0, and mesh 3 with blendMesh 3, the light and the object's U
// and V. Each one, DrawObject's base pass, without the hidden mesh (+0x58) or the R and H scripts, which RenderBody
// looks at and 0x4ecc30 does not; Waterspout01 has none. Without RenderBody's glColor: with the per-vertex light, each
// corner carries its own. A model the library does not have is not drawn.
void DrawWaterspout(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                    Core::Duration frameClock);

// 0x602e90 with Tarkan's 0x51 in 0x6002e0 (0x600c81..0x600cdc): between 0x4ecc10 and 0x4ecc20, two 0x4ecc30 on mesh 0
// without RenderBody. First the base pass (2) with the object's blendMesh, light, U and V, without the hidden mesh
// (+0x58) or the R and H scripts, which RenderBody looks at and 0x4ecc30 does not (Object82 has none); then 0x44 with
// Chrome01 (0x5c0): the chrome's UVs with the normal in the world, additive and with the model's color (that of
// 0x5ffce0, without the per-vertex light), times the alpha if it is below 0.99 (Q-C95). Without the chrome if the mesh
// has N. A model the library does not have is not drawn.
void DrawTarkanChrome(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                      Core::Duration frameClock);

// 0x425260 (from 0x602ed0) with Aida's 0x29 (0x4255f3) and 0x41/0x42 (0x42588d), between 0x4ecc10 and 0x4ecc20:
// model+0x88 at 0 (mesh 0 without per-vertex light, with the model's color and the U and the V scrolled) and one
// 0x4ecc30 on mesh 0 alone, with the object's alpha, blendMesh, light and V and the U (−ftol(T) % 100,000) · step, from
// `worldClock` (0x5767b88). The 0x29: flags 0x42 (additive), the model's color at 0.52 (0x3f051eb8, which stays in the
// model) and step 0.00001f (0x6b8a2c); the 0x41/0x42: flags 2, the color the body left and step 0.00005f (0x6b8a28).
// Then +0x88 goes back to 0xff, which is what those types have in Aida. The other types, or a model the library does
// not have, draw nothing. It draws no rand() and does not touch the object: it runs on each MuRet draw, not only on the
// original's frames.
[[nodiscard]] Core::Expected<void, std::string> DrawAidaMeshPass(Render::ModelBatch& batch,
                                                                 const Models::IModelLibrary& models,
                                                                 const DrawnObject& object, std::int16_t type,
                                                                 float worldClock, Core::Duration frameClock);

// 0x602e90 with the dark horse's case (0xbf) of 0x6002e0: DrawObject's base pass in white (it overwrites the color of
// 0x5ffce0); then 0x44 (additive chrome, Models::ShinePass::Chrome) of meshes 12 to 15 that exist (in DarkHorse.bmd,
// 12) at (1, 0.8, 0.3), times the alpha if it is below 0.99; and, in world `map` (0x6c9dd0), mesh 8's shadow (0x4eff40
// between 8 and 9) at (0, 0, 0, 0.7), blended and flattened on the floor from its place: no shadow on maps 10, 24 to 29
// and 36, and at 0.2 on 7 (0x60073c..0x600787). The normals carry the object's scale, for the light and for the
// chrome's UVs.
void DrawDarkHorse(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& horse,
                   std::int32_t map, Core::Duration frameClock);

// 0x602e90 with the raven's case (0xc0) of 0x6002e0, with the pet's level at 0 (the A9 that brings it does not reach
// the login): glColor3f(1, 1, 1) and the base pass of mesh 3 (dksbody.tga) and of mesh 2 (dkonebody.jpg, the one for a
// level below 20), with the color and the per-vertex light of 0x5ffce0; then the shadow of the five meshes
// (0x4eff40(−1, −1, −1, −1), also those not drawn, Q-C132) at (0, 0, 0, 1), blended and flattened on `groundHeight`,
// the terrain height under the raven (0x600a0b). On map `map` 10, 24 to 29 and 36 there is no shadow, and on 7 it goes
// with alpha 0.2 (0x6009a9..0x6009e5).
void DrawDarkSpirit(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& spirit,
                    float groundHeight, Core::Duration frameClock, std::int32_t map);

// 0x602e90(object, 0, 0, 10) in 0x6002e0 (0x60033c..0x600435), with `object` as 0x5ffdf0 posed it (which does not look
// at the 10): what the helper (0x445310) and the raven (0x420e11) get with the +0x78 & 0x4000 of their rider or owner.
// The model's color at 1 (0x60039d) and, from mesh 0 to the last, a bare 0x4ecc30(mesh, 0x4040, 0.5, blendMesh, light,
// U · 8, V · 2, 0x620), without RenderBody: glow pass 0x4000 (Models::ShinePass::Chrome5), which 0x4ecc30 looks at
// before the blendMesh and 0x40, with Effect\Chrome02 (0x620) forced, additive, without the per-vertex light and with
// alpha 0.5 instead of the object's. Each mesh that draws it first multiplies the model's color in place by 0.5
// (0x4ed3fd, Q-C95): the k-th one it draws, from 0, goes at 0.5 to the k + 1. As a bare 0x4ecc30, it does not skip the
// hidden mesh (+0x58) or those of script H; it does skip, without multiplying, the meshes without triangles (0x4ecc5b),
// those of the hidden texture 0x12d and those with N or the +0 of 0x60ff50 (0x4ed07a). No shadow and no per-type pass
// (DrawDarkHorse, DrawDarkSpirit, RenderBody(2), DrawFenrirBody). A model the library does not have draws nothing.
[[nodiscard]] Core::Expected<void, std::string> DrawChromeShell(Render::ModelBatch& batch,
                                                                const Models::IModelLibrary& models,
                                                                const DrawnObject& object, Core::Duration frameClock);

// 0x602e90 with the Fenrirs (0x14a to 0x14c) in 0x6002e0 (0x60277b..0x60292b), between 0x4ecc10 and 0x4ecc20: the
// model's color at 1 and +0x88 at 0 (without the per-vertex light), 0x4ecc30(0, 2, …) and 0x4ecc30(1, 2, …) with the
// object's alpha, blendMesh, light, U and V and, with `charging` (the Fenrir's action 4, +0x111), mesh 1 again with
// 0x46 (the additive chrome) and Effect\eff_lighting (0x68f). Then, except on maps 10, 24 to 29 and 36, the shadow of
// all the meshes with triangles (0x4eff40(−1, −1, −1, −1)) at (0, 0, 0, 0.7), or 0.2 on 7, blended and flattened from
// its place with the map's spread. Without the model, nothing; the error is that of 0x4ecc30.
[[nodiscard]] Core::Expected<void, std::string> DrawFenrirBody(Render::ModelBatch& batch,
                                                               const Models::IModelLibrary& models,
                                                               const DrawnObject& fenrir, std::int32_t map,
                                                               bool charging, Core::Duration frameClock);

// 0x600cf2..0x600daa, the 0x1c and 0x1d of maps 11 to 17 after their RenderBody: the shadow (0x4eff40(blendMesh, 2, −1,
// −1)) of the meshes with triangles, except mesh 2 and those with the blendMesh's texture, at (0, 0, 0, 1) (0x620a00(1)
// and glColor4f), blended and flattened on `groundHeight`, the terrain height under the object (0x600d80).
void DrawBloodCastleShadow(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                           float groundHeight, Core::Duration frameClock);

// What 0x42cc10 (the chain of 0x6002e0 on map 30, after 0x438fe0) reads from the object and from outside.
struct CastleSiegeBodyPass
{
    // +0x2: the object's type.
    std::int16_t type = 0;
    // +0x4: 0x606660 leaves it at 0; only the gates look at it.
    std::int32_t field04 = 0;
    // 0x4294b0: the siege byte (0x7239ec).
    bool siegeActive = false;
    // 0x5767b88, the world clock in ms.
    float worldClock = 0.0f;
    // 0x42cfcb: 0x4ee770(0, 0x12f, 0, 0, 0), on the vertices of the freshly posed buffer.
    std::function<Core::Expected<void, std::string>(const DrawnObject&, std::span<const Models::SkinnedMesh>)>
        emitGateVertices{};
};

// 0x42cc10 with a map 30 object that 0x6002e0 draws (+0x58 other than −2), with `object` as 0x5ffce0 left it (its
// color is the model's). Returns true with the types that have their case, which draw their own and stop the chain
// (0x6002e0 does not do its RenderBody); false with the others, which go through 0x42e3d0 (only monster types) and
// go on. The cases:
// - 0xc: the model's color at 1 and two bare 0x4ecc30 with flags 2, mesh 0 with its texture and mesh 1 with
//   Object31\Flag.tga (0x69c);
// - 0x11: the color at 0.45 (0x3ee66666), model+0x88 at 0 and RenderBody(2) with the V at
//   (ftol(T) % 10,000) · 0.0002f;
// - 0x33: model+0x88 at 0 and RenderBody(0x42) with the U at (−ftol(T) % 10,000) · 0.0002f;
// - 0x42: the color at (0.1, 0.4, 0.6) and RenderBody(0x44), the chrome, with the light at
//   sin(T · 0.001f) · 0.2f + 0.3f;
// - 0x32, 0x53, 0x54 and 0x55: RenderBody(2) and, without the siege, the black opaque shadow of 0x4eff40
//   (0x620980 and glColor3f(0, 0, 0)), without the hidden mesh or those with the blendMesh's texture, flattened
//   from its place with map 30's spread 2500;
// - 0x13, without the siege: RenderBody(2); with the color at 0.3, RenderBody(0x44) with Chrome01 (0x5c0) and
//   the blendMesh at 0; with (0.2, 0.2, 0.5), RenderBody(0x48) the same. With the siege it stops at 0x42ce09;
// - 0x3d to 0x40: with +0x4 at 0, RenderBody(2); otherwise it stops at 0x42cfcb (0x4ee770).
// After each case, model+0x88 goes back to 0xff, which on map 30 is what those types already have, and 0x5ffce0
// sets the model's color again. A model the library does not have draws nothing.
[[nodiscard]] Core::Expected<bool, std::string>
DrawCastleSiegeBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, const DrawnObject& object,
                    const CastleSiegeBodyPass& pass, Core::Duration frameClock);

// The bones of `object` posed as DrawObject poses them, in the world (the rotation times the scale; the translation
// times the scale plus the position, in float, like PosedBonePoint): what 0x4eb860 leaves with its eighth argument at 1
// in its own buffer. Without the model, empty.
[[nodiscard]] std::vector<Math::OriginalMatrix> PosedWorldBones(const Models::IModelLibrary& models,
                                                                const DrawnObject& object);
} // namespace Client::Actors::Drawing
