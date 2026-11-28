#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Render/ModelBatch.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Terrain/TerrainCulling.h"
#include "Effects/ModelEffects.h"
#include "Effects/GroundEffectDrawing.h"
#include "Terrain/SphereFrustum.h"
#include "Weather/Leaves.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Math/OriginalRotation.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::MapScripts::Kalima
{
class KalimaWorld;
} // namespace Client::MapScripts::Kalima

// A world's objects in a scene (CL7.2): their step (0x606560) and their draw (0x6046a0), with the scene's models.
// Each one's logic is in WorldObjects/WorldObjects.h. Contract: 06 § El mundo del login.
namespace Client::WorldObjects
{
struct ObjectModelStepState;
} // namespace Client::WorldObjects

namespace Client::Kernel
{
struct ActorState;
struct CrywolfGlobals;
struct JointRequest;
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::WorldObjects
{
// The step of an object that has its own (the login logo): returns true if it advanced it, and then StepWorldObject
// does not touch it.
using SpecialObjectStep = std::function<bool(WorldObjects::WorldObject&)>;

// 0x606560: WorldObjects::StepWorldObject on those seen in the last draw (+0x16c), with their type's skeleton, sector
// by sector like the draw (WorldObjects::DrawOrder). Those not seen stay as they are (Q-C135). `special`, if set, goes
// first with each one.
void StepWorldObjects(std::span<WorldObjects::WorldObject> objects, std::uint8_t map,
                      const Models::IModelLibrary& models, const SpecialObjectStep& special = {});

// 0x6063d0 with each object, after its 0x604ab0: what the map does with it (Icarus).
using AfterObjectStep = std::function<Core::Expected<void, std::string>(WorldObjects::WorldObject&)>;

// 0x606560's walk over the sectors, in WorldObjects::DrawOrder: in a sector that passed the last draw, `seenStep`
// (0x604ab0 and 0x6063d0) and `collapse` (0x5ff960) with each live one that was seen; in one that did not pass,
// `collapse` with each live one and +0x16c at 0. Cuts at the first one that cuts, with the earlier ones done.
[[nodiscard]] Core::Expected<void, std::string> WalkTheObjectSectors(std::span<WorldObjects::WorldObject> objects,
                                                                     const AfterObjectStep& seenStep,
                                                                     const AfterObjectStep& collapse);

// 0x606560 in the game (0x63706a): WorldObjects::StepWorldObjectChecked on the live ones seen in the last draw, in
// WorldObjects::DrawOrder, with their type's skeleton and the CRT rand(), `after` (0x6063d0) with each one, if set,
// and the collapse `collapse` (0x5ff960, Events/ObjectCollapse.h) also with the live ones of the sectors that did not
// pass (WalkTheObjectSectors). 0x6061c0 at the start goes before, in the scene, and so does the final 0x6c9ddc--.
// Cuts at the first object whose case MuRet does not have, with the earlier ones already advanced.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWorldObjects(std::span<WorldObjects::WorldObject> objects, std::uint8_t map,
                     const Models::IModelLibrary& models, Crt::CrtRandom& random,
                     const WorldObjects::ObjectStepGlobals& globals, const AfterObjectStep& after = {},
                     const AfterObjectStep& collapse = {});

// What 0x602ed0 does in the frame with each object that reaches it, after its draw (0x604a4d).
struct ObjectFrameEffects
{
    // The CRT rand() (the cloth's): 0x602ef4 takes one per object and original frame, before looking at the scene or
    // the type. Without it, 0x602ed0 does not run.
    Crt::CrtRandom* random = nullptr;
    // Where the lantern sprites go (WorldObjects::StreetLight); without them they are not requested.
    Particles::FrameSprites* sprites = nullptr;
    // The 0x622/10 clouds of marker 0x54, only on map 55 (0x6044bc). Without a pool, its guard's rand is still
    // consumed, but not the creation ones.
    Particles::Particles* particles = nullptr;
    // What the frame posed in the global pose buffer before the objects, in order: the horse (0x4453b0).
    std::span<const Actors::Drawing::PosedInTheBuffer> posedBefore{};
    // The lantern case belongs to scene 2 (0x602f09).
    WorldObjects::ObjectScene scene = WorldObjects::ObjectScene::Other;
    // The original frames that passed: the pure draw runs even with zero.
    int frames = 1;
    // The common rand of the last original frame by index; those not seen in it lose their sample. Without an earlier
    // sample there is no glow to repeat.
    std::span<std::optional<int>> lastRandoms{};
    std::function<Core::Expected<void, std::string>(const Actors::Drawing::DrawnObject&)> onPose{};
    const Kernel::PoseBuffer* retainedPose = nullptr;
    bool publishProjectedPose = false;
    // 0x5763538 (WorldRuntime::objectDrawCounter): 0x6046a0 counts it once per original frame, as in the game.
    std::int32_t* objectDrawCounter = nullptr;
    // The model effects table (0x7491fd8) where map 55's object 0x59 drops its 0xc2/9 meteors (0x6044b2). Without it,
    // the meteor cuts at 0x604427, before its three rand().
    Effects::ModelEffects* modelEffects = nullptr;
    // A cut of 0x602ed0: the meteor creation that fails or has no table. After a cut, the objects' effects and glows do
    // not go on.
    std::function<void(const std::string&)> onFailure{};
};

// 0x6046a0 with `objects`: by sector, in WorldObjects::DrawOrder; the sector that passes the trapezoid at its center
// (x · 16 + 8, y · 16 + 8) with −180, and in it each object that passes it at its place times 0.01f (in float) with
// its individual margin (+0xdc; −30 by default), is seen (+0x16c keeps the result; those of a sector that does not
// pass stay unseen, as 0x606560 clears them) and is drawn as WorldObjects::DrawOf says, with the terrain light at its
// place (`lighting`, or 0 without it) and its animation `alpha` one step further (Models::Interpolated).
// With the poses captured, `effects` advances in the order original frame → seen objects: the common rand and the
// cloud guard of map 55's marker 0x54. At the end it recreates the lantern's two sprites in scene 2, with the last
// random draw and the bones 8 and 9 the buffer had when visiting it (LastPosedBonePoint): the earlier posed objects
// or `posedBefore`; with none, (0, 0, 0). With zero frames it draws and repeats the light of the lanterns that have a
// sample from the last original frame, without consuming RNG.
void DrawWorldObjects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                      std::span<WorldObjects::WorldObject> objects, const Terrain::TerrainPolygon& polygon,
                      const Terrain::TerrainLighting* lighting, std::uint8_t map, Core::Duration frameClock,
                      float alpha, const ObjectFrameEffects& effects = {});

