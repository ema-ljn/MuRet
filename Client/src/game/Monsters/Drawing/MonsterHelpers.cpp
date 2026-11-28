#include "Monsters/Drawing/MonsterHelpers.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Effects/CrywolfDeathSpiritEffects.h"
#include "Joints/FaunaJointCreation.h"
#include "Assets/FaunaModels.h"
#include "Actors/Drawing/GameRibbonCreation.h"
#include "Kernel/DrawTailState.h"
#include "Effects/HighModelEffects.h"
#include "Joints/JointFamilies.h"
#include "Effects/MeteorEffects.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/WorldRuntime.h"
#include "Tables/PlayerModels.h"
#include "Render/TextureSlot.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "Particles/EffectParticleKernels.h"
#include "Kernel/JointRequest.h"
#include "Particles/KalimaParticleKernels.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/ParticleBurst.h"
#include "Particles/ParticleOwner.h"
#include "CastleSiege/SiegeVertexEffects.h"
#include "Particles/SmokeParticleKernels.h"
#include "Terrain/TerrainDecal.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Monsters::Drawing
{
namespace
{
using Math::BoneTransform;
using Result = Core::Expected<void, std::string>;
using Handled = Core::Expected<bool, std::string>;

// The bits 0x3f666666, 0x3e4ccccd and 0x3dcccccd.
constexpr glm::vec3 RedEyes{0.9f, 0.2f, 0.1f};
// Crywolf's blue light (0x3e99999a, 0x3e99999a and 0x3f333333).
constexpr glm::vec3 BlueGlow{0.3f, 0.3f, 0.7f};
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Shine = 0x5fd;
constexpr std::uint16_t Spark = 0x5ad;
constexpr std::uint16_t CrywolfMark = 0x69b;
using Maps::CrywolfMap;
// The 0x53fe70 types Particles::Particles creates: the bolt 0x5ca/0, the breath 0x5f2/11 (the stone dust), the dust
// 0x5f3/0, the fire 0x5d9/0 and the chasing light 0x729/0 and /1.
constexpr std::int32_t Thunder = 0x5ca;
constexpr std::int32_t Breath = 0x5f2;
constexpr std::int32_t StoneDust = 11;
constexpr std::int32_t Dust = 0x5f3;
constexpr std::int32_t Fire = 0x5d9;
constexpr std::int32_t ChaseLight = 0x729;
// The 0x190 pool characters (0x743a16c): the +0x3a4 target is read as a signed WORD.
constexpr std::int16_t PoolSize = 0x190;

using Math::D;
using Math::F32;

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

bool Resting(const Kernel::ActorState& actor)
{
    return actor.animation.action == 0 || actor.animation.action == 1;
}

bool Idling(const Kernel::ActorState& actor)
{
    return actor.animation.action == 2 || actor.animation.action == 10;
}

// rand() % 2: AND 0x80000001 with its sign fix-up, over a rand() that is never negative.
std::int32_t Coin(Crt::CrtRandom& random)
{
    return random.Next() & 1;
}

// 0x620a90: the additive blend with its integer profile, without touching the depth test or the bound texture (as the
// ground marks do, GroundMarks.cpp).
Render::ModelDrawState Blended(Render::ModelDrawState state, Render::ModelBlend blend)
{
    state.blend = blend;
    state.cullEnabled.reset();
    state.depthWrite.reset();
    state.alphaTest.reset();
    state.texture2d.reset();
    return state;
}

Core::Unexpected<std::string> Cut(std::uint32_t address, std::string_view what, const Kernel::ActorState& actor)
{
    return Core::Unexpected(std::format("{:#x}: {} (el tipo {:#x} en la acción {:#x}) no está portado", address, what,
                                        actor.type, actor.animation.action));
}

// 0x4010b0(object, name, point) and 0x4010d0(object, name, offset, point): the bone 0x4016c0 finds with the object and
// the exact name (the list of 0x401000), and 0x401550 or 0x4015f0: 0x4ec110 without translating in the own
// buffer (+0x120), times the scale (+0xc) plus the position (+0x10), as OriginalTransformPosition. Without the bone,
// the original returns 0 and leaves the point as it was on the stack: it cuts at the call `call`.
Core::Expected<glm::vec3, std::string> NamedBonePoint(const Kernel::WorldState& world, const Kernel::ActorState& actor,
                                                      std::size_t slot, std::string_view name, const glm::vec3& offset,
                                                      std::uint32_t call)
{
    const auto& bones = world.Runtime().namedBones;
    const auto found = std::ranges::find_if(bones, [slot, name](const Kernel::NamedBone& bone)
                                            { return bone.actor == slot && bone.name == name; });
    if (found == bones.end())
    {
        return Core::Unexpected(std::format("{:#x}: 0x4016c0 no encuentra el hueso {} del lugar {} (el tipo {:#x}) y "
                                            "el punto queda sin escribir",
                                            call, name, slot, actor.type));
    }
    const BoneTransform matrix =
        found->bone < actor.bodyPose.size() ? actor.bodyPose[found->bone] : BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, actor.scale, actor.position);
}

Core::Expected<glm::vec3, std::string> PointOf(const MonsterHelperPass& pass, std::string_view name, std::uint32_t call,
                                               const glm::vec3& offset = {})
{
    return NamedBonePoint(pass.world, pass.actor, pass.slot, name, offset, call);
}

// 0x5491e0(texture, point, scale, light, object, turn, blend): MuRet does not store the owner.
void Sprite(const MonsterHelperPass& pass, std::uint16_t texture, const glm::vec3& point, float scale,
            const glm::vec3& light, float rotation = 0.0f, std::int32_t blend = 0)
{
    static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, blend));
}

Result NeedTheSprites(const MonsterHelperPass& pass, std::uint32_t address)
{
    if (pass.sprites != nullptr)
        return {};
    return Core::Unexpected(
        std::format("{:#x}: faltan los sprites del frame (0x5491e0) para el tipo {:#x}", address, pass.actor.type));
}

Core::Unexpected<std::string> NoParticles(const Kernel::ActorState& actor, std::uint32_t call)
{
    return Core::Unexpected(
        std::format("{:#x}: faltan las partículas del frame (0x53fe70) para el tipo {:#x}", call, actor.type));
}

// 0x53fe70(type, position, angles, light, subtype, scale, 0) at `call`, with a type Particles::Particles creates: with
// the table full it creates nothing and takes no rand(); with room, the rand() of its spawn.
Result CreateParticle(Particles::Particles* particles, const Kernel::ActorState& actor, std::int32_t type,
                      std::int32_t subtype, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                      float scale, float worldClock, Crt::CrtRandom& random, std::uint32_t call)
{
    if (particles == nullptr)
        return NoParticles(actor, call);
    const auto created = particles->CreateChecked(type, position, angles, light, subtype, scale, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: la partícula {:#x}/{} (0x53fe70) del tipo {:#x} falla con el error {}", call, type,
                        subtype, actor.type, static_cast<int>(created.Error())));
    return {};
}

Result Particle(const MonsterHelperPass& pass, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                const glm::vec3& angles, const glm::vec3& light, float scale, std::uint32_t call)
{
    return CreateParticle(pass.particles, pass.actor, type, subtype, position, angles, light, scale, pass.worldClock,
                          pass.random, call);
}

// 0x53fe70 with the character as owner (+0x40), its pool slot in MuRet: the 0x6ec/3 and /4 fantas read it in their
// step.
Result OwnedParticle(const MonsterHelperPass& pass, std::int32_t type, std::int32_t subtype, const glm::vec3& position,
                     const glm::vec3& angles, const glm::vec3& light, float scale, std::uint32_t call)
{
    if (pass.particles == nullptr)
        return NoParticles(pass.actor, call);
    const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::Actor, pass.slot};
    const auto created = pass.particles->CreateOwnedChecked(type, position, angles, light, subtype, scale,
                                                            pass.worldClock, pass.random, 0, owner);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: la partícula {:#x}/{} (0x53fe70) del tipo {:#x} falla con el error {}", call, type,
                        subtype, pass.actor.type, static_cast<int>(created.Error())));
    return {};
}

// 0x4335c7..0x4336dd: even with every effect taken, it first reads the target's boundsTop with a signed index.
Result EmitDeathSpiritSwing(Kernel::WorldState& world, Kernel::ActorState& actor, Particles::Particles* particles,
                            Crt::CrtRandom& random, float clock)
{
    const auto targetSlot = static_cast<std::int16_t>(actor.attackTarget);
    if (targetSlot < 0 || targetSlot >= PoolSize)
        return Core::Unexpected(std::format("0x4335f6: el blanco firmado {} de147 está fuera del pool", targetSlot));
    Kernel::ActorState& target = world.Actors()[static_cast<std::size_t>(targetSlot)];
    const glm::vec3 light{0, 0, target.boundsTop};
    const BoneTransform bone = actor.bodyPose.empty() ? BoneTransform(0) : actor.bodyPose[0];
    glm::vec3 point =
        Math::OriginalTransformPosition(Math::OriginalMatrixOf(bone), {80, 0, 20}, actor.scale, actor.position);
    point.z = F32(D(point.z) + 50.0);
    for (const float yaw : {90.0f, -90.0f})
        for (const std::int32_t subtype : {0, 1})
        {
            const auto created = Effects::CreateCrywolfDeathSpirit({.type = 0x147,
                                                                    .position = point,
                                                                    .angles = {0, 0, F32(D(actor.angles.z) + D(yaw))},
                                                                    .light = light,
                                                                    .subtype = subtype,
                                                                    .owner = &target},
                                                                   world.Map().effects, particles, random, clock);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
        }
    return {};
}

// FLD T; FMUL rate; FSIN; FMUL factor (a positive .data float): the sine times the factor kept with a single R53
// (ADR0014). Outside the profile (a clock over 2^63 / rate ms) it cuts at `call`.
Core::Expected<double, std::string> ClockSine(const MonsterHelperPass& pass, float rate, float factor,
                                              std::uint32_t call)
{
    const auto product = Math::Detail::SineProductPC53(D(pass.worldClock) * D(rate), factor);
    if (!product)
        return Cut(call, "el seno del reloj fuera del perfil PC53", pass.actor);
    return *product;
}

// 0x41ed10(sound, 0, 0) (CL10): dispatched only in an original frame.
void Sound(const MonsterHelperPass& pass, Audio::SoundId sound)
{
    if (pass.originalFrame)
        pass.world.Runtime().RequestSound(sound);
}

