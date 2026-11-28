#include "WorldObjects/WorldObjectsDrawing.h"
#include "Maps/MapId.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/VertexEffects.h"

#include "Protocol/Events/Events.h"
#include "Kernel/ActorState.h"
#include "Kernel/PoseBuffer.h"
#include "MapScripts/Aida/AidaObjectServices.h"
#include "WorldObjects/EventMapObjectFrame.h"
#include "MapScripts/Noria/NoriaObjectServices.h"
#include "WorldObjects/ObjectFrameSprites.h"
#include "MapScripts/Icarus/IcarusObjectFrame.h"
#include "MapScripts/Tarkan/TarkanObjectFrame.h"
#include "MapScripts/Crywolf/CrywolfObjectServices.h"
#include "MapScripts/Crywolf/CrywolfAltarDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "MapScripts/Kalima/KalimaObjectServices.h"
#include "MapScripts/CastleSiege/CastleSiegeObjectServices.h"
#include "MapScripts/LandOfTrials/LandOfTrialsObjectServices.h"
#include "WorldObjects/ObjectModelStepState.h"
#include "Effects/GameParticles.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/FloatConstants.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Models/ObjectState.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Render/TextureEnvMode.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Effects/ParticleDrawServices.h"
#include "Weather/Leaves.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Math/Ftol.h"
#include "Effects/ModelEffects.h"
#include "Effects/GroundEffectDrawing.h"
#include "Terrain/SphereFrustum.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Render/Billboard.h"
#include "Math/OriginalRotation.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Effects/WizardModelEffects.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::WorldObjects
{
namespace
{
// 0x6046a0
constexpr double SectorMargin = -180.0;
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr int CellsPerSector = 16;
constexpr double SectorCenter = 8.0;
constexpr std::uint8_t LoginCloudMap = 55;
constexpr std::int16_t LoginCloudMarker = 0x54;
constexpr int LoginCloudChance = 5;
constexpr std::int16_t LoginMeteorMarker = 0x59;
constexpr std::int32_t LoginMeteorPeriod = 8000; // 0x604414
constexpr std::int32_t LoginMeteorWindow = 7950; // 0x60441b
// 0x60442c and 0x604451: rand() & 0x7ff (the signed remainder of 2048 of a rand() that is not negative).
constexpr int LoginMeteorSpreadMask = 0x7ff;
constexpr double LoginMeteorSpreadHalf = 1024.0; // 0x6b89b8
constexpr int LoginMeteorRiseSpan = 600;         // 0x604477
constexpr double LoginMeteorRise = 3000.0;       // 0x6b8c10
constexpr std::int32_t LoginMeteorSubtype = 9;   // 0x60448a

bool SectorPasses(const Terrain::TerrainPolygon& polygon, int sectorX, int sectorY)
{
    return Terrain::PolygonContains(polygon, (sectorX * CellsPerSector) + SectorCenter,
                                    (sectorY * CellsPerSector) + SectorCenter, SectorMargin);
}

// 0x6046a0 takes the position times 0.01f (0x6b88d4) stored in float and the individual margin (+0xdc), with
// `extra` added in the x87 and stored in float (0x604744..0x60474e).
bool ObjectPasses(const Terrain::TerrainPolygon& polygon, const WorldObjects::WorldObject& object, float extra)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return Terrain::PolygonContains(polygon, cells(object.transform.position.x), cells(object.transform.position.y),
                                    static_cast<double>(extra + object.cullingMargin));
}

Actors::Drawing::DrawnObject Drawn(const WorldObjects::WorldObject& object, const WorldObjects::ObjectDraw& draw,
                                   const Models::Skeleton* skeleton, float alpha)
{
    Actors::Drawing::DrawnObject drawn;
    drawn.model = static_cast<std::uint16_t>(object.type);
    drawn.animation = object.animation;
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        drawn.animation =
            Models::Interpolated(object.animation, skeleton->actions[object.animation.action], object.speed, alpha);
    }
    drawn.transform = object.transform;
    drawn.state = draw.state;
    drawn.color = draw.color;
    drawn.alpha = draw.alpha;
    drawn.vertexLight = draw.vertexLight;
    return drawn;
}
// 0x6046a0 with an object: its sector passes the trapezoid and so does the object (+0x16c, which keeps the result;
// those of a sector that does not pass stay unseen, as 0x606560 clears them).
// 0x6046b2..0x6046c3: each object's extra margin, −10 in Icarus (map 10) and 0 in the others.
float ExtraObjectMargin(std::uint8_t map)
{
    constexpr std::uint8_t IcarusMap = Maps::IcarusMap;
    constexpr float IcarusMargin = -10.0f;
    return map == IcarusMap ? IcarusMargin : 0.0f;
}

// 0x6046be..0x6046ce: the draw count goes back to 0 past 40 and adds 1, before looking at an object.
void CountTheObjectDraw(std::int32_t& counter)
{
    constexpr std::int32_t LastObjectDraw = 40;
    if (counter > LastObjectDraw)
        counter = 0;
    ++counter;
}

// 0x60470b: the sector's +8 keeps what the trapezoid gave, also for the objects that are not alive.
void PassTheSector(const Terrain::TerrainPolygon& polygon, WorldObjects::WorldObject& object)
{
    object.sectorPassed = SectorPasses(polygon, WorldObjects::SectorX(object), WorldObjects::SectorY(object));
}

bool SeenInTheDraw(const Terrain::TerrainPolygon& polygon, const WorldObjects::WorldObject& object,
                   float extraMargin = 0.0f)
{
    return object.sectorPassed && ObjectPasses(polygon, object, extraMargin);
}

// 0x5ffce0 with the terrain light at its place (0x5f99e0), or 0 without it, and what WorldObjects::DrawOf decides.
WorldObjects::ObjectDraw ObjectDrawAt(const WorldObjects::WorldObject& object, const Terrain::TerrainLighting* lighting,
                                      std::uint8_t map)
{
    const glm::vec3 light = lighting != nullptr
                                ? Terrain::LightAt(*lighting, object.transform.position.x, object.transform.position.y)
                                : glm::vec3(0.0f);
    return WorldObjects::DrawOf(object, light, map);
}

