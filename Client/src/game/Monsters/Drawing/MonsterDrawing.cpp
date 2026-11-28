#include "Monsters/Drawing/MonsterDrawing.h"

#include "Monsters/MonstersState.h"
#include "Actors/Drawing/CharacterDrawHooks.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/CharacterAppearance.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Models/IModelLibrary.h"
#include "Render/SpriteVertex.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/PassColors.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Models/Skinning.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorPickingBox.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Models/SkinningBuffers.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/FaunaModels.h"
#include "Actors/Drawing/GameCharacterEffects.h"
#include "Joints/FaunaJointCreation.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Actors/Drawing/GameHanging.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Effects/GuildMarkEffects.h"
#include "Effects/HighModelEffects.h"
#include "Actors/Drawing/ItemDrawing.h"
#include "Monsters/Drawing/KalimaSiegeHelpers.h"
#include "Monsters/Drawing/MonsterBody.h"
#include "Monsters/Drawing/MonsterBodyPasses.h"
#include "Actors/Drawing/MonsterCloth.h"
#include "Monsters/Drawing/MonsterHelpers.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Kernel/WorldState.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainDecal.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "CastleSiege/CastleSiegeState.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Monsters::Drawing
{
namespace
{
using Math::BoneTransform;
using Result = Core::Expected<void, std::string>;

constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint8_t NpcKind = 4;
constexpr std::uint8_t UnwalledKind = 8;
using Maps::IsBloodCastle;
using Maps::IsChaosCastle;
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
constexpr std::int32_t NoShadowMap = 10;
// 0x5ffdf0: below 0.01f (0x6b88d4) it neither poses nor draws; in Chaos Castle, with the hero's +0x1a1, 0.15f less
// (0x6b8ae8) on each call.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
constexpr float ChaosCastleFade = std::bit_cast<float>(0x3e19999au);
// 0x50be04: the shadow from 0.3f (0x6b89dc) on.
constexpr float ShadowAlpha = std::bit_cast<float>(0x3e99999au);
// 0x5ffdf0..0x600135: the outline's constants.
constexpr float OutlineScale = std::bit_cast<float>(0x3f99999au);      // 1.2
constexpr float InnerOutlineScale = std::bit_cast<float>(0x3f8a3d71u); // 1.08
constexpr float MinimumOutline = std::bit_cast<float>(0x3f8147aeu);    // 1.01
constexpr float NpcOutlineStep = std::bit_cast<float>(0x3df5c28fu);    // 0.12 (0x6ba1d8)
constexpr float OutlineStep = std::bit_cast<float>(0x3d23d70au);       // 0.04 (0x6b8aec)
constexpr float OuterSpan = std::bit_cast<float>(0x3dcccccdu);         // 0.1 (0x6b87b8)
// 0x50b870 and 0x50b920: sin(T · 0.002f (0x6b89bc)) · 0.3f (0x6b89dc).
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);
constexpr float PulseSpan = std::bit_cast<float>(0x3e99999au);
constexpr std::uint16_t WideOutlineType = 0x172;
constexpr std::uint16_t KalimaGate = 0xc5;                                    // monsters 152 to 158 (0x514390)
constexpr auto ArmorPart = static_cast<std::size_t>(Kernel::BodyPart::Armor); // +0x238
constexpr std::uint32_t RiderFlag = 0x4000;

// The third argument of 0x602e90, which it passes to 0x5ffdf0 (0x5fff7f..0x5fffb7): with 1, the outline; with 2 and 3,
// without it, 0x6c881c at 1.2f (0x3f99999a) and 1.4f (0x3fb33333) for the last 0x4ebc80, the body's; with any other, 1.
// 0x6002e0 does not look at it.
enum class PoseMode : std::uint8_t
{
    Plain = 0,
    Outlined = 1,
    Wider = 2,
    Widest = 3,
};

float BoneScaleOf(PoseMode mode)
{
    if (mode == PoseMode::Wider)
        return std::bit_cast<float>(0x3f99999au);
    if (mode == PoseMode::Widest)
        return std::bit_cast<float>(0x3fb33333u);
    return 1.0f;
}

using Math::D;
using Math::F32;

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

template <std::size_t N> bool OneOf(std::uint16_t value, const std::array<std::uint16_t, N>& values)
{
    return std::ranges::find(values, value) != values.end();
}

Result Cut(std::string_view address, std::string_view what, std::uint16_t type)
{
    return Core::Unexpected(std::format("{}: {} (el tipo {:#x}) no está portado", address, what, type));
}

// 0x50bd05..0x50bd7e: the fourth argument of 0x602e90, the +0x376 if it is in the list and 0 otherwise.
std::uint16_t SpecialOf(std::uint16_t monster)
{
    constexpr std::array<std::uint16_t, 6> Listed{0x43, 0x4a, 0x4b, 0x87, 0x88, 0x89};
    if (OneOf(monster, Listed) || Within(monster, 0x12c, 0x12f) || Within(monster, 0x13a, 0x13f))
        return monster;
    return 0;
}

// 0x5ffce0: the model's color. Type 0x189 at 0.6 and type 0xe4 with the clock's sine do not write the per-vertex
// light (+0x44 of the model), which only the outlines set to 0: it stays off (Q-C859, *inferred*). The others, the
// terrain light (0x5f99e0) plus their own (+0xf4), with the per-vertex light, or a tenth of it without.
struct ModelLight
{
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

// FLD T; FMUL rate; FSIN and FMUL or FADD by a .data float: the sine kept with a single R53 (ADR0014). The frame clock
// is the ms since startup, finite and far from 2^63 / rate, and DrawGameHanging cuts in the same frame past 2^40:
// outside the profile, which is never reached, it is 0 (as in GameHanging).
double ClockSine(float worldClock, float rate, float factor)
{
    return Math::Detail::SineProductPC53(D(worldClock) * D(rate), factor).value_or(0.0);
}

double ClockSinePlus(float worldClock, float rate, float addend)
{
    return Math::Detail::SineSumPC53(D(worldClock) * D(rate), D(addend)).value_or(0.0);
}

ModelLight ModelLightOf(const Kernel::ActorState& actor, const Terrain::TerrainLighting& lighting, float worldClock)
{
    if (actor.type == 0x189)
        return {glm::vec3(std::bit_cast<float>(0x3f19999au)), false};
    if (actor.type == 0xe4)
    {
        const float shade = F32(ClockSinePlus(worldClock, std::bit_cast<float>(0x3b449ba6u), 1.0f) * 0.5);
        return {glm::vec3(shade), false};
    }
    const glm::vec3 terrain = Terrain::LightAt(lighting, actor.position.x, actor.position.y);
    if (actor.byteE8 != 0)
        return {{F32(D(terrain.x) + D(actor.light.x)), F32(D(terrain.y) + D(actor.light.y)),
                 F32(D(terrain.z) + D(actor.light.z))},
                true};
    const float tenth = OuterSpan;
    return {{F32(D(terrain.x) * D(tenth) + D(actor.light.x)), F32(D(F32(D(terrain.y) * D(tenth))) + D(actor.light.y)),
             F32(D(F32(D(terrain.z) * D(tenth))) + D(actor.light.z))},
            false};
}

// 0x5fffbd..0x600135: the two outline passes of 0x5ffdf0, with the NPC's colors (+0x88 at 4, outside Chaos Castle) or
// the others', and the scale of +0x94 or the object's (+0xc). 0x622150 is the larger of the two (on a tie, the second).
std::array<Actors::Drawing::ObjectOutlinePass, 2> OutlinePassesOf(const Kernel::ActorState& actor, std::int32_t map)
{
    const float scale94 = actor.float94;
    const auto atLeast = [](float value) { return value > MinimumOutline ? value : MinimumOutline; };
    if (!IsChaosCastle(map) && actor.byte88 == NpcKind)
    {
        return {{{scale94, Render::Color{std::bit_cast<float>(0x3ca3d70au), OuterSpan, 0.0f, 1.0f}},
                 {atLeast(F32(D(scale94) - D(NpcOutlineStep))),
                  Render::Color{std::bit_cast<float>(0x3e23d70au), std::bit_cast<float>(0x3f333333u), 0.0f, 1.0f}}}};
    }
    const bool fixed = std::bit_cast<std::uint32_t>(scale94) == std::bit_cast<std::uint32_t>(OutlineScale);
    float outer = actor.type == WideOutlineType ? OutlineScale : F32(D(OuterSpan) / D(actor.scale) + 1.0);
    if (!fixed)
        outer = scale94;
    float inner = actor.type == WideOutlineType ? InnerOutlineScale : F32(D(OutlineStep) / D(actor.scale) + 1.0);
    if (!fixed)
        inner = atLeast(F32(D(scale94) - D(OutlineStep)));
    return {{{outer, Render::Color{OuterSpan, std::bit_cast<float>(0x3c23d70au), 0.0f, 1.0f}},
             {inner, Render::Color{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3d8f5c29u), 0.0f, 1.0f}}}};
}

// The same height as a function for the body (MonsterBodyFrame::terrainHeight); empty without heights or attributes.
std::function<double(float, float)> BodyTerrainHeight(const Actors::Drawing::GameCharacterPass& pass)
{
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(pass.terrain);
    if (!terrain.heights || !terrain.attributes)
        return {};
    return [&terrain, fixed = pass.fixedHeight](float x, float y)
    { return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, fixed); };
}

// 0x4eb860 (0x4eb86d..0x4eb94c): the key it leaves in +0xa8 of the model, the frame (+0x114) through 0x69fd3c into a
// short, at 0 if negative or if it reaches the keys of the model's action (+0xa0, at 0 if the model does not have it).
// Without actions it does not touch it; with an infinite frame the original is not observed: neither does MuRet.
std::optional<std::int16_t> PoseKeyOf(const Models::Skeleton& skeleton, const Models::AnimationState& animation)
{
    const auto truncated = Math::OriginalFtolLow(D(animation.frame));
    if (skeleton.actions.empty() || !truncated)
        return std::nullopt;
    auto key = static_cast<std::int16_t>(*truncated);
    if (key < 0)
        key = 0;
    const std::size_t action = animation.action < skeleton.actions.size() ? animation.action : 0;
    if (key >= skeleton.actions[action].keyCount)
        key = 0;
    return key;
}

