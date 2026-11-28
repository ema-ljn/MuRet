#include "Pets/HelperTable.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/PlayerActions.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Pets/DarkHorse.h"
#include "Actors/Drawing/GameRibbonCreation.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Pets/HelperStepParts.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Pets/PetHelperSteps.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Particles/WeaponSparkKernels.h"
#include "Terrain/WorldTerrain.h"
#include "CastleSiege/CastleSiegeState.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Pets
{
namespace
{
// 0x443faa..0x443fb3 and 0x5ffdff..0x5ffe07: maps 18 to 23.
using Maps::IsChaosCastle;
// 0x443fc0 and 0x4452c2: the helper that does not need a player rider.
constexpr std::uint16_t RiderlessHelper = GuardianAngelModel;
// 0x443e82: the 0xbf's scale on creation; 0x445309, the draw's in scene 5.
constexpr float HorseScale = 1.0f;
constexpr float RidingHelperScale = 0.9f;
constexpr float GuardianScale = 0.7f;
// 0x4452d2: 0x3f3ae148 with 0x445260's second argument not 0 (the card portrait).
constexpr float PortraitHelperScale = std::bit_cast<float>(0x3f3ae148u);

bool IsFenrir(std::uint16_t type)
{
    return type >= FirstFenrirModel && type <= LastFenrirModel;
}

bool IsRidingHelper(std::uint16_t type)
{
    return type == UniriaModel || type == DinorantModel || IsFenrir(type);
}
// 0x443e4b and 0x443df7: speed (+0xd8) and alpha (+0x174).
constexpr float CreatedSpeed = 0.5f;
constexpr float CreatedAlpha = 0.0f;
// 0x4440f2: the EBX at 0 of 0x44401e.
constexpr float HiddenAlpha = 0.0f;
// 0x44403e: with the rider's +0x88 not 1, scene 5 turns it off.
constexpr std::uint8_t RiderShown = 1;
// The rider (+0x111) and horse actions that the dust, the breath and the effect check.
constexpr std::uint8_t GallopingRiderAction = Tables::HorseWalkAction;
constexpr std::uint8_t DustHorseAction = 1;
constexpr std::uint8_t EffectHorseAction = 3;
// 0x444196: on map 10 the dust differs; 0x4445a9: on 7 the breath is the 607 drop.
using Maps::AtlansMap;
using Maps::IcarusMap;
// 0x444414..0x44441d and 0x44455a..0x444563: with 0x4000 in the rider's +0x78, no dust or breath; 0x445310 draws with
// 10. 0x5ffefc: with 0x1000, the horse's frame at 0.
constexpr std::uint32_t RiderWithoutDust = 0x4000;
constexpr std::uint32_t RiderFrozen = 0x1000;
// 0x4443e3 and 0x44456b: rand() % 2 and rand() % 3 == 0.
constexpr int DustOneIn = 2;
constexpr int BreathOneIn = 3;
// 0x4445b5..0x4445c8: the breath light, +0xf4, the 3 from 0x443d70 (nobody else writes it).
constexpr glm::vec3 HorseLight{3.0f, 3.0f, 3.0f};
constexpr std::int32_t ParticleSubtype = 0;
// 0x444588..0x44459d: bone 27 of the global buffer (0x68b1d2c).
constexpr std::size_t BreathBone = 27;
constexpr glm::vec3 BreathOffset{50.0f, -4.0f, 0.0f};
// 0x444693: with 1 at the rider's +8, the twelve 0x60f. Only 0x421db0 sets it to 1, on the hero, with the pets' A9 and
// its byte 4 at 0xfe (0x4220b1..0x4220c1), not ported.
constexpr std::uint8_t RiderWithOrbits = 1;
// 0x5ffe30: the alpha down to which 0x5ffdf0 still draws.
constexpr float DrawnAlpha = 0.01f;
// 0x443f1a: the 0x1c12's blendMesh; the others, the −1 of 0x443d70.
constexpr std::int16_t GuardianBlendMesh = 1;
constexpr std::int16_t NoBlendMesh = -1;
// 0x445336..0x445390: rand() % 30 + 70 times 0.01f (0x6b88d4) and, with 0x1c12, the 0x5ac sprite (Effect\flare01.jpg),
// unrotated and additive, with that light times (0.5; 0.8; 0.6) (0x6b882c, 0x6b8a38 and 0x6b8a20).
constexpr int GlowSpan = 30;
constexpr int GlowBase = 70;
constexpr double GlowUnit = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr std::uint16_t GlowSprite = 0x5ac;
constexpr float GlowScale = 1.0f;
constexpr double GlowRed = static_cast<double>(std::bit_cast<float>(0x3f000000u));
constexpr double GlowGreen = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3f19999au));

using Math::F32;

// 0x4441cd..0x4442ad: the global buffer bone by the horse's frame, already in action 1. The windows overlap and the
// first wins; outside all of them (or unordered), none.
std::optional<std::size_t> IcarusDustBone(float frame)
{
    struct Window
    {
        float low;
        float high;
        bool closed; // 0x44429a checks C0 and C3: the last one includes its top.
        std::size_t bone;
    };
    constexpr std::array<Window, 4> Windows{{
        {std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3f99999au), false, 19}, // 0x6b87d4, 0x6b8bb0
        {std::bit_cast<float>(0x3f8ccccdu), std::bit_cast<float>(0x3fb33333u), false, 25}, // 0x6b8830, 0x6b8c00
        {std::bit_cast<float>(0x3fa66666u), std::bit_cast<float>(0x3fcccccdu), false, 32}, // 0x6b8ac0, 0x6b8bfc
        {std::bit_cast<float>(0x3fc00000u), std::bit_cast<float>(0x3fe66666u), true, 38},  // 0x6b8b94, 0x6b8bf8
    }};
    for (const Window& window : Windows)
    {
        const bool belowTop = window.closed ? frame <= window.high : frame < window.high;
        if (frame > window.low && belowTop)
        {
            return window.bone;
        }
    }
    return std::nullopt;
}