void SoundOnce(const MonsterHelperPass& pass, std::int32_t sound, bool pair)
{
    if (pass.actor.subtype != 0)
        return;
    pass.actor.subtype = 1;
    Sound(pass, Audio::SoundId{pair ? sound + Coin(pass.random) : sound});
}

struct ActionSound
{
    std::uint8_t action = 0;
    std::int32_t sound = 0;
};

// The pattern of 0x434180 and 0x4271d0: in actions 2 and 10, rand() % 15 at 0 requests `idle` plus rand() % 2, without
// looking at +4; in the actions of `sounds`, with +4 at 0, +4 = 1 and its own.
void PlayTheSounds(const MonsterHelperPass& pass, std::int32_t idle, std::span<const ActionSound> sounds)
{
    const Kernel::ActorState& actor = pass.actor;
    if (Idling(actor))
    {
        if (pass.random.Next() % 15 == 0)
            Sound(pass, Audio::SoundId{idle + Coin(pass.random)});
        return;
    }
    const auto found = std::ranges::find_if(sounds, [&actor](const ActionSound& entry)
                                            { return entry.action == actor.animation.action; });
    if (found != sounds.end())
        SoundOnce(pass, found->sound, false);
}

// The common tail: +4 to 0 in actions 0 and 1 (0x442dd2, 0x43458e, 0x428dbd).
void ResetAtRest(Kernel::ActorState& actor)
{
    if (Resting(actor))
        actor.subtype = 0;
}

// 0x441440..0x44164b: a 0x5ac at 0.5 on each eye, the right one first; 0x1f5 plus rand() % 2 in actions 2 and 10 with
// rand() % 15 at 0, 0x1f7 plus rand() % 2 in 3 and 4, and 0x1f9 in 6.
Result DrawThe1a3(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4414fb); !ready.HasValue())
        return ready;
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster81_EyeRight", 0x44149e}, {"Monster81_EyeLeft", 0x441552}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
    }
    const std::uint8_t action = pass.actor.animation.action;
    if (Idling(pass.actor) && pass.random.Next() % 15 == 0)
        Sound(pass, Audio::SoundId{0x1f5 + Coin(pass.random)});
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x1f7, true);
    if (action == 6)
        SoundOnce(pass, 0x1f9, false);
    return {};
}