// What a monster draws in a frame: the character (the record or its copy), the rand() and the resources.
struct MonsterFrame
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& items;
    const Models::IModelLibrary& fauna;
    Kernel::WorldState& world;
    Kernel::ActorState& actor;
    const Actors::Drawing::GameCharacterPass& pass;
    Models::SkinningBuffers& buffers;
    Crt::CrtRandom& random;
    std::size_t slot = 0;
    std::int32_t map = 0;
    float worldClock = 0.0f;
    bool originalFrame = true;
    // The third argument of 0x50ba30 (0x510ea1: the slot in 0x6c8e30 or 0x6c8e2c): Outlined or Plain.
    PoseMode outline = PoseMode::Plain;
    // +0x6c..+0x74 of the type's model ([ebp−0x14], 0x50ba7f): what the last 0x5ffdf0 of this draw copied
    // (0x5ffe8b..0x5ffe9a; 0x18f and 0x18e, 60 and 120 more in y, 0x5ffea7..0x5ffeef) or, after the Blood Castle floor
    // (0x50be4d), the shadow's 0x60cf70 (0x50beb6, 0x60cfe8..0x60d015, already on the floor; 0x18e, 0x18f and 0xa8 to
    // 0xac do not call it and stay at the earlier z) and the 0x602e90 calls of 0x4d with its own type (0x50c20b,
    // 0x50c217; the one at 0x50c248 writes the one of the type + 1). 0x4ec110 translates with it the bone points of
    // what follows (0x50bff8, 0x50c3c1.., 0x50b8bf, 0x50b99a, 0x5107b4, 0x51096d) and the posed body's vertices are
    // repeated. Before the first copy (with the alpha below 0.01 it does not copy), the character's position: the
    // original reads the one another draw or the step left in the shared record.
    glm::vec3 modelOrigin{};
    MonstersState* monsters = nullptr;
};

void PublishMonsterPickingBox(MonsterFrame& frame, const glm::vec3& position)
{
    Kernel::ActorState& actor = frame.actor;
    const glm::vec3 max{actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop};
    const Actors::ActorPickingBox box = Actors::PickingBoxFor(position, actor.boundsMin, max);
    if (frame.slot < frame.pass.pickingBoxes.size())
        frame.pass.pickingBoxes[frame.slot] = box;
    else
        Actors::PublishPickingBox(actor, position, actor.boundsMin, max);
}

// 0x50bae7..0x50bbca: the cloth on the mesh (MonsterCloth.h) between 0x5ffdf0 and 0x6002e0, over `meshes` (what
// 0x4ebc80 left in 0x5786f9c) with `bones` and the model's position. Between original frames, the earlier one.
Result StepTheMeshCloth(MonsterFrame& frame, std::span<Models::SkinnedMesh> meshes,
                        std::span<const BoneTransform> bones, const glm::vec3& position)
{
    const Kernel::ActorState& actor = frame.actor;
    if (frame.slot >= frame.pass.cloths.size())
        return Core::Unexpected<std::string>("0x50bb96: falta la tela persistente del lugar del personaje");
    // object + 0x190: the slot's, in its cloths.
    Actors::Drawing::CharacterCloth& worn = frame.pass.cloths[frame.slot];
    Actors::Drawing::MonsterMeshCloth& cloth = worn.OnTheMesh();
    const Models::ClothBody body{.bones = bones, .angles = actor.angles, .scale = actor.scale, .position = position};
    if (!frame.originalFrame)
    {
        cloth.DrawCurrent(meshes, body);
        return {};
    }
    const Data::Models::Model* model = frame.fauna.Find(Models::ModelId{actor.type});
    if (model == nullptr)
        return Core::Unexpected(std::format("0x455ea0: falta el modelo {:#x} de la tela sobre la malla", actor.type));
    return cloth.Frame(*model, actor.type, meshes, body, *frame.pass.clothWorld, static_cast<std::uint8_t>(frame.map));
}