// 0x44419f..0x4443d1, the gallop on map 10, ignoring Kalima and the rider's +0x78: (rand() & 63) − 32 for the z of the
// offset (120; 0; z) before looking at the frame.
Core::Expected<void, std::string> RaiseTheIcarusDust(const Kernel::HelperState& helper,
                                                     const HelperStepContext& context)
{
    constexpr float Reach = 120.0f;                                                 // 0x44419f
    constexpr double Drop = static_cast<double>(std::bit_cast<float>(0x42000000u)); // 0x6b883c: 32.
    constexpr int LiftSpread = 64, WaveSpread = 16, WaveCentre = 8;
    Crt::CrtRandom& random = context.random;
    const int lift = random.Next() % LiftSpread;
    const glm::vec3 offset(Reach, 0.0f, F32(static_cast<double>(lift) - Drop));
    const std::optional<std::size_t> bone = IcarusDustBone(helper.horse.animation.frame);
    if (!bone)
    {
        return {};
    }
    const auto point = GlobalPosePoint(context.world, *bone, offset, "0x4442b4");
    if (!point.HasValue())
    {
        return Core::Unexpected(point.Error());
    }
    const glm::vec3 base = point.Value();
    const int waveX = (random.Next() % WaveSpread) - WaveCentre;
    const int waveY = (random.Next() % WaveSpread) - WaveCentre;
    const glm::vec3 wave(F32(static_cast<double>(waveX) + static_cast<double>(base.x)),
                         F32(static_cast<double>(waveY) + static_cast<double>(base.y)), base.z);
    static_cast<void>(context.world.Map().effects.CreateGallopShockWave(wave, helper.angles, HelperDustLight, random));
    return PuffTheIcarusDust(base, helper.angles, context, "0x4443b1");
}