// 0x602e90 with `draw`: the model posed as 0x5ffdf0 does and drawn as 0x6002e0 does without per-type cases, with its
// animation `alpha` one step further. Returns what was posed.
Actors::Drawing::DrawnObject DrawTheObject(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                           const WorldObjects::WorldObject& object,
                                           const WorldObjects::ObjectDraw& draw, Core::Duration frameClock, float alpha,
                                           const WorldObjects::ObjectModelStepState* modelState = nullptr,
                                           const Kernel::PoseBuffer* retainedPose = nullptr)
{
    const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)});
    Actors::Drawing::DrawnObject drawn = Drawn(object, draw, skeleton, alpha);
    if (modelState != nullptr)
    {
        const auto found = modelState->unlitMeshes.find(object.type);
        if (found != modelState->unlitMeshes.end())
            drawn.unlitMesh = found->second;
    }
    if (retainedPose != nullptr)
        drawn.retainedMeshes = retainedPose->meshes;
    switch (draw.kind)
    {
    case WorldObjects::ObjectDrawKind::Hidden:
        break;
    case WorldObjects::ObjectDrawKind::Body:
        Actors::Drawing::DrawObject(batch, models, drawn, frameClock);
        break;
    case WorldObjects::ObjectDrawKind::SubtractiveMesh:
        Actors::Drawing::DrawSubtractiveMesh(batch, models, drawn, frameClock);
        break;
    case WorldObjects::ObjectDrawKind::Waterspout:
        Actors::Drawing::DrawWaterspout(batch, models, drawn, frameClock);
        break;
    case WorldObjects::ObjectDrawKind::TarkanChrome:
        Actors::Drawing::DrawTarkanChrome(batch, models, drawn, frameClock);
        break;
    }
    return drawn;
}

// The pose is taken once per draw; the emission follows the order frame → objects.
struct SeenObjectEffects
{
    WorldObjects::WorldObject* object = nullptr;
    std::size_t index = 0;
    std::optional<std::array<glm::vec3, WorldObjects::StreetLightGlowBones.size()>> glowPositions;
    std::optional<int> lastRandom;
};

Core::Expected<void, std::string> CreateObjectCloud(const WorldObjects::WorldObject& object, std::uint8_t map,
                                                    float clock, const ObjectFrameEffects& effects)
{
    // 0x6043ff..0x604421: the 0x59 of map 55, with ftol(world clock) % 8000 from 7950, drops a meteor: three rand()
    // (0x604427, 0x60444c, 0x604471) and 0x518cf0(0xc2, point, +0x1c, +0xf4, 9, 0, −1, 0, 0, 0, 0) (0x6044b2).
    if (map == LoginCloudMap && object.type == LoginMeteorMarker)
    {
        if (Math::FtolLow(static_cast<double>(clock)) % LoginMeteorPeriod < LoginMeteorWindow)
            return {};
        if (effects.modelEffects == nullptr)
            return Core::Unexpected(std::string("0x604427: el meteoro 0xc2/9 del objeto 0x59 del mapa 55 (0x518cf0) "
                                                "no tiene la tabla de los efectos con modelo (CL11)"));
        // x and y: (rand() % 2048) + the object's − 1024 (0x60442c..0x60446e, fild, fadd and fsub in the x87); z: the
        // object's + rand() % 600 + 3000 (0x604471..0x6044af), with the remainder added first.
        glm::vec3 point{};
        for (int axis = 0; axis < 2; ++axis)
            point[axis] = static_cast<float>((static_cast<double>(effects.random->Next() & LoginMeteorSpreadMask) +
                                              static_cast<double>(object.transform.position[axis])) -
                                             LoginMeteorSpreadHalf);
        point.z = static_cast<float>((static_cast<double>(effects.random->Next() % LoginMeteorRiseSpan) +
                                      static_cast<double>(object.transform.position.z)) +
                                     LoginMeteorRise);
        const Kernel::ModelEffectArguments meteor{.type = Effects::FireEffect,
                                                  .position = point,
                                                  .angles = object.transform.angles,
                                                  .light = object.light,
                                                  .subtype = LoginMeteorSubtype};
        // Case 9 of the 0xc2 (0x51b280) only takes its rand(): it requests no services.
        const auto created = Effects::CreateWizardModelEffect(*effects.modelEffects, meteor, *effects.random,
                                                              Effects::WizardEffectServices{});
        if (!created.HasValue())
            return Core::Unexpected(created.Error() + ", en el meteoro del 0x59 (0x6044b2)");
        return {};
    }
    // 0x6044bc: map 55's hidden marker, after the common rand, with no scene guard.
    if (map != LoginCloudMap || object.type != LoginCloudMarker || effects.random->Next() % LoginCloudChance != 0 ||
        effects.particles == nullptr)
    {
        return {};
    }
    // The original caller passes an indeterminate light; subtype 10 overwrites it with zero RGB.
    (void)effects.particles->Create(Particles::CloudParticle, object.transform.position, object.transform.angles,
                                    glm::vec3(0.0f), Particles::LoginCloudSubtype, object.transform.scale, clock,
                                    *effects.random);
    return {};
}

Core::Expected<void, std::string> AdvanceObjectEffects(std::span<SeenObjectEffects> seen, std::uint8_t map,
                                                       Core::Duration frameClock, const ObjectFrameEffects& effects)
{
    const auto clock = static_cast<float>(std::chrono::duration<double, std::milli>(frameClock).count());
    for (int frame = 0; frame < effects.frames; ++frame)
    {
        for (SeenObjectEffects& entry : seen)
        {
            entry.lastRandom = effects.random->Next();
            if (auto created = CreateObjectCloud(*entry.object, map, clock, effects); !created.HasValue())
                return created;
        }
    }
    return {};
}