// 0x602e90(object, 1, mode, special): 0x5ffdf0 (with its 0x5ffce0) and 0x6002e0: the pose on its own bones, the outline
// (with Outlined) and the body, with the meshes at `mode`'s scale around their bones. With the cloth on the mesh
// (0x50bae7), 0x5ffdf0 and 0x6002e0 go apart with the cloth between them, with the fourth argument at 0.
Result DrawTheBody(MonsterFrame& frame, const Models::Skeleton& skeleton, std::uint16_t special, PoseMode mode)
{
    Kernel::ActorState& actor = frame.actor;
    const bool cloth = Actors::Drawing::WearsClothOnTheMesh(actor.word376);
    // 0x5ffdf0: in Chaos Castle, with the hero's +0x1a1, the stored alpha drops 0.15f.
    if (IsChaosCastle(frame.map) && frame.world.Hero().byte1a1 != 0)
        actor.alpha = F32(D(actor.alpha) - D(ChaosCastleFade));
    if (actor.alpha < DrawnAlpha)
    {
        if (!cloth)
            return {};
        // 0x50bafd: without a draw, the cloth is built and simulated anyway, with what the last 0x4ebc80 left in
        // 0x5786f9c, from another model; MuRet uses this one's stored pose (Q-C862, *inferred*).
        if (actor.bodyPose.empty())
            return Core::Unexpected<std::string>(
                "0x50bb3b: la tela sobre la malla de un monstruo que no se dibujó lee 0x5786f9c de otro modelo");
        Actors::Drawing::DrawnObject stale;
        stale.model = actor.type;
        stale.transform = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
        const std::span<Models::SkinnedMesh> meshes =
            Actors::Drawing::SkinObjectMeshesInto(frame.buffers.meshes, frame.fauna, stale, actor.bodyPose);
        return StepTheMeshCloth(frame, meshes, actor.bodyPose, actor.position);
    }
    // 0x5ffe9d..0x5ffeef: 0x18f and 0x18e go 60 and 120 further in y (0x6b86cc and 0x6b86c8).
    Models::ObjectTransform transform{.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    if (actor.type == 0x18f)
        transform.position.y = F32(D(actor.position.y) + 60.0);
    else if (actor.type == 0x18e)
        transform.position.y = F32(D(actor.position.y) + 120.0);
    // 0x5ffe8b..0x5ffeef: the record's origin, the one the tail's bone points read (also between original frames, on
    // the copy).
    frame.modelOrigin = transform.position;
    // 0x4ec110 copies the shared model's scale and origin before posing; not between original frames.
    if (frame.originalFrame)
        frame.world.PlaceModel(actor.type, Kernel::SharedModelPlacement{.scale = transform.scale,
                                                                        .origin = transform.position,
                                                                        .action = actor.animation.action});
    // 0x5ffef2: with an owner (+0x108) with +0x78 & 0x1000, the frame (+0x114) to 0. No spawn or packet writes the
    // +0x108 of a pool character: it stays at 0 (Q-C224, *inferred*).
    // 0x4eb860 on the own buffer (+0x120, which 0x511290 gave every character): the dummy bones stay as the previous
    // draw left them (Q-C318).
    std::vector<BoneTransform>& bones = frame.buffers.bones;
    bones.assign(skeleton.bones.size(), BoneTransform(0.0f));
    std::copy_n(actor.bodyPose.begin(), std::min(actor.bodyPose.size(), bones.size()), bones.begin());
    if (!Models::PoseSkeleton(skeleton, actor.animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::format("0x4eb860: la pose del modelo {:#x} falló", actor.type));
    actor.bodyPose = bones;
    // 0x4eb8b3..0x4eb94c: frame/key in +0xa4/+0xa8 of the shared model; the 0x1f5's step reads the key (0x4fce92).
    // Between original frames they are not stored.
    if (frame.originalFrame)
    {
        if (const auto key = PoseKeyOf(skeleton, actor.animation))
            frame.pass.faunaModels->RecordAnimation(actor.type, actor.animation.frame, *key);
    }
    if (frame.pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x5ffce0: falta el buffer dinámico de luz del terreno");
    const ModelLight light = ModelLightOf(actor, *frame.pass.dynamicLighting, frame.worldClock);
    Actors::Drawing::DrawnObject object;
    object.model = actor.type;
    object.animation = actor.animation;
    object.transform = transform;
    // +0x6c and +0x70 (the U and V): 0x511290 sets them to 0; 0x433b2f and the cases of 0x505de0 write them.
    object.state = Models::ObjectState{.blendMesh = static_cast<std::int16_t>(actor.dword64),
                                       .light = actor.blendMeshLight,
                                       .uv = {actor.textureU, actor.textureV},
                                       .hiddenMesh = static_cast<std::int16_t>(actor.dword58)};
    object.color = light.color;
    object.alpha = actor.alpha;
    object.vertexLight = light.vertexLight;
    // 0x5fffb4: the outline, except with +0x78 & 0x4000 (0x60cd74).
    if (mode == PoseMode::Outlined && (actor.flags78 & RiderFlag) == 0)
    {
        const auto passes = OutlinePassesOf(actor, frame.map);
        // 0x60cdd9: 0xc5 (the Kalima gates) only with its mesh 0, and with the +0x58 texture (0x60ce4e).
        if (actor.type == KalimaGate && actor.dword58 != -1)
            return Cut("0x60ce4e", "la textura +0x58 del contorno del 0xc5", actor.type);
        if (actor.type == KalimaGate)
            Actors::Drawing::DrawObjectOutlineOfTheFirstMesh(frame.batch, frame.fauna, object, frame.pass.frameClock,
                                                             bones, passes);
        else
            Actors::Drawing::DrawObjectOutline(frame.batch, frame.fauna, object, frame.pass.frameClock, bones, passes);
    }
    // 0x600177: the last 0x4ebc80 of 0x5ffdf0, with the 0x6c881c scale of the mode, leaves the meshes in 0x5786f9c.
    const std::span<Models::SkinnedMesh> meshes =
        Actors::Drawing::SkinObjectMeshesInto(frame.buffers.meshes, frame.fauna, object, bones, BoneScaleOf(mode));
    if (!(actor.alpha < DrawnAlpha))
        PublishMonsterPickingBox(frame, transform.position);
    if (cloth)
    {
        if (auto stepped = StepTheMeshCloth(frame, meshes, bones, transform.position); !stepped.HasValue())
            return stepped;
    }
    // 0x6002e0: with +0x58 at −2 (without the editor mode, 0x77f3b18) it draws nothing.
    if (actor.dword58 == -2)
        return {};
    // 0x600436..0x602e7d: the cases by type and by map, the body and what goes after it (MonsterBody.h).
    MonsterBodyModel model{.batch = frame.batch,
                           .models = frame.fauna,
                           .object = object,
                           .meshes = meshes,
                           .frameClock = frame.pass.frameClock,
                           .worldClock = frame.worldClock,
                           .boneScale = BoneScaleOf(mode)};
    MonsterBodyFrame body{.model = model,
                          .actor = actor,
                          .bones = bones,
                          .map = frame.map,
                          .special = special,
                          .worldClock = frame.worldClock,
                          .random = frame.random,
                          .cloth = *frame.pass.clothWorld,
                          .siegeActive = frame.world.Runtime().castleSiege.siegeActive,
                          .soundRequest = [&runtime = frame.world.Runtime()](Audio::SoundId sound)
                          { runtime.RequestSound(sound); },
                          .originalFrame = frame.originalFrame,
                          .cameraShake = frame.pass.cameraShake,
                          .effects = frame.pass.effects,
                          .effectHooks = frame.pass.effectHooks,
                          .particles = frame.pass.particles,
                          .terrainHeight = BodyTerrainHeight(frame.pass),
                          .joints = frame.originalFrame ? frame.world.DrawTail().joints : frame.pass.joints};
    return DrawMonsterBody(body);
}

// 0x50bed8..0x50bf3e: the 0x61e decal of +0x376 at 0xe8 and 0xe9, additive (0x620a90), with v = sin(T · 0.0015f) ·
// 0.3f + 0.8f on the x87 and the light (v · 0.5, v · 0.5, v): 0x5fb2f0(0x61e, x, y, 2.7f, 2.7f, &light, −turn z, 1, 5).
// Then +0x58 goes back to −1.
Result DrawTheFloorMark(MonsterFrame& frame)
{
    constexpr std::int32_t Mark = 0x61e;
    constexpr float Side = std::bit_cast<float>(0x402ccccdu); // 2.7
    constexpr float Rate = std::bit_cast<float>(0x3ac49ba6u); // 0x6b8a60: 0.0015
    constexpr float Span = std::bit_cast<float>(0x3e99999au); // 0x6b89dc: 0.3
    constexpr float Base = std::bit_cast<float>(0x3f4ccccdu); // 0x6b8a38: 0.8
    constexpr float Lift = std::bit_cast<float>(0x40a00000u); // 5
    Kernel::ActorState& actor = frame.actor;
    // 0x620a90: ONE / ONE, without culling or alpha test and without writing the depth.
    Render::ModelDrawState state = frame.batch.State();
    state.blend = Render::ModelBlend::Additive;
    state.cullEnabled = false;
    state.depthWrite = false;
    state.alphaTest = false;
    state.texture2d = true;
    constexpr std::array Operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const Render::ModelStateOperation operation : Operations)
        frame.batch.ApplyOperation(state, operation);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(frame.pass.terrain);
    if (!terrain.heights)
        return Core::Unexpected<std::string>("0x5fb2f0: faltan las alturas del terreno del calco del 0xe8 y el 0xe9");
    const double pulse = ClockSine(frame.worldClock, Rate, Span) + D(Base);
    const float half = F32(pulse * D(std::bit_cast<float>(0x3f000000u)));
    const Terrain::TerrainDecal decal{.slot = Mark,
                                      .x = actor.position.x,
                                      .y = actor.position.y,
                                      .width = Side,
                                      .height = Side,
                                      .color = {half, half, F32(pulse)},
                                      .angle = -actor.angles.z,
                                      .alpha = 1.0f,
                                      .lift = Lift};
    auto drawn = Terrain::DrawTerrainDecal(frame.batch, *terrain.heights, decal);
    actor.dword58 = -1;
    return drawn;
}

// 0x4ec110(own bones + bone · 0x30, offset, out, 1) of 0x50ba30 on [ebp−0x14]: the character's scale and the record's
// origin (MonsterFrame::modelOrigin: the shadow's, already on the floor, or 0x5ffdf0's without it).
glm::vec3 PointOnTheBone(const MonsterFrame& frame, std::size_t bone, const glm::vec3& offset)
{
    const Kernel::ActorState& actor = frame.actor;
    const BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, actor.scale, frame.modelOrigin);
}

// 0x50bf98..0x50c0f0, +0x376 at 0x45: x = (rand() % 30 + 70) · 0.01f on the x87; nine flare01 (0x5ac) of 0.6f with the
// light (x · 0.8f, x · 0.9f, x) on the bones of the 0x6c88a0 table, and three 0x53fe70(0x5c6, point, angles, light, 3,
// 1, 0) with the light (v · 0.6f, v · 0.7f, x · 0.8f), v = f(x), each on a random bone of the model (rand() % +0x22)
// offset by (rand() % 20 − 10) in x, y and z: four rand() before each. The offset stays in [ebp−0x50], which a monster
// does not read again before 0x50ea23. Between original frames it does not create the particles.
Result DrawTheSparksOf0x45(MonsterFrame& frame)
{
    constexpr std::array<std::size_t, 9> Bones{10, 18, 37, 38, 51, 52, 58, 59, 66};
    constexpr std::uint16_t Flare = 0x5ac;
    constexpr float FlareScale = std::bit_cast<float>(0x3f19999au); // 0.6
    constexpr std::int32_t Spark = 0x5c6;
    constexpr std::int32_t SparkSubtype = 3;
    constexpr int Sparks = 3;
    const Kernel::ActorState& actor = frame.actor;
    const auto point = [&frame](std::size_t bone, const glm::vec3& offset)
    { return PointOnTheBone(frame, bone, offset); };
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x50c014: falta la tabla de los sprites del frame (0x5491e0)");
    const double x = static_cast<double>(frame.random.Next() % 30 + 70) * D(std::bit_cast<float>(0x3c23d70au));
    const float v = F32(x);
    const float red = F32(x * D(std::bit_cast<float>(0x3f4ccccdu)));
    const glm::vec3 flareLight{red, F32(x * D(std::bit_cast<float>(0x3f666666u))), v};
    for (const std::size_t bone : Bones)
        static_cast<void>(
            frame.pass.sprites->Create(Flare, point(bone, glm::vec3(0.0f)), FlareScale, flareLight, 0.0f, 0));
    const glm::vec3 sparkLight{F32(D(v) * D(std::bit_cast<float>(0x3f19999au))),
                               F32(D(v) * D(std::bit_cast<float>(0x3f333333u))), red};
    const Models::Skeleton* skeleton = frame.fauna.SkeletonOf(Models::ModelId{actor.type});
    const auto bones = static_cast<std::int16_t>(skeleton != nullptr ? skeleton->bones.size() : 0);
    for (int spark = 0; spark < Sparks; ++spark)
    {
        glm::vec3 offset{};
        for (int axis = 0; axis < 3; ++axis)
            offset[axis] = static_cast<float>(static_cast<std::int32_t>(frame.random.Next() % 20) - 10);
        const std::int32_t drawn = frame.random.Next();
        if (bones <= 0)
            return Cut("0x50c0a9", "el hueso al azar de un modelo sin huesos (IDIV por 0) del +0x376 0x45", actor.type);
        const auto bone = static_cast<std::size_t>(drawn % bones);
        if (!frame.originalFrame)
            continue;
        if (frame.pass.particles == nullptr)
            return Core::Unexpected<std::string>("0x50c0db: falta el arreglo de las partículas (0x53fe70)");
        const auto created = frame.pass.particles->CreateChecked(Spark, point(bone, offset), actor.angles, sparkLight,
                                                                 SparkSubtype, 1.0f, frame.worldClock, frame.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x50c0db: la partícula {:#x}/{} del +0x376 0x45 falla con el error {}",
                                                Spark, SparkSubtype, static_cast<int>(created.Error())));
    }
    return {};
}

// 0x50c0fb..0x50c136, +0x376 at 0x46: one flare01 (0x5ac) of 0.8f on bone 20 (+0x3c0), with the offset of [ebp−0x50]
// (at 0 since the prologue) and the light of [ebp−0x44] (1, 1, 1 since 0x50bab9).
Result DrawTheFlareOf0x46(MonsterFrame& frame)
{
    constexpr std::size_t Bone = 20;
    constexpr std::uint16_t Flare = 0x5ac;
    constexpr float FlareScale = std::bit_cast<float>(0x3f4ccccdu); // 0.8
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x50c131: falta la tabla de los sprites del frame (0x5491e0)");
    static_cast<void>(frame.pass.sprites->Create(Flare, PointOnTheBone(frame, Bone, glm::vec3(0.0f)), FlareScale,
                                                 glm::vec3(1.0f), 0.0f, 0));
    return {};
}

// 0x60b130(model, object, type, +0x174, flags, factor, texture) with a monster's model. Without 0x10 and with a type
// that is neither 0x188 (0x60b15e) nor one of the items of 0x60b1dd..0x60b2cf, 0x608e00(type, +0x174, factor, +0x48,
// flags >> 8 & 1) leaves in the model's color k = +0x174 · factor (on the x87) times the type's color: the one of
// Models::ShineColorOf, except 0x16d with 0x100, which goes through case 8 of 0x60936c, (0.8k, 0.8k, k). Then
// RenderBody(flags, +0x174, +0x64, +0x68, +0x6c, +0x70, −1, texture), which does not look at 0x100.
Result DrawTheShinePass(MonsterFrame& frame, MonsterBodyModel& model, std::uint32_t flags, float factor,
                        std::int32_t texture)
{
    const Kernel::ActorState& actor = frame.actor;
    constexpr std::uint32_t Lit = 0x10;
    constexpr std::uint32_t SecondColor = 0x100;
    const auto special = [](std::uint16_t type)
    {
        constexpr std::array<std::uint16_t, 9> Items{0x188, 0x619, 0xc17, 0x226, 0x227, 0x231, 0x81c, 0xe22, 0x61a};
        return OneOf(type, Items) || Within(type, 0x61a, 0x61f);
    };
    if ((flags & Lit) != 0 || special(actor.type))
        return Cut("0x60b130", "el pase de brillo con el 0x10 o con un modelo de ítem", actor.type);
    const double k = D(actor.alpha) * D(factor);
    constexpr std::uint16_t Gorgon = 0x16d;
    const Render::Color base =
        (flags & SecondColor) != 0 && actor.type == Gorgon
            ? Render::Color{std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f4ccccdu), 1.0f, 1.0f}
            : Models::ShineColorOf(actor.type);
    model.object.color = {F32(k * D(base.red)), F32(k * D(base.green)), F32(k * D(base.blue))};
    BodyPassArgs args = ActorPassArgs(actor);
    args.hiddenMesh = -1;
    args.texture = texture;
    return DrawBodyPass(model, flags & ~SecondColor, args);
}