// 0x4ee770(model, 0, 0x13c, `subtype`, 0, 0) from `call` and +0 at 0 (the character stops being there). The effects
// only in an original frame: between frames the character is a copy and the effect table is the world's.
Result DieInStones(MonsterHelperPass& pass, std::int32_t subtype, std::uint32_t call)
{
    constexpr std::int32_t GolemStones = 0x13c;
    if (pass.originalFrame)
    {
        if (!pass.vertexEffects)
            return Cut(call, "0x4ee770 sin las mallas del cuerpo", pass.actor);
        if (auto emitted = pass.vertexEffects(GolemStones, subtype); !emitted.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", call, emitted.Error()));
    }
    pass.actor.alive = false;
    return {};
}

// 0x4416aa..0x441a02, with the light (1, 1, 1) and the angles of 0x1a4: the point (5, 10, 5) of the left hand
// (0x4010d0), the 0x6ec/3 fanta of 3.4f with it as owner and the 0x5f2/21 of 2.5f; the right hand (0x4010b0), the
// 0x6ec/4 of 3.4f with owner and the 0x5f2/21 of 2.5f; the point (−20, −2, −10) of the eye, the 0x6ec/0 and the
// 0x5f2/21 of 0.8f without owner, and the back, another 0x5f2/21 of 0.8f. With the table full, none creates anything or
// takes rand().
Result SmokeTheHandsEyeAndBackOf1a4(MonsterHelperPass& pass)
{
    constexpr glm::vec3 White{1.0f, 1.0f, 1.0f};
    constexpr std::int32_t Fanta = 0x6ec, Smoke = 0x5f2, SmokeSubtype = 21;
    constexpr float HandFanta = std::bit_cast<float>(0x4059999au); // 3.4f
    constexpr float HandSmoke = 2.5f;                              // 0x40200000
    constexpr float Small = std::bit_cast<float>(0x3f4ccccdu);     // 0.8f
    const glm::vec3& angles = pass.actor.angles;
    const auto left = PointOf(pass, "Monster82_LHand", 0x441713, {5.0f, 10.0f, 5.0f});
    if (!left.HasValue())
        return Core::Unexpected(left.Error());
    if (auto made = OwnedParticle(pass, Fanta, 3, left.Value(), angles, White, HandFanta, 0x441774); !made.HasValue())
        return made;
    if (auto made = Particle(pass, Smoke, SmokeSubtype, left.Value(), angles, White, HandSmoke, 0x44178f);
        !made.HasValue())
        return made;
    const auto right = PointOf(pass, "Monster82_RHand", 0x4417e5);
    if (!right.HasValue())
        return Core::Unexpected(right.Error());
    if (auto made = OwnedParticle(pass, Fanta, 4, right.Value(), angles, White, HandFanta, 0x441846); !made.HasValue())
        return made;
    if (auto made = Particle(pass, Smoke, SmokeSubtype, right.Value(), angles, White, HandSmoke, 0x441861);
        !made.HasValue())
        return made;
    const auto eye = PointOf(pass, "Monster82_Eye", 0x4418d0, {-20.0f, -2.0f, -10.0f});
    if (!eye.HasValue())
        return Core::Unexpected(eye.Error());
    if (auto made = Particle(pass, Fanta, 0, eye.Value(), angles, White, Small, 0x441930); !made.HasValue())
        return made;
    if (auto made = Particle(pass, Smoke, SmokeSubtype, eye.Value(), angles, White, Small, 0x44194b); !made.HasValue())
        return made;
    const auto back = PointOf(pass, "Monster82_Back", 0x4419a1);
    if (!back.HasValue())
        return Core::Unexpected(back.Error());
    return Particle(pass, Smoke, SmokeSubtype, back.Value(), angles, White, Small, 0x441a02);
}

// 0x441b33..0x441b7c and 0x441c0c..0x441c4c: the 0x636/2 crater (Effects::ModelEffects::CreateCraterEffect), the
// 0x61c/6 joint without owner (width 200, key −1) and the 0x5de/3 column (Effects::ModelEffects::CreateFlameColumn),
// without owner and with the key at −1. With the table full, each does nothing. Only in an original frame: between
// frames the character is a copy and the tables are the world's.
Result StrikeAt(MonsterHelperPass& pass, glm::vec3 point, glm::vec3 angles, const glm::vec3& columnAngles,
                const glm::vec3& columnLight, std::uint32_t joint)
{
    constexpr std::int32_t CraterSubtype = 2, LaserJoint = 0x61c, LaserSubtype = 6;
    constexpr float LaserWidth = 200.0f; // 0x43480000
    const glm::vec3 white(1.0f);         // [ebp−0x3c], 0x44166f
    if (!pass.originalFrame)
        return {};
    static_cast<void>(pass.world.Map().effects.CreateCraterEffect(
        {.type = 0x636, .position = point, .angles = angles, .light = white, .subtype = CraterSubtype}));
    const Kernel::JointRequest request{.type = LaserJoint,
                                       .firstPosition = point,
                                       .secondPosition = point,
                                       .angles = angles,
                                       .subtype = LaserSubtype,
                                       .ownerContext = nullptr,
                                       .scale = LaserWidth,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    if (auto made = Joints::CreateFaunaJoint(pass.world.DrawTail().joints, request, pass.random); !made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", joint, made.Error()));
    static_cast<void>(pass.world.Map().effects.CreateFlameColumn({.type = Effects::FlameJetEffect,
                                                                  .position = point,
                                                                  .angles = columnAngles,
                                                                  .light = columnLight,
                                                                  .subtype = Effects::FlameColumnSubtype}));
    return {};
}

// 0x441a8a..0x441c6e, the blow of 0x1a4's action 3: at the right hand (Monster82_RHand) with the character's z, the
// crater, the joint and the column with the angles (−90, 0, the character's z) and, the column, with the character's
// angles and light; then, on each of the first 400 players (+0x88 at 1, the 0x20a) seen and alive more than 200 and
// less than 1100 away on the plane (0x5fd0c0, on the x87) from that point, the same at their position, the column with
// their angles and the light in white.
Result StrikeTheGroundOf1a4(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const auto right = PointOf(pass, "Monster82_RHand", 0x441ac3);
    if (!right.HasValue())
        return Core::Unexpected(right.Error());
    const glm::vec3 point{right.Value().x, right.Value().y, actor.position.z};
    constexpr float Down = -90.0f; // 0xc2b40000
    const glm::vec3 angles{Down, 0.0f, actor.angles.z};
    if (auto struck = StrikeAt(pass, point, angles, actor.angles, actor.light, 0x441b57); !struck.HasValue())
        return struck;
    constexpr std::uint8_t PlayerClass = 1;
    constexpr double Near = 200.0, Far = 1100.0; // 0x6b8928 and 0x6b8bdc
    // 0x441c58..0x441c68: it stops at 0x69780 = 400 · 0x438; record 400 (the spare one of 0x5110d0) is not walked.
    constexpr std::size_t SearchedPlayers = 400;
    const glm::vec3 white(1.0f);
    for (const Kernel::ActorState& other : pass.world.Actors().first<SearchedPlayers>())
    {
        if (other.byte88 != PlayerClass || other.type != Tables::PlayerSkeletonModel || !other.seen || !other.alive)
            continue;
        const double dx = static_cast<double>(other.position.x) - static_cast<double>(point.x);
        const double dy = static_cast<double>(other.position.y) - static_cast<double>(point.y);
        const double distance = std::sqrt((dy * dy) + (dx * dx));
        if (!(distance > Near) || !(distance < Far))
            continue;
        if (auto struck = StrikeAt(pass, other.position, angles, other.angles, white, 0x441c2a); !struck.HasValue())
            return struck;
    }
    return {};
}

// 0x441669..0x441d91: in action 6, 0x4ee770(model, 0, 0x13c, 0, 0, 0) (DieInStones: one rand() per vertex and the 0x13c
// stones), +0 at 0 and sound 0x209; otherwise, the fantas and smokes of the hands, the eye and the back
// (SmokeTheHandsEyeAndBackOf1a4); then rand() % 20 at 0 requests 0x205 plus rand() % 2 without looking at +4, and with
// +4 at 0, action 3 with the frame from 8.4 to 10.2 cuts in its effects; action 4 from 5 to 5.7 sets +4 = 1, creates
// the 0x51fe10 burst from LHand + (80, 40, −25) and requests 0x208 even with the pool full.
Result DrawThe1a4(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    if (actor.animation.action == 6)
    {
        constexpr std::int32_t Stones = 0, DeathSound = 0x209;
        if (auto died = DieInStones(pass, Stones, 0x441696); !died.HasValue())
            return died;
        Sound(pass, Audio::SoundId{DeathSound});
        // 0x441a34 → 0x441a3c: the branches of actions 3 and 4 do not run.
        return {};
    }
    if (auto smoked = SmokeTheHandsEyeAndBackOf1a4(pass); !smoked.HasValue())
        return smoked;
    // 0x441a0a..0x441a34.
    if (pass.random.Next() % 20 == 0)
        Sound(pass, Audio::SoundId{0x205 + Coin(pass.random)});
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    // 0x441a3c..0x441a8a: 8.4 (0x6b8be4) and 10.2 (0x6b8be0).
    if (action == 3 && frame >= 8.4f && frame <= 10.2f && actor.subtype == 0)
    {
        actor.subtype = 1;
        if (auto struck = StrikeTheGroundOf1a4(pass); !struck.HasValue())
            return struck;
        Sound(pass, Audio::SoundId{0x207}); // 0x441c75
    }
    // 0x441c7d..0x441cd4: 5 (0x6b87f8) and 5.7 (0x6b8bd8).
    if (action == 4 && frame >= 5.0f && frame <= 5.7f && actor.subtype == 0)
    {
        actor.subtype = 1;
        const auto point = PointOf(pass, "Monster82_LHand", 0x441d2f, {80.0f, 40.0f, -25.0f});
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const auto burst =
            Particles::CreateParticleBurst(*pass.particles, point.Value(), pass.random, true, pass.worldClock);
        if (!burst.HasValue())
            return Core::Unexpected(std::format("0x441d7d: falla la ráfaga del tipo 0x1a4 con el error {}",
                                                static_cast<unsigned>(burst.Error())));
        Sound(pass, Audio::SoundId{0x208});
    }
    return {};
}

// 0x441d96..0x4420f2: with +4 at 0, 0x200 plus rand() % 2 in actions 2 and 10, 0x202 plus rand() % 2 in 3 and 4, and
// 0x1f4 in 6. In action 4, three rand() % 12 − 6 (x, y, z) as the offset of the tail's point, a 0x5fd at 2.5 additive
// and a 0x61e at 0.8 with its own light (+0xf4) and two 0x5ca/0 particles with (1, 0.3, 0); then twelve rounds of
// rand() % 40 − 20, rand() % 40 − 20 and rand() % 48 − 24 for the belly's point, with the same two sprites and a
// 0x5ca/0.
Result DrawThe1a5(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
        SoundOnce(pass, 0x200, true);
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x202, true);
    if (action == 6)
        SoundOnce(pass, 0x1f4, false);
    if (action != 4)
        return {};
    if (auto ready = NeedTheSprites(pass, 0x441f4b); !ready.HasValue())
        return ready;
    glm::vec3 offset{};
    for (int axis = 0; axis < 3; ++axis)
        offset[axis] = static_cast<float>(pass.random.Next() % 12 - 6);
    const auto point = PointOf(pass, "Monster83_Tail", 0x441ed5, offset);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    Sprite(pass, Shine, point.Value(), 2.5f, actor.light, 0.0f, 1);
    Sprite(pass, 0x61e, point.Value(), 0.8f, actor.light);
    // [ebp−0x30] (0x441f36): 1, 0.3 (0x3e99999a) and 0.
    constexpr glm::vec3 ThunderLight{1.0f, 0.3f, 0.0f};
    for (const std::uint32_t call : {0x441f7fu, 0x441f9cu})
    {
        if (auto made = Particle(pass, Thunder, 0, point.Value(), actor.angles, ThunderLight, 1.0f, call);
            !made.HasValue())
            return made;
    }
    // 0x441fa4..0x4420ec.
    constexpr int Turns = 12;
    for (int turn = 0; turn < Turns; ++turn)
    {
        glm::vec3 belly{};
        belly.x = static_cast<float>(pass.random.Next() % 40 - 20);
        belly.y = static_cast<float>(pass.random.Next() % 40 - 20);
        belly.z = static_cast<float>(pass.random.Next() % 48 - 24);
        const auto glow = PointOf(pass, "Monster83_Belly", 0x442048, belly);
        if (!glow.HasValue())
            return Core::Unexpected(glow.Error());
        Sprite(pass, Shine, glow.Value(), 2.5f, actor.light, 0.0f, 1);
        Sprite(pass, 0x61e, glow.Value(), 0.8f, actor.light);
        if (auto made = Particle(pass, Thunder, 0, glow.Value(), actor.angles, ThunderLight, 1.0f, 0x4420dd);
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x4420f7..0x44281f. timeGetTime() (0x4420f7) without using what it returns.
Result DrawThe1a6(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
        SoundOnce(pass, 0x1fa, true);
    if (action == 6)
    {
        // 0x442159..0x442179: 0x4ee770(model, 0, 0x13c, 1, 0, 0) (the small stones 0x12d/2 and 0x12e/2), +0 at 0 and
        // sound 0x1ff; it goes on at 0x44217c, with the poisons.
        constexpr std::int32_t Shower = 1, DeathSound = 0x1ff;
        if (auto died = DieInStones(pass, Shower, 0x442165); !died.HasValue())
            return died;
        Sound(pass, Audio::SoundId{DeathSound});
    }
    // [ebp−0x18] (0x442107): the particles' light, (0.4, 1, 0.7).
    constexpr glm::vec3 PoisonLight{0.4f, 1.0f, 0.7f};
    if (action == 3)
    {
        // 0x44217c: with +4 at 0, +4 = 1 and 0x1fe; the stone's angles (−55, FSIN(T · 0.03f) · 45, the object's z) in
        // [ebp−0x30] (0x4421a0..0x4421c1); rand() % 2 at 0 picks with another rand() % 2 the right poison (0) or the
        // left one, its point in [ebp−0x24], and rand() % 32 + 50 times 0.025f (0x6b8bd0) is the scale of its 0x5f2/11;
        // both paths go on to the 0x12e/3 stone (0x442397) at [ebp−0x24].
        SoundOnce(pass, 0x1fe, false);
        constexpr float StonePitch = -55.0f;                           // 0x4421af, 0xc25c0000
        constexpr float StoneRate = std::bit_cast<float>(0x3cf5c28fu); // 0x6b8bd4, 0.03f
        constexpr float StoneSwing = 45.0f;                            // 0x6b8848
        const auto swing = ClockSine(pass, StoneRate, StoneSwing, 0x4421b9);
        if (!swing.HasValue())
            return Core::Unexpected(swing.Error());
        const glm::vec3 stoneAngles{StonePitch, F32(swing.Value()), actor.angles.z};
        std::optional<glm::vec3> stonePoint;
        if (Coin(pass.random) == 0)
        {
            const bool right = Coin(pass.random) == 0;
            const auto point = right ? PointOf(pass, "Monster84_PoisonRight", 0x442240)
                                     : PointOf(pass, "Monster84_PoisonLeft", 0x4422f0);
            if (!point.HasValue())
                return Core::Unexpected(point.Error());
            const float scale = F32(static_cast<double>(pass.random.Next() % 32 + 50) * D(0.025f));
            if (auto made =
                    Particle(pass, Breath, StoneDust, point.Value(), actor.angles, PoisonLight, scale, 0x442370);
                !made.HasValue())
                return made;
            stonePoint = point.Value();
        }
        // With the first rand() % 2 at 1, [ebp−0x24] was not written in this call: it holds what was left on the stack
        // (Q-C1580).
        if (!stonePoint)
            return Cut(0x442397, "la piedra 0x12e/3 en un punto que esta llamada no escribió (Q-C1580)", actor);
        // 0x518cf0(0x12e, point, angles, the poison's light, 3, no owner, −1, 0, 0, 0, 0). Only in an original frame:
        // between frames the effect table is still the world's.
        constexpr std::int16_t ThrownStone = 0x12e;
        constexpr std::int32_t ThrownSubtype = 3;
        if (pass.originalFrame)
            static_cast<void>(CastleSiege::CreateSiegeVertexEffect({.type = ThrownStone,
                                                                    .position = *stonePoint,
                                                                    .angles = stoneAngles,
                                                                    .light = PoisonLight,
                                                                    .subtype = ThrownSubtype},
                                                                   pass.world.Map().effects, pass.random));
    }
    // 0x44239f..0x4425b8: in action 4 with the frame from 3.5 to 4.2 (0x6b8bcc), with +4 at 0, +4 = 1 and 0x1fc plus
    // rand() % 2; then, at each hand, the point with the object's z (0x44249d and 0x442579) and rand() % 32 + 50 times
    // 0.05f (0x6b8a48) for its 0x5f2/11. It goes on with the poisons.
    const float frame = actor.animation.frame;
    if (action == 4 && frame >= 3.5f && frame <= 4.2f)
    {
        SoundOnce(pass, 0x1fc, true);
        struct Hand
        {
            std::string_view bone;
            std::uint32_t call;
            std::uint32_t particle;
        };
        constexpr std::array<Hand, 2> Hands{
            {{"Monster84_RightHand", 0x442451, 0x4424d7}, {"Monster84_LeftHand", 0x44252d, 0x4425b3}}};
        for (const Hand& hand : Hands)
        {
            auto point = PointOf(pass, hand.bone, hand.call);
            if (!point.HasValue())
                return Core::Unexpected(point.Error());
            glm::vec3 floor = point.Value();
            floor.z = actor.position.z;
            const float scale = F32(static_cast<double>(pass.random.Next() % 32 + 50) * D(0.05f));
            if (auto made = Particle(pass, Breath, StoneDust, floor, actor.angles, PoisonLight, scale, hand.particle);
                !made.HasValue())
                return made;
        }
    }
    // 0x4425bb..0x442817: three rand() % 10, each at 0 with its 0x5f2/23 poison (Particles::WarmBreathSubtype) at the
    // bone's point, with the object's angles (+0x1c), the light of 0x442107, scale 1 and no owner.
    struct Poison
    {
        std::string_view bone;
        std::uint32_t call;
        std::uint32_t particle;
    };
    constexpr std::array<Poison, 3> Poisons{{{"Monster84_PoisonTop", 0x44261e, 0x44267f},
                                             {"Monster84_PoisonRight", 0x4426ea, 0x44274b},
                                             {"Monster84_PoisonLeft", 0x4427b6, 0x442817}}};
    for (const Poison& poison : Poisons)
    {
        if (pass.random.Next() % 10 != 0)
            continue;
        const auto point = PointOf(pass, poison.bone, poison.call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto made = Particle(pass, Breath, Particles::WarmBreathSubtype, point.Value(), actor.angles, PoisonLight,
                                 1.0f, poison.particle);
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x442824..0x442a5d: a 0x5ac at 0.7 on each eye, the left one first; in actions 2 and 10, rand() % 10 at 0 gives the
// 0x5f3/0 particle at the position, with the object's angles and the eyes' light, and sound 0x1f0 plus rand() % 2
// without looking at +4; with +4 at 0, 0x1f2 plus rand() % 2 in 3 and 4, and 0x1f4 in 6.
Result DrawThe1a7(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4428e6); !ready.HasValue())
        return ready;
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster85_LeftEye", 0x442889}, {"Monster85_RightEye", 0x44293c}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.7f, RedEyes);
    }
    const Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor) && pass.random.Next() % 10 == 0)
    {
        if (auto made = Particle(pass, Dust, 0, actor.position, actor.angles, RedEyes, 1.0f, 0x4429d8);
            !made.HasValue())
            return made;
        Sound(pass, Audio::SoundId{0x1f0 + Coin(pass.random)});
    }
    if (action == 3 || action == 4)
        SoundOnce(pass, 0x1f2, true);
    if (action == 6)
        SoundOnce(pass, 0x1f4, false);
    return {};
}

// 0x442a62..0x442dc1: two 0x5ac at 0.5 at the point (0, −2, 0) of each eye, the left one first; with +4 at 0, 0x20b
// plus rand() % 2 in action 3 and 0x20e in 6; in 4, with +4 at 0, +4 = 1, effect 0x13f (MeteorEffects) and 0x20d; then
// the point (0, 0, −10) of the hand, the 0x6ec/3 fanta with it as owner and the 0x5f2/21 of 2, both with the eyes'
// light.
Result DrawThe1a9(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x442b3d); !ready.HasValue())
        return ready;
    Kernel::ActorState& actor = pass.actor;
    constexpr glm::vec3 EyeOffset{0.0f, -2.0f, 0.0f};
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Eyes{
        {{"Monster87_LeftEye", 0x442ae0}, {"Monster87_RightEye", 0x442bb1}}};
    for (const auto& [eye, call] : Eyes)
    {
        const auto point = PointOf(pass, eye, call, EyeOffset);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
        Sprite(pass, Flare, point.Value(), 0.5f, RedEyes);
    }
    const std::uint8_t action = actor.animation.action;
    if (action == 3)
        SoundOnce(pass, 0x20b, true);
    if (action == 6)
        SoundOnce(pass, 0x20e, false);
    if (action != 4)
        return {};
    if (actor.subtype == 0)
    {
        actor.subtype = 1;
        // 0x442c9a..0x442cb9: 0x518cf0(0x13f, c+0x10, c+0x1c, c+0xf4, 0, c, −1, 0, 0, 0, 0), without rand() (full,
        // nothing); 0x442cc5 sounds even then. Only in an original frame, like its sounds: between frames the monster
        // is a copy and the effect table is the world's.
        if (pass.originalFrame)
        {
            if (auto made = Effects::CreateHighModelEffect(
                    {.type = Effects::MeteorCallEffect,
                     .position = actor.position,
                     .angles = actor.angles,
                     .light = actor.light,
                     .owner = &actor},
                    {.world = pass.world, .random = pass.random, .worldClock = pass.worldClock});
                !made.HasValue())
                return Core::Unexpected(std::format("0x442cb9: {}", made.Error()));
        }
        Sound(pass, Audio::SoundId{0x20d});
    }
    // 0x442d4c..0x442dbc: the 0x6ec/3 fanta of 3 with it as owner and the 0x5f2/21 of 2, with the eyes' light.
    const auto hand = PointOf(pass, "Monster87_LeftHand", 0x442d4c, {0.0f, 0.0f, -10.0f});
    if (!hand.HasValue())
        return Core::Unexpected(hand.Error());
    if (auto made = OwnedParticle(pass, 0x6ec, 3, hand.Value(), actor.angles, RedEyes, 3.0f, 0x442da1);
        !made.HasValue())
        return made;
    return Particle(pass, 0x5f2, 21, hand.Value(), actor.angles, RedEyes, 2.0f, 0x442dbc);
}

// 0x5fb2f0 with the light or the turn being the unwritten stack (Q-C1100): MuRet does not draw that decal, which takes
// no rand() and writes no state; it only binds its texture (0x5fb351, 0x620780), the only thing it does before reading
// them.
void BindTheMark(Render::ModelBatch& batch)
{
    Render::ModelDrawState state = batch.State();
    state.texture = Render::TextureSlot{CrywolfMark};
    state.rawTextureName.reset();
    batch.SetState(state);
}

// 0x4341d2..0x4343a2, from 0x204 to 0x208. The clock's sine times 0.002f (0x6b89bc) gives b = sin · 0.1f +
// 0.28f ([ebp+0xc]) and g = sin · 0.04f + 0.2f ([ebp−0x20], the first mark's turn); then 0x620a90 (additive). +0x7c has
// at most one of the bits (0x4e6380 writes it with MOV), but the code looks at each one:
// - with 0x40000 (0x43421a..0x43429f), the marks' light is (g, g, 0.05f), the first one's turn T · 0.01f (0x6b88d4) and
//   the second one's T · −0.01f (0x6b8a58, [ebp−0x1c]); its own light (+0xf4) becomes (0.09, 0.09, 0.04) and two 0x729
//   particles come out, the /0 and the /1, at the position, with the object's angles and that light, of scale 1;
// - with 0x100000 or 0x20000, the light is (b, 0.1, 0.1); with 0x10000 or 0x80000 (after, it overwrites), (0.15, 0.15,
//   b) and the first one's turn T · 0.01f.
// The two 0x69b marks are 0x5fb2f0(0x69b, x, y, side, side, &light, turn, 1, 5) with the light doubled (FADD ST0, ST0,
// on the same stack): the first of 2.8 (0x40333333) and the second of 3.6 (0x40666666). Without any bit the light is
// the unwritten stack, and without 0x40000 so is the second one's turn: that mark is not drawn (BindTheMark). 0x620980
// closes.
Result DrawTheCrywolfMark(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    if (pass.batch == nullptr || pass.heights == nullptr)
        return Cut(0x43436c, "las marcas 0x69b del piso sin el lote o las alturas del terreno (0x5fb2f0)", actor);
    constexpr float WaveRate = std::bit_cast<float>(0x3b03126fu);   // 0x6b89bc: 0.002
    using Math::Tenth;                                              // 0x6b87b8: 0.1
    constexpr float BrightBase = std::bit_cast<float>(0x3e8f5c29u); // 0x6b8af0: 0.28
    constexpr float SoftSwing = std::bit_cast<float>(0x3d23d70au);  // 0x6b8aec: 0.04
    constexpr float SoftBase = std::bit_cast<float>(0x3e4ccccdu);   // 0x6b8854: 0.2
    constexpr float SpinRate = std::bit_cast<float>(0x3c23d70au);   // 0x6b88d4: 0.01
    constexpr float BackRate = std::bit_cast<float>(0xbc23d70au);   // 0x6b8a58: −0.01
    constexpr float Dim = std::bit_cast<float>(0x3d4ccccdu);        // 0x434260: 0.05
    constexpr float Pale = std::bit_cast<float>(0x3e19999au);       // 0x6b8ae8 and 0x43431b: 0.15
    constexpr float Small = std::bit_cast<float>(0x40333333u);      // 2.8
    constexpr float Large = std::bit_cast<float>(0x40666666u);      // 3.6
    constexpr float Lift = 5.0f;
    const double argument = D(pass.worldClock) * D(WaveRate);
    const auto bright = Math::Detail::SineProductPC53(argument, Tenth);
    const auto soft = Math::Detail::SineProductPC53(argument, SoftSwing);
    if (!bright || !soft)
        return Cut(0x4341de, "el seno del reloj fuera del perfil PC53", actor);
    const float b = F32(*bright + D(BrightBase));
    const float g = F32(*soft + D(SoftBase));
    pass.batch->SetState(Blended(pass.batch->State(), Render::ModelBlend::Additive));
    const std::uint32_t flags = actor.flags7c;
    const auto has = [flags](std::uint32_t bit) { return (flags & bit) == bit; };
    // [ebp−0x18], [ebp−0x14] and [ebp−0x10] (the light), [ebp−0x20] (the first one's turn) and [ebp−0x1c] (the
    // second's).
    std::optional<glm::vec3> light;
    float spin = g;
    std::optional<float> backSpin;
    if (has(0x40000))
    {
        light = glm::vec3{g, g, Dim};
        spin = F32(D(pass.worldClock) * D(SpinRate));
        backSpin = F32(D(pass.worldClock) * D(BackRate));
        actor.light = {std::bit_cast<float>(0x3db851ecu), std::bit_cast<float>(0x3db851ecu),
                       std::bit_cast<float>(0x3d23d70au)};
        for (const auto& [subtype, call] : {std::pair{0, 0x434281u}, std::pair{1, 0x43429au}})
        {
            if (auto made = Particle(pass, ChaseLight, subtype, actor.position, actor.angles, actor.light, 1.0f, call);
                !made.HasValue())
                return made;
        }
    }
    // 0x4342a2..0x43432b.
    if (has(0x100000) || has(0x20000))
        light = glm::vec3{b, Tenth, Tenth};
    if (has(0x10000) || has(0x80000))
    {
        light = glm::vec3{Pale, Pale, b};
        spin = F32(D(pass.worldClock) * D(SpinRate));
    }
    Terrain::TerrainDecal mark{.slot = CrywolfMark,
                               .x = actor.position.x,
                               .y = actor.position.y,
                               .width = Small,
                               .height = Small,
                               .color = light ? *light + *light : glm::vec3(0.0f),
                               .angle = spin,
                               .alpha = 1.0f,
                               .lift = Lift};
    // 0x43436c and 0x43439a.
    for (const std::optional<float> turn : {std::optional(spin), backSpin})
    {
        if (light && turn)
        {
            mark.angle = *turn;
            if (auto drawn = Terrain::DrawTerrainDecal(*pass.batch, *pass.heights, mark); !drawn.HasValue())
                return drawn;
        }
        else
            BindTheMark(*pass.batch);
        mark.width = Large;
        mark.height = Large;
    }
    // 0x4343a2: 0x620980.
    pass.batch->SetState(Blended(pass.batch->State(), Render::ModelBlend::Opaque));
    return {};
}

// 0x434699..0x434a70, type 0x1af: a = rand() % 14 · 0.01f, the turn rand() % 360; at zx a white 0x699 at 1.5, three
// 0x5ac at a · 5 + 3.5 and a turned 0x69a at a + 0.8, blue; at zx01 a turned 0x5fd at 0.8 (0.1, 0, 0.6) and, with
// rand() % 2 at 0, another one (0.7, 0.7, 1) with 360 minus the turn; and a blue 0x5ac at 1.5.
Result DrawThe1af(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4347ea); !ready.HasValue())
        return ready;
    const float a = F32(static_cast<double>(pass.random.Next() % 14) * D(0.01f));
    const auto spin = static_cast<float>(pass.random.Next() % 360);
    const auto zx = PointOf(pass, "Monster94_zx", 0x434799);
    if (!zx.HasValue())
        return Core::Unexpected(zx.Error());
    Sprite(pass, 0x699, zx.Value(), 1.5f, glm::vec3(1.0f));
    const float size = F32(D(a) * 5.0 + 3.5);
    for (int flare = 0; flare < 3; ++flare)
        Sprite(pass, Flare, zx.Value(), size, BlueGlow);
    Sprite(pass, 0x69a, zx.Value(), F32(D(a) + D(0.8f)), BlueGlow, spin);
    const auto zx01 = PointOf(pass, "Monster94_zx01", 0x434970);
    if (!zx01.HasValue())
        return Core::Unexpected(zx01.Error());
    Sprite(pass, Shine, zx01.Value(), 0.8f, {0.1f, 0.0f, 0.6f}, spin);
    if (Coin(pass.random) == 0)
        Sprite(pass, Shine, zx01.Value(), 0.8f, {0.7f, 0.7f, 1.0f}, F32(360.0 - D(spin)));
    Sprite(pass, Flare, zx01.Value(), 1.5f, BlueGlow);
    return {};
}

