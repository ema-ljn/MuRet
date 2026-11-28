#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Actors/SkillHits.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainAttributeQuery.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>

// The 0x61c/0 (creation 0x5342e4..0x534399, step 0x53b3e1..0x53b623): the sweep requested by the step of the 0x5be
// effect (0x52a72a, with the effect's owner character, width 150 and the effect's skill and key), which MuRet already
// creates (0x4f9fae). It turns 180 from its target (the start position) lowering its turn's z by 11 per segment, eight
// segments per step, and on each releases two 0x614/3 bolts and sometimes the 0x5d9 particle; with the hero as owner,
// every five turns, the siege stops it or sends the skill's targets (0x53b569). Without the common tail or the common
// move (0x53585e).
namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

constexpr float Reach = std::bit_cast<float>(0xc3340000u); // −180

// 0x53b4c2..0x53b53b: 0x530860(0x614, start, position, angles, 3, 0, scale, 5, 0xa, 0, −1).
Core::Expected<void, std::string> Spark(const JointKernels::StepServices& services, Kernel::JointState& joint,
                                        glm::vec3& from, float scale)
{
    constexpr std::int32_t SparkJoint = 0x614, SparkSubtype = 3, SparkKey = 5, SparkSkill = 0xa;
    const Kernel::JointRequest spark{.type = SparkJoint,
                                     .firstPosition = from,
                                     .secondPosition = joint.position,
                                     .angles = joint.angles,
                                     .subtype = SparkSubtype,
                                     .ownerContext = nullptr,
                                     .scale = scale,
                                     .key = SparkKey,
                                     .skill = SparkSkill,
                                     .skillSerial = 0,
                                     .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, spark, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x53b4f6: {}", created.Error()));
    return {};
}

// 0x53b400..0x53b53b: one segment: the position is the target plus (0, −180, 0) rotated by the velocity (the sweep's
// angles), a segment with the angle matrix, vz − 11; with rand() & 1 at 0 the 0x5d9 particle (the joint's light, scale
// 1) and the two 0x614/3 from the previous position, at scale rand() % 10 + 5 and (rand() & 7) + 4.
Core::Expected<void, std::string> Sweep(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    glm::vec3 from = joint.position;
    const auto swing = Math::OriginalVectorRotate({0, Reach, 0}, Math::OriginalAngleMatrix(joint.velocity));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(swing[axis]) + D(joint.targetPosition[axis]));
    if (auto appended = JointKernels::AppendJointTail(joint, Math::OriginalAngleMatrix(joint.angles));
        !appended.HasValue())
        return appended;
    constexpr double Turn = 11.0; // 0x6b8bb4
    joint.velocity.z = F32(D(joint.velocity.z) - Turn);
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53b493: falta el RNG del joint 0x61c/0"));
    if ((services.random->Next() & 1) == 0)
    {
        constexpr std::int32_t Fire = 0x5d9;
        if (services.particles == nullptr)
            return Core::Unexpected(std::string("0x53b4ba: faltan las partículas del joint 0x61c/0"));
        const auto made = services.particles->CreateChecked(Fire, joint.position, joint.angles, joint.light, 0, 1.0f,
                                                            services.worldClock, *services.random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x53b4ba: falló la partícula 0x5d9 del joint 0x61c/0 ({})",
                                                static_cast<int>(made.Error())));
    }
    constexpr double SmallBase = 5.0, LargeBase = 4.0; // 0x6b87f8 and 0x6b8a44
    const std::int32_t first = services.random->Next() % 10;
    if (auto spark = Spark(services, joint, from, F32(static_cast<double>(first) + SmallBase)); !spark.HasValue())
        return spark;
    const std::int32_t second = services.random->Next() & 7;
    return Spark(services, joint, from, F32(static_cast<double>(second) + LargeBase));
}