// What 0x6046a0 reads outside the objects in the game.
struct GameObjectPass
{
    // DAT_006c9dd0.
    std::uint8_t map = 0;
    // Whether the draw is from an original frame: 0x602ed0 takes its rand() (0x602ef4) per object that arrives and
    // Devias' 0x64 opens the own record. An intermediate MuRet draw only draws.
    bool originalFrame = true;
    Crt::CrtRandom* random = nullptr;
    // What Devias' 0x64 looks at: the hero's class (+0x1d4), the own record's level (+0xe) and the count of that
    // record's open accesses (WorldRuntime::ownRecordDepth).
    std::uint8_t heroClass = 0;
    std::uint16_t ownLevel = 0;
    std::uint8_t* ownRecordDepth = nullptr;
    // 0x5767b88: the world clock in ms, from which Lorencia's 0x69 takes its V (0x603018).
    float worldClock = 0.0f;
    // The shared global table of 0x743a198. Without it, the historical cut at the first drop remains.
    Particles::Particles* particles = nullptr;
    // 0x5ffdf0 publishes the pose scratch before 0x602ed0 and before the birds.
    std::function<Core::Expected<void, std::string>(const Actors::Drawing::DrawnObject&)> onPose{};
    Particles::FrameSprites* sprites = nullptr;
    const Kernel::PoseBuffer* retainedPose = nullptr;
    std::function<Core::Expected<void, std::string>(const Kernel::JointRequest&)> createJoint{};
    WorldObjects::ObjectModelStepState* modelState = nullptr;
    // Publication on the caller's presentation scratch, without running 0x602ed0 or consuming RNG.
    bool publishProjectedPose = false;
    // DAT_07d2d360, lent by the global pulse 0x637700: without a producer no frame 0 is invented.
    std::optional<std::uint32_t> waterTextureFrame{};
    const Kernel::ActorState* hero = nullptr;
    const MapScripts::Kalima::KalimaWorld* kalima = nullptr;
    std::function<void(Audio::SoundId)> requestSound{};
    // 0x518cf0: the model effects, for butterfly 0xae of the 54's 0x3c (0x4257d5).
    Effects::ModelEffects* effects = nullptr;
    // 0x6b8544 (timeGetTime), which the 54's 0x3c and the 33's 0x46 read in the draw (Q-C304).
    std::function<std::uint32_t()> systemTime{};
    // DAT_006d5dc8: with 4 (the selection), the butterfly comes out hidden and the 0x6f1/2 with light 0.25.
    std::int32_t scene = 0;
    // 0x5f93c0(x, y): the terrain height, onto which the 0x1c and the 0x1d of 11 to 17 flatten their shadow (0x600d80).
    std::function<float(float, float)> terrainHeight{};
    // DAT_006c634c, the Chaos Castle area: 0xff until a packet moves it (CL9.5).
    std::uint8_t chaosCastleArea = 0xff;
    // 0x4294b0, the siege byte (0x7239ec): off until a packet turns it on (CL9.5).
    bool siegeActive = false;
    // 0x5763538 (WorldRuntime::objectDrawCounter): with an original frame, 0x6046a0 sets it to 1 after 41 and adds 1
    // before looking at an object (0x6046be..0x6046ce). Without it nothing is counted.
    std::int32_t* objectDrawCounter = nullptr;
    // Crywolf's globals (WorldRuntime::crywolf), which 0x430510 and 0x4306a0 read and write on the 34.
    Kernel::CrywolfGlobals* crywolf = nullptr;
};