// 0x434bf3..0x434d89, type 0x1b1: outside action 6, a red 0x5ac at 3.5 on the head; in actions 2 and 10, rand() % 10 at
// 0 gives the 0x5f3/0 particle (0x434d84) at the position, with the object's angles and the eyes' light, of scale 1.
Result DrawThe1b1(MonsterHelperPass& pass)
{
    if (pass.actor.animation.action != 6)
    {
        if (auto ready = NeedTheSprites(pass, 0x434d2b); !ready.HasValue())
            return ready;
        const auto head = PointOf(pass, "Monster95_Head", 0x434cd8);
        if (!head.HasValue())
            return Core::Unexpected(head.Error());
        Sprite(pass, Flare, head.Value(), 3.5f, RedEyes);
    }
    const Kernel::ActorState& actor = pass.actor;
    if (Idling(actor) && pass.random.Next() % 10 == 0)
        return Particle(pass, Dust, 0, actor.position, actor.angles, RedEyes, 1.0f, 0x434d84);
    return {};
}

// 0x434f8a..0x4353fb, type 0x1b2: the turn rand() % 360 and the light sin(T · 0.0012f) · 0.8f + 1.3f
// (0x434faf..0x434fc9, the kept sine of ADR0014); a 0x5ac of the size (1, or 0.5 in actions 3 and 4) at the center,
// with (0, light · 0.5, light · 0.1); above and below, two 0x5fd of half the size with (0.5, 0.5, 0.5), turned and with
// 360 minus the turn.
Result DrawThe1b2(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x435119); !ready.HasValue())
        return ready;
    const Kernel::ActorState& actor = pass.actor;
    const auto spin = static_cast<float>(pass.random.Next() % 360);
    const auto wave = ClockSine(pass, 0.0012f, 0.8f, 0x434fbb);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const float lum = F32(wave.Value() + D(1.3f));
    const std::uint8_t action = actor.animation.action;
    const float size = action == 3 || action == 4 ? 0.5f : 1.0f;
    const auto center = PointOf(pass, "Monster96_Center", 0x43509b);
    if (!center.HasValue())
        return Core::Unexpected(center.Error());
    Sprite(pass, Flare, center.Value(), size, {0.0f, F32(D(lum) * 0.5), F32(D(lum) * D(0.1f))});
    const float half = F32(D(size) * 0.5);
    const float back = F32(360.0 - D(spin));
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> Ends{
        {{"Monster96_Top", 0x4351ff}, {"Monster96_Bottom", 0x435383}}};
    for (const auto& [end, call] : Ends)
    {
        const auto point = PointOf(pass, end, call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Shine, point.Value(), half, glm::vec3(0.5f), spin);
        Sprite(pass, Shine, point.Value(), half, glm::vec3(0.5f), back);
    }
    return {};
}

