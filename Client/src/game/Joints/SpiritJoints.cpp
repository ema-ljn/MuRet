#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Effects/RingEffects.h"
#include "Actors/SkillHits.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainAttributeQuery.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>

// The 0x613 and 0x6ee: they share creation (0x53265e, blend at 3 and table 0x534f18 by subtype) and step (0x538d77).
// The step's sprites are those of 0x5491e0 (MuRet does not store the owner they receive).
namespace Client::Joints::JointFamilies
{
namespace
{
using Crt::CrtRandom;
using Math::D;
using Math::F32;

constexpr std::uint8_t Subtractive = 3;
constexpr std::uint8_t Lighter = 1;
constexpr std::uint8_t SecondFace = 2;
constexpr std::uint16_t SpiritSprite = 0x5ac;
constexpr std::uint16_t GlowSprite = 0x5fd;
using Math::FullTurn;

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// 0x532675 and 0x532bab: the own record's +0xf04 with an access opened and closed around it.
Core::Expected<void, std::string> ReadTheVolley(Kernel::JointState& joint, CrtRandom& random,
                                                const JointKernels::CreationServices* services, std::uint32_t address)
{
    if (services == nullptr || services->ownRecordDepth == nullptr || services->ownVolley == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: falta el registro propio del joint {:#x}/{:#x}", address, joint.type, joint.subtype));
    const Models::LogicalAccess own(*services->ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    joint.volley = *services->ownVolley;
    return {};
}

// 0x5331a9..0x5331b7 and 0x53463b..0x534649: +0x24 stores the start.
void KeepTheStart(Kernel::JointState& joint)
{
    joint.pathIncrement = joint.position;
}

// 0x533208..0x53323b and 0x53326c..0x5332ac (with 0x534633): the light of the /6 and /7 by skill (+0x9ea).
void LightBySkill(Kernel::JointState& joint)
{
    if (joint.skill == 0)
        joint.light = {Bits(0x3e99999au), Bits(0x3e99999au), 1.0f};
    else if (joint.skill == 1)
    {
        joint.speed = 10.0f;
        joint.light = glm::vec3(0.5f);
    }
}
} // namespace

Core::Expected<void, std::string> CreateSpiritJoint(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                    CrtRandom& random, const JointKernels::CreationServices* services)
{
    const bool spirit = joint.type == SpiritJoint;
    joint.blend = Subtractive; // 0x53265e
    switch (request.subtype)
    {
    case 0: // 0x532675..0x532b45
        if (auto read = ReadTheVolley(joint, random, services, 0x532675); !read.HasValue())
            return read;
        joint.speed = 70.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 6;
        break;
    case 0x13: // 0x532b64..0x532ba6
        if (spirit)
            joint.blend = Lighter;
        joint.speed = 70.0f;
        joint.light = {Bits(0x3dcccccdu), 0.5f, Bits(0x3e4ccccdu)};
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 9;
        break;
    case 5: // 0x532bab..0x53307f: the blend before the access.
        joint.blend = Lighter;
        if (auto read = ReadTheVolley(joint, random, services, 0x532bab); !read.HasValue())
            return read;
        joint.speed = 30.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 12;
        break;
    case 1: // 0x53309e..0x5330cd: and plays 0x3e.
    {
        constexpr Audio::SoundId BirthSound{0x3e};
        joint.speed = 70.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 6;
        if (services != nullptr && services->soundRequest)
            services->soundRequest(BirthSound);
        break;
    }
    case 3: // 0x5330d2..0x533127
        joint.speed = 140.0f;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 10;
        if (spirit)
        {
            joint.blend = Lighter;
            joint.light = {1.0f, 0.5f, Bits(0x3dcccccdu)};
        }
        else
            joint.light = glm::vec3(1.0f);
        break;
    case 0xd: // 0x53312c..0x53316e
        if (spirit)
            joint.blend = Lighter;
        joint.speed = 40.0f;
        joint.light = {1.0f, Bits(0x3f19999au), Bits(0x3ecccccdu)};
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 10;
        break;
    case 2:
    case 0x15: // 0x533173..0x5331ba
        joint.blend = Lighter;
        joint.width = request.scale;
        joint.drawEnabled = SecondFace;
        joint.speed = 50.0f;
        joint.light = glm::vec3(0.5f);
        joint.life = 20;
        joint.maximumCount = 3;
        KeepTheStart(joint);
        break;
    case 6: // 0x5331bf..0x533253: the key at rand() % 5; with one other than 0, no tail.
        joint.blend = Lighter;
        joint.drawEnabled = SecondFace;
        joint.speed = 50.0f;
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 5;
        joint.key = static_cast<std::int16_t>(random.Next() % 5);
        if (joint.key != 0)
            joint.tailEnabled = false;
        LightBySkill(joint);
        KeepTheStart(joint);
        break;
    case 7: // 0x533258..0x5332b3
        joint.drawEnabled = 0;
        joint.speed = 10.0f;
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 5;
        LightBySkill(joint);
        KeepTheStart(joint);
        break;
    case 8: // 0x5333be
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    case 0xa: // 0x5333da
        joint.life = 20;
        joint.width = request.scale;
        joint.maximumCount = 6;
        break;
    case 4:
    case 9: // 0x5332b8 and 0x5333f6: FaunaJointCreation.cpp creates them earlier (CreateHelperBeam and CreateMeteor).
        return Core::Unexpected(std::format("{:#x}: el joint {:#x}/{} no llega a CreateSpiritJoint",
                                            request.subtype == 4 ? 0x5332b8 : 0x5333f6, request.type, request.subtype));
    case 0xb: // 0x53344e..0x533495: still, one point.
    case 0xc:
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x11:
    {
        // 0xb..0x11, with 0xd left out: 0x53344e, 0x533474, 0x53349a, 0x5334c7, 0x5334ed and 0x533513.
        constexpr std::array<std::int32_t, 7> Lives{50, 30, 0, 10, 20, 50, 100};
        joint.speed = 0.0f;
        joint.life = Lives[static_cast<std::size_t>(request.subtype - 0xb)];
        joint.width = request.scale;
        joint.maximumCount = 1;
        if (request.subtype == 0xe)
            joint.angles.x = 20.0f; // 0x5334bb
        break;
    }
    case 0x12: // 0x533539..0x533574
        if (!spirit)
            joint.blend = Lighter;
        joint.speed = 50.0f;
        joint.light = glm::vec3(Bits(0x3f333333u));
        joint.life = 39;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    case 0x14: // 0x533579..0x533596
        if (!spirit)
            joint.blend = Lighter;
        joint.life = 49;
        joint.width = request.scale;
        joint.maximumCount = 15;
        break;
    default: // 0x532668: beyond 0x15, only the blend.
        break;
    }
    return {};
}

namespace
{
Core::Expected<void, std::string> Sprite(const JointKernels::StepServices& services, std::uint16_t texture,
                                         const glm::vec3& position, float scale, const glm::vec3& light, float rotation,
                                         std::uint32_t address)
{
    if (services.sprites == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan los sprites del joint 0x613", address));
    static_cast<void>(services.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}

// 0x539600..0x539641 and 0x5398be..0x5398ff: the light times 0.8333333f (0x6b8b98) as float; the local stays the same.
void FadeTheLight(Kernel::JointState& joint)
{
    constexpr double Fading = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    for (int axis = 0; axis < 3; ++axis)
        joint.light[axis] = F32(D(joint.light[axis]) * Fading);
}

// 0x538d99..0x538dda: the /1, the 0x5ac sprite at scale 4 with light (0.8; 0.4; 1) and a rand() % 360 turn.
Core::Expected<void, std::string> StepTheSpark(const Kernel::JointState& joint,
                                               const JointKernels::StepServices& services)
{
    const glm::vec3 local{Bits(0x3f4ccccdu), Bits(0x3ecccccdu), 1.0f};
    const auto rotation = static_cast<float>(services.random->Next() % FullTurn);
    return Sprite(services, SpiritSprite, joint.position, 4.0f, local, rotation, 0x538dda);
}

// 0x5396ff: the 0x613/2's 0x60f/2 drop, ownerless, width 40, at the point and with the turn's angles.
Core::Expected<void, std::string> DropTheDrop(const JointKernels::StepServices& services, glm::vec3 point,
                                              glm::vec3 angles)
{
    constexpr float Width = 40.0f; // 0x42200000
    const Kernel::JointRequest drop{.type = DropJoint,
                                    .firstPosition = point,
                                    .secondPosition = point,
                                    .angles = angles,
                                    .subtype = DropSubtype,
                                    .ownerContext = nullptr,
                                    .scale = Width,
                                    .key = -1,
                                    .skill = 0,
                                    .skillSerial = 0,
                                    .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, drop, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x5396ff: {}", created.Error()));
    return {};
}

// 0x539586..0x539775: the /2, /6, /7 and /0x15. The local light by subtype (the /6 and /7 by skill); with life under
// 10, its own fades and the local is it; with life 19, the /2 and /0x15 draw a point around the start (+0x24) and the
// /2 releases the drop there. Then the 0x5ac sprite, of scale (20 − life) / 5 + 4, and the speed rises 5.
Core::Expected<void, std::string> StepTheCross(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    CrtRandom& random = *services.random;
    std::optional<glm::vec3> local;
    if (joint.subtype == 2)
        local = glm::vec3{1.0f, 0.5f, Bits(0x3dcccccdu)}; // 0x53959d
    else if (joint.subtype == 0x15)
        local = glm::vec3{Bits(0x3dcccccdu), 0.5f, 1.0f}; // 0x5395b4
    else if (joint.skill == 0)
        local = glm::vec3{Bits(0x3e99999au), Bits(0x3e99999au), 1.0f}; // 0x5395eb
    else if (joint.skill == 1)
        local = glm::vec3(0.5f); // 0x5395d4
    constexpr std::int32_t FadeLife = 10, DropLife = 19;
    if (joint.life < FadeLife)
    {
        FadeTheLight(joint);
        local = joint.light;
    }
    else if (joint.life == DropLife && (joint.subtype == 2 || joint.subtype == 0x15))
    {
        // 0x539669..0x5396d8: angles (0, 0, rand() % 360) and the point (±100, ±100, −200) from the start.
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        angles.z = static_cast<float>(random.Next() % FullTurn);
        glm::vec3 point{};
        point.x = F32((static_cast<double>(random.Next() % 200) + D(joint.pathIncrement.x)) - 100.0);
        point.y = F32((static_cast<double>(random.Next() % 200) + D(joint.pathIncrement.y)) - 100.0);
        point.z = F32(D(joint.pathIncrement.z) - 200.0);
        if (joint.subtype == 2)
            if (auto dropped = DropTheDrop(services, point, angles); !dropped.HasValue())
                return dropped;
    }
    const auto rotation = static_cast<float>(random.Next() % FullTurn); // 0x539709, before reading the light.
    if (!local)
        return Core::Unexpected(
            std::format("0x53975b: la luz local del joint {:#x}/{} con la skill {} queda sin escribir (sin portar)",
                        joint.type, joint.subtype, joint.skill));
    const auto growth = std::bit_cast<std::int32_t>(20u - std::bit_cast<std::uint32_t>(joint.life)) / 5;
    const float scale = F32(static_cast<double>(growth) + 4.0);
    if (auto drawn = Sprite(services, SpiritSprite, joint.position, scale, *local, rotation, 0x53975b);
        !drawn.HasValue())
        return drawn;
    joint.speed = F32(D(joint.speed) + 5.0);
    return {};
}

// 0x539941..0x5399a2: the deviation of 0x53d1a0(slot, dir, 1): the shared counter with its access (4 rand() when
// closing it at 0) and the direction of the phase slot + counter, times 50. x adds the unrounded product.
Core::Expected<void, std::string> Wander(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    if (services.cloth == nullptr)
        return Core::Unexpected(
            std::format("0x53994e: falta el contador compartido del joint {:#x}/{}", joint.type, joint.subtype));
    const auto steps = std::bit_cast<std::uint32_t>(services.cloth->logicSteps);
    if (services.cloth->openCounterAccesses == 0)
        for (std::uint16_t draw = 0; draw < Models::SmallLogicalCloseRandoms; ++draw)
            static_cast<void>(services.random->Next());
    const auto phase = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(services.index) + steps);
    const auto direction = Math::Detail::FaunaDirectionPC53(phase);
    if (!direction)
        return Core::Unexpected(std::string("0x53994e: dirección fuera del perfil PC53"));
    constexpr double Reach = 50.0;
    const float y = F32(D((*direction)[1]) * Reach);
    const float z = F32(D((*direction)[2]) * Reach);
    joint.position.x = F32(D((*direction)[0]) * Reach + D(joint.position.x));
    joint.position.y = F32(D(y) + D(joint.position.y));
    joint.position.z = F32(D(z) + D(joint.position.z));
    return {};
}

// 0x53977a..0x539a09: the /3 and /0xd. The /0xd with life 28 or less seeks its owner: half a turn of 20 in x with
// rand() & 1 at 0, a point ±70 around the owner and 50 higher, 0x4e9810 with speed − 20, speed + 1 up to 50, one life
// less near the point, the local light is its own and the scale (life / 10 + 1) · 2. Otherwise, the deviation and light
// (1; 0.5; 0.1) with scale 3. Then the 0x5ac and 0x5fd sprites (at half scale).
Core::Expected<void, std::string> StepTheBolt(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    CrtRandom& random = *services.random;
    float scale = 3.0f; // [ebp−0x30]
    std::optional<glm::vec3> local;
    constexpr std::int32_t SeekLife = 28;
    if (joint.subtype == 0xd && joint.life <= SeekLife)
    {
        if (joint.ownerContext != nullptr)
        {
            const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
            if (owner == nullptr)
                return Core::Unexpected(std::string("0x5397dd: el dueño del joint 0x613/0xd no es un personaje"));
            if ((random.Next() & 1) == 0)
                joint.angles.x = F32(joint.angles.x >= -90.0f ? D(joint.angles.x) - 20.0 : D(joint.angles.x) + 20.0);
            glm::vec3 target = owner->position;
            target.x = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.x));
            target.y = F32((static_cast<double>(random.Next() % 150) - 70.0) + D(target.y));
            target.z = F32(D(target.z) + 50.0);
            const double distance = Math::MoveToward(joint.position, joint.angles, target, F32(D(joint.speed) - 20.0));
            joint.speed = joint.speed > 50.0f ? 50.0f : F32(D(joint.speed) + 1.0);
            if (!(distance >= 50.0))
                joint.life = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.life) - 1u);
            if (joint.life < 10)
                FadeTheLight(joint);
            local = joint.light;
            scale = F32((static_cast<double>(joint.life / 10) + 1.0) * 2.0);
        }
    }
    else
    {
        if (auto wandered = Wander(joint, services); !wandered.HasValue())
            return wandered;
        local = glm::vec3{1.0f, 0.5f, Bits(0x3dcccccdu)};
    }
    const auto first = static_cast<float>(random.Next() % FullTurn); // 0x5399a4, before reading the light.
    if (!local)
        return Core::Unexpected(
            std::string("0x5399cd: la luz local del joint 0x613/0xd sin dueño queda sin escribir (sin portar)"));
    if (auto drawn = Sprite(services, SpiritSprite, joint.position, scale, *local, first, 0x5399cd); !drawn.HasValue())
        return drawn;
    const auto second = static_cast<float>(random.Next() % FullTurn);
    return Sprite(services, GlowSprite, joint.position, F32(D(scale) * 0.5), *local, second, 0x539a09);
}

// 0x538e1e..0x538e7f: the /8 rises from 110 in x with speed 10 until life 41; then, with speed 50, x goes from 180 to
// 360 by rand() % 3 + 2.
void StepTheArc(Kernel::JointState& joint, CrtRandom& random)
{
    constexpr std::int32_t RiseLife = 42;
    if (joint.life >= RiseLife)
    {
        joint.speed = 10.0f;
        joint.angles.x = 110.0f;
        return;
    }
    joint.speed = 50.0f;
    if (!(joint.angles.x > 180.0f))
        joint.angles.x = 180.0f;
    if (!(joint.angles.x > 360.0f))
        joint.angles.x = F32((static_cast<double>(random.Next() % 3) + D(joint.angles.x)) + 2.0);
}
} // namespace

namespace
{
Core::Expected<CrtRandom*, std::string> RandomOf(const Kernel::JointState& joint,
                                                 const JointKernels::StepServices& services, std::uint32_t address)
{
    if (services.random == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: falta el RNG del joint {:#x}/{:#x}", address, joint.type, joint.subtype));
    return services.random;
}

// 0x53fe70(type, position, angles, light, subtype, scale, 0): an ownerless particle, with the step's RNG and clock.
// With the table full it does nothing (not even rand()).
Core::Expected<void, std::string> Particle(const Kernel::JointState& joint, const JointKernels::StepServices& services,
                                           std::uint32_t address, std::int32_t type, std::int32_t subtype,
                                           const glm::vec3& position, const glm::vec3& light, float scale)
{
    if (services.particles == nullptr || services.random == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas o el RNG del joint {:#x}/{:#x}", address,
                                            joint.type, joint.subtype));
    const auto created = services.particles->CreateChecked(type, position, joint.angles, light, subtype, scale,
                                                           services.worldClock, *services.random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: la partícula {:#x}/{:#x} del joint {:#x}/{:#x} falla con el error {}", address, type,
                        subtype, joint.type, joint.subtype, static_cast<unsigned>(created.Error())));
    return {};
}

// 0x530860(0x613, from, from, angles, subtype, 0, 0.9f, −1, 0, 0, −1): the joints the /0xb and /0xf release, ownerless
// and always 0x613 (also from a 0x6ee).
Core::Expected<void, std::string> Release(Kernel::JointState& joint, const JointKernels::StepServices& services,
                                          std::uint32_t address, glm::vec3& from, glm::vec3& angles,
                                          std::int32_t subtype)
{
    const auto random = RandomOf(joint, services, address);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    const Kernel::JointRequest request{.type = SpiritJoint,
                                       .firstPosition = from,
                                       .secondPosition = from,
                                       .angles = angles,
                                       .subtype = subtype,
                                       .ownerContext = nullptr,
                                       .scale = Bits(0x3f666666u),
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, request, *random.Value()); !created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// FCOS and FSIN of the angles' z (not converted to radians) times a float from memory, added to x and y.
Core::Expected<void, std::string> Drift(Kernel::JointState& joint, float reach, std::uint32_t address)
{
    const auto cosine = Math::Detail::CosineProductPC53(D(joint.angles.z), reach);
    const auto sine = Math::Detail::SineProductPC53(D(joint.angles.z), reach);
    if (!cosine || !sine)
        return Core::Unexpected(
            std::format("{:#x}: el giro del joint {:#x}/{:#x} excede el perfil", address, joint.type, joint.subtype));
    joint.position.x = F32(*cosine + D(joint.position.x));
    joint.position.y = F32(*sine + D(joint.position.y));
    return {};
}

// 0x538f45..0x538feb (the /0xb): the angles' x is the life (fild); the 0x5da/5 particle of 0.9f with light 1 at a copy
// of the position; with rand() % 200 at 0, the 0x613/0xc joint from that copy; then it advances 20 by FCOS and FSIN of
// z (0x6b86d0).
Core::Expected<void, std::string> StepTheStill(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    glm::vec3 from = joint.position;
    joint.angles.x = static_cast<float>(joint.life);
    constexpr std::int32_t Glow = 0x5da, GlowSubtype = 5;
    if (auto made = Particle(joint, services, 0x538f8e, Glow, GlowSubtype, from, glm::vec3(1.0f), Bits(0x3f666666u));
        !made.HasValue())
        return made;
    const auto random = RandomOf(joint, services, 0x538f96);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr std::int32_t Rarity = 200; // 0xc8
    constexpr std::int32_t SmokeSubtype = 0xc;
    if (random.Value()->Next() % Rarity == 0)
        if (auto released = Release(joint, services, 0x538fc3, from, joint.angles, SmokeSubtype); !released.HasValue())
            return released;
    constexpr float Reach = 20.0f; // 0x6b86d0
    return Drift(joint, Reach, 0x538fce);
}

// 0x539328..0x539384 (the /0x10): the angles' x is the life; the 0x6f4/4 particle at scale 1 at the position, with the
// joint's light (+0x48); then it advances 30 by FCOS and FSIN of z (0x6b8858).
Core::Expected<void, std::string> StepTheColumn(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    joint.angles.x = static_cast<float>(joint.life);
    constexpr std::int32_t Splash = 0x6f4, SplashSubtype = 4;
    if (auto made = Particle(joint, services, 0x53935c, Splash, SplashSubtype, joint.position, joint.light, 1.0f);
        !made.HasValue())
        return made;
    constexpr float Reach = 30.0f; // 0x6b8858
    return Drift(joint, Reach, 0x539361);
}

// 0x538ff9..0x539091 (the /0xc): at a copy of the position, the 0x5f5/0 at scale ((rand() & 31) + 48) · 0.01f
// (0x6b88d4) with light (1; 0.2f; 0.1f) and the 0x5da/6 of 0.6f with light 1.
Core::Expected<void, std::string> StepTheSmoke(const Kernel::JointState& joint,
                                               const JointKernels::StepServices& services)
{
    const auto random = RandomOf(joint, services, 0x539024);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    const glm::vec3 from = joint.position;
    constexpr std::int32_t Mask = 31, Base = 0x30;
    constexpr double Hundredth = Math::HundredthAsDouble; // 0x6b88d4
    const std::int32_t size = (random.Value()->Next() & Mask) + Base;
    const float scale = F32(static_cast<double>(size) * Hundredth);
    constexpr std::int32_t Smoke = 0x5f5, Glow = 0x5da, GlowSubtype = 6;
    const glm::vec3 ember{1.0f, Bits(0x3e4ccccdu), Bits(0x3dcccccdu)};
    if (auto made = Particle(joint, services, 0x539058, Smoke, 0, from, ember, scale); !made.HasValue())
        return made;
    return Particle(joint, services, 0x539089, Glow, GlowSubtype, from, glm::vec3(1.0f), Bits(0x3f19999au));
}

// 0x53909f..0x539125 (the /0xe): the angles' x − 2 (0x6b8818) and rand() % 5: with 0 the 0x6f2/2 particle, with 1 or 2
// the 0x6f3/2 and otherwise the 0x6f4/4, at scale 1 at the position, with the joint's light (+0x48).
Core::Expected<void, std::string> StepTheSplash(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    const auto random = RandomOf(joint, services, 0x5390c2);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr double Tilt = 2.0; // 0x6b8818
    joint.angles.x = F32(D(joint.angles.x) - Tilt);
    constexpr std::int32_t Choices = 5;
    const std::int32_t pick = random.Value()->Next() % Choices;
    constexpr std::int32_t First = 0x6f2, Second = 0x6f3, Third = 0x6f4;
    constexpr std::int32_t SplashSubtype = 2, LastSubtype = 4;
    if (pick == 0)
        return Particle(joint, services, 0x5390e6, First, SplashSubtype, joint.position, joint.light, 1.0f);
    if (pick <= 2)
        return Particle(joint, services, 0x539109, Second, SplashSubtype, joint.position, joint.light, 1.0f);
    return Particle(joint, services, 0x539120, Third, LastSubtype, joint.position, joint.light, 1.0f);
}

// 0x539249..0x5392e6: the /0xf's owner (Kundun's 0x126, 0x51cf45: an effect with a model). The x of its angles at 0 if
// it is not zero (fcomp with C3: a zero of either sign or NaN stays); with its life (+0x60) over 5, the alpha (+0x174)
// at 1; its position, the joint's with k = key · 0.1f + 30 stored as float: x + FSIN(z) · k (the fst leaves the sum on
// the x87 stack and the fstp stores it the same), y − FCOS(z) · k and the z; again the angles' x and y at 0.
Core::Expected<void, std::string> CarryThePhoenix(const Kernel::JointState& joint, Effects::ModelEffect& owner)
{
    const auto flatten = [](float& pitch)
    {
        if (pitch < 0.0f || pitch > 0.0f)
            pitch = 0.0f;
    };
    flatten(owner.transform.angles.x);
    constexpr std::int32_t ShownLife = 5;
    if (owner.life > ShownLife)
        owner.alpha = 1.0f;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    constexpr double Reach = 30.0;                // 0x6b8858
    const float radius = F32(static_cast<double>(joint.key) * Tenth + Reach);
    const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), D(radius));
    const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), D(radius));
    if (!sine || !cosine)
        return Core::Unexpected(std::format("0x5392a0: el giro del joint {:#x}/0xf excede el perfil", joint.type));
    owner.transform.position = {F32(D(joint.position.x) + *sine), F32(D(joint.position.y) - *cosine), joint.position.z};
    flatten(owner.transform.angles.x);
    owner.transform.angles.y = 0.0f;
    return {};
}

// 0x539133..0x53931e: the /0xf. The key (WORD) adds 30. With life under 35, at a copy of the position: the 0x5d9/7
// particle at scale 1 with light 1 (0x53918a); with rand() & 1 at 0, the copy becomes (x − FSIN(z) · k, FCOS(z) · k +
// y, 350) with k = 40 − key · 0.1f retained on the x87 stack and releases two 0x613/0xe joints from it with angles (0,
// 0, 0) and (0, 0, 3); then, with an owner, CarryThePhoenix. Always (0x5392e9): it advances k = key · 0.1f + 30 (on the
// x87 stack, unrounded) in x with the sine of the angles' z and subtracts it in y with the cosine.
Core::Expected<void, std::string> StepTheSpiral(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::uint16_t KeyStep = 30;         // 0x53913b
    constexpr std::int32_t SpiralLife = 35;       // 0x53914c
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    joint.key =
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(joint.key) + KeyStep));
    if (joint.life < SpiralLife)
    {
        glm::vec3 from = joint.position;
        constexpr std::int32_t Fall = 0x5d9, FallSubtype = 7;
        if (auto made = Particle(joint, services, 0x53918a, Fall, FallSubtype, from, glm::vec3(1.0f), 1.0f);
            !made.HasValue())
            return made;
        const auto random = RandomOf(joint, services, 0x539192);
        if (!random.HasValue())
            return Core::Unexpected(random.Error());
        if ((random.Value()->Next() & 1) == 0)
        {
            constexpr float Hover = 350.0f; // 0x43af0000
            constexpr double Span = 40.0;   // 0x6b8bf4
            from.z = Hover;
            const double radius = Span - static_cast<double>(joint.key) * Tenth;
            const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), radius);
            const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), radius);
            if (!sine || !cosine)
                return Core::Unexpected(
                    std::format("0x5391d3: el giro del joint {:#x}/0xf excede el perfil", joint.type));
            from.x = F32(D(from.x) - *sine);
            from.y = F32(*cosine + D(from.y));
            constexpr int Pieces = 2;
            constexpr double Spread = 3.0; // 0x6b86b8
            constexpr std::int32_t SplashSubtype = 0xe;
            for (int piece = 0; piece < Pieces; ++piece)
            {
                glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(piece) * Spread)};
                if (auto released = Release(joint, services, 0x539228, from, angles, SplashSubtype);
                    !released.HasValue())
                    return released;
            }
        }
        if (joint.ownerContext != nullptr)
        {
            if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
                return Core::Unexpected(std::format(
                    "0x539249: el dueño del joint {:#x}/0xf que no es un efecto con modelo no está portado (CL11)",
                    joint.type));
            if (auto carried = CarryThePhoenix(joint, *static_cast<Effects::ModelEffect*>(joint.ownerContext));
                !carried.HasValue())
                return carried;
        }
    }
    constexpr double Reach = 30.0; // 0x6b8858
    const double radius = static_cast<double>(joint.key) * Tenth + Reach;
    const auto sine = Math::Detail::SineDoubleProductPC53(D(joint.angles.z), radius);
    if (!sine)
        return Core::Unexpected(std::format("0x539305: el seno del joint {:#x}/0xf excede el perfil", joint.type));
    joint.position.x = F32(*sine + D(joint.position.x));
    const auto cosine = Math::Detail::CosineDoubleProductPC53(D(joint.angles.z), radius);
    if (!cosine)
        return Core::Unexpected(std::format("0x539312: el coseno del joint {:#x}/0xf excede el perfil", joint.type));
    joint.position.y = F32(D(joint.position.y) - *cosine);
    return {};
}

