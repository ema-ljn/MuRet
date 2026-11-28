#include "Pets/PetHelperSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Pets/HelperStepParts.h"
#include "Pets/HelperTable.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Pets
{
namespace
{
// 0x444877..0x44487f and 0x4448fe..0x444906: the Dinorant flies in Tarkan (8) and Icarus (10).
using Maps::IcarusMap;
using Maps::TarkanMap;
// 0x444881: with the world at −1 the Dinorant stays at the rider's height.
constexpr std::int32_t NoMap = -1;
constexpr double FlyingDrop = 10.0;  // 0x6b87a8
constexpr double WalkingDrop = 30.0; // 0x6b8858
// 0x444ab6: the Uniria's and the Dinorant's speed.
constexpr float RidingSpeed = std::bit_cast<float>(0x3eae147bu); // 0.34
// 0x4449bf..0x4449d0: the gallop dust with rand() % 2 == 0; 0x444e1b..0x444e2a, the Fenrir's with rand() % 3.
constexpr int RidingDustOneIn = 2;
constexpr int FenrirDustOneIn = 3;
// 0x4449f1..0x4449fe, 0x444e5a..0x444e69 and 0x444f32..0x444f41: with 0x4000 in the rider's +0x78, nothing.
constexpr std::uint32_t RiderWithoutDust = 0x4000;
// 0x444c06, 0x444c2a and 0x444c4b
constexpr float FenrirCalm = std::bit_cast<float>(0x3ecccccdu); // 0.4
constexpr float FenrirRun = 1.0f;
constexpr float FenrirTrot = std::bit_cast<float>(0x3f19999au); // 0.6
// 0x444c84..0x444c92: the bone offset; 0x444ccd and 0x444d20, the 40 in x (0x6b8bf4); 0x444d2c, the 700 in z (0x6b8bec)
// of the second window.
constexpr glm::vec3 FenrirDustReach{120.0f, 0.0f, 32.0f};
constexpr double FenrirDustAhead = 40.0;
constexpr double FenrirDustLift = 700.0;
// 0x444f47..0x444f55 and 0x444f5f
constexpr float GuardianLightLevel = std::bit_cast<float>(0x3ecccccdu); // 0.4
constexpr int GuardianSparks = 4;
constexpr std::int32_t GuardianSparkSubtype = 1;
constexpr int SparkSpread = 16;
constexpr int SparkCentre = 8;
// 0x444fff: the flight reach, squared as float (0x44507a..0x445080); 0x4450a8, the turn.
constexpr float GuardianReach = 150.0f;
constexpr float GuardianTurn = 20.0f;
// 0x4450f7..0x445108: the flight's z; 0x445117..0x445128, the course change with rand() % 32 == 0.
constexpr int FlightJitter = 16;
constexpr int FlightJitterCentre = 8;
constexpr int NewCourseOneIn = 32;
// 0x44513b..0x445157: the far y speed, ((rand() & 63) + 128) · −0.1f; 0x44515f..0x445179, the near one,
// ((rand() & 63) + 16) · −0.1f, with course rand() % 360; 0x4451ab..0x4451c5, the z one, ((rand() & 63) − 32) · 0.1f.
constexpr int CourseSpread = 64;
constexpr int FarCourseBase = 128;
constexpr int NearCourseBase = 16;
constexpr int CourseCentre = 32;
constexpr int Degrees = 360;
constexpr double Backwards = static_cast<double>(std::bit_cast<float>(0xbdcccccdu)); // 0x6b8930, −0.1
constexpr double Upwards = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));   // 0x6b87b8, 0.1
// 0x4451d7..0x445215: between 100 (0x6b86b4) and 200 (0x6b8928) above the rider, by 1.5 (0x6b8b94).
constexpr double FlightFloor = 100.0;
constexpr double FlightCeiling = 200.0;
constexpr double FlightClimb = 1.5;

using Math::F32;

float Shifted(int by, float value)
{
    return F32(static_cast<double>(by) + static_cast<double>(value));
}

bool InRange(std::uint8_t value, std::uint8_t low, std::uint8_t high)
{
    return value >= low && value <= high;
}