// 0x50c671..0x50c705: +0x376 at 0x26, 0x2b, 0x34, 0x3b, 0x43 and 0x4e to 0x53, with the factor at 1 (0.5 for 0x3b):
// 0x2b and 0x4e to 0x53 with pass 0x48 and then all with 0x44 (0x43, 0x144). 0x50c71b..0x50c74e: 0x35 and 0x36 with
// 0x48 and texture 0x5fd. 0x50c8ce..0x50c901: 0x87 with 0x140. All over the meshes of 0x5786f9c, which MuRet passes
// again to the stored pose (+0x120) with the position, angles and scale, like the shadow; if the monster never posed
// (the alpha below 0.01f since it arrived), the original draws them with another model's and a color under a hundredth,
// and MuRet does not draw (Q-C863). The model's +0x44 (the per-vertex light, which 0x140 looks at) is the one of
// 0x5ffce0 (*inferred*: the shadow does not touch it).
constexpr std::uint32_t ShinyPass = 0x48;
constexpr std::uint32_t ChromePass = 0x44;
constexpr std::int32_t OwnTexture = -1;

// The meshes of 0x5786f9c the passes of 0x60b130 read after the shadow (Q-C863): the stored pose (+0x120) passed again
// to the world with the position, angles and scale, like the shadow.
struct PosedBody
{
    Actors::Drawing::DrawnObject object;
    std::span<const Models::SkinnedMesh> meshes;
};

PosedBody PosedBodyOf(MonsterFrame& frame)
{
    const Kernel::ActorState& actor = frame.actor;
    PosedBody posed;
    Actors::Drawing::DrawnObject& object = posed.object;
    object.model = actor.type;
    object.animation = actor.animation;
    // The vertices of 0x5786f9c as the last 0x4ebc80 left them, with the record's origin (0x4ebeec): the shadow's
    // 0x60cf70 one (0x60d186, already on the floor) or, if it does not run (its guards, or 0x18e, 0x18f and 0xa8 to
    // 0xac), the one of 0x5ffdf0.
    object.transform = {.position = frame.modelOrigin, .angles = actor.angles, .scale = actor.scale};
    object.state = Models::ObjectState{.blendMesh = static_cast<std::int16_t>(actor.dword64),
                                       .light = actor.blendMeshLight,
                                       .uv = {actor.textureU, actor.textureV},
                                       .hiddenMesh = static_cast<std::int16_t>(actor.dword58)};
    object.alpha = actor.alpha;
    object.vertexLight = actor.byteE8 != 0 && actor.type != 0x189 && actor.type != 0xe4;
    posed.meshes =
        Actors::Drawing::SkinObjectMeshesInto(frame.buffers.posedMeshes, frame.fauna, object, actor.bodyPose);
    return posed;
}

Result DrawTheShines(MonsterFrame& frame)
{
    const Kernel::ActorState& actor = frame.actor;
    const std::uint16_t monster = actor.word376;
    if (actor.bodyPose.empty())
        return {};
    const PosedBody posed = PosedBodyOf(frame);
    MonsterBodyModel model{.batch = frame.batch,
                           .models = frame.fauna,
                           .object = posed.object,
                           .meshes = posed.meshes,
                           .frameClock = frame.pass.frameClock};
    constexpr std::uint32_t Shiny = ShinyPass;
    constexpr std::uint32_t Chrome = ChromePass;
    if (monster == 0x35 || monster == 0x36)
    {
        constexpr std::int32_t Flare = 0x5fd;
        return DrawTheShinePass(frame, model, Shiny, 1.0f, Flare);
    }
    if (monster == 0x87)
        return DrawTheShinePass(frame, model, 0x140, 1.0f, OwnTexture);
    const float factor = monster == 0x3b ? 0.5f : 1.0f;
    if (monster == 0x2b || Within(monster, 0x4e, 0x53))
    {
        if (auto first = DrawTheShinePass(frame, model, Shiny, factor, OwnTexture); !first.HasValue())
            return first;
    }
    return DrawTheShinePass(frame, model, monster == 0x43 ? 0x144 : Chrome, factor, OwnTexture);
}

// 0x50c36b..0x50c595, +0x376 at 0x49 and 0x4b, with the offset of [ebp−0x50] at 0. 0x49 (0x50c3ed..0x50c50e): fourteen
// flare01 (0x5ac) of 0.8f with the light (0.1f, 0.1f, 1) on bones 13 to 26 (+0x270..+0x4e0) and, from the previous
// bone's point ([ebp−0x8c]) to the current one's, the 0x614/7 joint without owner of width 20 (0x41a00000) with the
// actor's angles on bones 14, 15, 16 and 23 (+0x2a0..+0x300 and +0x450); then seven more flare01 on bones 52 to
// 58 (+0x9c0..+0xb10). 0x4b (0x50c37e..0x50c3e8): particle 0x5d9/0 of 0.3f (0x3e99999a) with the light (1, 1, 1) on
// bone 18 (+0x360). Between original frames they do not create the joints or the particle. Then 0x60b130 with pass 0x44
// (0x4b with 0x100: 0x50c51d..0x50c53c) over the stored pose, like MonsterShines; 0x49 goes on with 0x60b300
// (0x50c595), which cuts.
Result DrawTheChainsOf0x49And0x4b(MonsterFrame& frame)
{
    const Kernel::ActorState& actor = frame.actor;
    const std::uint16_t monster = actor.word376;
    constexpr std::uint16_t Chained = 0x49;
    if (monster == Chained)
    {
        constexpr std::uint16_t Flare = 0x5ac;
        constexpr float FlareScale = std::bit_cast<float>(0x3f4ccccdu); // 0.8
        const glm::vec3 light{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 1.0f};
        constexpr std::size_t FirstBone = 13;
        constexpr std::size_t LastBone = 26;
        constexpr std::size_t FirstLink = 14;
        constexpr std::size_t LastLink = 16;
        constexpr std::size_t LoneLink = 23;
        constexpr float LinkWidth = 20.0f;
        if (frame.pass.sprites == nullptr)
            return Core::Unexpected<std::string>("0x50c43b: falta la tabla de los sprites del frame (0x5491e0)");
        glm::vec3 previous{};
        for (std::size_t bone = FirstBone; bone <= LastBone; ++bone)
        {
            const glm::vec3 point = PointOnTheBone(frame, bone, glm::vec3(0.0f));
            static_cast<void>(frame.pass.sprites->Create(Flare, point, FlareScale, light, 0.0f, 0));
            const bool linked = (bone >= FirstLink && bone <= LastLink) || bone == LoneLink;
            if (linked && frame.originalFrame)
            {
                if (frame.pass.joints.empty())
                    return Core::Unexpected<std::string>("0x50c492: faltan los joints del frame (0x530860)");
                glm::vec3 from = previous;
                glm::vec3 to = point;
                glm::vec3 angles = actor.angles;
                const Kernel::JointRequest request{.type = 0x614,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = 7,
                                                   .ownerContext = nullptr,
                                                   .scale = LinkWidth,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
                if (auto made = Joints::CreateFaunaJoint(frame.pass.joints, request, frame.random); !made.HasValue())
                    return Core::Unexpected(std::format("0x50c492: {}", made.Error()));
            }
            previous = point;
        }
        constexpr std::size_t FirstTail = 52;
        constexpr std::size_t LastTail = 58;
        for (std::size_t bone = FirstTail; bone <= LastTail; ++bone)
            static_cast<void>(frame.pass.sprites->Create(Flare, PointOnTheBone(frame, bone, glm::vec3(0.0f)),
                                                         FlareScale, light, 0.0f, 0));
    }
    else if (frame.originalFrame)
    {
        constexpr std::size_t SparkBone = 18;
        constexpr float SparkScale = std::bit_cast<float>(0x3e99999au); // 0.3
        if (frame.pass.particles == nullptr)
            return Core::Unexpected<std::string>("0x50c3e0: falta el arreglo de las partículas (0x53fe70)");
        const auto created = frame.pass.particles->CreateChecked(
            Particles::FireParticle, PointOnTheBone(frame, SparkBone, glm::vec3(0.0f)), actor.angles, glm::vec3(1.0f),
            0, SparkScale, frame.worldClock, frame.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x50c3e0: la partícula 0x5d9/0 del +0x376 0x4b falla con el error {}",
                                                static_cast<int>(created.Error())));
    }
    if (!actor.bodyPose.empty())
    {
        const PosedBody posed = PosedBodyOf(frame);
        MonsterBodyModel model{.batch = frame.batch,
                               .models = frame.fauna,
                               .object = posed.object,
                               .meshes = posed.meshes,
                               .frameClock = frame.pass.frameClock};
        constexpr std::uint32_t SecondColor = 0x100;
        const std::uint32_t flags = monster == Chained ? ChromePass : ChromePass | SecondColor;
        if (auto shone = DrawTheShinePass(frame, model, flags, 1.0f, OwnTexture); !shone.HasValue())
            return shone;
    }
    if (monster == Chained)
        return Cut("0x50c595", "el pase 0x250 de 0x60b300 del +0x376 0x49", actor.type);
    return {};
}