void DrawObjectGlows(std::span<const SeenObjectEffects> seen, const ObjectFrameEffects& effects)
{
    if (effects.frames > 0)
    {
        // Only a sample of the last original frame can be repeated, not one from an earlier appearance.
        for (std::optional<int>& random : effects.lastRandoms)
        {
            random.reset();
        }
    }
    for (const SeenObjectEffects& entry : seen)
    {
        if (entry.index < effects.lastRandoms.size())
        {
            effects.lastRandoms[entry.index] = entry.lastRandom;
        }
        if (!entry.glowPositions || !entry.lastRandom)
        {
            continue;
        }
        const glm::vec3 glow = WorldObjects::StreetLightGlowLight(*entry.lastRandom);
        for (const glm::vec3& position : *entry.glowPositions)
        {
            (void)effects.sprites->Create(WorldObjects::StreetLightGlowTexture, position, 1.0f, glow, 0.0f, 0);
        }
    }
}
} // namespace

Core::Expected<void, std::string> WalkTheObjectSectors(std::span<WorldObjects::WorldObject> objects,
                                                       const AfterObjectStep& seenStep, const AfterObjectStep& collapse)
{
    for (const std::size_t index : WorldObjects::DrawOrder(objects))
    {
        WorldObjects::WorldObject& object = objects[index];
        // +0x16c only stays on in a sector that passed the last draw (MuRet clears it in the draw).
        if (!object.sectorPassed && !object.seen)
        {
            // 0x6065d1..0x6065f4: 0x5ff960 with the live one and +0x16c at 0.
            if (object.alive && collapse)
            {
                if (auto collapsed = collapse(object); !collapsed.HasValue())
                    return collapsed;
            }
            object.seen = false;
            continue;
        }
        if (!object.alive || !object.seen)
        {
            continue;
        }
        // 0x6065a8..0x6065bd: 0x604ab0, 0x6063d0 and 0x5ff960.
        if (seenStep)
        {
            if (auto stepped = seenStep(object); !stepped.HasValue())
                return stepped;
        }
        if (collapse)
        {
            if (auto collapsed = collapse(object); !collapsed.HasValue())
                return collapsed;
        }
    }
    return {};
}

Core::Expected<void, std::string> StepGameWorldObjects(std::span<WorldObjects::WorldObject> objects, std::uint8_t map,
                                                       const Models::IModelLibrary& models, Crt::CrtRandom& random,
                                                       const WorldObjects::ObjectStepGlobals& globals,
                                                       const AfterObjectStep& after, const AfterObjectStep& collapse)
{
    const AfterObjectStep seenStep = [&](WorldObjects::WorldObject& object) -> Core::Expected<void, std::string>
    {
        if (auto stepped = WorldObjects::StepWorldObjectChecked(
                object, map, models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)}), random,
                globals);
            !stepped.HasValue())
        {
            return stepped;
        }
        // 0x6065b7: 0x6063d0(object, &selector, &0x7d365a0) after 0x604ab0.
        if (after)
            return after(object);
        return {};
    };
    return WalkTheObjectSectors(objects, seenStep, collapse);
}

void StepWorldObjects(std::span<WorldObjects::WorldObject> objects, std::uint8_t map,
                      const Models::IModelLibrary& models, const SpecialObjectStep& special)
{
    for (const std::size_t index : WorldObjects::DrawOrder(objects))
    {
        WorldObjects::WorldObject& object = objects[index];
        if (!object.seen || (special && special(object)))
        {
            continue;
        }
        WorldObjects::StepWorldObject(object, map,
                                      models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)}));
    }
}

void DrawWorldObjects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                      std::span<WorldObjects::WorldObject> objects, const Terrain::TerrainPolygon& polygon,
                      const Terrain::TerrainLighting* lighting, std::uint8_t map, Core::Duration frameClock,
                      float alpha, const ObjectFrameEffects& effects)
{
    // The global pose buffer, in the order it is overwritten: what came before the objects and each object that
    // 0x5ffdf0 poses. Only needed with the lantern sprites.
    const bool glows =
        effects.random != nullptr && effects.sprites != nullptr && effects.scene == WorldObjects::ObjectScene::Login;
    std::vector<Actors::Drawing::PosedInTheBuffer> posed;
    std::vector<SeenObjectEffects> seen;
    if (glows)
    {
        posed.assign(effects.posedBefore.begin(), effects.posedBefore.end());
    }
    if (effects.objectDrawCounter != nullptr)
    {
        for (int frame = 0; frame < effects.frames; ++frame)
            CountTheObjectDraw(*effects.objectDrawCounter);
    }
    for (const std::size_t index : WorldObjects::DrawOrder(objects))
    {
        WorldObjects::WorldObject& object = objects[index];
        PassTheSector(polygon, object);
        object.seen = SeenInTheDraw(polygon, object, ExtraObjectMargin(map));
        if (!object.seen)
        {
            continue;
        }
        const WorldObjects::ObjectDraw draw = ObjectDrawAt(object, lighting, map);
        // 0x5ffdf0 poses before the hiddenMesh=-2 gate of 0x600328; do not confuse pose with geometry.
        if (effects.onPose && (effects.frames > 0 || effects.publishProjectedPose) && draw.posed)
        {
            const auto poseObject =
                Drawn(object, draw, models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)}), alpha);
            if (poseObject.alpha >= .01f)
                if (auto published = effects.onPose(poseObject); !published.HasValue())
                    return;
        }
        const auto* retained = (effects.frames > 0 || effects.publishProjectedPose) ? effects.retainedPose : nullptr;
        const Actors::Drawing::DrawnObject drawn =
            DrawTheObject(batch, models, object, draw, frameClock, alpha, nullptr, retained);
        if (glows && draw.posed)
        {
            posed.push_back(Actors::Drawing::PosedInTheBuffer{&models, drawn});
        }
        if (effects.random == nullptr)
        {
            continue;
        }
        SeenObjectEffects entry{.object = &object,
                                .index = index,
                                .glowPositions = std::nullopt,
                                .lastRandom =
                                    index < effects.lastRandoms.size() ? effects.lastRandoms[index] : std::nullopt};
        if (glows && object.type == WorldObjects::StreetLight)
        {
            entry.glowPositions.emplace();
            for (std::size_t k = 0; k < WorldObjects::StreetLightGlowBones.size(); ++k)
            {
                const auto bone = WorldObjects::StreetLightGlowBones[k];
                if (retained != nullptr && bone < retained->bones.size())
                    (*entry.glowPositions)[k] =
                        Math::OriginalVectorTransform({}, Math::OriginalMatrixOf(retained->bones[bone]));
                else
                    (*entry.glowPositions)[k] =
                        Actors::Drawing::LastPosedBonePoint(posed, bone, glm::vec3(0)).value_or(glm::vec3(0));
            }
        }
        seen.push_back(entry);
    }
    if (auto advanced = AdvanceObjectEffects(seen, map, frameClock, effects); !advanced.HasValue())
    {
        if (effects.onFailure)
            effects.onFailure(advanced.Error());
        return;
    }
    DrawObjectGlows(seen, effects);
}