// 0x4448b1..0x4449b7, and whether it is the gallop (0x44499b).
struct RidingGait
{
    std::uint8_t action = 0;
    bool gallop = false;
};

RidingGait RidingGaitOf(std::uint16_t type, std::int32_t map, std::uint8_t riderAction)
{
    const bool dinorant = type == DinorantModel;
    const bool flying = map == TarkanMap || map == IcarusMap;
    if (InRange(riderAction, 0x0d, 0x21) || riderAction == 0x46 || riderAction == 0x47)
    {
        return {static_cast<std::uint8_t>(dinorant && flying ? 3 : 2), true};
    }
    if (riderAction == 0x40 || riderAction == 0x41)
    {
        return {static_cast<std::uint8_t>(flying ? 7 : 6), false}; // 0x44496c: regardless of the type.
    }
    if (InRange(riderAction, 0x22, 0x37))
    {
        return {static_cast<std::uint8_t>(dinorant ? (flying ? 5 : 4) : 3), false};
    }
    return {static_cast<std::uint8_t>(dinorant && flying ? 1 : 0), false};
}

// 0x444867..0x44489a: on the x87 until its fstp.
float DinorantHeight(std::int32_t map, float riderZ)
{
    if (map == TarkanMap || map == IcarusMap)
    {
        return F32(static_cast<double>(riderZ) - FlyingDrop);
    }
    if (map != NoMap)
    {
        return F32(static_cast<double>(riderZ) - WalkingDrop);
    }
    return riderZ;
}