// The grid cloth of a +0x376 of WearsAGridClothOfItsOwn (0x50c283..0x50c35c and 0x50c59f..0x50c669): the slot's
// (pass.cloths[slot]), built, simulated and drawn over the stored pose (+0x120) with CharacterCloth, and between
// original frames drawn as it was left. Without the slot's cloth, it cuts with `address`.
Result RunTheGridCloth(MonsterFrame& frame, std::string_view address)
{
    const Kernel::ActorState& actor = frame.actor;
    if (frame.slot >= frame.pass.cloths.size())
        return Core::Unexpected(std::format("{}: falta la tela persistente del lugar del personaje", address));
    Actors::Drawing::CharacterCloth& cloth = frame.pass.cloths[frame.slot];
    const Models::ClothBody body{
        .bones = actor.bodyPose, .angles = actor.angles, .scale = actor.scale, .position = actor.position};
    if (frame.originalFrame)
        cloth.FrameTheMonsterCloth(frame.batch, actor.word376, body, *frame.pass.clothWorld,
                                   static_cast<std::uint8_t>(frame.map), frame.worldClock);
    else
        cloth.DrawTheMonsterCloth(frame.batch, body, frame.worldClock);
    return {};
}

// 0x50c24d..0x50c366 with the type + 1 (0x18a): 0x602e90(object, 1, outline, 0); bones 23 and 14 of its pose to
// 0x743748c and 0x74374bc (only in the original's frame); and the 0x4d cloth over that pose, the slot's
// (pass.cloths[slot]), which between original frames is drawn as it was left.
Result DrawTheSecondPhoenixModel(MonsterFrame& frame)
{
    constexpr std::size_t FirstBone = 23;
    constexpr std::size_t SecondBone = 14;
    Kernel::ActorState& actor = frame.actor;
    const Models::Skeleton* skeleton = frame.fauna.SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x50c248: falta el modelo {:#x} del +0x376 0x4d", actor.type));
    if (auto drawn = DrawTheBody(frame, *skeleton, 0, frame.outline); !drawn.HasValue())
        return drawn;
    if (actor.bodyPose.size() <= FirstBone)
        return Cut("0x50c256", "el hueso 23 de una pose más corta (el buffer de +0x120 fuera del modelo)", actor.type);
    if (frame.originalFrame)
    {
        if (frame.monsters == nullptr)
            return Core::Unexpected(std::string("0x50c177: no Monsters::MonstersState for the matrices at 0x743748c"));
        frame.monsters->phoenixBones[0] = actor.bodyPose[FirstBone];
        frame.monsters->phoenixBones[1] = actor.bodyPose[SecondBone];
    }
    return RunTheGridCloth(frame, "0x50c283");
}

// 0x50c177..0x50c366, +0x376 at 0x4d (monster 77: type 0x189, which the spawn loads with its second model, 0x18a,
// Monster57): s = f((sin((ftol(T) % 10000) · 0.001f) + 1) · 0.5f), without rounding the sine before the sum; pass 0x44
// of 0x60b130 with the factor f(s · 0.7f + 0.3f), over the stored pose like those of MonsterShines; +0x64 at 0 and
// +0x68 at f((1 − s + 1) · 0.3f), stored; 0x602e90(object, 1, 2, 0) and (object, 1, 3, 0), the body again at 1.2 and
// 1.4 around its bones, without outline; bone 24 of the pose to 0x74374ec (in the original's frame); +0x64 at −1; and
// the type + 1 (DrawTheSecondPhoenixModel), which then goes back. It takes no rand() of its own: only the cloth's and
// those of each 0x6002e0.
Result DrawThePhoenix(MonsterFrame& frame)
{
    constexpr std::int32_t Period = 10000;
    constexpr std::size_t TailBone = 24;
    constexpr float Rate = std::bit_cast<float>(0x3a83126fu);   // 0x6b89c8: 0.001
    constexpr float Weight = std::bit_cast<float>(0x3f333333u); // 0x6b8a5c: 0.7
    constexpr float Floor = std::bit_cast<float>(0x3e99999au);  // 0x6b89dc: 0.3
    Kernel::ActorState& actor = frame.actor;
    const auto clock = Math::OriginalFtolLow(D(frame.worldClock));
    if (!clock)
        return Core::Unexpected<std::string>("0x50c17d: el reloj del mundo no cabe en 0x69fd3c");
    const std::optional<double> wave = Math::Detail::SineSumPC53(static_cast<double>(*clock % Period) * D(Rate), 1.0);
    if (!wave)
        return Core::Unexpected<std::string>("0x50c1b7: el seno del reloj no cabe en el perfil PC53");
    const float s = F32(*wave * 0.5);
    if (!actor.bodyPose.empty())
    {
        const PosedBody posed = PosedBodyOf(frame);
        MonsterBodyModel model{.batch = frame.batch,
                               .models = frame.fauna,
                               .object = posed.object,
                               .meshes = posed.meshes,
                               .frameClock = frame.pass.frameClock};
        if (auto shone = DrawTheShinePass(frame, model, ChromePass, F32(D(s) * D(Weight) + D(Floor)), OwnTexture);
            !shone.HasValue())
            return shone;
    }
    actor.dword64 = 0;
    actor.blendMeshLight = F32((1.0 - D(s) + 1.0) * D(Floor));
    const Models::Skeleton* skeleton = frame.fauna.SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x50c20b: falta el modelo {:#x} del +0x376 0x4d", actor.type));
    for (const PoseMode mode : {PoseMode::Wider, PoseMode::Widest})
    {
        if (auto drawn = DrawTheBody(frame, *skeleton, 0, mode); !drawn.HasValue())
            return drawn;
    }
    if (actor.bodyPose.size() <= TailBone)
        return Cut("0x50c227", "el hueso 24 de una pose más corta (el buffer de +0x120 fuera del modelo)", actor.type);
    if (frame.originalFrame)
    {
        if (frame.monsters == nullptr)
            return Core::Unexpected(std::string("0x50c177: no Monsters::MonstersState for the matrix at 0x74374ec"));
        frame.monsters->phoenixBones[2] = actor.bodyPose[TailBone];
    }
    actor.dword64 = -1;
    // 0x50c244..0x50c34b: INC and DEC of +0x2 around the second model and the cloth.
    const std::uint16_t type = actor.type;
    actor.type = static_cast<std::uint16_t>(type + 1);
    Result second = DrawTheSecondPhoenixModel(frame);
    actor.type = type;
    return second;
}

// What 0x509340 reads from the frame of 0x50ba30, with the library `models`.
Actors::Drawing::GameHangingPass HangingPassOf(MonsterFrame& frame, const Models::IModelLibrary& models)
{
    const Actors::Drawing::GameCharacterPass& pass = frame.pass;
    return Actors::Drawing::GameHangingPass{.batch = frame.batch,
                                            .models = models,
                                            .body = frame.actor.bodyPose,
                                            .random = frame.random,
                                            .carry = pass.poseCarry,
                                            .sprites = pass.sprites,
                                            .joints = pass.joints,
                                            .jointHooks = pass.hooks,
                                            .particles = pass.particles,
                                            .itemContext = pass.itemContext,
                                            .frameClock = pass.frameClock,
                                            .worldClock = frame.worldClock,
                                            .map = frame.map,
                                            .heroByte1a1 = frame.world.Hero().byte1a1,
                                            .siegeActive = frame.world.Runtime().castleSiege.siegeActive,
                                            .logicSteps = pass.clothWorld->logicSteps,
                                            .originalFrame = frame.originalFrame,
                                            .modelOrigin = frame.modelOrigin};
}

// 0x50c762..0x50c826 and 0x50c845..0x50c857: the wings slot (+0x2f8) with the model, the bone, the action and the
// previous one at 1 and the speed at 0.2f (0x3e4ccccd).
void ArmTheWingsSlot(Kernel::ActorState& actor, std::uint16_t model, std::uint8_t bone)
{
    Kernel::ActorItemPart& wings = actor.wings;
    wings.model = model;
    wings.bone = bone;
    wings.animation.action = 1;
    wings.animation.previousAction = 1;
    wings.speed = std::bit_cast<float>(0x3e4ccccdu);
}

// 0x50c762..0x50c82c: +0x376 at 0x2a hangs from the wings slot 0xef (BossHead01) from bone 9 with (0, 0, −40) and 0xf0
// (Princess01) from 0x3d with (0, −40, 45), the latter with the light of what hangs (+0x3b4) at 1 and the scale at 0.9f
// (0x3f666666), which are restored afterwards. Both are objects of map 5 (Object6, 0x617325): in another map, whatever
// is in those table slots, and with nothing, only the rand() of 0x509340 (Q-C102). They go with the fauna library,
// which falls back to the map's and the global one, like the original's single table.
Result HangTheCarriedModels(MonsterFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    const float scale = actor.scale;
    ArmTheWingsSlot(actor, 0xef, 9);
    if (auto hung = Actors::Drawing::HangFromTheWingsSlot(actor, HangingPassOf(frame, frame.fauna),
                                                          {0.0f, 0.0f, -40.0f}, false, scale);
        !hung.HasValue())
        return hung;
    // 0x50c79f: the model and the bone; the actions and speed stay as 0xef left them.
    actor.wings.model = 0xf0;
    actor.wings.bone = 0x3d;
    const glm::vec3 light = actor.drawnLight;
    actor.drawnLight = glm::vec3(1.0f);
    actor.scale = std::bit_cast<float>(0x3f666666u);
    auto hung = Actors::Drawing::HangFromTheWingsSlot(actor, HangingPassOf(frame, frame.fauna), {0.0f, -40.0f, 45.0f},
                                                      false, scale);
    actor.drawnLight = light;
    actor.scale = scale;
    return hung;
}

// 0x50c83b..0x50c8c6: the Blood Castle statues (+0x376 from 0x84 to 0x86) hang from bone 1, with the flag (PlacementOf
// of 0x509340), 0xc1c, 0x225 or 0xa24 at scale 0.7f (0x3f333333; 0xa24, 0.9f), which is restored afterwards.
Result HangTheStatueWeapon(MonsterFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    const float scale = actor.scale;
    constexpr std::array<std::uint16_t, 3> Weapons{0xc1c, 0x225, 0xa24};
    ArmTheWingsSlot(actor, Weapons[static_cast<std::size_t>(actor.word376 - 0x84)], 1);
    actor.scale = actor.word376 == 0x86 ? std::bit_cast<float>(0x3f666666u) : std::bit_cast<float>(0x3f333333u);
    auto hung = Actors::Drawing::HangFromTheWingsSlot(actor, HangingPassOf(frame, frame.fauna), {}, true, scale);
    actor.scale = scale;
    return hung;
}

// 0x50bf45..0x50c904: what is added by +0x376. 0x45 with its particles, 0x46, 0x4d, the cloths of 0x47, 0x4a and 0x12d
// (0x50c59f, with any type: all three are 0x186's), the shine passes (MonsterShines), what 0x2a and the statues 0x84 to
// 0x86 hang, and the chains of 0x49 and 0x4b (0x49 cuts at 0x60b300); the others cut.
Result DrawTheDecorations(MonsterFrame& frame)
{
    const std::uint16_t monster = frame.actor.word376;
    if (monster == 0x45)
        return DrawTheSparksOf0x45(frame);
    if (monster == 0x46)
        return DrawTheFlareOf0x46(frame);
    if (monster == 0x4d)
        return DrawThePhoenix(frame);
    if (monster == 0x47 || monster == 0x4a || monster == 0x12d)
        return RunTheGridCloth(frame, "0x50c59f");
    if (MonsterShines(frame.actor.word376))
        return DrawTheShines(frame);
    if (monster == 0x2a)
        return HangTheCarriedModels(frame);
    if (Within(monster, 0x84, 0x86))
        return HangTheStatueWeapon(frame);
    if (monster == 0x49 || monster == 0x4b)
        return DrawTheChainsOf0x49And0x4b(frame);
    return Actors::Drawing::CheckMonsterDecorations(frame.actor.word376);
}

// 0x50bd89..0x50bf3e
Result DrawTheShadow(MonsterFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    constexpr std::array<std::uint16_t, 7> Unshadowed{0x19, 0x16, 0x2a, 0xf2, 0x3b, 0x3f, 0x98};
    if (actor.byte88 == UnwalledKind || OneOf(actor.word376, Unshadowed) || frame.map == NoShadowMap ||
        actor.alpha < ShadowAlpha)
        return {};
    // 0x50be15..0x50be52: in Blood Castle, with +0x1a1 and dying, the z drops to the terrain's (also with NaN).
    const auto grounded = Actors::Drawing::ShadowGroundOf(frame.pass, actor, frame.map, 0x50be3b);
    if (!grounded.HasValue())
        return Core::Unexpected(grounded.Error());
    actor.position.z = grounded.Value();
    const bool marked = actor.word376 == 0xe8 || actor.word376 == 0xe9;
    if (marked)
        actor.dword58 = 2;
    // 0x50be6e..0x50beb6: no shadow for 0x18e, 0x18f and 0xa8 to 0xac; the others, 0x60cf70(object, type, ...) with
    // +0x98 at 1.
    if (actor.type != 0x18e && actor.type != 0x18f && !Within(actor.type, 0xa8, 0xac))
    {
        // 0x60cfe8..0x60d015: before 0x60b3d0 and without looking at its guards, the scale (+0xc → +0x68, the
        // character's, the same 0x5ffdf0 copied) and the position already on the floor (+0x10..+0x18 → +0x6c..+0x74) go
        // to the type's record, the same [ebp−0x14] (0x510e9f: the object is the character), and 0x4ebc80 (0x60d186)
        // passes the meshes of 0x5786f9c again with that origin: the tail reads it. Between original frames, on the
        // copy.
        frame.modelOrigin = actor.position;
        // 0x60d44b → 0x60b3d0 with +0x98 at 1, without +0x1bc at 3 (MuRet does not have it: 0, *inferred*), without
        // +0x78 & 0x4000 and with +0x92.
        if ((actor.flags78 & RiderFlag) == 0 && actor.byte92 != 0 && actor.bodyPose.size() > 0)
        {
            Actors::Drawing::DrawnObject object;
            object.model = actor.type;
            object.transform = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
            object.state.blendMesh = static_cast<std::int16_t>(actor.dword64);
            object.state.hiddenMesh = static_cast<std::int16_t>(actor.dword58);
            const auto meshes =
                Actors::Drawing::SkinObjectMeshesInto(frame.buffers.shadowMeshes, frame.fauna, object, actor.bodyPose);
            Actors::Drawing::DrawObjectShadow(frame.batch, frame.fauna, object, meshes, frame.map);
        }
    }
    if (marked)
        return DrawTheFloorMark(frame);
    return {};
}

// 0x60cf70 with its mode 1, on a character that is not 0x20a: the two 0x60cd60 passes of each part
// (0x60d056..0x60d14c), with the scale of +0x94 and the colors by +0x88 (with 4, the NPC's: 0x60d069 and 0x60d0dd) and
// the second 0.02f (0x6b8b90) smaller; in maps 18 to 23, other colors and the scales by the object's (+0xc): 0.1 /
// scale + 1 stored as a float and 0.04 / scale + 1.02 − 0.02 on the x87. They are not the body's (OutlinePassesOf,
// 0x5ffdf0).
std::array<Actors::Drawing::ObjectOutlinePass, 2> PartOutlinePassesOf(const Kernel::ActorState& actor, std::int32_t map)
{
    constexpr float InnerStep = std::bit_cast<float>(0x3ca3d70au); // 0.02 (0x6b8b90)
    if (IsChaosCastle(map))
    {
        const float outer = F32(D(OuterSpan) / D(actor.scale) + 1.0);
        const double inner = D(OutlineStep) / D(actor.scale) + D(std::bit_cast<float>(0x3f828f5cu)); // 1.02 (0x6ba764)
        return {{{outer, Render::Color{OuterSpan, std::bit_cast<float>(0x3c23d70au), 0.0f, 1.0f}},
                 {F32(inner - D(InnerStep)),
                  Render::Color{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3d8f5c29u), 0.0f, 1.0f}}}};
    }
    const float inner = F32(D(actor.float94) - D(InnerStep));
    if (actor.byte88 == NpcKind)
        return {
            {{actor.float94, Render::Color{std::bit_cast<float>(0x3ca3d70au), OuterSpan, 0.0f, 1.0f}},
             {inner, Render::Color{std::bit_cast<float>(0x3e23d70au), std::bit_cast<float>(0x3f333333u), 0.0f, 1.0f}}}};
    return {{{actor.float94, Render::Color{OuterSpan, std::bit_cast<float>(0x3cf5c28fu), 0.0f, 1.0f}},
             {inner, Render::Color{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3e4ccccdu), 0.0f, 1.0f}}}};
}