// 0x539392..0x53941e: the /0x11, with life at exactly 100, releases 120 ownerless 0x613/0x10 joints from a copy of the
// position to the position, of width 0.9f (0x3f666666) and with angles (0, 0, 3 · i).
Core::Expected<void, std::string> StepTheBurst(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t BurstLife = 100;
    if (joint.life != BurstLife)
        return {};
    const auto random = RandomOf(joint, services, 0x53940d);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    constexpr int Pieces = 120;    // 0x78
    constexpr double Spread = 3.0; // 0x6b86b8
    glm::vec3 from = joint.position;
    for (int piece = 0; piece < Pieces; ++piece)
    {
        glm::vec3 angles{0.0f, 0.0f, F32(static_cast<double>(piece) * Spread)};
        const Kernel::JointRequest request{.type = SpiritJoint,
                                           .firstPosition = from,
                                           .secondPosition = joint.position,
                                           .angles = angles,
                                           .subtype = 0x10,
                                           .ownerContext = nullptr,
                                           .scale = Bits(0x3f666666u),
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = CreateFaunaJoint(services.joints, request, *random.Value()); !created.HasValue())
            return Core::Unexpected(std::format("0x53940d: {}", created.Error()));
    }
    return {};
}

// 0x53942c..0x539500: the /0x12. Speed + 3; the key (WORD) + 6 with life % 12 of 5 or less and − 6 otherwise; the
// angles' x −90 − key and z + 10. With an owner (Kundun's 0x125, 0x51ce91: JointOwnerKind::ModelEffect), it sets the
// owner's alpha (+0x174) to 1 if its life (+0x60) is over 5 and copies the position to it (+0x10, 0x539494); another
// owner stops there. Then (0x5394be..0x5394fb), at a copy of the position, the 0x5f2/0x13 particle at scale 7
// (0x40e00000) with light 1, and it goes on to the tail (0x53c390).
Core::Expected<void, std::string> StepTheSwing(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    constexpr std::int32_t SwingPeriod = 12, RisingHalf = 5;
    const std::int32_t phase = joint.life % SwingPeriod;
    joint.speed = F32(D(joint.speed) + 3.0);
    const std::uint16_t delta = phase <= RisingHalf ? 6 : 0xfffa;
    joint.key =
        std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(std::bit_cast<std::uint16_t>(joint.key) + delta));
    constexpr double Down = -90.0; // 0x6ba1dc
    joint.angles.x = F32(Down - static_cast<double>(joint.key));
    joint.angles.z = F32(D(joint.angles.z) + 10.0);
    if (joint.ownerContext != nullptr)
    {
        if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
            return Core::Unexpected(std::format(
                "0x539494: el dueño del joint {:#x}/0x12 que no es un efecto con modelo no está portado (CL11)",
                joint.type));
        auto& owner = *static_cast<Effects::ModelEffect*>(joint.ownerContext);
        constexpr std::int32_t ShownLife = 5;
        if (owner.life > ShownLife)
            owner.alpha = 1.0f;
        owner.transform.position = joint.position;
    }
    const glm::vec3 from = joint.position;
    constexpr std::int32_t Flame = 0x5f2, FlameSubtype = 0x13;
    constexpr float FlameScale = 7.0f; // 0x40e00000
    return Particle(joint, services, 0x5394fb, Flame, FlameSubtype, from, glm::vec3(1.0f), FlameScale);
}