// 0x4449bf..0x444aad
Core::Expected<void, std::string> RaiseTheRidingDust(const Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                                     const HelperStepContext& context)
{
    if (context.random.Next() % RidingDustOneIn != 0)
    {
        return {};
    }
    if (context.world.Runtime().map == IcarusMap || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    return ScatterTheDust(helper, context, "0x444aa8");
}

// 0x444bbc..0x444c4b. Returns whether it goes on to the dust (0x444c55): the still actions go straight to the advance.
bool TakeTheFenrirGait(Kernel::HelperState& helper, const Models::Skeleton& skeleton, std::uint8_t riderAction)
{
    std::optional<std::uint8_t> calm;
    if (InRange(riderAction, 0x56, 0x61))
    {
        calm = std::uint8_t{3};
    }
    else if (InRange(riderAction, 0x62, 0x65))
    {
        calm = std::uint8_t{4};
    }
    else if (InRange(riderAction, 0x66, 0x69))
    {
        calm = std::uint8_t{5};
    }
    else if (InRange(riderAction, 0x76, 0x79) || riderAction == 0xc3)
    {
        calm = std::uint8_t{0};
    }
    if (calm)
    {
        Models::SetAction(helper.horse.animation, skeleton, *calm);
        helper.horse.speed = FenrirCalm;
        return false;
    }
    if (InRange(riderAction, 0x7a, 0x7d))
    {
        Models::SetAction(helper.horse.animation, skeleton, 1);
        helper.horse.speed = FenrirRun;
    }
    else if (InRange(riderAction, 0x6a, 0x75))
    {
        Models::SetAction(helper.horse.animation, skeleton, 2);
        helper.horse.speed = FenrirTrot;
    }
    return true;
}

// 0x444c78..0x444e16: the Fenrir's Icarus dust, with no rand() before the frame. The windows compare the frame (+0x114)
// with the floats at 0x6b87d4, 0x6b8bb0, 0x6b8bf0 and 0x6b87f8; outside them (or unordered), nothing.
Core::Expected<void, std::string> RaiseTheIcarusFenrirDust(const Kernel::HelperState& helper,
                                                           const HelperStepContext& context)
{
    constexpr float FirstLow = std::bit_cast<float>(0x3f800000u);   // 1
    constexpr float FirstHigh = std::bit_cast<float>(0x3f99999au);  // 1.2
    constexpr float SecondLow = std::bit_cast<float>(0x4099999au);  // 4.8
    constexpr float SecondHigh = std::bit_cast<float>(0x40a00000u); // 5
    constexpr std::size_t FirstBone = 22;                           // 0x68b1c3c
    constexpr std::size_t SecondBone = 44;                          // 0x68b205c
    const float frame = helper.horse.animation.frame;
    const bool first = frame > FirstLow && frame < FirstHigh;
    if (!first && !(frame > SecondLow && frame <= SecondHigh))
    {
        return {};
    }
    const auto point = first ? GlobalPosePoint(context.world, FirstBone, FenrirDustReach, "0x444cc5")
                             : GlobalPosePoint(context.world, SecondBone, FenrirDustReach, "0x444d18");
    if (!point.HasValue())
    {
        return Core::Unexpected(point.Error());
    }
    glm::vec3 base = point.Value();
    base.x = F32(static_cast<double>(base.x) + FenrirDustAhead);
    if (!first)
    {
        base.z = F32(static_cast<double>(base.z) + FenrirDustLift); // 0x444d29..0x444d32, Q-C421
    }
    static_cast<void>(
        context.world.Map().effects.CreateGallopShockWave(base, helper.angles, HelperDustLight, context.random));
    return PuffTheIcarusDust(base, helper.angles, context, "0x444df6");
}

// 0x444c55..0x444f25: the Fenrir's dust. Outside map 10, the one without windows.
Core::Expected<void, std::string> RaiseTheFenrirDust(const Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                                     const HelperStepContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    if (map == IcarusMap)
    {
        return RaiseTheIcarusFenrirDust(helper, context);
    }
    if (context.random.Next() % FenrirDustOneIn != 0)
    {
        return {};
    }
    // 0x444e30..0x444e46.
    if (Maps::IsKalimaMap(map) || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    return ScatterTheDust(helper, context, "0x444f1d");
}
} // namespace

Core::Expected<void, std::string> StepRidingHelper(Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                                   const Models::Skeleton& skeleton, const HelperStepContext& context)
{
    const auto safe = InTheRidersSafeCell(context, rider, "0x4447e7");
    if (!safe.HasValue())
    {
        return Core::Unexpected(safe.Error());
    }
    // 0x4447f9: with the second argument not 0 (the card portrait), the cell does not hide it.
    if (safe.Value() && !context.ignoresTheSafeCell)
    {
        helper.horse.alpha = 0.0f; // 0x444800: the EBX at 0.
        return {};
    }
    FadeWithTheRider(helper, rider);
    const std::int32_t map = context.world.Runtime().map;
    helper.position = rider.position;
    if (helper.type == DinorantModel)
    {
        helper.position.z = DinorantHeight(map, rider.position.z);
    }
    helper.angles = rider.angles;
    const RidingGait gait = RidingGaitOf(helper.type, map, rider.animation.action);
    Models::SetAction(helper.horse.animation, skeleton, gait.action);
    if (gait.gallop)
    {
        if (auto dust = RaiseTheRidingDust(helper, rider, context); !dust.HasValue())
        {
            return dust;
        }
    }
    helper.horse.speed = RidingSpeed;
    helper.alive = rider.alive; // 0x444ab0..0x444ac2
    return {};
}

Core::Expected<void, std::string> StepFenrir(Kernel::HelperState& helper, const Kernel::ActorState& rider,
                                             const Models::Skeleton& skeleton, const HelperStepContext& context)
{
    const auto safe = InTheRidersSafeCell(context, rider, "0x444b13");
    if (!safe.HasValue())
    {
        return Core::Unexpected(safe.Error());
    }
    // 0x444b25: the same.
    if (safe.Value() && !context.ignoresTheSafeCell)
    {
        helper.horse.alpha = 0.0f; // 0x444b2c
        return {};
    }
    FadeWithTheRider(helper, rider);
    // 0x444b84..0x444b96: also the rider's head (+0x28..+0x30), which the ported code does not read.
    helper.position = rider.position;
    helper.angles = rider.angles;
    if (!TakeTheFenrirGait(helper, skeleton, rider.animation.action))
    {
        return {};
    }
    return RaiseTheFenrirDust(helper, rider, context);
}

Core::Expected<void, std::string> SparkleTheGuardianAngel(const Kernel::HelperState& helper,
                                                          const Kernel::ActorState& rider,
                                                          const HelperStepContext& context)
{
    if ((rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    const glm::vec3 light(GuardianLightLevel);
    Crt::CrtRandom& random = context.random;
    for (int spark = 0; spark < GuardianSparks; ++spark)
    {
        const int x = (random.Next() % SparkSpread) - SparkCentre;
        const int y = (random.Next() % SparkSpread) - SparkCentre;
        const int z = (random.Next() % SparkSpread) - SparkCentre;
        const glm::vec3 place(Shifted(x, helper.position.x), Shifted(y, helper.position.y),
                              Shifted(z, helper.position.z));
        if (auto emitted = EmitHelperParticle(context, Particles::FairySparkParticle, GuardianSparkSubtype, place,
                                              helper.angles, light, "0x444fe7");
            !emitted.HasValue())
        {
            return emitted;
        }
    }
    return {};
}

void FlyTheGuardianAngel(Kernel::HelperState& helper, const Kernel::ActorState& rider, glm::vec2 riderStart,
                         Crt::CrtRandom& random)
{
    const float reach = F32(static_cast<double>(GuardianReach) * static_cast<double>(GuardianReach));
    // 0x445057..0x445078: the squared distance on the x87; the float is stored for the course change.
    const double dx = static_cast<double>(riderStart.x) - static_cast<double>(helper.position.x);
    const double dy = static_cast<double>(riderStart.y) - static_cast<double>(helper.position.y);
    const double distance = (dy * dy) + (dx * dx);
    const float storedDistance = F32(distance);
    // 0x445083..0x4450b4: less or unordered does not turn.
    if (distance >= static_cast<double>(reach))
    {
        const float course = F32(Math::CreateAngle(helper.position.x, helper.position.y, riderStart.x, riderStart.y));
        helper.angles.z = F32(Math::TurnAngle(helper.angles.z, course, GuardianTurn));
    }
    // 0x4450ba..0x4450f4: the speed rotated by its angle matrix (+0x9c), added axis by axis.
    const glm::vec3 move = Math::OriginalVectorRotate(helper.velocity, Math::OriginalAngleMatrix(helper.angles));
    helper.position.x = F32(static_cast<double>(move.x) + static_cast<double>(helper.position.x));
    helper.position.y = F32(static_cast<double>(move.y) + static_cast<double>(helper.position.y));
    helper.position.z = F32(static_cast<double>(move.z) + static_cast<double>(helper.position.z));
    helper.position.z = Shifted((random.Next() % FlightJitter) - FlightJitterCentre, helper.position.z);
    if (random.Next() % NewCourseOneIn == 0)
    {
        // 0x44512e..0x445139: less or unordered, near.
        if (!(storedDistance >= reach))
        {
            const float pull = F32(static_cast<double>((random.Next() % CourseSpread) + NearCourseBase) * Backwards);
            helper.angles.z = static_cast<float>(random.Next() % Degrees);
            helper.velocity.y = pull;
        }
        else
        {
            helper.velocity.y = F32(static_cast<double>((random.Next() % CourseSpread) + FarCourseBase) * Backwards);
        }
        helper.velocity.x = 0.0f;
        helper.velocity.z = F32(static_cast<double>((random.Next() % CourseSpread) - CourseCentre) * Upwards);
    }
    // 0x4451d1..0x4451f6: below 100 over the rider (greater and ordered), it climbs.
    if (static_cast<double>(rider.position.z) + FlightFloor > static_cast<double>(helper.position.z))
    {
        helper.velocity.z = F32(static_cast<double>(helper.velocity.z) + FlightClimb);
    }
    // 0x4451fc..0x44521b: above 200 (less or unordered), it descends.
    if (!(static_cast<double>(rider.position.z) + FlightCeiling >= static_cast<double>(helper.position.z)))
    {
        helper.velocity.z = F32(static_cast<double>(helper.velocity.z) - FlightClimb);
    }
}
} // namespace Client::Pets