// 0x44416b..0x4444ef with the rider's action 0x4b, after 0x4ea030(1). 0x44440a..0x444412: with no rider, nothing
// (0x443fa0 always gives one). In ScatterTheDust, the 10 of 0x4444c0 is never reached.
Core::Expected<void, std::string> RaiseTheDust(const Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                               const HelperStepContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    if (map == IcarusMap)
    {
        return RaiseTheIcarusDust(helper, context);
    }
    if (context.random.Next() % DustOneIn != 0)
    {
        return {};
    }
    // 0x4443f6..0x444408.
    if (Maps::IsKalimaMap(map) || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    return ScatterTheDust(helper, context, "0x4444a6");
}

// 0x44453f..0x4445de
Core::Expected<void, std::string> Breathe(const Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                          const HelperStepContext& context)
{
    if (helper.horse.animation.action == DustHorseAction || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    if (context.random.Next() % BreathOneIn != 0)
    {
        return {};
    }
    const auto point = GlobalPosePoint(context.world, BreathBone, BreathOffset, "0x44459d");
    if (!point.HasValue())
    {
        return Core::Unexpected(point.Error());
    }
    const std::int32_t type =
        context.world.Runtime().map == AtlansMap ? Particles::DropParticle : std::int32_t{Particles::BreathParticle};
    return EmitHelperParticle(context, type, ParticleSubtype, point.Value(), helper.angles, HorseLight, "0x4445d9");
}

// 0x4445e1..0x444654: the 0x52e470 ribbon with the hero (0x743a170) as owner, on bone 9 of the global buffer
// (0x68b19cc). No rand().
constexpr std::size_t EarthquakeRibbonBone = 9;
constexpr glm::vec3 EarthquakeRibbonReach{60.0f, 0.0f, 0.0f}; // 0x4445ff
constexpr glm::vec3 EarthquakeRibbonLight{1.0f, 1.0f, 1.0f};  // 0x444629..0x444637
// 0x4453e0: the wave count (0x7d3a0f0 % 10), the frame window (0x6b86c4 and 0x6b8c04), the stone radius per two counts
// (0x6b87ac), the turn between stones (0x6b86cc), the shake (0x6b8a5c) and the counts that create the 0xfa and follow.
constexpr std::int32_t ShockWaveEvery = 10;
constexpr float QuakeFirstFrame = 8.0f;
constexpr float QuakeLastFrame = 9.5f;
constexpr double StoneRingStep = 150.0;
constexpr double StoneTurn = 60.0;
constexpr int StoneCount = 6;
constexpr int StoneDegrees = 360;
constexpr int ShakeSpan = 3;
constexpr double ShakeUnit = static_cast<double>(std::bit_cast<float>(0x3f333333u));
constexpr std::uint8_t EarthquakeCount = 0x13;
constexpr std::uint8_t EarthquakeDone = 0xfd;
constexpr std::uint8_t EarthquakeState = 2; // 0x44558e, the 0xfa's state (+0x88)

Core::Expected<void, std::string> LeaveTheEarthquakeRibbon(const HelperStepContext& context)
{
    const auto first = GlobalPosePoint(context.world, EarthquakeRibbonBone, EarthquakeRibbonReach, "0x44463e");
    if (!first.HasValue())
    {
        return Core::Unexpected(first.Error());
    }
    const auto second = GlobalPosePoint(context.world, EarthquakeRibbonBone, glm::vec3(0.0f), "0x444654");
    if (!second.HasValue())
    {
        return Core::Unexpected(second.Error());
    }
    Kernel::ActorState& hero = context.world.Hero();
    const auto created = Actors::Drawing::CreateRibbon(
        context.world.DrawTail().ribbons, Actors::Drawing::RibbonCreateRequest{.ownerContext = &hero,
                                                                               .ownerEquipment = &hero.level,
                                                                               .first = first.Value(),
                                                                               .second = second.Value(),
                                                                               .light = EarthquakeRibbonLight,
                                                                               .type = 0,
                                                                               .shortTrail = false,
                                                                               .key = 0});
    if (!created.HasValue())
    {
        return Core::Unexpected("0x444671 → " + created.Error());
    }
    return {};
}

// 0x4453e0(horse, model), with action 3. 0x7d3a0f0 is signed; a NaN frame falls outside [8; 9.5]; each stone's type is
// drawn after the turn and before its creation. The 0xfa (state 2) opens and closes the own record: 3848 rand() with
// the depth at 0.
Core::Expected<void, std::string> ShakeTheEarth(Kernel::HelperState& helper, const HelperStepContext& context)
{
    ++helper.horse.effectCount;
    Effects::ModelEffects& effects = context.world.Map().effects;
    if (context.logicSteps % ShockWaveEvery == 0)
    {
        static_cast<void>(effects.CreateStompShockWave(helper.position, helper.angles, helper.light)); // 0x445425
    }
    const float frame = helper.horse.animation.frame;
    if (frame >= QuakeFirstFrame && frame <= QuakeLastFrame)
    {
        if ((helper.horse.effectCount & 1) != 0)
        {
            const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
            const glm::vec3 reach(0.0f, F32(static_cast<double>(helper.horse.effectCount >> 1) * StoneRingStep), 0.0f);
            glm::vec3 turn(0.0f, 0.0f, static_cast<float>(context.random.Next() % StoneDegrees));
            for (int stone = 0; stone < StoneCount; ++stone)
            {
                turn.z = F32(static_cast<double>(turn.z) + StoneTurn);
                const glm::vec3 offset = Math::OriginalVectorRotate(reach, Math::OriginalAngleMatrix(turn));
                const glm::vec3 place(F32(static_cast<double>(offset.x) + static_cast<double>(helper.position.x)),
                                      F32(static_cast<double>(offset.y) + static_cast<double>(helper.position.y)),
                                      F32(static_cast<double>(offset.z) + static_cast<double>(helper.position.z)));
                const auto type =
                    static_cast<std::int16_t>(Effects::FirstGroundStoneEffect + (context.random.Next() & 1));
                const auto created = effects.CreateGroundStone(type, place, helper.angles, helper.light,
                                                               terrain.attributes.get(), context.random);
                if (!created.HasValue())
                {
                    return Core::Unexpected("0x44554a → " + created.Error());
                }
            }
        }
        if (context.cameraShake == nullptr)
        {
            return Core::Unexpected(std::string("0x445577: falta el temblor de la cámara (0x7d325c8)"));
        }
        *context.cameraShake = F32(static_cast<double>((context.random.Next() % ShakeSpan) - ShakeSpan) * ShakeUnit);
        return {};
    }
    if (helper.horse.effectCount == EarthquakeCount)
    {
        // 0x44558a..0x4455b3: the owner (its type and angles) is read live while the 0xfa lasts.
        static_cast<void>(effects.CreateEarthquake(
            helper.position, helper.angles, helper.light, EarthquakeState,
            Effects::ModelEffectOwner{.type = &helper.type, .effect = nullptr, .angles = &helper.angles},
            context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random));
        helper.horse.effectCount = EarthquakeDone;
    }
    return {};
}

// 0x44469d..0x44474c, with the rider's +8 at 1: the matrix of 0x5fc8f0 and 0x5fcbd0, each axis stored as float; per
// turn, three equal 0x530860(0x60f, point, point, angles, 0, horse, 40, 2, 0, 0, −1) owned by the horse: they are born
// with its light (+0xf4) and, with width 40, the step does not read it again. Three rand() per joint; with the pool
// full, none.
Core::Expected<void, std::string> RaiseTheOrbits(Kernel::HelperState& helper, const HelperStepContext& context)
{
    constexpr int Turns = 4;                                                            // 0x4446c7
    constexpr int JointsPerTurn = 3;                                                    // 0x444705
    constexpr double TurnStep = static_cast<double>(std::bit_cast<float>(0x42b40000u)); // 0x6b8948: 90.
    constexpr std::int32_t OrbitJoint = 0x60f, OrbitSubtype = 0, OrbitKey = 2, NoTarget = -1;
    constexpr float OrbitWidth = 40.0f;       // 0x444727
    const glm::vec3 reach{0.0f, 50.0f, 0.0f}; // 0x4446b2..0x4446c0
    glm::vec3 angles{};                       // 0x44469d..0x4446ab
    for (int turn = 0; turn < Turns; ++turn)
    {
        angles.z = F32(static_cast<double>(angles.z) + TurnStep);
        const glm::vec3 rotated = Math::OriginalVectorRotate(reach, Math::OriginalAngleMatrix(angles));
        glm::vec3 point{F32(static_cast<double>(rotated.x) + static_cast<double>(helper.position.x)),
                        F32(static_cast<double>(rotated.y) + static_cast<double>(helper.position.y)),
                        F32(static_cast<double>(rotated.z) + static_cast<double>(helper.position.z))};
        for (int made = 0; made < JointsPerTurn; ++made)
        {
            const Kernel::JointRequest request{.type = OrbitJoint,
                                               .firstPosition = point,
                                               .secondPosition = point,
                                               .angles = angles,
                                               .subtype = OrbitSubtype,
                                               .ownerContext = &helper,
                                               .scale = OrbitWidth,
                                               .key = OrbitKey,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = NoTarget};
            const auto created = Joints::CreateFaunaJoint(context.world.DrawTail().joints, request, context.random,
                                                          Kernel::JointOwnerKind::Helper);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("0x444740: {}", created.Error()));
            }
        }
    }
    return {};
}