// 0x539b10..0x539cc7: the /0 and /5 with a width other than 80. The target is the owner 80 higher; the distance on the
// plane is the one before the move (0x539b33, as float); 0x4e9810 turns toward the target by 10 (the /5, by 2) and with
// twice the speed not less than that distance sets +0x9d4. The /0 shakes with two (rand() & 31) − 16: vx + r1 · 0.2f,
// the angles' x + vx, z + (r2 · 0.8f + vz), vx · 0.6f and vz the shake · 0.8f. With 0x5f93c0, less than 100 above the
// terrain it rises (x at −5) and over 400 it falls (x at 5), with vx at 0. The light is life · 0.1f and the radius-4
// dynamic light subtracts ((rand() & 3) + 4) · 0.01f.
Core::Expected<void, std::string> StepTheVolley(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    const Kernel::ActorState* owner = JointKernels::ActorOwner(joint);
    if (owner == nullptr)
        return Core::Unexpected(
            std::format("0x539b13: el joint {:#x}/{} no tiene un personaje de dueño", joint.type, joint.subtype));
    const auto random = RandomOf(joint, services, 0x539b8e);
    if (!random.HasValue())
        return Core::Unexpected(random.Error());
    if (services.terrainHeight == nullptr || !*services.terrainHeight)
        return Core::Unexpected(
            std::format("0x539c19: falta la altura del terreno del joint {:#x}/{}", joint.type, joint.subtype));
    constexpr double Above = 80.0; // 0x6b9ffc
    joint.targetPosition = {owner->position.x, owner->position.y, F32(D(owner->position.z) + Above)};
    const float distance = F32(std::sqrt(D(services.startDeltaY) * D(services.startDeltaY) +
                                         D(services.startDeltaX) * D(services.startDeltaX)));
    constexpr float SlowTurn = 2.0f, FastTurn = 10.0f; // 0x40000000 and 0x41200000
    static_cast<void>(
        Math::MoveToward(joint.position, joint.angles, joint.targetPosition, joint.subtype == 5 ? SlowTurn : FastTurn));
    if (joint.field9d4 == 0 && D(joint.speed) + D(joint.speed) >= D(distance))
        joint.field9d4 = 1;
    if (joint.subtype != 5)
    {
        constexpr double Pitch = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));   // 0x6b8854, 0.2f
        constexpr double Yaw = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));     // 0x6b8a38, 0.8f
        constexpr double Damping = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0.6f
        constexpr int Mask = 31, Center = 16;
        const auto first = (random.Value()->Next() & Mask) - Center;
        joint.velocity.x = F32(static_cast<double>(first) * Pitch + D(joint.velocity.x));
        const auto second = (random.Value()->Next() & Mask) - Center;
        const double shake = static_cast<double>(second) * Yaw + D(joint.velocity.z);
        joint.angles.x = F32(D(joint.angles.x) + D(joint.velocity.x));
        joint.angles.z = F32(shake + D(joint.angles.z));
        joint.velocity.x = F32(D(joint.velocity.x) * Damping);
        joint.velocity.z = F32(shake * Yaw);
    }
    const double ground = (*services.terrainHeight)(joint.position.x, joint.position.y);
    constexpr double Low = 100.0, High = 400.0; // 0x6b86b4 and 0x6b87b0
    if (ground + Low > D(joint.position.z))
    {
        joint.velocity.x = 0.0f;
        joint.angles.x = -5.0f; // 0xc0a00000
    }
    if (!(ground + High >= D(joint.position.z)))
    {
        joint.velocity.x = 0.0f;
        joint.angles.x = 5.0f; // 0x40a00000
    }
    constexpr double Tenth = Math::TenthAsDouble;                                    // 0x6b87b8
    constexpr double Shade = static_cast<double>(std::bit_cast<float>(0xbc23d70au)); // 0x6b8a58, −0.01f
    joint.light = glm::vec3(F32(static_cast<double>(joint.life) * Tenth));
    const auto shadeSteps = (random.Value()->Next() & 3) + 4;
    const glm::vec3 shade(F32(static_cast<double>(shadeSteps) * Shade));
    constexpr std::int32_t ShadeRadius = 4;
    return Terrain::AddDynamicLight(services.lighting, joint.position, shade, ShadeRadius, "0x539cbf");
}