// 0x4343ba..0x435569, through the byte table 0x43559c over the jump table 0x43556c.
Handled DrawTheCrywolfCase(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    const std::uint8_t action = actor.animation.action;
    if (Within(type, 0x204, 0x208))
    {
        // 0x4343a9: returns 0 and the chain goes on.
        if (auto drawn = DrawTheCrywolfMark(pass); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return false;
    }
    if (type == 0x1b5)
    {
        // 0x4345a8..0x434600: without actions 2 and 10; in 3 and 6 it leaves without touching +4, and in the others it
        // sets it to 0.
        if (action == 3 || action == 4)
            SoundOnce(pass, 0x241, false);
        else if (action == 6)
            SoundOnce(pass, 0x242, false);
        if (action != 3 && action != 6)
            actor.subtype = 0;
        return false;
    }
    if (type == 0x1ab || type == 0x1ae)
    {
        // 0x4343ba..0x434578: both with actions 8 and 9, and they return 0.
        const std::int32_t base = type == 0x1ab ? 0x22e : 0x235;
        const std::array<ActionSound, 5> sounds{
            {{3, base + 2}, {4, base + 3}, {8, base + 5}, {9, base + 6}, {6, base + 4}}};
        PlayTheSounds(pass, base, sounds);
        ResetAtRest(actor);
        return false;
    }
    struct Voice
    {
        std::uint16_t type;
        std::int32_t idle;
    };
    // The sound of actions 2 and 10; those of 3, 4 and 6 are the second, third and fourth after it.
    constexpr std::array<Voice, 7> Voices{{{0x1ad, 0x229},
                                           {0x1af, 0x23c},
                                           {0x1b0, 0x224},
                                           {0x1b1, 0x210},
                                           {0x1b2, 0x21f},
                                           {0x1b3, 0x21a},
                                           {0x1b4, 0x215}}};
    const auto voice = std::ranges::find_if(Voices, [type](const Voice& entry) { return entry.type == type; });
    if (voice == Voices.end())
        return false; // 0x434595: the others from 0x1ab to 0x208, without touching +4.
    Result drawn{};
    if (type == 0x1b1)
        drawn = DrawThe1b1(pass);
    else if (type == 0x1b2)
        drawn = DrawThe1b2(pass);
    else if (type == 0x1b3 && action == 4 && actor.animation.frame >= 4.0f && actor.animation.frame <= 6.0f &&
             pass.originalFrame)
        // 0x434e78..0x434e9a turns +0xcc in a dead local; 0x434eb0 passes the actor's position to the constructor.
        // Between original frames, nothing: the character is a copy and the table is the world's.
        static_cast<void>(pass.world.Map().effects.CreatePiercingEffect(
            {.type = 0x115, .position = actor.position, .angles = actor.angles, .light = {1, 0, 0.5f}, .subtype = 1}));
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    const std::array<ActionSound, 3> sounds{{{3, voice->idle + 2}, {4, voice->idle + 3}, {6, voice->idle + 4}}};
    PlayTheSounds(pass, voice->idle, sounds);
    ResetAtRest(actor);
    // 0x1af: its sprites go after the sounds (0x434699).
    if (type == 0x1af)
    {
        if (auto glowing = DrawThe1af(pass); !glowing.HasValue())
            return Core::Unexpected(glowing.Error());
    }
    return true;
}

// The character's +0x3a4 target as a signed WORD within the pool's 0x190 (0x4279cf..0x4279e3 and 0x427fb5..0x427fcd).
bool HasTarget(const Kernel::ActorState& actor)
{
    const auto target = static_cast<std::int16_t>(actor.attackTarget);
    return target >= 0 && target < PoolSize;
}

// 0x427764..0x427a4d, the foot and target particles of 0x1b6 in actions 3 and 4: from frame 4 (0x6b8a44, C0 also when
// unordered), at the point (70, 0, 0) of the foot, the 0x5f2/0x1b of 2 with the light (0.5, 0.2f, 0.5) and the 0x5ad/2
// of 1 with the light 1; in action 3 from frame 6 (0x6b87f4), at another point (70, 0, 0) of the foot, the 0x5ad/3 of
// 1.3f with the angles (0, 0, 0) and, with the +0x3a4 target in the pool, the 0x5ad/3 of 1.3f 100 above the target,
// both with the light 1. The others' angles are [EBP−0x60], which 0x4271d0 does not write: they are born without speed
// and nobody reads them (0x4e98d0 turns the zero speed), and MuRet passes (0, 0, 0).
Result SparkTheFootAndTheTargetOf1b6(MonsterHelperPass& pass)
{
    const Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (action != 3 && action != 4)
        return {};
    constexpr glm::vec3 Unwritten{};
    constexpr glm::vec3 White{1.0f, 1.0f, 1.0f};
    constexpr glm::vec3 StepOffset{70.0f, 0.0f, 0.0f};         // 0x428c0000
    constexpr float Flash = std::bit_cast<float>(0x3fa66666u); // 1.3f
    const float frame = actor.animation.frame;
    if (frame >= 4.0f)
    {
        const auto step = PointOf(pass, "Monster100_Footstepst", 0x4277f7, StepOffset);
        if (!step.HasValue())
            return Core::Unexpected(step.Error());
        constexpr glm::vec3 DustLight{0.5f, std::bit_cast<float>(0x3e4ccccdu), 0.5f};
        if (auto made = Particle(pass, 0x5f2, 0x1b, step.Value(), Unwritten, DustLight, 2.0f, 0x42786d);
            !made.HasValue())
            return made;
        if (auto made = Particle(pass, Spark, 2, step.Value(), Unwritten, White, 1.0f, 0x4278a0); !made.HasValue())
            return made;
    }
    if (frame >= 6.0f && action == 3)
    {
        const auto step = PointOf(pass, "Monster100_Footstepst", 0x42794b, StepOffset);
        if (!step.HasValue())
            return Core::Unexpected(step.Error());
        if (auto made = Particle(pass, Spark, 3, step.Value(), {}, White, Flash, 0x4279c4); !made.HasValue())
            return made;
        if (HasTarget(actor))
        {
            const Kernel::ActorState& target = pass.world.Actors()[static_cast<std::size_t>(actor.attackTarget)];
            const glm::vec3 above{target.position.x, target.position.y, F32(D(target.position.z) + 100.0)};
            if (auto made = Particle(pass, Spark, 3, above, Unwritten, White, Flash, 0x427a4d); !made.HasValue())
                return made;
        }
    }
    return {};
}

// 0x42720d..0x427a92, type 0x1b6: r = rand() % 30, v = r · 0.01f + 0.6f; the light (v + 0.5, v − 0.05f, v + 0.5) for
// five 0x5ad (the left hand at 0.8, z02 at 1.1, z03 at 1, z04 at 1.7 and z05 at 0.8) and, at the point (0, 0, −65) of
// the foot, the 0x5ad/4 particle of 4 with the light (0.7f, 0.5, 0.7f) and the unwritten angles (those of
// SparkTheFootAndTheTargetOf1b6). Then its sounds: in actions 2 and 10, rand() % 15 at 0 requests 0x261 plus rand() %
// 2; with +4 at 0, +4 = 1 and 0x264 in 3, 0x9c plus rand() % 2 and 0x263 in 4, and 0x265 in 6; the foot and target
// particles, and with the +0x3a4 target in the pool, in action 4 from frame 6, the 0x614/0x10 joint.
Result DrawThe1b6(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4272ee); !ready.HasValue())
        return ready;
    Kernel::ActorState& actor = pass.actor;
    const double v = static_cast<double>(pass.random.Next() % 30) * D(0.01f) + D(0.6f);
    const glm::vec3 light{F32(v + 0.5), F32(v - D(0.05f)), F32(v + 0.5)};
    struct Glow
    {
        std::string_view bone;
        std::uint32_t call;
        float scale;
    };
    constexpr std::array<Glow, 5> Glows{{{"Monster100_L_Hand", 0x427291, 0.8f},
                                         {"Monster100_z02", 0x427344, 1.1f},
                                         {"Monster100_z03", 0x4273f7, 1.0f},
                                         {"Monster100_z04", 0x4274aa, 1.7f},
                                         {"Monster100_z05", 0x42755d, 0.8f}}};
    for (const Glow& glow : Glows)
    {
        const auto point = PointOf(pass, glow.bone, glow.call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Spark, point.Value(), glow.scale, light);
    }
    const auto foot = PointOf(pass, "Monster100_Footstepst", 0x42763e, {0.0f, 0.0f, -65.0f});
    if (!foot.HasValue())
        return Core::Unexpected(foot.Error());
    constexpr glm::vec3 FootLight{std::bit_cast<float>(0x3f333333u), 0.5f, std::bit_cast<float>(0x3f333333u)};
    if (auto made = Particle(pass, Spark, 4, foot.Value(), {}, FootLight, 4.0f, 0x42769f); !made.HasValue())
        return made;
    // 0x4276a4..0x427764.
    const std::uint8_t action = actor.animation.action;
    if (Idling(actor))
    {
        if (pass.random.Next() % 15 == 0)
            Sound(pass, Audio::SoundId{0x261 + Coin(pass.random)});
    }
    else if (action == 3)
        SoundOnce(pass, 0x264, false);
    else if (action == 4)
    {
        if (actor.subtype == 0)
        {
            actor.subtype = 1;
            Sound(pass, Audio::SoundId{0x9c + Coin(pass.random)});
            Sound(pass, Audio::SoundId{0x263});
        }
    }
    else if (action == 6)
        SoundOnce(pass, 0x265, false);
    if (auto sparked = SparkTheFootAndTheTargetOf1b6(pass); !sparked.HasValue())
        return sparked;
    // 0x4279cc..0x427a8d: 6 (0x6b87f4). The 0x614/0x10 without owner from the target up to 100 (0x6b86b4) above it,
    // with the actor's angles and width 10. Only in an original frame (the table is the world's).
    if (action == 4 && HasTarget(actor) && actor.animation.frame >= 6.0f && pass.originalFrame)
    {
        const Kernel::ActorState& target = pass.world.Actors()[static_cast<std::size_t>(actor.attackTarget)];
        glm::vec3 from = target.position;
        glm::vec3 to{target.position.x, target.position.y, F32(D(target.position.z) + 100.0)};
        glm::vec3 angles = actor.angles;
        constexpr float Width = 10.0f; // 0x41200000
        const Kernel::JointRequest request{.type = 0x614,
                                           .firstPosition = from,
                                           .secondPosition = to,
                                           .angles = angles,
                                           .subtype = 0x10,
                                           .ownerContext = nullptr,
                                           .scale = Width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto made = Joints::CreateFaunaJoint(pass.world.DrawTail().joints, request, pass.random); !made.HasValue())
            return Core::Unexpected(std::format("0x427a8d: {}", made.Error()));
    }
    return {};
}