// 0x4440b8..0x444766
Core::Expected<void, std::string> StepTheDarkHorse(Kernel::HelperState& helper, Kernel::ActorState& rider,
                                                   const Models::Skeleton& skeleton, const HelperStepContext& context)
{
    const auto safe = InTheRidersSafeCell(context, rider, "0x4440db");
    if (!safe.HasValue())
    {
        return Core::Unexpected(safe.Error());
    }
    // 0x4440eb: with the second argument not 0 (the card portrait), the cell does not hide it.
    if (safe.Value() && !context.ignoresTheSafeCell)
    {
        // 0x4440f2..0x4440f8: on to the advance with the action and speed it had.
        helper.horse.alpha = HiddenAlpha;
        return {};
    }
    // 0x444103..0x444142: the model's head bone (+0x54) at 7 and the rider's head (+0x28..+0x30): in DarkHorse.bmd bone
    // 7 is a childless root no vertex uses, and MuRet does not keep it.
    helper.position = rider.position;
    helper.angles = rider.angles;
    const std::uint8_t riderAction = rider.animation.action;
    // 0x444145..0x444535. With another action, 0x444523 also sets +0x8e to 0, which only the action 3 effect (0x4453e0,
    // below) reads.
    TakeTheRidersGait(helper.horse, skeleton, riderAction);
    if (riderAction == GallopingRiderAction)
    {
        if (auto dust = RaiseTheDust(helper, rider, context); !dust.HasValue())
        {
            return dust;
        }
    }
    if (auto breath = Breathe(helper, rider, context); !breath.HasValue())
    {
        return breath;
    }
    // 0x4445e1..0x44468a: action 3, the Dark Lord's earthquake: the ribbon and 0x4453e0 (the action is checked again
    // between the two, and the ribbon does not change it).
    if (helper.horse.animation.action == EffectHorseAction)
    {
        if (auto ribbon = LeaveTheEarthquakeRibbon(context); !ribbon.HasValue())
        {
            return ribbon;
        }
        if (auto quake = ShakeTheEarth(helper, context); !quake.HasValue())
        {
            return quake;
        }
    }
    if (rider.byte8 == RiderWithOrbits)
    {
        if (auto orbits = RaiseTheOrbits(helper, context); !orbits.HasValue())
        {
            return orbits;
        }
    }
    rider.byte8 = 0;            // 0x444758
    helper.alive = rider.alive; // 0x44475c..0x444764
    return {};
}

// 0x444059..0x4440b3: the body by type. 0xae (0x44476b) is not created in MuRet; the other types go straight to the
// advance, but 0x443d70 does not build them.
bool HasItsBody(std::uint16_t type)
{
    return type == DarkHorseModel || type == RiderlessHelper || IsRidingHelper(type);
}

Core::Expected<void, std::string> StepTheBody(Kernel::HelperState& helper, Kernel::ActorState& rider,
                                              const Models::Skeleton& skeleton, const HelperStepContext& context)
{
    if (helper.type == DarkHorseModel)
    {
        return StepTheDarkHorse(helper, rider, skeleton, context);
    }
    if (helper.type == RiderlessHelper)
    {
        return SparkleTheGuardianAngel(helper, rider, context);
    }
    if (IsFenrir(helper.type))
    {
        return StepFenrir(helper, rider, skeleton, context);
    }
    return StepRidingHelper(helper, rider, skeleton, context);
}

// 0x444010(slot, 0) with a live slot, in scene 4 or 5, with its rider (+0x108).
Core::Expected<void, std::string> StepHelper(Kernel::HelperState& helper, Kernel::ActorState& rider,
                                             const HelperStepContext& context)
{
    // 0x44402a..0x444047
    if (context.scene == Kernel::SceneId::Game && (!rider.alive || rider.byte88 != RiderShown))
    {
        helper.alive = false;
        return {};
    }
    FadeInDarkHorse(helper.horse); // 0x444054
    if (!HasItsBody(helper.type))
    {
        return Core::Unexpected(std::format("0x444083: el paso del ayudante {:#x} no está portado", helper.type));
    }
    const Models::Skeleton* skeleton =
        context.models != nullptr ? context.models->SkeletonOf(Models::ModelId{helper.type}) : nullptr;
    if (skeleton == nullptr)
    {
        return Core::Unexpected(
            std::format("0x444010: el ayudante {:#x} sin su modelo ({})", helper.type,
                        helper.type == RiderlessHelper ? "Player\\Helper01.bmd, 0x60ff50" : "Skill\\, 0x615700"));
    }
    // 0x444080..0x44408b: where the rider was at the start, for the 0x1c12's flight.
    const glm::vec2 riderStart(rider.position.x, rider.position.y);
    if (auto body = StepTheBody(helper, rider, *skeleton, context); !body.HasValue())
    {
        return body;
    }
    // 0x445006..0x445039: the model's +0xa0 with the action, which the ported code does not read, and the advance.
    AdvanceDarkHorse(helper.horse, *skeleton,
                     context.scene == Kernel::SceneId::CharacterSelect ? Models::AnimationScene::CharacterSelect
                                                                       : Models::AnimationScene::Other);
    if (helper.type == RiderlessHelper)
    {
        FlyTheGuardianAngel(helper, rider, riderStart, context.random);
    }
    return {};
}

// 0x4452db..0x445309, with the second argument at 0: 1.6 in scene 2 and 1 in 4; in the others the Fenrir keep
// 0x443d70's (0.9) and the rest go to 1.
float DrawScaleOf(const Kernel::HelperState& helper, Kernel::SceneId scene)
{
    if (scene == Kernel::SceneId::Login)
    {
        return DarkHorseLoginScale; // 0x3fcccccd
    }
    if (scene != Kernel::SceneId::CharacterSelect && IsFenrir(helper.type))
    {
        return helper.scale;
    }
    return HorseScale;
}