// 0x6046a0 in the game (0x6374ce), on maps 0 to 4, 6 to 23, 30, 31, 33, 34 and Kalima (06 § Los objetos del juego):
// with an original frame, the count of `pass.objectDrawCounter` and, sector by sector, in WorldObjects::DrawOrder,
// each live object that passes the trapezoid as in DrawWorldObjects is seen. Devias' 0x64 goes on only with the own
// record's level from 0x50 (0x35 with class 3 or 4), with its access. Then 0x602e90: the draw of
// WorldObjects::DrawOf, without interpolating the animation (Lorencia's 0x69, with DrawWaterspout); and, with an
// original frame, 0x602ed0: one rand() and its type's case. The 0x69's is StepGameWaterspoutFrame, with the shared
// pool of `pass.particles`: the two 0x5f2/0 on the freshly posed bones, with their rand() interleaved. A bone not
// written needs the earlier global pose and cuts after its random draws; without a pool it keeps the historical cut at
// 0x6030c7. The 0's 0x38 leaves its two sprites at NaN (WorldObjects/ObjectFrameSprites.h). On the 4, the Chrome01
// pass before the body and the bone sprites; on the 8, the 0x51's chrome and Tarkan's sprites, smokes and pebbles; on
// the 10, the −10 trapezoid margin and Icarus' clouds and 0x5ac/0; on the 33 and the 54, the cases of 0x425260 and,
// in every draw, mesh 0 of the 0x29 and the 0x41/0x42 (DrawAidaMeshPass); on the 30, the cases of 0x42cc10
// (DrawCastleSiegeBody) in every draw and those of 0x42c9f0 (StepCastleSiegeObjectFrame); on the 34, the body and
// the random draws of 0x4306a0 (CrywolfObjectBody) and the case of 0x430510 (StepCrywolfObjectFrame). The other cases
// of 0x602ed0 (the 2's 0x64 and 0x67) cut with their address. The earlier objects stay drawn. Kalima 24..29/36 reuses
// the public pose and body of 0x43948d, its caustics from DAT_07d2d360, decal 0x22 and the sprite of type 12; the
// producers of 0x438c10 still missing keep their address and prefix. The other maps cut before looking at an object.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameWorldObjects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                     std::span<WorldObjects::WorldObject> objects, const Terrain::TerrainPolygon& polygon,
                     const Terrain::TerrainLighting* lighting, Core::Duration frameClock, const GameObjectPass& pass);