// 0x60cf70(c, model, part, &c+0x3b4, c+0x174, level · 8, +3, +4, 0, 0, 1, mode, 2): the owner is the actor itself.
Actors::Drawing::ItemPartOwner PartOwnerOf(MonsterFrame& frame,
                                           std::span<const Actors::Drawing::ObjectOutlinePass> outline)
{
    Kernel::ActorState& actor = frame.actor;
    Actors::Drawing::ItemPartOwner owner;
    owner.type = actor.type;
    owner.transform = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    owner.bones = actor.bodyPose;
    owner.light = actor.drawnLight;
    owner.alpha = actor.alpha;
    owner.hiddenMesh = static_cast<std::int16_t>(actor.dword58);
    owner.blendMesh = static_cast<std::int16_t>(actor.dword64);
    owner.blendMeshLight = &actor.blendMeshLight;
    owner.storedLight = &actor.light;
    owner.uv = {actor.textureU, actor.textureV};
    owner.stateFlags = actor.flags78;
    owner.vertexLight = actor.byteE8 != 0;
    if (frame.outline == PoseMode::Outlined)
        owner.outline = outline;
    owner.boneScale = BoneScaleOf(frame.outline);
    return owner;
}

// 0x50d363..0x50d4f5: the parts of a character that is not the 0x20a, without +0x3e3 (0x50d36e). In Chaos Castle (18
// to 23) with +0x88 at 1, the Angel (0x1f7) instead of the parts (0x50d383): it cuts. The head keeps no level or
// options (+0x1fa..+0x1fc): 0x511290 zeroes them and only a name over 24 letters overwrites them (P-C352). The guild
// mark that follows (0x50d509..0x50d588) needs the 0x20a.
Result DrawTheParts(MonsterFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    if (actor.byte3e3 != 0)
        return {};
    if (actor.byte88 == PlayerKind && IsChaosCastle(frame.map))
        return Cut("0x50d383", "el Angel de Chaos Castle en un personaje que no es un jugador", actor.type);
    Actors::Drawing::WornParts parts{};
    if (actor.head != Kernel::NoItemModel)
        parts[0] = Actors::Drawing::ItemPart{.model = actor.head};
    for (std::size_t k = 0; k < actor.body.size(); ++k)
    {
        const Kernel::ActorItemPart& part = actor.body[k];
        if (part.model != Kernel::NoItemModel)
            parts[k + 1] = Actors::Drawing::ItemPart{
                .model = part.model, .level = part.level, .excellent = part.flags1b, .ancient = part.flags1c};
    }
    const std::array<Actors::Drawing::ObjectOutlinePass, 2> outline = PartOutlinePassesOf(actor, frame.map);
    const Actors::Drawing::ItemPartOwner owner = PartOwnerOf(frame, outline);
    Actors::Drawing::ItemDrawContext resources =
        frame.pass.itemContext != nullptr ? *frame.pass.itemContext : Actors::Drawing::ItemDrawContext{};
    Actors::Drawing::ItemSpecialDrawContext special =
        resources.special != nullptr ? *resources.special : Actors::Drawing::ItemSpecialDrawContext{};
    special.flags = 2;
    special.worldClock = frame.worldClock;
    special.random = &frame.random;
    special.sprites = frame.pass.sprites;
    special.objectFlags = static_cast<std::uint32_t>(frame.outline);
    resources.special = &special;
    resources.map = frame.map;
    resources.originalFrame = frame.originalFrame;
    resources.ownerContext = &actor;
    return Actors::Drawing::DrawWornParts(frame.batch, frame.fauna, owner, parts, actor.characterClass,
                                          frame.pass.frameClock, resources, [&frame](const glm::vec3& position)
                                          { PublishMonsterPickingBox(frame, position); });
}