// A helper other than the horse, drawn between two steps: the 0x5ffce0 color with per-vertex light (the terrain's plus
// +0xf4, the 3 of 0x443d70).
Actors::Drawing::DrawnObject DrawnHelper(const Kernel::HelperState& helper, const Models::Skeleton* skeleton,
                                         const glm::vec3& terrainLight, float stepAlpha)
{
    Actors::Drawing::DrawnObject drawn;
    drawn.model = helper.type;
    drawn.animation = helper.horse.animation;
    if (skeleton != nullptr && helper.horse.animation.action < skeleton->actions.size())
    {
        drawn.animation = Models::Interpolated(helper.horse.animation, skeleton->actions[helper.horse.animation.action],
                                               helper.horse.speed, stepAlpha);
    }
    drawn.transform = Models::ObjectTransform{.position = helper.position, .angles = helper.angles};
    drawn.transform.scale = helper.scale;
    drawn.state.blendMesh = helper.blendMesh;
    // 0x5ffd6a..0x5ffd8b (0x5ffce0): the own light is +0xf4..+0xfc.
    drawn.color = terrainLight + helper.light;
    drawn.alpha = helper.horse.alpha;
    drawn.vertexLight = true;
    return drawn;
}

// 0x60277b..0x602e7d
constexpr double FenrirPulseRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc: 0.002f.
constexpr float FenrirPulseSwing = std::bit_cast<float>(0x3e4ccccdu);                      // 0x6b8854: 0.2f.
constexpr std::uint8_t FenrirChargeAction = 4;
constexpr std::uint8_t FenrirFirstStrikeAction = 1, FenrirSecondStrikeAction = 2;
constexpr int FenrirSparkSpreadX = 10, FenrirSparkSpreadZ = 40, FenrirSparkCentreZ = 20;
constexpr double Half = Math::HalfAsDouble;                                                     // 0x6b882c.
constexpr double FenrirSparkScaleStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8a48.
constexpr double FenrirSparkScaleBase = static_cast<double>(std::bit_cast<float>(0x3f333333u)); // 0x6b8a5c.
constexpr std::size_t FenrirSparkBone = 14, FenrirEyeBone = 11, FenrirChestBone = 13;
constexpr double FenrirEyeRed = static_cast<double>(std::bit_cast<float>(0x3f666666u));   // 0x6b8a1c: 0.9f.
constexpr double FenrirEyeGreen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854: 0.2f.
constexpr double FenrirEyeBlue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));  // 0x6b87b8: 0.1f.
constexpr double FenrirEyeScaleStep = FenrirEyeBlue;                                      // 0x6b87b8.
constexpr glm::vec3 FenrirLeftEye{50.0f, 2.0f, 11.0f}, FenrirRightEye{50.0f, 2.0f, -11.0f};
constexpr glm::vec3 FenrirChest{40.0f, 15.0f, 0.0f};
constexpr glm::vec3 FenrirChestLight{1.0f, std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e4ccccdu)};
constexpr float FenrirChestScale = 1.5f;
constexpr std::uint16_t FenrirFlare = 0x5ac;
constexpr std::uint16_t RedFenrir = 0x14b, BlueFenrir = 0x14c;
constexpr std::array<std::size_t, 4> FenrirStrikeBones{22, 28, 36, 44};

// The bones above are all under the buffer's 200.
glm::vec3 FenrirBonePoint(std::span<const Math::BoneTransform> bones, std::size_t bone, const glm::vec3& offset)
{
    return GlobalPosePoint(bones, bone, offset, "0x4ec110").Value();
}