// 0x427ab0..0x428036, type 0x1b7: at the points of Monster101_L_Arm, R_Arm and Head, the 0x6f1/1 mist (0x427b8b,
// 0x427c42, 0x427cfd) with the angles (0, 0, 0), the light 1 and scale 1; its sounds (0x247, and 0x249, 0x24a and 0x24b
// in actions 3, 4 and 6) and, in 3, at the point (30, 0, 0) of the left arm with the angles (0, 45, 45): from frame 4
// (0x6b8a44) the 0x5f2/0x19 smoke of 1 with the light 1 (0x427eaf) and, from 5 to 5.5 (0x6b87f8, 0x6b8a40), at another
// identical point, the 0x6eb/3 wave of 0.5 with the light 5 (0x427fad). With the +0x3a4 target in the pool, from frame
// 6.9 (0x6b8a3c), the 0x618/0x10 joint (0x427fea..0x428036 jumps to 0x427a8d): from the target, 100 (0x6b86b4) higher,
// towards the actor, with the target's angles, the actor as owner and width 20; it goes back to it (BeamJoints.cpp).
Result DrawThe1b7(MonsterHelperPass& pass)
{
    constexpr glm::vec3 White{1.0f, 1.0f, 1.0f};
    struct Mist
    {
        std::string_view bone;
        std::uint32_t point;
        std::uint32_t call;
    };
    constexpr std::array<Mist, 3> Mists{{{"Monster101_L_Arm", 0x427b2a, 0x427b8b},
                                         {"Monster101_R_Arm", 0x427be1, 0x427c42},
                                         {"Monster101_Head", 0x427c98, 0x427cfd}}};
    for (const Mist& mist : Mists)
    {
        const auto point = PointOf(pass, mist.bone, mist.point);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto made = Particle(pass, Particles::KalimaWaterfallMist, 1, point.Value(), {}, White, 1.0f, mist.call);
            !made.HasValue())
            return made;
    }
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x249}, {4, 0x24a}, {6, 0x24b}}};
    PlayTheSounds(pass, 0x247, Sounds);
    const Kernel::ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    if (actor.animation.action == 3 && frame >= 4.0f)
    {
        constexpr glm::vec3 Tilt{0.0f, 45.0f, 45.0f};     // 0x42340000
        constexpr glm::vec3 ArmOffset{30.0f, 0.0f, 0.0f}; // 0x41f00000
        constexpr float Wave = 0.5f;                      // 0x3f000000
        constexpr glm::vec3 Bright{5.0f, 5.0f, 5.0f};     // 0x40a00000
        const auto arm = PointOf(pass, "Monster101_L_Arm", 0x427e4e, ArmOffset);
        if (!arm.HasValue())
            return Core::Unexpected(arm.Error());
        if (auto made = Particle(pass, Particles::BreathParticle, Particles::CrywolfTideSmokeSubtype, arm.Value(), Tilt,
                                 White, 1.0f, 0x427eaf);
            !made.HasValue())
            return made;
        if (frame >= 5.0f && frame <= 5.5f)
        {
            const auto wave = PointOf(pass, "Monster101_L_Arm", 0x427f4c, ArmOffset);
            if (!wave.HasValue())
                return Core::Unexpected(wave.Error());
            if (auto made = Particle(pass, Particles::ShockWaveParticle, 3, wave.Value(), Tilt, Bright, Wave, 0x427fad);
                !made.HasValue())
                return made;
        }
    }
    // 0x427fd3: 6.9 (0x6b8a3c). Only in an original frame (the table is the world's).
    if (actor.animation.action == 3 && HasTarget(actor) && actor.animation.frame >= 6.9f && pass.originalFrame)
    {
        Kernel::ActorState& target = pass.world.Actors()[static_cast<std::size_t>(actor.attackTarget)];
        glm::vec3 from{target.position.x, target.position.y, F32(D(target.position.z) + 100.0)};
        glm::vec3 to = pass.actor.position;
        constexpr std::int32_t BeamSubtype = 0x10;
        constexpr float Width = 20.0f; // 0x41a00000
        const Kernel::JointRequest request{.type = Joints::JointFamilies::BeamJoint,
                                           .firstPosition = from,
                                           .secondPosition = to,
                                           .angles = target.angles,
                                           .subtype = BeamSubtype,
                                           .ownerContext = &pass.actor,
                                           .scale = Width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto made = Joints::CreateFaunaJoint(pass.world.DrawTail().joints, request, pass.random); !made.HasValue())
            return Core::Unexpected(std::format("0x427a8d: {}", made.Error()));
    }
    return {};
}

// 0x42803b..0x428222, type 0x1b8: r = rand() % 10, a 0x5ad at 3 with (0, r · 0.01f + 0.2f, 0) at the point (60, −30, 0)
// of the head and, at the same point, the green spark 0x5c6/7 (0x428164) with angles (0, 0, 0), that light, scale 1 and
// no owner; then its sounds (0x24c, and 0x24e, 0x24f and 0x250 in actions 3, 4 and 6).
Result DrawThe1b8(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x428146); !ready.HasValue())
        return ready;
    const float green = F32(static_cast<double>(pass.random.Next() % 10) * D(0.01f) + D(0.2f));
    const auto head = PointOf(pass, "Monster102_Head", 0x4280e9, {60.0f, -30.0f, 0.0f});
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    const glm::vec3 light{0.0f, green, 0.0f};
    Sprite(pass, Spark, head.Value(), 3.0f, light);
    if (auto made = Particle(pass, Particles::SparkParticle, Particles::GreenSparkSubtype, head.Value(), {}, light,
                             1.0f, 0x428164);
        !made.HasValue())
        return made;
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x24e}, {4, 0x24f}, {6, 0x250}}};
    PlayTheSounds(pass, 0x24c, Sounds);
    return {};
}