// 0x539a0e..0x539ae5: the 0x613/0, /5 and /0x13 of width 80. Releases the 0xd0 (0x518cf0(0xd0, position, angles,
// light, 3 for the /5 and 0 for the others, no owner, −1, 0, 0, 0, 0), the falling ray, which draws no rand()). With
// the siege on (0x4294b0) and bit 0x100 of 0x5f82c0 at its point, speed at 0 and life at _ftol(life · 0.2f), and it
// returns to the 0x53c393 tail without the rest (false). Otherwise, with life a multiple of 15 and the hero as owner,
// its targets (0x517a20(the skill +0x9ea, the position, 150, the batch +0x9eb, the key +0x9e8, the byte +0xa08)), and
// it goes on (true).
Core::Expected<bool, std::string> VolleyFromTheSky(Kernel::JointState& joint,
                                                   const JointKernels::StepServices& services)
{
    constexpr std::int32_t ShieldSubtype = 5;
    constexpr std::int32_t FallingRay = 3; // 0x539a2f
    constexpr std::uint32_t SiegeCell = 0x100;
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8cfc: 0.2f
    constexpr std::int32_t HitEvery = 15;                                             // 0x539aa0
    constexpr float HitRadius = 150.0f;                                               // 0x539ad6: 0x43160000
    if (services.effects == nullptr || services.random == nullptr)
        return Core::Unexpected(std::format("0x539a48: el joint {:#x}/{:#x} de ancho 80 pide la tabla de efectos "
                                            "(0x518cf0) para el 0xd0",
                                            joint.type, joint.subtype));
    const Kernel::ModelEffectArguments ray{.type = Effects::LaserEffect,
                                           .position = joint.position,
                                           .angles = joint.angles,
                                           .light = joint.light,
                                           .subtype = joint.subtype == ShieldSubtype ? FallingRay : 0,
                                           .owner = nullptr,
                                           .key = 0xffff};
    const auto created =
        Effects::CreateWizardModelEffect(*services.effects, ray, *services.random, Effects::WizardEffectServices{});
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x539a48: {}", created.Error()));
    if (services.volley == nullptr)
        return Core::Unexpected(std::format("0x539a50: el joint {:#x}/{:#x} de ancho 80 pide el asedio, los atributos "
                                            "y los blancos del héroe",
                                            joint.type, joint.subtype));
    if (services.volley->siegeActive)
    {
        if (services.volley->attributes == nullptr)
            return Core::Unexpected(std::string("0x539a60: faltan los atributos de las celdas (0x7d0d348)"));
        const auto cell =
            Terrain::TerrainAttributeQueryAt(*services.volley->attributes, joint.position.x, joint.position.y);
        if (!cell.HasValue())
            return Core::Unexpected(std::string("0x539a60: 0x5f82c0 lee fuera de la tabla de atributos"));
        if ((cell.Value() & SiegeCell) == SiegeCell)
        {
            joint.speed = 0.0f;
            joint.life = Math::FtolLow(static_cast<double>(joint.life) * Shrink);
            return false;
        }
    }
    if (joint.life % HitEvery != 0)
        return true;
    if (services.hero == nullptr)
        return Core::Unexpected(std::string("0x539aaf: falta el héroe ([0x743a170])"));
    if (joint.ownerContext != services.hero)
        return true;
    if (!services.volley->skillHits)
        return Core::Unexpected(std::string("0x539add: faltan los blancos de la skill (0x517a20)"));
    const Actors::SkillHitsRequest request{.skillIndex = joint.skill,
                                           .point = joint.position,
                                           .radius = HitRadius,
                                           .volley = joint.volley,
                                           .key = static_cast<std::uint16_t>(joint.key),
                                           .extra = joint.skillSerial};
    if (auto hit = services.volley->skillHits(request); !hit.HasValue())
        return Core::Unexpected(hit.Error());
    return true;
}
} // namespace