// 0x60277b..0x602e7d, after 0x5ffdf0. It asks for everything before drawing the first rand().
Core::Expected<void, std::string> DrawTheFenrir(Kernel::HelperState& helper, const HelperDrawContext& context)
{
    if (context.effects == nullptr || context.particles == nullptr || context.sprites == nullptr ||
        !context.systemTime || !context.globalPose)
    {
        return Core::Unexpected(std::format("0x60277b: el dibujo del Fenrir {:#x} pide los efectos (0x518cf0), las "
                                            "partículas (0x53fe70), los sprites (0x5491e0), timeGetTime() y el buffer "
                                            "global de la pose",
                                            helper.type));
    }
    // 0x60277b..0x602792: fld clock, fmul 0.002f, fsin, fmul 0.2f and fstp.
    const auto sine =
        Math::Detail::SineProductPC53(static_cast<double>(context.worldClock) * FenrirPulseRate, FenrirPulseSwing);
    if (!sine)
        return Core::Unexpected(std::string("0x60278a: el seno del reloj del Fenrir no cabe en el perfil PC53"));
    const double pulse = static_cast<double>(F32(*sine));
    const std::int32_t map = context.world.Runtime().map;
    auto drawn = DrawnHelper(helper, context.models.SkeletonOf(Models::ModelId{helper.type}), glm::vec3(0.0f),
                             context.stepAlpha);
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    const bool charging = helper.horse.animation.action == FenrirChargeAction;
    if (auto body =
            Actors::Drawing::DrawFenrirBody(context.batch, context.models, drawn, map, charging, context.frameClock);
        !body.HasValue())
        return body;
    const std::span<const Math::BoneTransform> bones = context.globalPose();
    Crt::CrtRandom& random = context.random;
    if (charging)
    {
        // 0x60282f..0x6028c6
        const int x = (random.Next() % FenrirSparkSpreadX) - FenrirSparkSpreadX;
        const int z = (random.Next() % FenrirSparkSpreadZ) - FenrirSparkCentreZ;
        const glm::vec3 offset(F32(static_cast<double>(x) * Half), 0.0f, F32(static_cast<double>(z) * Half));
        const glm::vec3 point = FenrirBonePoint(bones, FenrirSparkBone, offset);
        const float scale = F32(pulse * FenrirSparkScaleStep + FenrirSparkScaleBase);
        const auto created = context.particles->CreateChecked(
            Particles::SparkParticle, point, helper.angles, glm::vec3(1.0f, 0.0f, 0.0f), Particles::FenrirSparkSubtype,
            scale, context.worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x6028c6: el alta de 0x5c6/15 falla con error {}",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x602930..0x602aa2
    const double halfPulse = pulse * Half;
    const glm::vec3 eyeLight(F32(pulse + FenrirEyeRed), F32(halfPulse + FenrirEyeGreen),
                             F32(halfPulse + FenrirEyeBlue));
    const float eyeScale = F32(pulse * FenrirEyeScaleStep + Half);
    for (const glm::vec3& eye : {FenrirLeftEye, FenrirRightEye})
    {
        const glm::vec3 point = FenrirBonePoint(bones, FenrirEyeBone, eye);
        static_cast<void>(context.sprites->Create(FenrirFlare, point, eyeScale, eyeLight, 0.0f, 0));
        static_cast<void>(context.sprites->Create(FenrirFlare, point, eyeScale, eyeLight, 0.0f, 0));
    }
    const glm::vec3 chest = FenrirBonePoint(bones, FenrirChestBone, FenrirChest);
    static_cast<void>(context.sprites->Create(FenrirFlare, chest, FenrirChestScale, FenrirChestLight, 0.0f, 0));
    static_cast<void>(context.sprites->Create(FenrirFlare, chest, 1.0f, FenrirChestLight, 0.0f, 0));
    // 0x602aa7..0x602b50: two 0x518cf0(0x14d, place, angles, light, 0, Fenrir, −1).
    glm::vec3 strikeLight(1.0f, 1.0f, std::bit_cast<float>(0x3e4ccccdu));
    std::int32_t strike = 3;
    if (helper.type == RedFenrir)
    {
        strikeLight = glm::vec3(std::bit_cast<float>(0x3f4ccccdu), 0.0f, 0.0f);
        strike = 1;
    }
    else if (helper.type == BlueFenrir)
    {
        strikeLight = glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu),
                                std::bit_cast<float>(0x3f4ccccdu));
        strike = 2;
    }
    const auto bonePoint = [bones](std::size_t bone) { return FenrirBonePoint(bones, bone, glm::vec3(0.0f)); };
    for (int bolt = 0; bolt < 2; ++bolt)
    {
        const auto created = context.effects->CreateLightning(helper.position, helper.angles, strikeLight, &helper,
                                                              random, bonePoint, *context.sprites);
        if (!created.HasValue())
            return Core::Unexpected("0x602b30 → " + created.Error());
    }
    // 0x602b55..0x602e75: the 0x14e decals.
    const std::uint8_t action = helper.horse.animation.action;
    const float frame = helper.horse.animation.frame;
    std::span<const std::size_t> strikes{};
    if (action == FenrirFirstStrikeAction && frame >= 0.0f && frame <= 1.5f)
        strikes = FenrirStrikeBones;
    else if (action == FenrirSecondStrikeAction && frame > 1.0f && frame <= std::bit_cast<float>(0x3fb33333u))
        strikes = std::span(FenrirStrikeBones).first(2);
    else if (action == FenrirSecondStrikeAction && frame > std::bit_cast<float>(0x4099999au) &&
             frame <= std::bit_cast<float>(0x40a66666u))
        strikes = std::span(FenrirStrikeBones).last(2);
    for (const std::size_t bone : strikes)
    {
        const glm::vec3 point = FenrirBonePoint(bones, bone, glm::vec3(0.0f));
        static_cast<void>(context.effects->CreateLightningDecal(point, helper.angles, glm::vec3(1.0f), strike, &helper,
                                                                context.systemTime()));
    }
    return {};
}

// 0x6002e0, with the helper already posed by 0x5ffdf0.
Core::Expected<void, std::string> DrawTheBody(Kernel::HelperState& helper, const HelperDrawContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    if (helper.type == DarkHorseModel)
    {
        const Models::ObjectTransform place{.position = helper.position, .angles = helper.angles};
        auto drawn = DrawnDarkHorse(helper.horse, context.models.SkeletonOf(Models::ModelId{DarkHorseModel}), place,
                                    helper.scale, context.stepAlpha);
        if (context.onPose)
            if (const auto published = context.onPose(drawn); !published.HasValue())
                return published;
        if (context.posedMeshes)
            drawn.retainedMeshes = context.posedMeshes();
        Actors::Drawing::DrawDarkHorse(context.batch, context.models, drawn, map, context.frameClock);
        return {};
    }
    if (IsFenrir(helper.type))
        return DrawTheFenrir(helper, context);
    // 0x5ffce0
    if (context.lighting == nullptr)
    {
        return Core::Unexpected(std::format(
            "0x5ffce0: falta el buffer dinámico de la luz del terreno para el ayudante {:#x}", helper.type));
    }
    const glm::vec3 terrainLight = Terrain::LightAt(*context.lighting, helper.position.x, helper.position.y);
    auto drawn =
        DrawnHelper(helper, context.models.SkeletonOf(Models::ModelId{helper.type}), terrainLight, context.stepAlpha);
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    // 0x600b35 (Dinorant) and 0x602360 (Uniria and Guardian Angel): RenderBody(2, alpha, blendMesh, 1, 0, 0, −1, −1).
    Actors::Drawing::DrawObject(context.batch, context.models, drawn, context.frameClock);
    return {};
}

// 0x600343..0x600399: 0x6002e0's path for 10 looks up the rider (+0x108) in the pool's 400 slots (0x743a16c, 0x438
// each). If it is not the hero (0x743a170), with the siege (0x4294b0 reads 0x7239ec), +0x78 & 0x4000 and +0x370 not 1,
// it exits through 0x602e7d without drawing or touching the model's color.
constexpr std::size_t CharacterTableSlots = 400; // 0x69780 / 0x438
constexpr std::uint8_t ShownInTheSiege = 1;