// 0x4282ea..0x4285a9, type 0x1ba: its sounds and, on each horn (Monster104_Horn0 to Horn5, the format of 0x6c72a8), r =
// rand() % 10, a 0x5ad at 0.4 with (0.4, r · 0.01f + 0.8f, 0.5) and, at the same point, the 0x5c6/6 spark (0x42849c)
// with angles (0, 0, 0), that light, scale 1 and no owner. In action 4 (0x4284b4), the 0x5f2/26 smoke (0x4285a4) at the
// point (0, −100, 20) of Monster104_Footsteps (0x428540), with the object's angles, (0.5, 0.7f, 0.5), scale 1 and no
// owner.
Result DrawThe1ba(MonsterHelperPass& pass)
{
    constexpr std::array<ActionSound, 3> Sounds{{{3, 0x253}, {4, 0x254}, {6, 0x255}}};
    PlayTheSounds(pass, 0x251, Sounds);
    if (auto ready = NeedTheSprites(pass, 0x42847e); !ready.HasValue())
        return ready;
    constexpr int Horns = 6;
    for (int horn = 0; horn < Horns; ++horn)
    {
        const std::string name = std::format("Monster104_Horn{}", horn);
        const float green = F32(static_cast<double>(pass.random.Next() % 10) * D(0.01f) + D(0.8f));
        const auto point = PointOf(pass, name, 0x428421);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const glm::vec3 light{0.4f, green, 0.5f};
        Sprite(pass, Spark, point.Value(), 0.4f, light);
        if (auto made = Particle(pass, Particles::SparkParticle, Particles::RareSparkSubtype, point.Value(), {}, light,
                                 1.0f, 0x42849c);
            !made.HasValue())
            return made;
    }
    if (pass.actor.animation.action != 4)
        return {};
    // 0x4284f4; 0x3f333333 is 0.7f.
    constexpr glm::vec3 FootLight{0.5f, std::bit_cast<float>(0x3f333333u), 0.5f};
    const auto foot = PointOf(pass, "Monster104_Footsteps", 0x428540, {0.0f, -100.0f, 20.0f});
    if (!foot.HasValue())
        return Core::Unexpected(foot.Error());
    return Particle(pass, Breath, Particles::FadingSmokeSubtype, foot.Value(), pass.actor.angles, FootLight, 1.0f,
                    0x4285a4);
}

// 0x4285b1..0x428daf, type 0x1bb: r = rand() % 8, the light (r · 0.1f + 0.6f, 0, 0) for five 0x5ad (the eyes at 0.3 and
// L_Arm00 to L_Arm02 at 1), its sounds and a 0x6ec/6 fanta on each hand, the left one first, with angles (0, 0, 0)
// (0x4285b1), that light and no owner. In action 4 (0x428a1d), both at 5 (0x40a00000) at the hand's point (0x428adc and
// 0x428d56); in the others, the left at 2.5 (0x40200000, 0x428c52) and the right at 2.1f (0x40066666, 0x428d56) at the
// point (0, 0, −10) of the hand (0x428cf8). In action 4 with the frame from 9 (0x6b887c) to 10 (0x6b87a8) the 0x5e8
// effect follows at the right hand's point.
Result DrawThe1bb(MonsterHelperPass& pass)
{
    if (auto ready = NeedTheSprites(pass, 0x4286b0); !ready.HasValue())
        return ready;
    const float red = F32(static_cast<double>(pass.random.Next() % 8) * D(0.1f) + D(0.6f));
    const glm::vec3 light{red, 0.0f, 0.0f};
    struct Glow
    {
        std::string_view bone;
        std::uint32_t call;
        float scale;
    };
    constexpr std::array<Glow, 5> Glows{{{"Monster105_R_Eye", 0x428653, 0.3f},
                                         {"Monster105_L_Eye", 0x428706, 0.3f},
                                         {"Monster105_L_Arm00", 0x4287b9, 1.0f},
                                         {"Monster105_L_Arm01", 0x42886c, 1.0f},
                                         {"Monster105_L_Arm02", 0x42891f, 1.0f}}};
    for (const Glow& glow : Glows)
    {
        const auto point = PointOf(pass, glow.bone, glow.call);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        Sprite(pass, Spark, point.Value(), glow.scale, light);
    }
    constexpr std::array<ActionSound, 4> Sounds{{{3, 0x25f}, {4, 0x25e}, {8, 0x25d}, {6, 0x260}}};
    PlayTheSounds(pass, 0x25b, Sounds);
    const Kernel::ActorState& actor = pass.actor;
    const bool attacking = actor.animation.action == 4;
    constexpr float AttackScale = 5.0f;                             // 0x428aaf and 0x428b70: 0x40a00000.
    constexpr float LeftScale = 2.5f;                               // 0x428c25: 0x40200000.
    constexpr float RightScale = std::bit_cast<float>(0x40066666u); // 0x428d3b: 2.1f.
    const auto left = PointOf(pass, "Monster105_L_Hand", attacking ? 0x428a78 : 0x428bee);
    if (!left.HasValue())
        return Core::Unexpected(left.Error());
    if (auto made = Particle(pass, Particles::CrywolfFanta, Particles::LongFantaSubtype, left.Value(), {}, light,
                             attacking ? AttackScale : LeftScale, attacking ? 0x428adc : 0x428c52);
        !made.HasValue())
        return made;
    const auto right = attacking ? PointOf(pass, "Monster105_R_Hand", 0x428b32)
                                 : PointOf(pass, "Monster105_R_Hand", 0x428cf8, {0.0f, 0.0f, -10.0f});
    if (!right.HasValue())
        return Core::Unexpected(right.Error());
    if (auto made = Particle(pass, Particles::CrywolfFanta, Particles::LongFantaSubtype, right.Value(), {}, light,
                             attacking ? AttackScale : RightScale, 0x428d56);
        !made.HasValue())
        return made;
    const float frame = actor.animation.frame;
    // 0x428d5b..0x428da7: 0x518cf0(0x5e8, [ebp−0x48], c+0x1c, the red light, 1, no owner, −1, 0, 0, 0, 0), the white
    // ray, without rand(), at the last point 0x4010b0 wrote: the right hand's. Only in an original frame, like its
    // sounds.
    if (attacking && frame >= 9.0f && frame <= 10.0f && pass.originalFrame)
    {
        constexpr std::int16_t WhiteRay = 0x5e8;
        constexpr std::int32_t WhiteRaySubtype = 1;
        const auto made =
            Effects::CreateHighModelEffect({.type = WhiteRay,
                                            .position = right.Value(),
                                            .angles = actor.angles,
                                            .light = light,
                                            .subtype = WhiteRaySubtype},
                                           {.world = pass.world, .random = pass.random, .worldClock = pass.worldClock});
        if (!made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}

// What the trails of 0x432bd0 share: the frame of the first pose (the object's minus the speed of the model's action,
// +0x30 + action · 0x10 + 4) and the step between poses (a tenth of that speed), as floats.
constexpr int SwingSampleCount = 10;

struct Swing
{
    const Models::Skeleton* skeleton = nullptr;
    float first = 0.0f;
    float step = 0.0f;
};

Core::Expected<Swing, std::string> SwingOf(const Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
                                           std::uint8_t speedAction)
{
    const Models::Skeleton* skeleton = Assets::FaunaModelsOrEmpty(faunaModels).SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr || skeleton->actions.size() <= speedAction)
    {
        return Core::Unexpected(std::format("0x432bd0: el modelo {:#x} no tiene la acción {} cuya velocidad lee la "
                                            "estela",
                                            actor.type, speedAction));
    }
    const float speed = skeleton->actions[speedAction].speed;
    return Swing{
        .skeleton = skeleton, .first = F32(D(actor.animation.frame) - D(speed)), .step = F32(D(speed) * D(0.1f))};
}

// 0x4eb860 with flags (0, 1): PoseWorldModel keeps the shared placement, writes the world scratch and records the
// model's key, dummy bones included. 0x1b0 also consumes the last sample through that scratch.
Result PoseTheSwing(Kernel::WorldState& world, Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
                    const Swing& swing, float frame, const glm::vec3& angles)
{
    return Actors::Drawing::PoseWorldModel(world, faunaModels, actor, *swing.skeleton, frame, angles);
}

struct SwingLeg
{
    std::uint8_t firstBone;
    std::uint8_t secondBone;
    glm::vec3 first;
    glm::vec3 second;
    glm::vec3 light;
    std::int32_t key;
};

Result EmitSwing(Kernel::WorldState& world, Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
                 const Swing& swing, Particles::Particles* particles, std::span<const SwingLeg> legs,
                 Crt::CrtRandom& random, float worldClock, bool weaponBone = false, bool particle640 = false)
{
    const std::uint8_t bone = weaponBone ? actor.hands[0].bone : 0;
    float frame = swing.first;
    glm::vec3 particleLight(1.0f);
    for (int sample = 0; sample < SwingSampleCount; ++sample)
    {
        if (auto posed =
                Actors::Drawing::PoseWorldModel(world, faunaModels, actor, *swing.skeleton, frame, actor.angles);
            !posed.HasValue())
            return posed;
        const auto& bones = world.PoseCarry().bones;
        for (const auto& leg : legs)
        {
            const std::uint8_t firstBone = weaponBone ? bone : leg.firstBone;
            const std::uint8_t secondBone = weaponBone ? bone : leg.secondBone;
            const auto& firstMatrix = firstBone < bones.size() ? bones[firstBone] : BoneTransform(0.0f);
            const auto& secondMatrix = secondBone < bones.size() ? bones[secondBone] : BoneTransform(0.0f);
            const glm::vec3 first = Math::OriginalVectorTransform(leg.first, Math::OriginalMatrixOf(firstMatrix));
            const glm::vec3 second = Math::OriginalVectorTransform(leg.second, Math::OriginalMatrixOf(secondMatrix));
            if (particle640 && actor.animation.frame > 3.5f && actor.animation.frame < 4.5f)
            {
                if (auto made = CreateParticle(particles, actor, Particles::PoundingBallParticle, 0, second,
                                               actor.angles, particleLight, 1, worldClock, random, 0x433056);
                    !made.HasValue())
                    return made;
            }
            particleLight = leg.light;
            const auto created =
                Actors::Drawing::CreateRibbon(world.DrawTail().ribbons, {.ownerContext = &actor,
                                                                         .ownerEquipment = &actor.level,
                                                                         .first = first,
                                                                         .second = second,
                                                                         .light = leg.light,
                                                                         .type = 3,
                                                                         .shortTrail = true,
                                                                         .key = leg.key});
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
        }
        frame = F32(D(frame) + D(swing.step));
    }
    return {};
}

// 0x432c12..0x432db0, type 0x1b0 in action 4: ten poses from the frame minus action 4's speed, each with the object's
// angles and its z 18 degrees less per round (0x6b886c, which 0x432da1 restores at the end), the point (0, 100, −150)
// of bone 16 (0x4ec110 on 0x68b1b1c, without translating: the buffer is already in the world) for particle 0x5d9/0 with
// the object's frame in (5, 7), with those angles, white light and scale 1 (0x432d07), the point (0, −150, 0) turned
// (0x5fc8f0 and 0x5fcbd0), which nobody reads, and two rand() (0x432d6f and 0x432d74).
Result SwingThe1b0(Kernel::WorldState& world, Assets::FaunaModels* faunaModels, const Kernel::ActorState& actor,
                   Crt::CrtRandom& random, Particles::Particles* particles, float worldClock)
{
    const auto swing = SwingOf(faunaModels, actor, 4);
    if (!swing.HasValue())
        return Core::Unexpected(swing.Error());
    const float frame = actor.animation.frame;
    const bool sparks = frame > 5.0f && frame < 7.0f;
    glm::vec3 angles = actor.angles;
    float pose = swing.Value().first;
    constexpr std::size_t SparkBone = 16;
    constexpr glm::vec3 SparkOffset{0.0f, 100.0f, -150.0f};
    constexpr int Poses = 10;
    for (int turn = 0; turn < Poses; ++turn)
    {
        if (auto posed = PoseTheSwing(world, faunaModels, actor, swing.Value(), pose, angles); !posed.HasValue())
            return posed;
        if (sparks)
        {
            const auto& bones = world.PoseCarry().bones;
            const BoneTransform matrix = SparkBone < bones.size() ? bones[SparkBone] : BoneTransform(0.0f);
            const glm::vec3 point = Math::OriginalVectorTransform(SparkOffset, Math::OriginalMatrixOf(matrix));
            if (auto made = CreateParticle(particles, actor, Fire, 0, point, angles, glm::vec3(1.0f), 1.0f, worldClock,
                                           random, 0x432d07);
                !made.HasValue())
                return made;
        }
        angles.z = F32(D(angles.z) - 18.0);
        static_cast<void>(random.Next());
        static_cast<void>(random.Next());
        pose = F32(D(pose) + D(swing.Value().step));
    }
    return {};
}
} // namespace