// 0x50c929..0x50c96e and 0x50d58b..0x50db57: on a character that is not 0x20a, bVar23 is only +0x376 at 0x37 outside
// maps 0x12 to 0x17; with it, the block of CharacterCloth's cloths over the own bones (+0x120) with the white of
// 0x50c979 (+0x1f2 at 0xffff). Between original frames, the earlier ones.
Result RunTheCloth(MonsterFrame& frame)
{
    constexpr std::uint16_t ClothFlare = 0x5ac; // flare01, additive (0x5491e0 with 0)
    const Kernel::ActorState& actor = frame.actor;
    const auto map = static_cast<std::uint8_t>(frame.map);
    const bool wears = Actors::Drawing::WearsTheMonsterCloth(actor.word376, map);
    if (!wears)
        return {};
    if (frame.slot >= frame.pass.cloths.size())
        return Core::Unexpected<std::string>("0x50d58b: falta la tela persistente del lugar del personaje");
    Actors::Drawing::CharacterCloth& cloth = frame.pass.cloths[frame.slot];
    const Actors::Drawing::ClothWearer wearer{.characterClass = actor.characterClass,
                                              .armor = actor.body[ArmorPart].model,
                                              .wings = actor.wings.model == Kernel::NoItemModel
                                                           ? std::nullopt
                                                           : std::optional<std::uint16_t>(actor.wings.model),
                                              .monster = actor.word376,
                                              .objectFlags = actor.flags78};
    const Models::ClothBody body{
        .bones = actor.bodyPose, .angles = actor.angles, .scale = actor.scale, .position = actor.position};
    if (frame.originalFrame)
        cloth.Frame(frame.batch, wearer, body, *frame.pass.clothWorld, map, frame.worldClock, wears);
    else
        cloth.DrawCurrent(frame.batch, wearer, body, frame.pass.frameClock, wears);
    if (frame.pass.sprites != nullptr)
    {
        for (const Models::ClothFlare& flare : cloth.Flares())
            static_cast<void>(
                frame.pass.sprites->Create(ClothFlare, flare.position, flare.scale, glm::vec3(flare.light), 0.0f, 0));
    }
    return {};
}

// 0x50b870(object, sprite, scale, bone, x, y, z): the own bone's point with the character's scale and the model's
// position (0x50b8bf, MonsterFrame::modelOrigin) and the additive sprite with the light sin(T · 0.002f) · 0.3f + 0.7f,
// times 0.6f and 0.4f in green and blue (with the light on the x87 stack).
void BoneSprite(const MonsterFrame& frame, std::uint16_t texture, float scale, std::size_t bone,
                const glm::vec3& offset)
{
    const glm::vec3 point = PointOnTheBone(frame, bone, offset);
    const double lum = ClockSine(frame.worldClock, PulseRate, PulseSpan) + D(std::bit_cast<float>(0x3f333333u));
    const glm::vec3 light{F32(lum), F32(lum * D(std::bit_cast<float>(0x3f19999au))),
                          F32(lum * D(std::bit_cast<float>(0x3ecccccdu)))};
    static_cast<void>(frame.pass.sprites->Create(texture, point, scale, light, 0.0f, 0));
}

// 0x50b920(object, bone a, bone b, scale): the 0x5ff at (5, 0, 0) of bone a and at (−5, 0, 0) of b, with the light
// sin(T · 0.002f) · 0.3f + 0.8f stored; the last point stays in +0x40..+0x48.
void PairOfSprites(MonsterFrame& frame, std::size_t first, std::size_t second, float scale)
{
    Kernel::ActorState& actor = frame.actor;
    const float lum = F32(ClockSine(frame.worldClock, PulseRate, PulseSpan) + D(std::bit_cast<float>(0x3f4ccccdu)));
    // 0x50b99a and 0x50b9f1: 0x4ec110 with the type's model.
    const auto at = [&frame](std::size_t bone, float x) { return PointOnTheBone(frame, bone, {x, 0.0f, 0.0f}); };
    constexpr std::uint16_t Glow = 0x5ff;
    static_cast<void>(frame.pass.sprites->Create(Glow, at(first, 5.0f), scale, glm::vec3(lum), 0.0f, 0));
    const glm::vec3 last = at(second, -5.0f);
    static_cast<void>(frame.pass.sprites->Create(Glow, last, scale, glm::vec3(lum), 0.0f, 0));
    actor.effectOrigin = last;
}

struct BoneSpriteCall
{
    std::uint16_t texture = 0;
    float scale = 1.0f;
    std::uint8_t bone = 0;
    float z = 0.0f;
};

void BoneSprites(const MonsterFrame& frame, std::span<const BoneSpriteCall> calls)
{
    for (const BoneSpriteCall& call : calls)
        BoneSprite(frame, call.texture, call.scale, call.bone, {0.0f, 0.0f, call.z});
}