bool HiddenInTheSiege(const Kernel::WorldState& world, std::size_t rider)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    if (rider >= CharacterTableSlots || rider == runtime.hero || !runtime.castleSiege.siegeActive)
    {
        return false;
    }
    const Kernel::ActorState& riding = world.Actors()[rider];
    return (riding.flags78 & RiderWithoutDust) != 0 && riding.byte370 != ShownInTheSiege;
}

// 0x6002e0 with 10 (0x60033c..0x600435), the helper already posed by 0x5ffdf0 as its type's path poses it: it does not
// reach the type's case, so the Fenrir asks for no effects and draws no rand(), and none reads the terrain light, which
// 0x60039d overwrites with white. The 0x1c12 keeps its blendMesh, which the 0x4000 does not check.
Core::Expected<void, std::string> DrawTheShell(Kernel::HelperState& helper, const HelperDrawContext& context)
{
    Actors::Drawing::DrawnObject drawn;
    if (helper.type == DarkHorseModel)
    {
        const Models::ObjectTransform place{.position = helper.position, .angles = helper.angles};
        drawn = DrawnDarkHorse(helper.horse, context.models.SkeletonOf(Models::ModelId{DarkHorseModel}), place,
                               helper.scale, context.stepAlpha);
    }
    else
    {
        drawn = DrawnHelper(helper, context.models.SkeletonOf(Models::ModelId{helper.type}), glm::vec3(0.0f),
                            context.stepAlpha);
    }
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    if (HiddenInTheSiege(context.world, helper.rider))
    {
        return {};
    }
    return Actors::Drawing::DrawChromeShell(context.batch, context.models, drawn, context.frameClock);
}

// 0x445260(slot, `always`) with a live slot and its rider (+0x108). With `always` (the card portrait), seen without the
// trapezoid (0x4452a4) and with 0x4452d2's scale.
Core::Expected<void, std::string> DrawHelper(Kernel::HelperState& helper, const Kernel::ActorState& rider, bool always,
                                             const HelperDrawContext& context)
{
    Kernel::WorldState& world = context.world;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    constexpr Terrain::TerrainPolygon BssTrapezoid{};
    helper.seen = always || Actors::CharacterSeen(context.trapezoid != nullptr ? *context.trapezoid : BssTrapezoid,
                                                  helper.position);
    if (!helper.seen)
    {
        return {};
    }
    if (rider.type != Tables::PlayerSkeletonModel && helper.type != RiderlessHelper)
    {
        return {};
    }
    if (helper.type != DarkHorseModel && helper.type != RiderlessHelper && !IsRidingHelper(helper.type))
    {
        return Core::Unexpected(std::format("0x445260: el dibujo del ayudante {:#x} no está portado", helper.type));
    }
    helper.scale = always ? PortraitHelperScale : DrawScaleOf(helper, context.scene);
    // 0x602e90 → 0x5ffdf0: on maps 18 to 23, with the hero's +0x1a1, alpha drops 0.15f and is stored, once per original
    // frame.
    if (context.originalFrame)
        helper.horse.alpha =
            Events::ChaosCastle::ChaosCastlePoseAlpha(runtime.map, world.Hero().byte1a1, helper.horse.alpha);
    // 0x5ffe2a..0x5ffe3b: below 0.01 (or unordered) it is not drawn.
    if (helper.horse.alpha >= DrawnAlpha)
    {
        if ((rider.flags78 & RiderFrozen) != 0)
        {
            helper.horse.animation.frame = 0.0f; // 0x5fff0d
        }
        // 0x445310..0x445323: 0x602e90(slot, 0, 0, 10).
        const bool shell = (rider.flags78 & RiderWithoutDust) != 0;
        if (auto body = shell ? DrawTheShell(helper, context) : DrawTheBody(helper, context); !body.HasValue())
        {
            return body;
        }
    }
    // 0x445336..0x445358: on the x87; only the 0x1c12 uses it.
    const double glow = static_cast<double>((context.random.Next() % GlowSpan) + GlowBase) * GlowUnit;
    if (helper.type != RiderlessHelper)
    {
        return {};
    }
    if (context.sprites == nullptr)
    {
        return Core::Unexpected(std::string("0x445390: falta la tabla de los sprites del frame (0x5491e0) para el "
                                            "brillo del Guardian Angel"));
    }
    const glm::vec3 light(F32(glow * GlowRed), F32(glow * GlowGreen), F32(glow * GlowBlue));
    static_cast<void>(context.sprites->Create(GlowSprite, helper.position, GlowScale, light, 0.0f, 0));
    return {};
}
} // namespace

Core::Expected<bool, std::string> ConstructHelper(Kernel::HelperState& helper, std::uint16_t type,
                                                  const glm::vec3& position, const Kernel::ActorState& rider,
                                                  std::size_t riderSlot, std::int32_t map, Crt::CrtRandom* random)
{
    // 0x443d73..0x443d87 (returns 0).
    if (IsChaosCastle(map))
    {
        return false;
    }
    // 0x443d8d: a live slot stays as it is (returns 1: 0x443fa0 tries the next one).
    if (helper.alive)
    {
        return false;
    }
    if (type != DarkHorseModel && type != RiderlessHelper && !IsRidingHelper(type))
    {
        return Core::Unexpected(std::format("0x443d70: el constructor del ayudante {:#x} no está portado", type));
    }
    helper.alive = true;
    helper.type = type;
    helper.rider = riderSlot;
    helper.seen = false;
    helper.scale = type == DarkHorseModel ? HorseScale : IsRidingHelper(type) ? RidingHelperScale : GuardianScale;
    helper.position = position;
    helper.angles = rider.angles;
    // 0x443e1d and 0x443e23; +0x111 and +0x112 stay (Q-C221).
    helper.horse.animation.previousKey = 0;
    helper.horse.animation.frame = 0.0f;
    helper.horse.speed = CreatedSpeed;
    helper.horse.alpha = CreatedAlpha;
    helper.light = HorseLight; // 0x443e2f..0x443e45
    helper.blendMesh = NoBlendMesh;
    if (type == RiderlessHelper)
    {
        helper.blendMesh = GuardianBlendMesh; // 0x443f1a
        if (random == nullptr)
        {
            return Core::Unexpected(std::string("0x443f21: falta el rand() compartido del Guardian Angel"));
        }
        constexpr int HorizontalSpan = 512;
        constexpr int HorizontalCentre = 256;
        constexpr int VerticalSpan = 128;
        constexpr int VerticalBase = 128;
        helper.position.x = static_cast<float>(static_cast<double>(rider.position.x) +
                                               (random->Next() % HorizontalSpan) - HorizontalCentre);
        helper.position.y = static_cast<float>(static_cast<double>(rider.position.y) +
                                               (random->Next() % HorizontalSpan) - HorizontalCentre);
        helper.position.z =
            static_cast<float>(static_cast<double>(rider.position.z) + (random->Next() % VerticalSpan) + VerticalBase);
    }
    return true;
}