Handled DrawTheEffectsOf1a3To1a9(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    // 0x441430: outside the table it leaves without touching anything; 0x1a8 goes straight to 0x442dd5.
    if (!Within(type, 0x1a3, 0x1a9) || type == 0x1a8)
        return false;
    Result drawn{};
    switch (type)
    {
    case 0x1a3:
        drawn = DrawThe1a3(pass);
        break;
    case 0x1a4:
        drawn = DrawThe1a4(pass);
        break;
    case 0x1a5:
        drawn = DrawThe1a5(pass);
        break;
    case 0x1a6:
        drawn = DrawThe1a6(pass);
        break;
    case 0x1a7:
        drawn = DrawThe1a7(pass);
        break;
    default:
        drawn = DrawThe1a9(pass);
        break;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    ResetAtRest(actor);
    return false;
}

Handled DrawTheCrywolfEffectsOf1abTo208(MonsterHelperPass& pass)
{
    // 0x4303c0
    if (pass.map != CrywolfMap)
        return false;
    return DrawTheCrywolfCase(pass);
}

Handled DrawTheEffectsOf1b6To1bb(MonsterHelperPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint16_t type = actor.type;
    // 0x427200: outside the table, 0x428dc0 without touching +4.
    if (!Within(type, 0x1b6, 0x1bb))
        return false;
    Result drawn{};
    switch (type)
    {
    case 0x1b6:
        drawn = DrawThe1b6(pass);
        break;
    case 0x1b7:
        drawn = DrawThe1b7(pass);
        break;
    case 0x1b8:
        drawn = DrawThe1b8(pass);
        break;
    case 0x1b9:
    {
        // 0x42822a..0x4282e5: only its sounds.
        constexpr std::array<ActionSound, 3> Sounds{{{3, 0x258}, {4, 0x259}, {6, 0x25a}}};
        PlayTheSounds(pass, 0x256, Sounds);
        break;
    }
    case 0x1ba:
        drawn = DrawThe1ba(pass);
        break;
    default:
        drawn = DrawThe1bb(pass);
        break;
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    ResetAtRest(actor);
    return false;
}

Core::Expected<void, std::string> DrawTheMonsterHelpers(MonsterHelperPass& pass)
{
    const auto first = DrawTheEffectsOf1a3To1a9(pass);
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    if (first.Value())
        return {};
    // 0x510a54: 0x42fa30 returns 0 without doing anything.
    const auto crywolf = DrawTheCrywolfEffectsOf1abTo208(pass);
    if (!crywolf.HasValue())
        return Core::Unexpected(crywolf.Error());
    if (crywolf.Value())
        return {};
    const auto last = DrawTheEffectsOf1b6To1bb(pass);
    if (!last.HasValue())
        return Core::Unexpected(last.Error());
    return {};
}

Core::Expected<void, std::string> StepTheSwingsOf1abTo1b4(Kernel::ActorState& actor, std::size_t slot,
                                                          Kernel::WorldState& world, Assets::FaunaModels* faunaModels,
                                                          Crt::CrtRandom& random, Particles::Particles* particles,
                                                          float worldClock)
{
    // 0x432bf1: before looking at the type.
    static_cast<void>(random.Next());
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    switch (actor.type)
    {
    case 0x1ab:
    {
        // 0x432f19: ten poses and 0x640/0 before each ribbon; first a white light and then (0, 0, 5).
        if (action != 3)
            return {};
        const auto swing = SwingOf(faunaModels, actor, 3);
        if (!swing.HasValue())
            return Core::Unexpected(swing.Error());
        constexpr std::array legs{SwingLeg{0, 0, {0, 0, -60}, {0, 0, -150}, {0, 0, 5}, 80}};
        return EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock, true, true);
    }
    case 0x1ae:
        // 0x4330a9: in action 8, the left hand's point and its three particles. In action 4 with the frame in (4.5, 5),
        // effect 0x145 before posing; in actions 3 and 4, the trail.
        if (action == 8)
        {
            const auto hand = NamedBonePoint(world, actor, slot, "Left_Hand", {}, 0x4331de);
            if (!hand.HasValue())
                return Core::Unexpected(hand.Error());
            if (auto mist =
                    CreateParticle(particles, actor, Particles::BreathParticle, Particles::CrywolfSwingSmokeSubtype,
                                   hand.Value(), actor.angles, {0.2f, 0.2f, 0.7f}, 1, worldClock, random, 0x433248);
                !mist.HasValue())
                return mist;
            for (const float scale : {0.8f, 0.6f})
                if (auto spark = CreateParticle(particles, actor, Particles::MagicSparkParticle, 2, hand.Value(),
                                                actor.angles, {0.3f, 0.3f, 0.4f}, scale, worldClock, random, 0x43328d);
                    !spark.HasValue())
                    return spark;
            return {};
        }
        if (action != 3 && action != 4)
            return {};
        if (action == 4 && frame > 4.5f && frame < 5.0f)
        {
            static_cast<void>(world.Map().effects.CreateCrywolfTideEffect({.type = 0x145,
                                                                           .position = actor.position,
                                                                           .angles = actor.angles,
                                                                           .light = actor.light,
                                                                           .subtype = 2,
                                                                           .owner = &actor}));
        }
        {
            const auto swing = SwingOf(faunaModels, actor, action);
            if (!swing.HasValue())
                return Core::Unexpected(swing.Error());
            constexpr std::array legs{SwingLeg{0, 0, {0, 0, -60}, {0, 0, -150}, {1, 1, 1}, 80}};
            return EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock, true);
        }
    case 0x1af:
        // 0x433451: in actions 3 and 4, with the speed of its own action.
        if (action != 3 && action != 4)
            return {};
        {
            const auto swing = SwingOf(faunaModels, actor, action);
            if (!swing.HasValue())
                return Core::Unexpected(swing.Error());
            constexpr std::array legs{SwingLeg{27, 27, {0, 250, 0}, {}, {0.2f, 1.0f, 0.4f}, 80}};
            if (auto emitted = EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock);
                !emitted.HasValue())
                return emitted;
            if (frame > 4.5f && frame < 5.0f)
                return EmitDeathSpiritSwing(world, actor, particles, random, worldClock);
            return {};
        }
    case 0x1b0:
        if (action != 4)
            return {};
        return SwingThe1b0(world, faunaModels, actor, random, particles, worldClock);
    case 0x1b1:
        if (action != 4)
            return {};
        {
            const auto swing = SwingOf(faunaModels, actor, 4);
            if (!swing.HasValue())
                return Core::Unexpected(swing.Error());
            constexpr std::array legs{SwingLeg{80, 80, {0, 0, -90}, {}, {1, 1, 1}, 80},
                                      SwingLeg{82, 82, {0, 0, 90}, {}, {1, 1, 1}, 84}};
            return EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock);
        }
    case 0x1b3:
        if (action != 3)
            return {};
        {
            const auto swing = SwingOf(faunaModels, actor, 3);
            if (!swing.HasValue())
                return Core::Unexpected(swing.Error());
            constexpr std::array legs{SwingLeg{25, 25, {0, 0, 120}, {}, {1, 1, 1}, 25}};
            return EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock);
        }
    case 0x1b4:
        if (action != 3)
            return {};
        {
            const auto swing = SwingOf(faunaModels, actor, 3);
            if (!swing.HasValue())
                return Core::Unexpected(swing.Error());
            constexpr std::array legs{SwingLeg{12, 16, {20, 0, 0}, {60, 0, 0}, {1, 1, 1}, 23},
                                      SwingLeg{20, 24, {20, 0, 0}, {60, 0, 0}, {1, 1, 1}, 24}};
            return EmitSwing(world, faunaModels, actor, swing.Value(), particles, legs, random, worldClock);
        }
    default:
        // 0x432c05: 0x1ac, 0x1ad, 0x1b2 and those outside the table, nothing else.
        return {};
    }
}
} // namespace Client::Monsters::Drawing