namespace
{
constexpr std::uint8_t LorenciaMap = Maps::LorenciaMap;
constexpr std::uint8_t DeviasMap = Maps::DeviasMap;
constexpr std::uint8_t NoriaMap = Maps::NoriaMap;
constexpr std::uint8_t LostTowerMap = Maps::LostTowerMap;
constexpr std::uint8_t ArenaMap = Maps::ArenaMap;
constexpr std::uint8_t AtlansMap = Maps::AtlansMap;
constexpr std::uint8_t TarkanMap = Maps::TarkanMap;
constexpr std::uint8_t DevilSquareMap = Maps::DevilSquareMap;
constexpr std::uint8_t IcarusMap = Maps::IcarusMap;
constexpr std::uint8_t FirstBloodCastleMap = 11;
constexpr std::uint8_t LastBloodCastleMap = 17;
constexpr std::uint8_t CastleSiegeMap = Maps::CastleSiegeMap;
constexpr std::uint8_t LandOfTrialsMap = Maps::LandOfTrialsMap;
constexpr std::uint8_t AidaMap = Maps::AidaMap;
constexpr std::uint8_t CrywolfMap = Maps::CrywolfMap;
constexpr std::uint8_t SelectionWorldMap = Maps::CharacterSelectMap;
// 0x60479c: Devias' 0x64 is drawn by the own record's level (0x604944..0x604957, signed jl): from 0x35 with the
// hero's class (+0x1d4 & 7) at 3 or 4, and from 0x50 with the others.
constexpr std::int16_t DeviasLeveledObject = 0x64;
// 0x600d04..0x600d0e: the two that carry a shadow in 11 to 17.
constexpr std::int16_t BloodCastleShadowedFirst = 0x1c;
constexpr std::int16_t BloodCastleShadowedSecond = 0x1d;
constexpr Kernel::CharacterClass FirstLowLevelClass = Kernel::CharacterClass::MagicGladiator;
constexpr Kernel::CharacterClass SecondLowLevelClass = Kernel::CharacterClass::DarkLord;
constexpr int LowLevelThreshold = 0x35;
constexpr int LevelThreshold = 0x50;

// 0x6047a7..0x604a36: the hero's class, before the access; the level, with the own record opened (0x6047cb) and
// closed (0x604a1a: with the count at 0, 0x40b4d0 encrypts it again with its 3848 rand()). An intermediate MuRet draw
// has no original frame and does not open it.
bool OwnLevelAllows(const GameObjectPass& pass)
{
    const Kernel::CharacterClass heroClass = Kernel::BaseClassOf(pass.heroClass);
    const int threshold =
        heroClass == FirstLowLevelClass || heroClass == SecondLowLevelClass ? LowLevelThreshold : LevelThreshold;
    if (pass.originalFrame && pass.random != nullptr && pass.ownRecordDepth != nullptr)
    {
        const Models::LogicalAccess access(*pass.ownRecordDepth, *pass.random, Models::OwnRecordCloseRandoms);
        return static_cast<int>(pass.ownLevel) >= threshold;
    }
    return static_cast<int>(pass.ownLevel) >= threshold;
}

// 0x6002e0 with map 4 (0x600b80..0x601067 in the pseudocode, map 4's lines): the 0x17, the 0x13, the 0x14, the 3 and
// the 4 take a rand() that is discarded (in the original's frame, which is when 0x6002e0 and its rand run); the last
// four, before the body, a RenderBody(2) with Chrome01 (0x5c0) forced, the model light at (1; 0.2; 0.1) and
// model+0x88 at 2 (0x13 and 0x14) or at 1 (3 and 4): that mesh unlit and with the object's U and V. Then +0x88 goes
// back to 0xff, which on the 4 is what those types already had.
void DrawLostTowerChrome(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                         const WorldObjects::WorldObject& object, const WorldObjects::ObjectDraw& draw,
                         Core::Duration frameClock, const GameObjectPass& pass, const Kernel::PoseBuffer* retained)
{
    const std::int16_t type = object.type;
    const bool chrome = type == 0x13 || type == 0x14 || type == 3 || type == 4;
    if (!chrome && type != 0x17)
        return;
    if (pass.originalFrame)
        static_cast<void>(pass.random->Next());
    if (!chrome)
        return;
    constexpr glm::vec3 ChromeLight{1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3dcccccdu)};
    Actors::Drawing::DrawnObject chromed =
        Drawn(object, draw, models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(type)}), 0.0f);
    if (retained != nullptr)
        chromed.retainedMeshes = retained->meshes;
    chromed.color = ChromeLight;
    chromed.unlitMesh = type == 0x13 || type == 0x14 ? std::int16_t{2} : std::int16_t{1};
    Actors::Drawing::DrawObjectWithTexture(batch, models, chromed, frameClock, Actors::Drawing::ChromeTextureSlot);
}