Core::Expected<void, std::string> StepSpiritJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    const std::int32_t subtype = joint.subtype;
    if (subtype == 0 || subtype == 5 || subtype == 0x13)
    {
        // 0x539a0e: with the width at 80 (bits 0x42a00000), the 0xd0 and what follows (VolleyFromTheSky); if it does
        // not return earlier, the same tail as with another width. With another width, the /0x13 first releases the
        // 0x11b/0 flash (0x539b08).
        constexpr std::uint32_t VolleyWidth = 0x42a00000u;
        if (std::bit_cast<std::uint32_t>(joint.width) == VolleyWidth)
        {
            const auto fallen = VolleyFromTheSky(joint, services);
            if (!fallen.HasValue())
                return Core::Unexpected(fallen.Error());
            if (!fallen.Value())
                return {};
            return StepTheVolley(joint, services);
        }
        if (subtype == 0x13)
        {
            // 0x539aec..0x539b08: 0x518cf0(0x11b, position, angles, light, 0, 0, −1, 0, 0, 0, 0), no rand().
            if (services.effects == nullptr)
                return Core::Unexpected(std::format(
                    "0x539b08: el joint {:#x}/0x13 pide la tabla de efectos (0x518cf0) para el 0x11b/0", joint.type));
            static_cast<void>(
                Effects::CreateTheRingSpark(*services.effects, joint.position, joint.angles, joint.light));
        }
        return StepTheVolley(joint, services);
    }
    switch (subtype)
    {
    case 0xa:
        // 0x538e89..0x538e9f: the angles' z − 50 and speed 50.
        joint.angles.z = F32(D(joint.angles.z) - 50.0);
        joint.speed = 50.0f;
        return {};
    case 0xb:
        return StepTheStill(joint, services);
    case 0x10:
        return StepTheColumn(joint, services);
    case 0xc:
        return StepTheSmoke(joint, services);
    case 0xe:
        return StepTheSplash(joint, services);
    case 0xf:
        return StepTheSpiral(joint, services);
    case 0x11:
        return StepTheBurst(joint, services);
    case 0x12:
        return StepTheSwing(joint, services);
    case 1:
    case 2:
    case 3:
    case 6:
    case 7:
    case 8:
    case 0xd:
    case 0x14:
    case 0x15:
        break;
    default:
        // 0x538d77..0x539505: the comparison chain has no case for the /4 (and the /9 has its own in
        // FaunaJointCreation.cpp, StepMeteor) nor for subtypes over 0x15: they go straight to the 0x53c393 tail.
        return {};
    }
    if (services.random == nullptr)
        return Core::Unexpected(std::format("0x538d77: falta el RNG del joint {:#x}/{:#x}", joint.type, subtype));
    switch (subtype)
    {
    case 1:
        return StepTheSpark(joint, services);
    case 3:
    case 0xd:
        return StepTheBolt(joint, services);
    case 8:
    case 0x14: // 0x53950e..0x53956b: same as the /8.
        StepTheArc(joint, *services.random);
        return {};
    default:
        return StepTheCross(joint, services);
    }
}
} // namespace Client::Joints::JointFamilies