Core::Expected<std::optional<std::size_t>, std::string> CreateHelper(Kernel::WorldState& world, std::uint16_t type,
                                                                     const glm::vec3& position, std::size_t rider,
                                                                     Crt::CrtRandom* random)
{
    const std::int32_t map = world.Runtime().map;
    const Kernel::ActorState& riding = world.Actors()[rider];
    if (IsChaosCastle(map) || (riding.type != Tables::PlayerSkeletonModel && type != RiderlessHelper))
    {
        return std::optional<std::size_t>{};
    }
    if (type != DarkHorseModel && type != RiderlessHelper && !IsRidingHelper(type))
    {
        return Core::Unexpected(std::format("0x443d70: el constructor del ayudante {:#x} no está portado", type));
    }
    Kernel::HelperTable& helpers = world.Helpers();
    for (std::size_t slot = 0; slot < helpers.size(); ++slot)
    {
        auto built = ConstructHelper(helpers[slot], type, position, riding, rider, map, random);
        if (!built.HasValue())
        {
            return Core::Unexpected(built.Error());
        }
        if (built.Value())
        {
            return std::optional<std::size_t>{slot};
        }
    }
    return std::optional<std::size_t>{};
}

Core::Expected<void, std::string> StepHelpers(const HelperStepContext& context)
{
    for (Kernel::HelperState& helper : context.world.Helpers())
    {
        if (!helper.alive)
        {
            continue;
        }
        if (auto stepped = StepHelper(helper, context.world.Actors()[helper.rider], context); !stepped.HasValue())
        {
            return stepped;
        }
    }
    return {};
}

Core::Expected<void, std::string> StepTheHelperOutsideTheTable(Kernel::HelperState& helper, Kernel::ActorState& rider,
                                                               const HelperStepContext& context)
{
    // 0x444021
    if (!helper.alive)
    {
        return {};
    }
    HelperStepContext anywhere = context;
    anywhere.ignoresTheSafeCell = true;
    return StepHelper(helper, rider, anywhere);
}

Core::Expected<void, std::string> DrawTheHelperOutsideTheTable(Kernel::HelperState& helper,
                                                               const Kernel::ActorState& rider,
                                                               const HelperDrawContext& context)
{
    // 0x44526b
    if (!helper.alive)
    {
        return {};
    }
    return DrawHelper(helper, rider, true, context);
}

Core::Expected<void, std::string> DrawHelpers(const HelperDrawContext& context)
{
    Crt::CrtRandom projectedRandom = context.random;
    const HelperDrawContext projected{context.batch,
                                      context.models,
                                      context.world,
                                      projectedRandom,
                                      context.frameClock,
                                      context.stepAlpha,
                                      true,
                                      context.publishProjectedPose ? context.onPose : decltype(context.onPose){},
                                      false,
                                      context.posedMeshes,
                                      context.scene,
                                      context.lighting,
                                      context.sprites,
                                      context.worldClock,
                                      context.effects,
                                      context.particles,
                                      context.systemTime,
                                      context.globalPose,
                                      context.trapezoid};
    for (Kernel::HelperState& helper : context.world.Helpers())
    {
        if (!helper.alive)
        {
            continue;
        }
        Kernel::HelperState projectedHelper = helper;
        const Kernel::ActorState& rider = context.world.Actors()[helper.rider];
        auto drawn = context.originalFrame ? DrawHelper(helper, rider, false, context)
                                           : DrawHelper(projectedHelper, rider, false, projected);
        if (!drawn.HasValue())
        {
            return drawn;
        }
    }
    return {};
}

TableHelpers::TableHelpers(Kernel::WorldState& world, Crt::CrtRandom* random) : m_world(world), m_random(random) {}

void TableHelpers::TurnOffTheHelpersOf(std::size_t rider) const
{
    Kernel::TurnOffHelpersOf(m_world.Helpers(), rider);
}

Core::Expected<void, std::string> TableHelpers::CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                                std::size_t rider) const
{
    const auto created = CreateHelper(m_world, type, position, rider, m_random);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}

OwnHelper::OwnHelper(Kernel::HelperState& helper, const Kernel::ActorState& rider, const Kernel::WorldState& world,
                     Crt::CrtRandom* random)
    : m_helper(helper), m_rider(rider), m_world(world), m_random(random)
{
}

void OwnHelper::TurnOffTheHelpersOf(std::size_t /*rider*/) const
{
    m_helper.alive = false;
}

Core::Expected<void, std::string> OwnHelper::CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                             std::size_t rider) const
{
    const auto built = ConstructHelper(m_helper, type, position, m_rider, rider, m_world.Runtime().map, m_random);
    if (!built.HasValue())
        return Core::Unexpected(built.Error());
    return {};
}
} // namespace Client::Pets
