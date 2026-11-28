#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "Kernel/ActorState.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Effects/SkinShells.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <string>

namespace Client::Joints::JointFamilies
{
namespace
{
using Math::D;
using Math::F32;

// 0x53c0a5..0x53c390 with the /0..3 and /5, after the common move. The /2 turns its z by rand() % 10 + 10 (on the x87
// stack, rand + z + 10). The speed rises 10, the tail with the angle matrix (0x535520, if it is on), it rises another
// 10 and advances (0, −speed, 0) rotated by +0x3c. Below the terrain height minus +0x9ec (150, or 300 for the /2 and
// /3), z stays there and, with the tail and a key other than 1: the /0 and /1 with an owner pose the 0x20b and release
// the 0x137 shells of its vertices (ShedTheSkinShells); the /2, /3 and /5, the 0x6eb/4 wave (0x53c295: 0x518cf0(0x6eb,
// position, angles, light, 4, 0, −1, 0, 0, 0, 0), with its rand() if there is room) with light (1; 0.8f; 0.3f) for the
// /5 and (0.3f; 0.8f; 1) for the others; then the /5 sets the key to 1 and the speed to −20, the others turn the tail
// off, and the light times 0.8333333f. Always, the sprite with a rand() % 360 turn: the 0x60f at scale 1 with the light
// for the /0 and /1, at 2.5 for the /5, and the 0x5fe at 1.6f with (0; 1; 1) for the /2 and /3.
Core::Expected<void, std::string> StepFallingJoint(Kernel::JointState& joint,
                                                   const JointKernels::StepServices& services)
{
    if (services.random == nullptr || services.sprites == nullptr || services.terrainHeight == nullptr ||
        !*services.terrainHeight)
        return Core::Unexpected(std::format(
            "0x53c0c9: faltan el RNG, los sprites o la altura del terreno del joint 0x632/{}", joint.subtype));
    constexpr std::int32_t TurningSubtype = 2, FirstGlowSubtype = 2;
    constexpr double Boost = 10.0; // 0x6b87a8
    if (joint.subtype == TurningSubtype)
    {
        constexpr int TurnSpan = 10;
        const int turn = services.random->Next() % TurnSpan;
        joint.angles.z = F32((static_cast<double>(turn) + D(joint.angles.z)) + Boost);
    }
    joint.speed = F32(D(joint.speed) + Boost);
    if (joint.tailEnabled)
        if (auto appended = JointKernels::AppendJointTail(joint, Math::OriginalAngleMatrix(joint.angles));
            !appended.HasValue())
            return appended;
    joint.speed = F32(D(joint.speed) + Boost);
    const auto moved = Math::OriginalVectorRotate({0, -joint.speed, 0}, Math::OriginalAngleMatrix(joint.bendAngles));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    // 0x53c16d..0x53c18a: fisub of +0x9ec and fcom with z (test ah,0x41): only with the ground strictly above.
    const double floor =
        (*services.terrainHeight)(joint.position.x, joint.position.y) - static_cast<double>(joint.field9ec);
    if (floor > D(joint.position.z))
    {
        joint.position.z = F32(floor);
        constexpr std::int16_t Landed = 1;
        if (joint.tailEnabled && joint.key != Landed)
        {
            if (joint.subtype < FirstGlowSubtype)
            {
                // 0x53c1bb..0x53c246: with an owner (the target character of the 0x44 or 0x47), the 0x20b posed with
                // its angles and the 0x137 shells of its vertices (Effects/SkinShells.h); without an owner, nothing.
                if (joint.ownerContext != nullptr)
                {
                    if (joint.ownerKind != Kernel::JointOwnerKind::Actor &&
                        joint.ownerKind != Kernel::JointOwnerKind::ByType)
                        return Core::Unexpected(
                            std::format("0x53c1bb: el dueño del joint 0x632/{} no es un personaje", joint.subtype));
                    if (services.models == nullptr || services.world == nullptr || services.effects == nullptr)
                        return Core::Unexpected(std::format("0x53c1c6: el joint 0x632/{} en el suelo pide los modelos, "
                                                            "los modelos compartidos y la tabla de efectos",
                                                            joint.subtype));
                    auto& owner = *static_cast<Kernel::ActorState*>(joint.ownerContext);
                    if (auto shed = Effects::ShedTheSkinShells(owner, joint.subtype, *services.models, *services.world,
                                                               *services.effects, *services.random);
                        !shed.HasValue())
                        return shed;
                }
            }
            else
            {
                if (services.effects == nullptr)
                    return Core::Unexpected(std::format(
                        "0x53c295: el joint 0x632/{} pide la tabla de efectos (0x518cf0) para la onda 0x6eb/4",
                        joint.subtype));
                constexpr std::int32_t WaveSubtype = 4;
                const glm::vec3 glow =
                    joint.subtype == FallingStarSubtype
                        ? glm::vec3{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e99999au)}
                        : glm::vec3{std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3f4ccccdu), 1.0f};
                static_cast<void>(services.effects->CreateCastleShockWave({.type = Effects::ShockWaveEffect,
                                                                           .position = joint.position,
                                                                           .angles = joint.angles,
                                                                           .light = glow,
                                                                           .subtype = WaveSubtype},
                                                                          *services.random));
            }
        }
        if (joint.subtype == FallingStarSubtype)
        {
            constexpr float Bounce = std::bit_cast<float>(0xc1a00000u); // −20
            joint.key = Landed;
            joint.speed = Bounce;
        }
        else
            joint.tailEnabled = false;                                                  // 0x53c2b8
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98, 0.8333333f
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    // 0x53c2e7..0x53c38b: 0x5491e0(texture, position, scale, light, 0, turn, 0); MuRet does not store the sprite's
    // owner.
    constexpr int FullTurn = Math::FullTurn;
    const auto rotation = static_cast<float>(services.random->Next() % FullTurn);
    if (joint.subtype < FirstGlowSubtype || joint.subtype == FallingStarSubtype)
    {
        constexpr std::uint16_t StarTexture = 0x60f;
        const float scale = joint.subtype == FallingStarSubtype ? 2.5f : 1.0f; // 0x40200000 and 0x3f800000
        static_cast<void>(services.sprites->Create(StarTexture, joint.position, scale, joint.light, rotation, 0));
        return {};
    }
    constexpr std::uint16_t ShinyTexture = 0x5fe;
    constexpr float ShinyScale = std::bit_cast<float>(0x3fcccccdu); // 1.6f
    const glm::vec3 cyan{0.0f, 1.0f, 1.0f};
    static_cast<void>(services.sprites->Create(ShinyTexture, joint.position, ShinyScale, cyan, rotation, 0));
    return {};
}

// 0x53be90..0x53bebd and 0x53c076..0x53c0a0: the /4 follows its owner (the 0x13a effect, 0x51e01a): without it, it
// turns off; with it, it takes its alive byte and position, and with less than 15 life the light times 0.7692308f.
Core::Expected<void, std::string> FollowTheBlast(Kernel::JointState& joint)
{
    if (joint.ownerContext == nullptr)
    {
        joint.alive = false; // 0x53be97
        return {};
    }
    if (joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
        return Core::Unexpected(std::string("0x53be9f: el dueño del joint 0x632/4 no es un efecto con modelo"));
    const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
    joint.alive = owner.alive;
    joint.position = owner.transform.position;
    constexpr std::int32_t FadingLife = 15;
    if (joint.life < FadingLife)
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateStormJoint(Kernel::JointState& joint, const Kernel::JointRequest& request,
                                                   Crt::CrtRandom& random)
{
    if (request.subtype == StormSubtype)
    {
        // 0x534b27..0x534b2e: no tail and life 0; FaunaJointCreation.cpp creates its three /6 (0x534baa).
        joint.tailEnabled = false;
        joint.life = 0;
        return {};
    }
    if (request.subtype > StormSubtype)
    {
        // 0x534b1e → 0x534cb6: after the first segment, only the 50-point cap.
        constexpr std::int32_t MostPoints = 50;
        if (joint.maximumCount > MostPoints)
            joint.maximumCount = MostPoints;
        return {};
    }
    if (request.subtype <= LastFallingSubtype || request.subtype == FallingStarSubtype)
    {
        // 0x534bcb..0x534c0c: 10 points, life 40, speed 70, the width, +0x9f1 at rand() & 1, the key at 0 and +0x9ec at
        // 150 (what the step subtracts from the terrain height). The /2 and /3 (0x534c1e..0x534c54): speed 30, +0x9ec
        // at 300 and textures 0x62c and 0x62b. 0x534c61..0x534c8e: the /5 with texture 0x60f, 15 points, speed rand() %
        // 20 + 10 and +0x9f1 at 2; the others (0x534c97), angles at (90, 0, 0). 0x534ca4: +0x3c, the angles. One rand()
        // (two for the /5).
        constexpr std::int32_t Life = 0x28, Points = 0xf, Below = 0x96, DeepBelow = 0x12c;
        constexpr float Speed = 70, SlowSpeed = 30; // 0x428c0000 and 0x41f00000
        constexpr std::uint16_t StarTexture = 0x60f, SpiralTexture = 0x62c, FlareTexture = 0x62b;
        constexpr int SpeedSpan = 0x14;
        constexpr double SpeedBase = 10.0; // 0x6b87a8
        constexpr std::uint8_t StarOrientation = 2;
        constexpr float Upright = 90.0f; // 0x42b40000
        joint.maximumCount = 10;
        joint.life = Life;
        joint.speed = Speed;
        joint.width = request.scale;
        joint.orientation = static_cast<std::uint8_t>(random.Next() & 1);
        joint.key = 0;
        joint.field9ec = Below;
        if (request.subtype == 2 || request.subtype == 3)
        {
            joint.speed = SlowSpeed;
            joint.field9ec = DeepBelow;
            joint.texture = request.subtype == 2 ? SpiralTexture : FlareTexture;
        }
        if (request.subtype == FallingStarSubtype)
        {
            joint.texture = StarTexture;
            joint.maximumCount = Points;
            joint.speed = F32(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase);
            joint.orientation = StarOrientation;
        }
        else
            joint.angles = {Upright, 0.0f, 0.0f};
        joint.bendAngles = joint.angles;
        return {};
    }
    if (request.subtype == BlastSubtype)
    {
        // 0x534977..0x534a08: the width, speed 0, pass 1 (+0x64), texture 0x62b, light at 1, the velocity
        // (+0x9dc..+0x9e4) at 0, 20 points, life 30 and the batch (+0x9eb) at 0; with an owner (the 0x13a effect), its
        // position and angles (0x5343f8). Without an owner, the original turns the joint off and dereferences the null
        // pointer (0x5349d3): MuRet stops. No rand().
        constexpr std::uint16_t FlareTexture = 0x62b;
        constexpr std::int32_t Points = 0x14, Life = 0x1e;
        joint.width = request.scale;
        joint.speed = 0;
        joint.category = 1;
        joint.texture = FlareTexture;
        joint.light = glm::vec3(1.0f);
        joint.velocity = glm::vec3(0.0f);
        joint.maximumCount = Points;
        joint.life = Life;
        joint.volley = 0;
        if (joint.ownerContext == nullptr || joint.ownerKind != Kernel::JointOwnerKind::ModelEffect)
        {
            joint.alive = false;
            return Core::Unexpected(std::string("0x5349d3: el joint 0x632/4 sin un efecto con modelo de dueño"));
        }
        const auto& owner = *static_cast<const Effects::ModelEffect*>(joint.ownerContext);
        joint.position = owner.transform.position;
        joint.angles = owner.transform.angles;
        return {};
    }
    if (request.subtype != BoltSubtype)
        return Core::Unexpected(std::format("0x534959: el subtipo {} del joint 0x632 no llega acá", request.subtype));
    // 0x534a16..0x534b19.
    constexpr std::int32_t Life = 0x19;
    constexpr float Radian = Math::DegreesToRadians;          // 0x6b8cf8
    constexpr float Pale = std::bit_cast<float>(0x3f4ccccdu); // 0.8f
    joint.life = Life;
    joint.width = request.scale;
    joint.maximumCount = 0x1e;
    joint.volley = static_cast<std::uint8_t>(Life);
    joint.speed = F32(static_cast<double>(random.Next() % 3 + 0x46) * D(Radian));
    joint.light = {Pale, Pale, 1};
    joint.velocity.x = 0;
    joint.velocity.y = F32(-(static_cast<double>(random.Next() & 3) + 40));
    const auto swing = Math::Detail::SineProductPC53(D(joint.speed), 10.0f);
    if (!swing)
        return Core::Unexpected(std::string("0x534aa4: el seno del joint 0x632/6 excede el perfil PC53"));
    joint.velocity.z = F32(*swing);
    joint.position.y = F32(D(joint.position.y) - 20);
    joint.position.z = F32(D(joint.position.z) + 130);
    joint.pathIncrement = joint.position;
    joint.angles = request.angles;
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    joint.tailEnabled = false;
    joint.orientation = 3;
    if (auto appended = JointKernels::AppendCrossedJointTail(joint, matrix, true); !appended.HasValue())
        return appended;
    joint.count = 0; // 0x534b12: after 0x535020.
    return {};
}

namespace
{
// 0x53bfa9..0x53c04f: the 0x632/6 on the ground. A copy of the position; the 0x614/4 joint from the position to the
// position with its angles, ownerless, at scale 60 (0x42700000), key −1 (0x530860, 0x53bfd3); with the velocity's z
// (+0x9e4) at 0 or less (fcomp, C0 or C3), z at FSIN(speed) · 20 (0x52e3a0, retained until the fmul); the position's z
// on the ground + 5 (fst) and the copy's at that unrounded value + 30; the 0x6ac/2 particle at scale 3 at the copy,
// with the joint's angles and light (0x53c04a).
Core::Expected<void, std::string> HitTheGround(Kernel::JointState& joint, const JointKernels::StepServices& services,
                                               float ground)
{
    glm::vec3 from = joint.position;
    constexpr std::int32_t GroundBoltJoint = 0x614, GroundBoltSubtype = 4;
    constexpr float GroundBoltScale = 60.0f; // 0x42700000
    const Kernel::JointRequest bolt{.type = GroundBoltJoint,
                                    .firstPosition = joint.position,
                                    .secondPosition = joint.position,
                                    .angles = joint.angles,
                                    .subtype = GroundBoltSubtype,
                                    .ownerContext = nullptr,
                                    .scale = GroundBoltScale,
                                    .key = -1,
                                    .skill = 0,
                                    .skillSerial = 0,
                                    .target = -1};
    if (auto created = CreateFaunaJoint(services.joints, bolt, *services.random); !created.HasValue())
        return Core::Unexpected(std::format("0x53bfd3: {}", created.Error()));
    if (!(joint.velocity.z > 0.0f))
    {
        constexpr float Bounce = 20.0f; // 0x6b86d0
        const auto sine = Math::Detail::SineProductPC53(D(joint.speed), Bounce);
        if (!sine)
            return Core::Unexpected(std::string("0x53c00a: la velocidad del joint 0x632/6 excede el perfil del seno"));
        joint.velocity.z = F32(*sine);
    }
    constexpr double Lift = 5.0, Flame = 30.0; // 0x6b87f8 and 0x6b8858
    const double landed = D(ground) + Lift;
    joint.position.z = F32(landed);
    from.z = F32(landed + Flame);
    constexpr std::int32_t Flare = 0x6ac, FlareSubtype = 2;
    constexpr float FlareScale = 3.0f; // 0x40400000
    if (services.particles == nullptr)
        return Core::Unexpected(std::string("0x53c04a: faltan las partículas del joint 0x632/6"));
    const auto made = services.particles->CreateChecked(Flare, from, joint.angles, joint.light, FlareSubtype,
                                                        FlareScale, services.worldClock, *services.random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("0x53c04a: la partícula 0x6ac/2 del joint 0x632/6 falla con el error {}",
                                            static_cast<unsigned>(made.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepStormJoint(Kernel::JointState& joint, const JointKernels::StepServices& services)
{
    if (joint.subtype >= StormSubtype)
        return {}; // 0x53bec5: the /7 and those over 7 only go through the tail and life.
    if (joint.subtype <= LastFallingSubtype || joint.subtype == FallingStarSubtype)
        return StepFallingJoint(joint, services);
    if (joint.subtype == BlastSubtype)
        return FollowTheBlast(joint);
    if (joint.subtype != BoltSubtype)
        return Core::Unexpected(std::format("0x53be76: el subtipo {} del joint 0x632 no llega acá", joint.subtype));
    // 0x53becb..0x53bf55: the velocity's y trembles rand() % 5 − 2, z drops 10 and it moves by the angles.
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x53becb: falta el RNG del paso del joint 0x632/6"));
    const int shake = services.random->Next() % 5 - 2;
    joint.velocity.y = F32(static_cast<double>(shake) + D(joint.velocity.y));
    joint.velocity.z = F32(D(joint.velocity.z) - 10);
    const auto matrix = Math::OriginalAngleMatrix(joint.angles);
    const auto moved = Math::OriginalVectorRotate(joint.velocity, matrix);
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    // 0x53bf3c: the batch (+0x9eb, the creation's life) minus the life. With less than 10 (the first ten steps), the
    // 0x5ee/0 particle at +0x24, with the light and scale 3, and width 100; then, width 40.
    if (static_cast<std::int32_t>(joint.volley) - joint.life < 10)
    {
        // 0x53bf57..0x53bf6e: 0x53fe70(0x5ee, +0x24, +0x30, +0x48, 0, 3, 0); its creation requests sound 0x40
        // (0x542aa7).
        constexpr float BlazeScale = 3.0f; // 0x53bf59
        constexpr Audio::SoundId BlazeSound = Sound::ExplosionSound;
        if (services.particles == nullptr)
            return Core::Unexpected(std::string("0x53bf6e: faltan las partículas del joint 0x632/6"));
        const auto made =
            services.particles->CreateChecked(Particles::BlazeParticle, joint.pathIncrement, joint.angles, joint.light,
                                              0, BlazeScale, services.worldClock, *services.random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x53bf6e: la partícula 0x5ee del joint 0x632/6 falla con el error {}",
                                                static_cast<unsigned>(made.Error())));
        if (made.Value().has_value() && services.soundRequest != nullptr && *services.soundRequest)
            (*services.soundRequest)(BlazeSound);
        joint.width = 100;
    }
    else
        joint.width = 40;
    // 0x53bf86..0x53bfa3: the terrain height (0x5f93c0), stored as float. With z above it, only the crossed tail; on
    // the ground or below (or NaN), HitTheGround first.
    if (services.terrainHeight == nullptr || !*services.terrainHeight)
        return Core::Unexpected(std::string("0x53bf8d: falta la altura del terreno del joint 0x632/6"));
    const float ground = F32((*services.terrainHeight)(joint.position.x, joint.position.y));
    if (!(joint.position.z > ground))
        if (auto hit = HitTheGround(joint, services, ground); !hit.HasValue())
            return hit;
    // 0x53c052..0x53c0a0: 0x535020(joint, the step's matrix, 1) and, with life below half the batch, the light ·
    // 0.7692308f.
    if (auto appended = JointKernels::AppendCrossedJointTail(joint, matrix, true); !appended.HasValue())
        return appended;
    if (joint.life < static_cast<std::int32_t>(joint.volley >> 1))
    {
        constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * Fade);
    }
    return {};
}
} // namespace Client::Joints::JointFamilies