// 0x510763..0x5108b1, type 0x1d9: the point (3.5, −12, 10) of bone 20, two 0x5d5 at 0.3 with the light sin(T · 0.002f)
// · 0.35f (0x6b8bc4) + 0.65f (0x6b8bc0) stored and turned T · 0.02f (0x6b89e4) and −T · 0.02f, and with rand() % 30 at
// 0, three rand() % 100 plus the point minus 50 (0x6b86b0), in x, y and z, for the 0x618/6 joint (0x530860) from there
// towards the point, with the object's angles, without owner and of width 20.
Result DrawTheFairy(MonsterFrame& frame)
{
    const Kernel::ActorState& actor = frame.actor;
    constexpr std::size_t Bone = 20;
    constexpr std::uint16_t Flare = 0x5d5;
    // 0x510780..0x5107b4: 0x4ec110 with [ebp−0x14].
    glm::vec3 point = PointOnTheBone(frame, Bone, {3.5f, -12.0f, 10.0f});
    const float lum = F32(ClockSine(frame.worldClock, PulseRate, std::bit_cast<float>(0x3eb33333u)) +
                          D(std::bit_cast<float>(0x3f266666u)));
    const float spin = std::bit_cast<float>(0x3ca3d70au);
    const float scale = std::bit_cast<float>(0x3e99999au);
    static_cast<void>(
        frame.pass.sprites->Create(Flare, point, scale, glm::vec3(lum), F32(D(frame.worldClock) * D(spin)), 0));
    static_cast<void>(
        frame.pass.sprites->Create(Flare, point, scale, glm::vec3(lum), F32(-D(frame.worldClock) * D(spin)), 0));
    if (frame.random.Next() % 30 != 0)
        return {};
    const auto scatter = [&frame](float axis)
    {
        constexpr double Spread = 50.0;
        return F32(static_cast<double>(frame.random.Next() % 100) + D(axis) - Spread);
    };
    glm::vec3 from{};
    from.x = scatter(point.x);
    from.y = scatter(point.y);
    from.z = scatter(point.z);
    if (frame.pass.joints.empty())
        return Core::Unexpected<std::string>("0x5108a9: faltan los joints del frame (0x530860)");
    glm::vec3 angles = actor.angles;
    constexpr std::int32_t FairyJoint = 0x618;
    constexpr std::int32_t FairySubtype = 6;
    constexpr float Width = 20.0f;
    const auto created = Joints::CreateFaunaJoint(
        frame.pass.joints, {FairyJoint, from, point, angles, FairySubtype, nullptr, Width, -1, 0, 0, -1}, frame.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x5108a9: {}", created.Error()));
    return {};
}

// What the helpers of KalimaSiegeHelpers.h read: the frame's pass and, in 0x42a1b0, the pass's terrain heights (without
// it, none). The sound, only in the original's frame.
KalimaSiegeHelperPass HelperPassOf(MonsterFrame& frame)
{
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(frame.pass.terrain);
    return {.batch = frame.batch,
            .actor = frame.actor,
            .random = frame.random,
            .sprites = frame.pass.sprites,
            .particles = frame.pass.particles,
            .joints = frame.pass.joints,
            .effects = frame.pass.effects,
            .kalima = frame.pass.kalima,
            .cameraShake = frame.pass.cameraShake,
            .heights = terrain.heights.get(),
            .soundRequest =
                frame.originalFrame
                    ? std::function<void(Audio::SoundId)>{[&runtime = frame.world.Runtime()](Audio::SoundId sound)
                                                          { runtime.RequestSound(sound); }}
                    : std::function<void(Audio::SoundId)>{},
            .worldClock = frame.worldClock,
            .map = frame.map,
            .siegeActive = frame.world.Runtime().castleSiege.siegeActive,
            .modelOrigin = frame.modelOrigin};
}

// 0x50e9fa..0x510a1e: the switch by model (the 0x510b80 table over 0x510b50). Returns true if the type has its case,
// which skips the 0x510a1e chain.
Core::Expected<bool, std::string> DrawTheModelCase(MonsterFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    constexpr std::array<std::uint16_t, 11> Cased{0x152, 0x170, 0x186, 0x177, 0x174, 0x176,
                                                  0x173, 0x1d8, 0x1f2, 0x1d9, 0x16e};
    if (!OneOf(type, Cased))
        return false;
    if (type == 0x16e)
    {
        // 0x5108b6 (KalimaSiegeHelpers.h), with the model's bones.
        const Models::Skeleton* skeleton = frame.fauna.SkeletonOf(Models::ModelId{type});
        if (skeleton == nullptr)
            return Core::Unexpected(std::format("0x5108b6: falta el modelo {:#x}", type));
        if (auto sparks = DrawTheBoneSparks(HelperPassOf(frame), *skeleton); !sparks.HasValue())
            return Core::Unexpected(sparks.Error());
        return true;
    }
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x5491e0: falta la tabla de sprites del frame");
    constexpr std::uint16_t Flare = 0x5d5, Shine = 0x5fe, Spark = 0x5c5, Star = 0x5ac;
    switch (type)
    {
    case 0x152:
    case 0x170:
        // 0x5104d0: type 0x152 only with +0x1d6 at 1.
        if (type == 0x170 || frame.actor.level == 1)
            PairOfSprites(frame, 0x16, 0x17, 1.0f);
        break;
    case 0x186:
        PairOfSprites(frame, 0x1a, 0x1b, 2.0f);
        break;
    case 0x177:
    {
        constexpr std::array<BoneSpriteCall, 2> Calls{{{Flare, 1.0f, 0x3f, 20.0f}, {Shine, 4.0f, 0x3f, 20.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x174:
    {
        constexpr std::array<BoneSpriteCall, 6> Calls{{{Flare, 0.5f, 0x1e, -5.0f},
                                                       {Flare, 0.5f, 0x27, -5.0f},
                                                       {Spark, 4.0f, 0x1e, -5.0f},
                                                       {Spark, 4.0f, 0x27, -5.0f},
                                                       {Shine, 2.0f, 0x1e, -5.0f},
                                                       {Shine, 2.0f, 0x27, -5.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x176:
    {
        PairOfSprites(frame, 0x2a, 0x2b, 1.0f);
        constexpr std::array<BoneSpriteCall, 8> Calls{{{Spark, 2.0f, 0x1a, 0.0f},
                                                       {Spark, 2.0f, 0x1f, 0.0f},
                                                       {Spark, 2.0f, 0x24, 0.0f},
                                                       {Spark, 2.0f, 0x29, 0.0f},
                                                       {Shine, 1.0f, 0x1a, 0.0f},
                                                       {Shine, 1.0f, 0x1f, 0.0f},
                                                       {Shine, 1.0f, 0x24, 0.0f},
                                                       {Shine, 1.0f, 0x29, 0.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x173:
    {
        constexpr std::array<BoneSpriteCall, 2> Calls{{{Spark, 4.0f, 9, 5.0f}, {Shine, 3.0f, 9, 5.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x1d8:
        BoneSprite(frame, Star, 1.5f, 0x20, {});
        break;
    case 0x1d9:
        if (auto drawn = DrawTheFairy(frame); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        break;
    default: // 0x1f2
        BoneSprite(frame, Star, 2.5f, 6, {});
        break;
    }
    return true;
}

// 0x50db57..0x510ac2 over the already drawn body.
Result DrawTheTail(MonsterFrame& frame, const glm::vec3& terrainLight)
{
    Kernel::ActorState& actor = frame.actor;
    const Actors::Drawing::GameCharacterPass& pass = frame.pass;
    // 0x50db94: the light of what hangs; DrawGameHanging takes the rand() of 0x50db57 and the two +0x38e closes of the
    // hands (Q-C860).
    actor.drawnLight = Actors::Drawing::HangingLightOf(actor, terrainLight, frame.map);
    if (auto hung = Actors::Drawing::DrawGameHanging(actor, HangingPassOf(frame, frame.items)); !hung.HasValue())
        return hung;
    const auto cased = DrawTheModelCase(frame);
    if (!cased.HasValue())
        return Core::Unexpected(cased.Error());
    if (!cased.Value())
    {
        // 0x510a27 and 0x510a36: 0x43c150 and 0x42dfe0 (KalimaSiegeHelpers.h); the first with the type skips the rest.
        const auto helped = DrawKalimaSiegeHelpers(HelperPassOf(frame));
        if (!helped.HasValue())
            return Core::Unexpected(helped.Error());
        if (!helped.Value())
        {
            // 0x510a42..0x510a72: 0x441400, 0x42fa30, 0x434180 and 0x4271d0.
            // The pass's heights (without it, none) for the marks of 0x434180.
            const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(pass.terrain);
            MonsterHelperPass helperPass{.actor = actor,
                                         .slot = frame.slot,
                                         .world = frame.world,
                                         .random = frame.random,
                                         .sprites = pass.sprites,
                                         .particles = pass.particles,
                                         .worldClock = frame.worldClock,
                                         .map = frame.map,
                                         .originalFrame = frame.originalFrame,
                                         .batch = &frame.batch,
                                         .heights = terrain.heights.get()};
            // 0x4ee770 from 0x441400: mesh 0 of the posed body again, as it left it in 0x5786f9c (Q-C1581).
            helperPass.vertexEffects = [&frame](std::int32_t type,
                                                std::int32_t subtype) -> Core::Expected<void, std::string>
            {
                if (frame.actor.bodyPose.empty())
                    return Core::Unexpected(std::string("0x4ee770: el cuerpo no tiene pose para sus vértices"));
                const PosedBody posed = PosedBodyOf(frame);
                return Actors::Drawing::EmitVertexEffects(frame.fauna, posed.object, posed.meshes, 0, type, subtype,
                                                          frame.world.Map().effects, frame.pass.effectHooks,
                                                          frame.random);
            };
            if (auto helpers = DrawTheMonsterHelpers(helperPass); !helpers.HasValue())
                return helpers;
        }
    }
    // 0x510a7a: in scene 5, with +0x88 at 1, outside Blood Castle, Chaos Castle and map 30, 0x510ef0 with +0x369 and
    // +0x36c (Q-C352): effect 0x140 (Effects/GuildMarkEffects) with bone 20 of the freshly posed body. Only in an
    // original frame: between frames, the character is a copy and the tables are the world's.
    if (actor.byte88 == PlayerKind && !IsBloodCastle(frame.map) && !IsChaosCastle(frame.map) &&
        frame.map != CastleMap && frame.originalFrame && Effects::GuildMarkIsDue(actor, frame.worldClock))
    {
        if (pass.particles == nullptr)
            return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 pide las partículas del frame");
        const glm::vec3 point = Effects::ActorBonePoint(actor, Effects::GuildMarkBone, glm::vec3(0.0f), frame.world);
        if (auto raised = Effects::RaiseTheGuildMark(actor, point, frame.world.Map().effects, *pass.particles,
                                                     frame.random, frame.worldClock);
            !raised.HasValue())
            return Core::Unexpected(std::format("0x510f4d: {}", raised.Error()));
    }
    // 0x510abd: 0x420df0, the raven of +0x430, with any type.
    if (!actor.darkSpirit)
        return {};
    if (pass.polygon == nullptr)
        return Core::Unexpected<std::string>("0x418320: falta el trapecio del dibujo para el cuervo");
    return Actors::Drawing::DrawGameRaven(
        actor, Actors::Drawing::GameRavenPass{.batch = frame.batch,
                                              .models = frame.items,
                                              .polygon = *pass.polygon,
                                              .lighting = pass.dynamicLighting,
                                              .terrain = Terrain::TerrainOrEmpty(pass.terrain),
                                              .fixedHeight = pass.fixedHeight,
                                              .carry = pass.poseCarry,
                                              .frameClock = pass.frameClock,
                                              .map = frame.map,
                                              .heroByte1a1 = frame.world.Hero().byte1a1,
                                              .originalFrame = frame.originalFrame});
}
} // namespace

bool MonsterShines(std::uint16_t monster)
{
    constexpr std::array<std::uint16_t, 8> Shining{0x26, 0x2b, 0x34, 0x3b, 0x43, 0x35, 0x36, 0x87};
    return OneOf(monster, Shining) || Within(monster, 0x4e, 0x53);
}

Core::Expected<void, std::string> DrawGameMonster(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                  Kernel::WorldState& world, std::size_t slot,
                                                  const Actors::Drawing::GameCharacterPass& pass, bool originalFrame,
                                                  MonstersState* monsters)
{
    Kernel::ActorState& stored = world.Actors()[slot];
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50db57: falta el flujo global de rand del dibujo de los personajes");
    if (pass.faunaModels == nullptr)
        return Core::Unexpected(std::format("0x50ba82: no fauna models (0x5769ad8) for model {:#x}", stored.type));
    const Models::IModelLibrary& fauna = *pass.faunaModels;
    const Models::Skeleton* skeleton = fauna.SkeletonOf(Models::ModelId{stored.type});
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x50ba82: falta el modelo {:#x}", stored.type));
    // Between original frames, on a copy of the character and the rand(): it stores nothing.
    std::optional<Kernel::ActorState> projected;
    std::optional<Crt::CrtRandom> projectedRandom;
    Kernel::ActorState& actor = originalFrame ? stored : projected.emplace(stored);
    Crt::CrtRandom& random = originalFrame ? pass.clothWorld->random : projectedRandom.emplace(pass.clothWorld->random);
    Models::SkinningBuffers ownBuffers;
    MonsterFrame frame{.batch = batch,
                       .items = models,
                       .fauna = fauna,
                       .world = world,
                       .actor = actor,
                       .pass = pass,
                       .buffers = pass.drawBuffers != nullptr ? *pass.drawBuffers : ownBuffers,
                       .random = random,
                       .slot = slot,
                       .map = world.Runtime().map,
                       .worldClock =
                           static_cast<float>(std::chrono::duration<double, std::milli>(pass.frameClock).count()),
                       .originalFrame = originalFrame,
                       .modelOrigin = actor.position,
                       .monsters = monsters};
    const auto slotIndex = static_cast<std::int32_t>(slot);
    frame.outline = slotIndex == pass.hovered || slotIndex == pass.targeted ? PoseMode::Outlined : PoseMode::Plain;
    const std::uint16_t special = Actors::Drawing::WearsClothOnTheMesh(actor.word376) ? 0 : SpecialOf(actor.word376);
    if (auto body = DrawTheBody(frame, *skeleton, special, frame.outline); !body.HasValue())
        return body;
    if (auto shadow = DrawTheShadow(frame); !shadow.HasValue())
        return shadow;
    if (auto decorated = DrawTheDecorations(frame); !decorated.HasValue())
        return decorated;
    // 0x50c972..0x50cc29: with +0x1f2 other than 0xffff (a long NpcName name overwrites it, P-C352), the guild war
    // colors ask for [0x576796d] or [0x576796f], and the duel's, for everyone, [0x78abad8]. The war packets
    // (GuildState, milestone 8) and the duel packets (SocialState::duel, milestone 7) write them, but MuRet does not
    // run the block here: the duel does not reach a monster (the keys belong to other objects), and the war only NPCs
    // with +0x1f2 overwritten, which would compare the guild table's names by +0x1f2 (0x787aa55 + i · 0x58) and set
    // bVar23 ([EBP+0xf], which turns on the cloth of 0x50d58b) and its color ([EBP−0x5c..−0x54]) (08 P-C352).
    // 0x50cc55: the terrain light (0x5f99e0) plus its own, in +0x3b4; type 0x189 at 0.6.
    if (pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x50cc55: falta el buffer dinámico de luz del terreno para 0x50ba30");
    const glm::vec3 terrainLight = Terrain::LightAt(*pass.dynamicLighting, actor.position.x, actor.position.y);
    actor.drawnLight = terrainLight + actor.light;
    if (actor.type == 0x189)
        actor.drawnLight = glm::vec3(std::bit_cast<float>(0x3f19999au));
    if (auto parts = DrawTheParts(frame); !parts.HasValue())
        return parts;
    if (auto clothed = RunTheCloth(frame); !clothed.HasValue())
        return clothed;
    return DrawTheTail(frame, terrainLight);
}
} // namespace Client::Monsters::Drawing