// 0x602ed0 after the common rand() (0x602ef4): the per-type cases of maps 0 (0x602ffb), 2 (0x6031e5), 4 (0x603799), 8
// (0x603a3e), 9 (0x603dd7) and 11 to 17 (0x604152); the 1 goes to the chain of 0x6044ed, which has nothing for it, the
// Chaos Castles (18 to 23) to their first link, 0x4059d0, and the 34 to its own, 0x430510. The 0x69 creates the drops
// with the shared pool. `posed`: whether 0x602e90 called 0x6002e0, whose frame stays on the stack that the sprites of
// the 0's 0x38 and the 4's 9 read, and whether 0x5ffdf0 left the model's +0xa8 of the 2's 0x67.
Core::Expected<void, std::string> FrameCaseOf(WorldObjects::WorldObject& object, const GameObjectPass& pass,
                                              const Models::IModelLibrary& models,
                                              const Actors::Drawing::DrawnObject& drawn, int commonRandom, bool posed)
{
    const std::uint8_t map = pass.map;
    if (Maps::IsKalimaMap(map))
        return MapScripts::Kalima::StepKalimaObjectFrame(object, pass);
    if (map == NoriaMap)
        return MapScripts::Noria::StepNoriaObjectFrame(object, commonRandom, pass);
    if (map == AidaMap || map == SelectionWorldMap)
        return MapScripts::Aida::StepAidaObjectFrame(object, pass);
    if (map == LandOfTrialsMap)
        return MapScripts::LandOfTrials::StepLandOfTrialsObjectFrame(object, pass);
    if (map == CastleSiegeMap)
        return MapScripts::CastleSiege::StepCastleSiegeObjectFrame(object, pass);
    if (map == CrywolfMap)
        return MapScripts::Crywolf::StepCrywolfObjectFrame(object, pass);
    if (map == LostTowerMap)
        return StepLostTowerObjectFrame(object, commonRandom, pass, posed);
    if (map == ArenaMap)
        return StepArenaObjectFrame(object, commonRandom, pass, posed);
    if (map == TarkanMap)
        return MapScripts::Tarkan::StepTarkanObjectFrame(object, pass);
    if (map == IcarusMap)
        return MapScripts::Icarus::StepIcarusObjectFrame(object, pass);
    if (map == DevilSquareMap)
        return StepDevilSquareObjectFrame(object, pass);
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap)
        return StepBloodCastleObjectFrame(object, pass);
    if (Maps::IsChaosCastle(map))
        return StepChaosCastleObjectFrame(object, pass);
    if (map == LorenciaMap && object.type == 0x38)
        return StepMerchantAnimalFrame(object, commonRandom, pass, posed);
    if (map == LorenciaMap && object.type == WorldObjects::Waterspout)
    {
        if (pass.particles != nullptr)
        {
            return Effects::StepGameWaterspoutFrame(object, *pass.particles, *pass.random, pass.worldClock, models,
                                                    drawn, pass.retainedPose);
        }
        return WorldObjects::StepTheWaterspoutFrame(object, *pass.random, pass.worldClock);
    }
    if (map == DeviasMap)
    {
        return StepDeviasObjectFrame(object, commonRandom, pass,
                                     models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)}),
                                     posed);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameWorldObjects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       std::span<WorldObjects::WorldObject> objects,
                                                       const Terrain::TerrainPolygon& polygon,
                                                       const Terrain::TerrainLighting* lighting,
                                                       Core::Duration frameClock, const GameObjectPass& pass)
{
    const bool bloodCastle = pass.map >= FirstBloodCastleMap && pass.map <= LastBloodCastleMap;
    if (pass.map > LostTowerMap && pass.map != ArenaMap && pass.map != AtlansMap && pass.map != TarkanMap &&
        pass.map != DevilSquareMap && pass.map != IcarusMap && pass.map != CastleSiegeMap &&
        pass.map != LandOfTrialsMap && pass.map != AidaMap && pass.map != CrywolfMap && pass.map != SelectionWorldMap &&
        !Maps::IsKalimaMap(pass.map) && !bloodCastle && !Maps::IsChaosCastle(pass.map))
    {
        return Core::Unexpected(std::format("0x6046a0: los casos por tipo del mapa {} (0x6002e0 y 0x602ed0) no están "
                                            "leídos",
                                            static_cast<int>(pass.map)));
    }
    if (pass.originalFrame && pass.random == nullptr)
    {
        return Core::Unexpected(std::string("0x602ef4: el frame del original saca rand() por objeto, y no lo tiene"));
    }
    if (pass.originalFrame && pass.objectDrawCounter != nullptr)
        CountTheObjectDraw(*pass.objectDrawCounter);
    for (const std::size_t index : WorldObjects::DrawOrder(objects))
    {
        WorldObjects::WorldObject& object = objects[index];
        PassTheSector(polygon, object);
        // 0x60473b: an object that is not alive is not looked at.
        if (!object.alive)
        {
            continue;
        }
        object.seen = SeenInTheDraw(polygon, object, ExtraObjectMargin(pass.map));
        if (!object.seen)
        {
            continue;
        }
        if (pass.map == DeviasMap && object.type == DeviasLeveledObject && !OwnLevelAllows(pass))
        {
            continue;
        }
        // 0x5ffdff..0x5ffe24: on maps 18 to 23, with the hero's +0x1a1 (only packets write it), the object's alpha
        // drops 0.15f and stays stored, once per original frame.
        if (pass.originalFrame && pass.hero != nullptr)
            object.alpha = Events::ChaosCastle::ChaosCastlePoseAlpha(pass.map, pass.hero->byte1a1, object.alpha);
        // 0x602e90: 0x5ffdf0 and, if it posed it, 0x6002e0 (on the 0, the 0x69 with its four 0x4ecc30).
        WorldObjects::ObjectDraw draw = ObjectDrawAt(object, lighting, pass.map);
        const auto poseObject =
            Drawn(object, draw, models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(object.type)}), 0);
        // The hidden markers also overwrite the global pose before the geometry is skipped.
        if ((pass.originalFrame || pass.publishProjectedPose) && pass.onPose && draw.posed &&
            poseObject.alpha >= 0.01f && models.Find(Models::ModelId{poseObject.model}) != nullptr &&
            models.SkeletonOf(Models::ModelId{poseObject.model}) != nullptr)
        {
            if (auto published = pass.onPose(poseObject); !published.HasValue())
                return published;
        }
        const auto* retained = (pass.originalFrame || pass.publishProjectedPose) ? pass.retainedPose : nullptr;
        if (Maps::IsKalimaMap(pass.map) && draw.kind != WorldObjects::ObjectDrawKind::Hidden)
        {
            // 0x438fe0 has global cases outside the public domain 0x43948d: do not use the body as a fallback for a
            // type that still needs its own handler.
            constexpr std::int16_t FirstKalimaBody = 0;
            constexpr std::int16_t EndKalimaBody = 160;
            if (object.type < FirstKalimaBody || object.type >= EndKalimaBody)
                return Core::Unexpected(std::string("0x438fe0: tipo Kalima fuera del cuerpo público0..159"));
            if (models.Find(Models::ModelId{poseObject.model}) == nullptr ||
                models.SkeletonOf(Models::ModelId{poseObject.model}) == nullptr)
                return Core::Unexpected(std::string("0x5ffdf0: falta el modelo o el esqueleto de Kalima"));
        }
        if (pass.map == LostTowerMap && draw.kind != WorldObjects::ObjectDrawKind::Hidden)
            DrawLostTowerChrome(batch, models, object, draw, frameClock, pass, retained);
        // 0x4403b0, the chain of 0x6002e0 on the 31: no rand() or state, in every MuRet draw.
        if (pass.map == LandOfTrialsMap && draw.kind == WorldObjects::ObjectDrawKind::Body)
        {
            const auto body = MapScripts::LandOfTrials::LandOfTrialsBody(object, draw.color, pass.worldClock);
            if (!body.HasValue())
                return Core::Unexpected(body.Error());
            if (body.Value())
            {
                draw.color = body.Value()->color;
                // Q-C317: the 0x34's own RenderBody and, after it, 0x6002e0's with the same color.
                if (body.Value()->twice)
                    static_cast<void>(
                        DrawTheObject(batch, models, object, draw, frameClock, 0.0f, pass.modelState, retained));
            }
        }
        // 0x42cc10, the chain of 0x6002e0 on the 30, with no rand() or state: in every MuRet draw. With a case,
        // 0x6002e0 does not do its RenderBody.
        if (pass.map == CastleSiegeMap && draw.kind == WorldObjects::ObjectDrawKind::Body)
        {
            WorldObjects::ObjectDraw posed = draw;
            posed.kind = WorldObjects::ObjectDrawKind::Hidden;
            const Actors::Drawing::DrawnObject body =
                DrawTheObject(batch, models, object, posed, frameClock, 0.0f, pass.modelState, retained);
            const auto drew = Actors::Drawing::DrawCastleSiegeBody(
                batch, models, body,
                Actors::Drawing::CastleSiegeBodyPass{
                    .type = object.type,
                    .field04 = object.field04,
                    .siegeActive = pass.siegeActive,
                    .worldClock = pass.worldClock,
                    .emitGateVertices =
                        [&](const Actors::Drawing::DrawnObject& gate,
                            std::span<const Models::SkinnedMesh> meshes) -> Core::Expected<void, std::string>
                    {
                        if (pass.effects == nullptr || pass.random == nullptr)
                            return Core::Unexpected(std::string("0x42cfcb: falta el pool o CRT para los vértices12f"));
                        return Actors::Drawing::EmitVertexEffects(models, gate, meshes, 0, 0x12f, 0, *pass.effects,
                                                                  nullptr, *pass.random);
                    }},
                frameClock);
            if (!drew.HasValue())
                return Core::Unexpected(drew.Error());
            if (drew.Value())
                draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        }
        // 0x4306a0, the last link of the chain of 0x6002e0 on the 34: the body and, with an original frame, its random
        // draws, before the common rand().
        if (pass.map == CrywolfMap && draw.kind == WorldObjects::ObjectDrawKind::Body)
        {
            const bool activeAltar = object.type == 0x51 && pass.crywolf != nullptr &&
                                     pass.crywolf->state == Protocol::Events::CrywolfWarState;
            // 0x430a46 comes after 0x5ffdf0 and before the RGB: it does not pose the object again this frame.
            if (activeAltar)
                object.transform.scale = std::bit_cast<float>(0x3f83d70au);
            if (auto body = MapScripts::Crywolf::CrywolfObjectBody(object, draw, pass); !body.HasValue())
                return body;
            if (activeAltar)
            {
                Actors::Drawing::DrawnObject altar = poseObject;
                altar.color = draw.color;
                if (retained != nullptr)
                    altar.retainedMeshes = retained->meshes;
                if (auto drawn =
                        MapScripts::Crywolf::DrawCrywolfAltarMeshes(batch, models, altar, frameClock, pass.modelState);
                    !drawn.HasValue())
                    return drawn;
                draw.color = altar.color;
            }
        }
        const Actors::Drawing::DrawnObject drawn =
            DrawTheObject(batch, models, object, draw, frameClock, 0.0f, pass.modelState, retained);
        // 0x600cf2..0x600db0: the 0x1c and the 0x1d of 11 to 17, after their RenderBody, the shadow with the terrain
        // height; +0x58 stays at −1.
        if (bloodCastle && (object.type == BloodCastleShadowedFirst || object.type == BloodCastleShadowedSecond) &&
            draw.kind == WorldObjects::ObjectDrawKind::Body)
        {
            if (!pass.terrainHeight)
                return Core::Unexpected(std::string("0x600d80: falta la altura del terreno (0x5f93c0) para la sombra "
                                                    "del objeto"));
            Actors::Drawing::DrawBloodCastleShadow(
                batch, models, drawn, pass.terrainHeight(object.transform.position.x, object.transform.position.y),
                frameClock);
            object.state.hiddenMesh = Models::NoHiddenMesh;
        }
        if (Maps::IsKalimaMap(pass.map) && draw.kind != WorldObjects::ObjectDrawKind::Hidden)
        {
            if (auto tail = MapScripts::Kalima::DrawKalimaObjectTail(batch, models, object, drawn, frameClock, pass);
                !tail.HasValue())
                return tail;
        }
        // 0x425260 (from 0x602ed0): Aida's 0x29 and 0x41/0x42 repeat their mesh 0 with 0x4ecc30. With no rand() or
        // state, they go in every MuRet draw; the order with the common rand() changes nothing.
        if (pass.map == AidaMap || pass.map == SelectionWorldMap)
        {
            if (auto meshPass =
                    Actors::Drawing::DrawAidaMeshPass(batch, models, drawn, object.type, pass.worldClock, frameClock);
                !meshPass.HasValue())
                return meshPass;
        }
        // 0x604a4d: 0x602ed0, once per original frame.
        if (!pass.originalFrame)
        {
            continue;
        }
        const int commonRandom = pass.random->Next();
        if (auto frameCase = FrameCaseOf(object, pass, models, drawn, commonRandom, draw.posed); !frameCase.HasValue())
        {
            return frameCase;
        }
    }
    return {};
}