// 0x52dc20 (0x6374d3): for each live place with +0x16c retained, a common rand(), the additive blend and the type's
// case. Keeps the effects of the earlier places if one cuts. One original pass; GroundEffectFrame keeps the geometry
// between MuRet frames (D-C4).
[[nodiscard]] Core::Expected<void, std::string> DrawGroundEffects(Render::ModelBatch& batch,
                                                                  std::span<Effects::ModelEffect> effects,
                                                                  const Effects::GroundEffectResources& resources);

// 0x52d220: Effects::StepModelEffect on each live effect, with its model's skeleton.
void StepModelEffects(std::span<Effects::ModelEffect> effects, const Models::IModelLibrary& models);

// 0x52d620(0) with the live effects: one RenderBody(2, 1, −1, 1, 0, 0, −1, −1) for each, opaque, with the color of
// the terrain light at its place (`lighting`, or 0 without it) plus its own, the light of each normal and the whole
// frame (06 § El dragón de fondo); the position, `alpha` one step further with what it moved in the last one (D-C4:
// the original draws it as is, at 25 per second). First it publishes +0x16c with the 400 sphere, on the logical
// position, even if the model is not found. A dead place keeps its mark; a live one outside the sphere does not query
// the model.
void DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                      std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                      const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha);
// The same legacy walk, publishing the pose and consuming that scratch without posing again.
[[nodiscard]] Core::Expected<void, std::string>
DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                 std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                 const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha,
                 const Actors::Drawing::TailPoseObserver& onPose, const Actors::Drawing::TailMeshProvider& posedMeshes);

// 0x549440(0), in the eye pass (after 0x6213e0, with the modelview at identity): each live sprite, with the blend of
// its +4 (0 additive, 1 that of 0x620b10, 2 the normal one, 3 that of 0x620b90: ONE_MINUS_SRC_COLOR/ONE; texture
// 0x637, the normal one), the alpha of 0x549290 and the square of 0x621400 with its texture's size (`textures`) times
// the scale and the alpha, with the view `view` (0x7d366f0). After drawing it, it turns off in modes 0/2. Mode 1
// retains and skips z>350; mode 2 turns off z<=300/NaN without drawing.
void DrawFrameSprites(Render::ModelBatch& batch, Particles::FrameSprites& sprites,
                      const Models::IModelLibrary& textures, const Math::OriginalMatrix& view, std::uint8_t mode = 0);

// 0x548550(mode), in the same eye pass, after the sprites: the single contract of Particles::DrawParticlesChecked,
// with the scene's services (ParticleDrawServices) over the batch adapter (Render::ModelBatchParticleBackend). The
// dimensions, the material and the color come from the metadata of `textures` (the resource's +20/+24/+28) and the
// binder from its TextureSlotOf; the square of 0x621400, from the view `view`. The mist's rotation changes with
// `worldClock` in ms and the real pool index. A place that fails cuts the pass after its prefix, without rollback: the
// earlier ones stay in the batch and the error says which one.
[[nodiscard]] Core::Expected<void, Particles::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, Particles::Particles& particles, const Models::IModelLibrary& textures,
              const Math::OriginalMatrix& view, float worldClock = 0.0f, std::uint8_t mode = 0);

// 0x5306a0, in the world pass, after the model effects: the live leaves, additive (on map 0x37, 0x620a90) and with
// the white of a single glColor3f(1, 1, 1), each one the square of LeafQuadOf translated to its position, with its
// type's texture.
void DrawLeaves(Render::ModelBatch& batch, const Weather::Leaves& leaves);
} // namespace Client::WorldObjects