// 0x53b569..0x53b5b6 and 0x53b5cb..0x53b5e6, with the hero as owner. With the siege on (0x4294b0, the byte 0x7239ec)
// and bit 0x100 of the attribute of the position's cell (0x5f82c0), the speed (+0x9d8) at 0 and life at ftol(life ·
// 0.2f) (0x6b8cfc, 0x69fd3c): returns true and the step leaves the turns (0x53b5ec). Otherwise, 0x517a20(+0x9ea,
// position, 150, +0x9eb, +0x9e8, 0): the skill's targets around, with the C1 10, and it goes on to the next turn. The
// same that the 0x613 of width 80 reads (JointKernels::VolleyServices, 0x539a50).
Core::Expected<bool, std::string> HitAroundTheHero(Kernel::JointState& joint,
                                                   const JointKernels::StepServices& services)
{
    if (services.volley == nullptr)
        return Core::Unexpected(
            std::string("0x53b569: el joint 0x61c/0 del héroe pide el asedio, los atributos y los blancos del héroe"));
    const JointKernels::VolleyServices& volley = *services.volley;
    if (volley.siegeActive)
    {
        if (volley.attributes == nullptr)
            return Core::Unexpected(std::string("0x53b57a: faltan los atributos de las celdas (0x7d0d348)"));
        const auto cell = Terrain::TerrainAttributeQueryAt(*volley.attributes, joint.position.x, joint.position.y);
        if (!cell.HasValue())
            return Core::Unexpected(std::string("0x53b57a: 0x5f82c0 lee fuera de la tabla de atributos"));
        constexpr std::uint32_t Blocked = 0x100;
        if ((cell.Value() & Blocked) == Blocked)
        {
            constexpr double Cut = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8cfc, 0.2f
            joint.speed = 0.0f;
            joint.life = Math::FtolLow(static_cast<double>(joint.life) * Cut);
            return true;
        }
    }
    if (!volley.skillHits)
        return Core::Unexpected(std::string("0x53b5b1: faltan los blancos de la skill (0x517a20)"));
    constexpr float HitRadius = std::bit_cast<float>(0x43160000u); // 150, 0x53b5a2
    const Actors::SkillHitsRequest request{.skillIndex = joint.skill,
                                           .point = joint.position,
                                           .radius = HitRadius,
                                           .volley = joint.volley,
                                           .key = static_cast<std::uint16_t>(joint.key),
                                           .extra = 0};
    if (auto hit = volley.skillHits(request); !hit.HasValue())
        return Core::Unexpected(std::format("0x53b5b1: {}", hit.Error()));
    return false;
}
} // namespace

void CreateSweepJoint(Kernel::JointState& joint, const Kernel::JointRequest& request)
{
    // 0x5342e4..0x534399: without the common tail, speed 0, the width, the angles' z + 30, the target at the start
    // position, life 20, 18 points, the velocity (+0x9dc) with the new angles, the count at −1; the position, 180
    // toward −y rotated by those angles, and white light.
    constexpr double Tilt = 30.0; // 0x6b8858
    constexpr std::int32_t Life = 20, Points = 18;
    joint.tailEnabled = false;
    joint.speed = 0;
    joint.width = request.scale;
    joint.angles.z = F32(D(joint.angles.z) + Tilt);
    joint.targetPosition = joint.position;
    joint.life = Life;
    joint.maximumCount = Points;
    joint.velocity = joint.angles;
    joint.count = -1;
    const auto reach = Math::OriginalVectorRotate({0, Reach, 0}, Math::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(reach[axis]) + D(joint.position[axis]));
    joint.light = {1, 1, 1};
}

Core::Expected<void, std::string> StepSweepJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    // 0x53b3f1..0x53b5c3: eight turns; each appends a segment while the count is less than points − 1.
    // 0x53b53e..0x53b567: in the /0 (+0xc) with the owner (+0x54) equal to the hero ([0x743a170]), life over 18 and the
    // turn a multiple of 5, the siege or the skill's targets (HitAroundTheHero). With another owner (or none) it goes
    // on to the next turn. Without the hero in the services it cannot tell: with an owner, life over 18 and the turn a
    // multiple of 5, it stops.
    constexpr int Turns = 8, HitEvery = 5;
    constexpr std::int32_t HitLife = 18;
    for (int turn = 0; turn < Turns; ++turn)
    {
        if (joint.count < joint.maximumCount - 1)
            if (auto swept = Sweep(joint, services); !swept.HasValue())
                return swept;
        // The comparisons have no side effects: the order (owner, life, turn) changes nothing.
        if (joint.subtype != 0 || joint.ownerContext == nullptr || !(joint.life > HitLife) || turn % HitEvery != 0)
            continue;
        if (services.hero == nullptr)
            return Core::Unexpected(
                std::string("0x53b548: falta el héroe ([0x743a170]) para el dueño del joint 0x61c/0"));
        if (joint.ownerContext != static_cast<const void*>(services.hero))
            continue;
        const auto stopped = HitAroundTheHero(joint, services);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        if (stopped.Value())
            break;
    }
    // 0x53b5ec..0x53b620: with life under 10, the light's x times 0.6666667f in all three channels; otherwise, life
    // times 0.033333335f.
    constexpr std::int32_t DimLife = 10;
    constexpr double Dimming = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)); // 0x6b8c64
    constexpr double Glow = static_cast<double>(std::bit_cast<float>(0x3d088889u));    // 0x6ba29c
    const float light =
        joint.life < DimLife ? F32(D(joint.light.x) * Dimming) : F32(static_cast<double>(joint.life) * Glow);
    joint.light = {light, light, light};
    return {};
}
} // namespace Client::Joints::JointFamilies