Core::Expected<void, std::string> DrawGroundEffects(Render::ModelBatch& batch, std::span<Effects::ModelEffect> effects,
                                                    const Effects::GroundEffectResources& resources)
{
    // What the draw creates (0x52e04c, 0x52e0a2) goes to the same table being walked.
    Effects::GroundEffectResources onTable = resources;
    onTable.table = effects;
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        Effects::ModelEffect& effect = effects[slot];
        // 0x52dc2d..0x52dc40: does not recompute the sphere, reads this place's history.
        if (!effect.alive || !effect.seen)
        {
            continue;
        }
        static_cast<void>(resources.random.Next()); // 0x52dc46, even without a ground case.
        Effects::ApplyGroundEffectAdditive(batch);  // 0x620a90, before dispatching by type.
        if (auto drawn = Effects::DrawGroundEffectCase(batch, effect, onTable); !drawn.HasValue())
        {
            return Core::Unexpected(drawn.Error() + std::format(", en el efecto {} (el {:#x})", slot, effect.type));
        }
    }
    return {};
}

void StepModelEffects(std::span<Effects::ModelEffect> effects, const Models::IModelLibrary& models)
{
    for (Effects::ModelEffect& effect : effects)
    {
        if (effect.alive)
        {
            Effects::StepModelEffect(effect,
                                     models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(effect.type)}));
        }
    }
}

void DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                      std::span<Effects::ModelEffect> effects, const Terrain::SphereFrustum& frustum,
                      const Terrain::TerrainLighting* lighting, Core::Duration frameClock, float alpha)
{
    // Without a callback there is no error exit: keeps the 7-argument legacy API.
    static_cast<void>(DrawModelEffects(batch, models, effects, frustum, lighting, frameClock, alpha, {}, {}));
}

Core::Expected<void, std::string> DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   std::span<Effects::ModelEffect> effects,
                                                   const Terrain::SphereFrustum& frustum,
                                                   const Terrain::TerrainLighting* lighting, Core::Duration frameClock,
                                                   float alpha, const Actors::Drawing::TailPoseObserver& onPose,
                                                   const Actors::Drawing::TailMeshProvider& posedMeshes)
{
    for (Effects::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        // 0x52d63c..0x52d655: publishes +0x16c before the type, the model or the alpha. The next frame's ground pass
        // reads it as is. The sphere uses +0x10 without MuRet's extrapolation.
        constexpr float Radius = 400.0f;
        effect.seen = Terrain::SphereVisible(frustum, effect.transform.position, Radius);
        if (!effect.seen)
        {
            continue;
        }
        // 0x52da84: these types only have the ground pass; even without a model they publish seen. Icarus' 0x5ac
        // (0x52daa1) is not drawn: it shows through its 0x5ac/1 particles.
        if (effect.type == 0x61d || effect.type == 0x61e || effect.type == 0x61f ||
            effect.type == Effects::IcarusFlareEffect)
        {
            continue;
        }
        const glm::vec3 terrain =
            lighting != nullptr ? Terrain::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                                : glm::vec3(0.0f);
        Actors::Drawing::DrawnObject drawn;
        drawn.model = static_cast<std::uint16_t>(effect.type);
        drawn.animation = effect.animation;
        drawn.transform = effect.transform;
        drawn.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
        drawn.state = effect.objectState;
        // 0x5ffce0: with per-vertex light, the terrain plus the light; without it (the 0xbb cloud), the terrain times
        // 0.1f plus the light, with x unrounded before the sum and y and z rounded (0x5ffda5..0x5ffde7).
        if (effect.vertexLight)
            drawn.color = terrain + effect.light;
        else
        {
            constexpr double Tenth = Math::TenthAsDouble;
            drawn.color.x = static_cast<float>(static_cast<double>(terrain.x) * Tenth + effect.light.x);
            for (int axis = 1; axis < 3; ++axis)
                drawn.color[axis] = static_cast<float>(
                    static_cast<double>(static_cast<float>(static_cast<double>(terrain[axis]) * Tenth)) +
                    effect.light[axis]);
        }
        drawn.alpha = effect.alpha;
        drawn.vertexLight = effect.vertexLight;
        if (onPose && !(drawn.alpha < 0.01f) && models.Find(Models::ModelId{drawn.model}) != nullptr &&
            models.SkeletonOf(Models::ModelId{drawn.model}) != nullptr)
        {
            if (auto published = onPose(drawn); !published.HasValue())
                return published;
            if (auto rendered = Actors::Drawing::DrawPublishedObject(batch, models, drawn, frameClock, posedMeshes);
                !rendered.HasValue())
                return rendered;
        }
        else
            Actors::Drawing::DrawObject(batch, models, drawn, frameClock);
    }
    return {};
}

namespace
{
// 0x549290 with the atlas texture: the cell by its +4 (0x54941c), 64 × 64, with a third of the texture (0x3ea8f5c3
// and 0x3f28f5c3).
constexpr float AtlasSide = 64.0f;
constexpr float AtlasThird = 0.33f;
constexpr float AtlasTwoThirds = 0.66f;

glm::vec2 AtlasCell(std::int32_t cell)
{
    switch (cell)
    {
    case 1:
        return {AtlasThird, 0.0f};
    case 2:
        return {AtlasTwoThirds, 0.0f};
    case 3:
        return {0.0f, AtlasThird};
    case 4:
        return {AtlasThird, AtlasThird};
    case 5:
        return {AtlasTwoThirds, AtlasThird};
    case 6:
        return {0.0f, AtlasTwoThirds};
    case 7:
        return {AtlasThird, AtlasTwoThirds};
    default:
        return {0.0f, 0.0f};
    }
}

Render::ModelBlend SpriteBlend(const Particles::Sprite& sprite)
{
    if (sprite.texture == Particles::AtlasSprite)
    {
        return Render::ModelBlend::Alpha;
    }
    switch (sprite.blend)
    {
    case 1:
        return Render::ModelBlend::Subtractive;
    case 2:
        return Render::ModelBlend::Alpha;
    case 3:
        return Render::ModelBlend::InverseColorAdditive;
    default:
        return Render::ModelBlend::Additive;
    }
}
} // namespace

void DrawFrameSprites(Render::ModelBatch& batch, Particles::FrameSprites& sprites,
                      const Models::IModelLibrary& textures, const Math::OriginalMatrix& view, std::uint8_t mode)
{
    for (Particles::Sprite& sprite : sprites.Sprites())
    {
        // 0x54944e..0x54949e: mode 1 retains; mode 2 also turns off the lower band, NaN included.
        if (mode == 1 && sprite.position.z > 350.0f)
            continue;
        if (mode == 2 && !(sprite.position.z > 300.0f))
        {
            sprite.alive = false;
            continue;
        }
        if (!sprite.alive)
        {
            continue;
        }
        Particles::StepSpriteAlpha(sprite);
        const Render::TextureSlot slot{sprite.texture};
        Render::Billboard billboard;
        // 0x5493de/0x549410 call 0x621400: the components byte picks Color3 or Color4 here too.
        const auto metadata = textures.TextureMetadataOf(sprite.texture);
        constexpr int RgbComponents = 3;
        const int components = metadata ? metadata->components : RgbComponents;
        // The size: the scale times the alpha in the x87, times the texture's width and height.
        const double size = static_cast<double>(sprite.scale) * static_cast<double>(sprite.alpha);
        if (sprite.texture == Particles::AtlasSprite)
        {
            billboard =
                Render::MakeBillboard(view, sprite.position, AtlasSide, AtlasSide, sprite.light, sprite.rotation,
                                      AtlasCell(sprite.blend), glm::vec2(AtlasThird), sprite.texture, components);
        }
        else
        {
            const glm::vec2 texture = textures.TextureSize(slot).value_or(glm::vec2(0.0f));
            const auto width = static_cast<float>(size * static_cast<double>(texture.x));
            const auto height = static_cast<float>(size * static_cast<double>(texture.y));
            billboard = Render::MakeBillboard(view, sprite.position, width, height, sprite.light, sprite.rotation,
                                              glm::vec2(0.0f), glm::vec2(1.0f), sprite.texture, components);
        }
        batch.SetState(Render::ModelDrawState{.texture = slot, .blend = SpriteBlend(sprite)});
        const auto vertex = [&billboard](std::size_t k)
        { return Render::ModelVertex{billboard.corners[k].position, billboard.corners[k].uv, billboard.color}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
        // 0x549440(0) turns it off after drawing it.
        if (mode == 0 || mode == 2)
            sprite.alive = false;
    }
}

Core::Expected<void, Particles::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, Particles::Particles& particles, const Models::IModelLibrary& textures,
              const Math::OriginalMatrix& view, float worldClock, std::uint8_t mode)
{
    bool restoreTextureEnv = false;
    const auto drawn = [&]
    {
        // One adapter per pass imports the material the batch left.
        Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(textures),
                                                  Render::ModelBatchParticleState::ForModelPass());
        Effects::ParticleDrawServices services(std::cref(textures), std::ref(backend), std::cref(view));
        const auto result = Particles::DrawParticlesChecked(particles, mode, std::cref(worldClock), services);
        restoreTextureEnv = !result.HasValue() && services.HasOpenTextureEnvOverride();
        return result;
    }();
    // 0x548a4a restores GL_MODULATE after the billboard. If MuRet interrupts the pass, it closes the incomplete quad
    // first and completes only this restore.
    if (restoreTextureEnv)
        batch.SetTextureEnv(Render::TextureEnvMode::Modulate);
    return drawn;
}

void DrawLeaves(Render::ModelBatch& batch, const Weather::Leaves& leaves)
{
    const Render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    for (const Weather::Leaf& leaf : leaves.All())
    {
        if (!leaf.alive)
        {
            continue;
        }
        const Weather::LeafQuad quad = Weather::LeafQuadOf(leaf);
        batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(leaf.type)},
                                              .blend = Render::ModelBlend::Additive});
        // glTranslatef and the vertex: GL does the sum, in float.
        const auto vertex = [&quad, &white](std::size_t k)
        { return Render::ModelVertex{quad.translation + quad.corners[k], quad.uv[k], white}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
    }
}
} // namespace Client::WorldObjects
