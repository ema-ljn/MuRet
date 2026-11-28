#include "Skills/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrowEffects.h"
#include "Skills/AttackSkillReads.h"
#include "Combat/BlastEffectCalls.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Effects/FireBreathEffect.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkills.h"
#include "Effects/HighModelEffects.h"
#include "Effects/JavelinEffect.h"
#include "Joints/JointFamilies.h"
#include "Joints/JointKernels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Effects/SkillBurstEffects.h"
#include "Effects/WizardEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/ColoredDecals.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainHeight.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"
#include "Tables/PlayerModels.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <tuple>

// The skill cases of 0x4fd5f0 (0x4fdeeb, 0x504372 and 0x5042ce), checked against the PE c32bbd5f: 06, "El paso del
// ataque".
namespace Client::Skills
{
namespace
{
using Math::D;
using Math::F32;
constexpr std::int16_t NoTarget = -1;
// The sound of the 0xc2 fires of skills 0x02 and 0x04 (0x5049f9, 0x504bda).
constexpr Audio::SoundId FireSound = Sound::MeteoriteSound;
constexpr std::size_t PoolSlots = 400;
// +0x78 & 0x4000: target cases 0x1b, 0x1c and 0x10 do nothing (0x50451c, 0x5046ca, 0x504836).
constexpr std::uint32_t QuietFlag = 0x4000;
// The joint of the decals that 0x534f90 removes (0x4fe3ce, 0x504876).
constexpr std::int32_t FlameJoint = 0x10f;

// 0x41ed10(sound, …): CL10 decides how it sounds (WorldRuntime::lastSoundId in the game).
void RequestTheSound(const Actors::CharacterStepContext& context, Audio::SoundId sound,
                     const Kernel::ActorState* actor = nullptr)
{
    if (actor != nullptr && context.soundAt)
        context.soundAt(sound, actor->position);
    else if (context.soundRequest)
        context.soundRequest(sound);
}

// 0x518cf0 of a high type (Effects/HighModelEffects) from a case: the error carries the skill and the slot.
Core::Expected<void, std::string> CreateHighEffect(const Kernel::ModelEffectArguments& arguments, std::uint8_t skill,
                                                   std::size_t slot, const Actors::CharacterStepContext& context)
{
    if (auto made = Effects::CreateHighModelEffect(arguments, Effects::HighContextOf(context)); !made.HasValue())
        return Core::Unexpected(std::format("{} (la habilidad {:#x} del lugar {})", made.Error(), skill, slot));
    return {};
}

// 0x4fd81e and 0x4fdaeb..0x4fdb4d: the step's local light, (1, 1, 1) or, with action 0x4d, 0x4f or 0x5a past frame
// 5.5, that of particle 0x5ac/0 (0.3, 0.5, 1).
glm::vec3 CaseLight(const Kernel::ActorState& actor)
{
    constexpr std::array<std::uint8_t, 3> FlaredActions{0x4d, 0x4f, 0x5a};
    constexpr float FlareFrame = 5.5f; // 0x6b8a40
    const bool flared = std::ranges::find(FlaredActions, actor.animation.action) != FlaredActions.end() &&
                        actor.animation.frame > FlareFrame;
    return flared ? glm::vec3(std::bit_cast<float>(0x3e99999au), 0.5f, 1.0f) : glm::vec3(1.0f);
}

// 0x530860(0x613, from, to, angles, subtype, o, width, key, skill, serial, −1): a 0x613 joint hung from o, with what
// its creation asks of the step (the /1's sound and the /0's own record). With the pool full, nothing.
struct SpiritJointRequest
{
    std::uint32_t address = 0;
    std::int32_t subtype = 0;
    glm::vec3 from{};
    glm::vec3 to{};
    glm::vec3 angles{};
    float width = 0.0f;
    std::int32_t key = 0;
    std::int32_t skill = 0;
    std::int32_t serial = 0;
};

Core::Expected<void, std::string>
CreateTheSpirit(Kernel::ActorState& actor, const Actors::CharacterStepContext& context, SpiritJointRequest spirit)
{
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", spirit.address));
    const Joints::JointKernels::CreationServices services{.playerBonePoint = {},
                                                          .soundRequest = [&context](Audio::SoundId sound)
                                                          { RequestTheSound(context, sound); },
                                                          .ownRecordDepth = &context.world.Runtime().ownRecordDepth,
                                                          .ownVolley = &context.world.OwnCharacter().bytef04};
    const Kernel::JointRequest request{.type = Joints::JointFamilies::SpiritJoint,
                                       .firstPosition = spirit.from,
                                       .secondPosition = spirit.to,
                                       .angles = spirit.angles,
                                       .subtype = spirit.subtype,
                                       .ownerContext = &actor,
                                       .scale = spirit.width,
                                       .key = spirit.key,
                                       .skill = spirit.skill,
                                       .skillSerial = spirit.serial,
                                       .target = -1};
    const auto created =
        Joints::CreateFaunaJoint(context.joints, request, context.random, Kernel::JointOwnerKind::Actor, &services);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", spirit.address, created.Error()));
    return {};
}

// 0x530860(0x10f, from, from, angles, subtype, owner, width, −1, 0, 0, target): a 0x10f joint (AuraJoints.cpp). With
// the pool full, nothing.
Core::Expected<void, std::string> CreateTheAura(std::uint32_t address, Kernel::ActorState& owner, glm::vec3& from,
                                                glm::vec3& angles, std::int32_t subtype, float width,
                                                std::int32_t target, const Actors::CharacterStepContext& context)
{
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", address));
    const Kernel::JointRequest request{.type = FlameJoint,
                                       .firstPosition = from,
                                       .secondPosition = from,
                                       .angles = angles,
                                       .subtype = subtype,
                                       .ownerContext = &owner,
                                       .scale = width,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = target};
    const auto created =
        Joints::CreateFaunaJoint(context.joints, request, context.random, Kernel::JointOwnerKind::Actor);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// Five 0x10f of width 20 around the target, from its +0x10 and with its angles.
Core::Expected<void, std::string> CreateFiveAuras(std::uint32_t address, Kernel::ActorState& victim,
                                                  std::int32_t subtype, std::int32_t target,
                                                  const Actors::CharacterStepContext& context)
{
    constexpr float Width = 20;
    for (int ring = 0; ring < 5; ++ring)
        if (auto created =
                CreateTheAura(address, victim, victim.position, victim.angles, subtype, Width, target, context);
            !created.HasValue())
            return created;
    return {};
}

// 0x4fe147..0x4fe18b (skill 0x43): 0x530860(0x632, o+0x10, o+0x10, o+0x1c, 7, null, 10, −1, 0, 0, −1), which drops
// three 0x632/6 (StormJoints.cpp), and sound 0x266 even with the pool full.
Core::Expected<void, std::string> CallTheStorm(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr Audio::SoundId StormSound{0x266};
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x4fe172: faltan los joints del frame (0x530860)"));
    const Kernel::JointRequest request{.type = Joints::JointFamilies::StormJoint,
                                       .firstPosition = actor.position,
                                       .secondPosition = actor.position,
                                       .angles = actor.angles,
                                       .subtype = Joints::JointFamilies::StormSubtype,
                                       .ownerContext = nullptr,
                                       .scale = 10,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
        return Core::Unexpected(std::format("0x4fe172: {}", created.Error()));
    RequestTheSound(context, StormSound);
    return {};
}

// 0x504ac7..0x504b49: skill 0x49 creates an ownerless 0x61c/4 from 130 above o, with angles (0, 0, o's z) and width 40
// (InfernoJoints.cpp, no rand()), and requests sound 0x26f even with the pool full.
Core::Expected<void, std::string> RaiseTheInferno(const Kernel::ActorState& actor,
                                                  const Actors::CharacterStepContext& context)
{
    constexpr std::int32_t InfernoSubtype = 4;
    constexpr float InfernoWidth = 40;  // 0x42200000
    constexpr double InfernoLift = 130; // 0x6ba194
    constexpr Audio::SoundId InfernoSound{0x26f};
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x504b33: faltan los joints del frame (0x530860)"));
    glm::vec3 from{actor.position.x, actor.position.y, F32(D(actor.position.z) + InfernoLift)};
    glm::vec3 turn{0, 0, actor.angles.z};
    const Kernel::JointRequest request{.type = Joints::JointFamilies::InfernoJoint,
                                       .firstPosition = from,
                                       .secondPosition = from,
                                       .angles = turn,
                                       .subtype = InfernoSubtype,
                                       .ownerContext = nullptr,
                                       .scale = InfernoWidth,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
        return Core::Unexpected(std::format("0x504b33: {}", created.Error()));
    RequestTheSound(context, InfernoSound);
    return {};
}

// 0x4f5b10(o, x, y, z, point): (x, y, z) turned by o's angles (0x5fc8f0 and 0x5fcbd0) and added to its position, no
// rand().
glm::vec3 TurnedPoint(const Kernel::ActorState& actor, const glm::vec3& offset)
{
    const auto turned = Math::OriginalVectorRotate(offset, Math::OriginalAngleMatrix(actor.angles));
    return {F32(D(actor.position.x) + D(turned.x)), F32(D(actor.position.y) + D(turned.y)),
            F32(D(actor.position.z) + D(turned.z))};
}

// 0x4fe795..0x4fe981 (skill 0x35), no rand(): 0x6a1a90 is the CRT sine and 0x6a19e0 the cosine (FSIN and FCOS, the
// product by 80 on the x87). The creation puts them at the owner's position (FaunaJointCreation.cpp, CreateSetEnergy).
Core::Expected<void, std::string> SpinTheEnergy(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr std::int32_t EnergyJoint = 0x619, EnergySubtype = 0xa, EnergySound = 0x7f;
    constexpr float Radius = 80.0f, Width = 15.0f;
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x4fe856: faltan los joints del frame (0x530860)"));
    const auto point = [&actor](double argument, bool sineFirst) -> Core::Expected<glm::vec3, std::string>
    {
        const auto sine = Math::Detail::SineProductPC53(argument, Radius);
        const auto cosine = Math::Detail::CosineProductPC53(argument, Radius);
        if (!sine || !cosine)
            return Core::Unexpected(std::string("0x4fe7d7: el seno de la 0x35 excede el perfil PC53"));
        return glm::vec3(F32((sineFirst ? *sine : *cosine) + D(actor.position.x)),
                         F32((sineFirst ? *cosine : *sine) + D(actor.position.y)), F32(D(actor.position.z) + 300.0));
    };
    const auto near = point(4.5, true);
    if (!near.HasValue())
        return Core::Unexpected(near.Error());
    const auto far = point(22.5, false);
    if (!far.HasValue())
        return Core::Unexpected(far.Error());
    struct Energy
    {
        std::uint32_t address;
        float turn;
        glm::vec3 from;
    };
    const std::array<Energy, 4> energies{{{0x4fe856, 45.0f, near.Value()},
                                          {0x4fe895, 405.0f, near.Value()},
                                          {0x4fe92c, 225.0f, far.Value()},
                                          {0x4fe96b, 450.0f, far.Value()}}};
    for (const Energy& energy : energies)
    {
        glm::vec3 from = energy.from;
        glm::vec3 angles{-45.0f, 0.0f, energy.turn};
        const Kernel::JointRequest request{.type = EnergyJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = EnergySubtype,
                                           .ownerContext = &actor,
                                           .scale = Width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", energy.address, created.Error()));
    }
    RequestTheSound(context, Audio::SoundId{EnergySound});
    return {};
}

// 0x4feb3b..0x4febd3 (skill 0x41): the point of 0x4f5b10(o, 0, −90, −50), 80 higher in action 0x51 and 40 in 0x5b,
// and 0x518cf0(0x62c, point, o+0x1c, o+0xf4, 0, o, −1, 0, 0, 0, 0). Its case (0x51f25e) sets the life to 0 and, with
// an owner, drops the five 0x62c (SpiralJoints.cpp): /1, /0 of width 250, /2, /3 and /4, the others of width 100,
// from the point and with o's angles. Then sound 0x1cd even with the table full.
Core::Expected<void, std::string> RaiseTheSpirals(Kernel::ActorState& actor,
                                                  const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t SpiralEffect = 0x62c;
    constexpr Audio::SoundId SpiralSound{0x1cd};
    constexpr std::uint8_t HighAction = 0x51, LowAction = 0x5b;
    glm::vec3 point = TurnedPoint(actor, {0.0f, -90.0f, -50.0f});
    if (actor.animation.action == HighAction)
        point.z = F32(D(point.z) + 80.0); // 0x6b9ffc
    else if (actor.animation.action == LowAction)
        point.z = F32(D(point.z) + 40.0); // 0x6b8bf4
    const Kernel::ModelEffectArguments arguments{.type = SpiralEffect,
                                                 .position = point,
                                                 .angles = actor.angles,
                                                 .light = actor.light,
                                                 .subtype = 0,
                                                 .owner = &actor,
                                                 .key = 0xffff,
                                                 .byte89 = 0,
                                                 .byte88 = 0,
                                                 .byte1bd = 0,
                                                 .scale = 0.0f};
    if (const auto slot = context.world.Map().effects.Begin(arguments))
    {
        context.world.Map().effects.Effects()[*slot].life = 0;
        if (context.joints.empty())
            return Core::Unexpected(std::string("0x51f293: faltan los joints del frame (0x530860)"));
        struct Spiral
        {
            std::uint32_t address;
            std::int32_t subtype;
            float width;
        };
        constexpr std::array<Spiral, 5> Spirals{{{0x51f293, 1, 100.0f},
                                                 {0x51f2b6, 0, 250.0f},
                                                 {0x51f2dd, 2, 100.0f},
                                                 {0x51f301, 3, 100.0f},
                                                 {0x51f3ad, 4, 100.0f}}};
        for (const Spiral& spiral : Spirals)
        {
            const Kernel::JointRequest request{.type = Joints::JointFamilies::SpiralJoint,
                                               .firstPosition = point,
                                               .secondPosition = point,
                                               .angles = actor.angles,
                                               .subtype = spiral.subtype,
                                               .ownerContext = &actor,
                                               .scale = spiral.width,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
            if (auto created =
                    Joints::CreateFaunaJoint(context.joints, request, context.random, Kernel::JointOwnerKind::Actor);
                !created.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", spiral.address, created.Error()));
        }
    }
    RequestTheSound(context, SpiralSound, &actor);
    return {};
}

// 0x4fe505..0x4fe790 (skill 0x48): o's +0x78 at 0 and six 0x10f/5..7 of width 170 from o + 100 in z, turned 45, 135
// and 225 and then 90, 180 and 270; the copy drops 10 after each one except the /7s. Finally, sound 0x26b.
Core::Expected<void, std::string> RaiseTheAuras(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr float Width = std::bit_cast<float>(0x432a0000u);
    constexpr Audio::SoundId RiseSound{0x26b};
    actor.flags78 = 0;
    glm::vec3 from = actor.position;
    from.z = static_cast<float>(static_cast<double>(from.z) + 100);
    constexpr std::array<std::uint32_t, 6> Turns{0x42340000u, 0x43070000u, 0x43610000u,
                                                 0x42b40000u, 0x43340000u, 0x43870000u};
    constexpr std::array<std::uint32_t, 6> Addresses{0x4fe590, 0x4fe5f5, 0x4fe65a, 0x4fe6ad, 0x4fe712, 0x4fe777};
    for (std::size_t i = 0; i < Turns.size(); ++i)
    {
        glm::vec3 angles{0, 0, std::bit_cast<float>(Turns[i])};
        const auto subtype = static_cast<std::int32_t>(5 + i % 3);
        if (auto created = CreateTheAura(Addresses[i], actor, from, angles, subtype, Width, -1, context);
            !created.HasValue())
            return created;
        if (subtype != 7)
            from.z = static_cast<float>(static_cast<double>(from.z) - 10);
    }
    RequestTheSound(context, RiseSound);
    return {};
}

// 0x4fe015..0x4fe13f (skill 0x30) and 0x4fe253..0x4fe37b (skill 0x45): thirty-six 0x613 joints of width 60 hung from
// o, 100 higher, with angles (−10, 0, 10 · i); in turns 0 and 20, after the joint, the 0x61e decal (`decalSubtype`)
// over o with those angles, the light `light` and o as owner (0x4fe127, 0x4fe360). No rand(). Then the sound.
struct SpiritRing
{
    std::int32_t subtype = 0;
    std::uint32_t jointAddress = 0;
    std::int32_t decalSubtype = 0;
    glm::vec3 light{1.0f};
    std::int32_t sound = 0;
};

Core::Expected<void, std::string> RingTheSpirits(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                 const Actors::CharacterStepContext& context, const SpiritRing& ring)
{
    constexpr int Spirits = 36, DecalEvery = 20;
    constexpr float Width = 60.0f;  // 0x42700000
    constexpr float Pitch = -10.0f; // 0xc1200000
    for (int index = 0; index < Spirits; ++index)
    {
        const glm::vec3 angles{Pitch, 0.0f, static_cast<float>(static_cast<double>(index) * 10.0)};
        const glm::vec3 point{actor.position.x, actor.position.y,
                              static_cast<float>(static_cast<double>(actor.position.z) + 100.0)};
        if (auto made = CreateTheSpirit(actor, context,
                                        {.address = ring.jointAddress,
                                         .subtype = ring.subtype,
                                         .from = point,
                                         .to = point,
                                         .angles = angles,
                                         .width = Width});
            !made.HasValue())
            return made;
        if (index % DecalEvery != 0)
            continue;
        if (auto made = CreateHighEffect({.type = Effects::ColoredDecalEffect,
                                          .position = actor.position,
                                          .angles = angles,
                                          .light = ring.light,
                                          .subtype = ring.decalSubtype,
                                          .owner = &actor},
                                         skill, slot, context);
            !made.HasValue())
            return made;
    }
    RequestTheSound(context, Audio::SoundId{ring.sound});
    return {};
}

// 0x500141/0x501147/0x501867/0x50202e: only the hero publishes the volley; the close keeps its 3848 rand().
void CountHeroVolley(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    if (&actor != &context.world.Hero())
        return;
    const Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                    Models::OwnRecordCloseRandoms);
    ++context.world.OwnCharacter().bytef04;
}

// With the table full, nothing and no rand(); what the effect asks for and MuRet lacks cuts with the call's address.
Core::Expected<void, std::string> CreateWizard(std::uint32_t address, const Kernel::ModelEffectArguments& arguments,
                                               std::uint8_t skill, std::size_t slot,
                                               const Actors::CharacterStepContext& context)
{
    const auto created = Effects::CreateWizardEffect(context, arguments);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{:#x}: el efecto {:#x}/{} de la habilidad {:#x} del lugar {}: {}", address,
                                            arguments.type, arguments.subtype, skill, slot, created.Error()));
    }
    return {};
}

// 0x5042d3..0x504324: o's angles with z at the heading of 0x4e93f0 from o to the target (fstp, no rand()).
glm::vec3 TowardsTheTarget(const Kernel::ActorState& actor, const Kernel::ActorState& victim)
{
    return glm::vec3(actor.angles.x, actor.angles.y,
                     static_cast<float>(
                         Math::CreateAngle(actor.position.x, actor.position.y, victim.position.x, victim.position.y)));
}

// MOVSX: the hand's type as a signed WORD.
std::int32_t HandOf(const Kernel::ActorState& actor, std::size_t hand)
{
    return std::bit_cast<std::int16_t>(actor.hands[hand].model);
}

// 0x4fdf2b..0x4fdf57 (and 0x4fdfb3, 0x4fdfe4, 0x4fec30): o+0x8c = the right-hand weapon (+0x2b8, MOVSX) − 0x212 and
// o+0x8e = its level (+0x2ba), which effects 0xf4 and 0xfa read through their owner (Scenes/GameDrawTail.cpp).
void KeepTheWeapon(Kernel::ActorState& actor)
{
    constexpr std::int32_t FirstWeaponModel = 0x212;
    actor.word8c = static_cast<std::int16_t>(HandOf(actor, 0) - FirstWeaponModel);
    actor.byte8e = actor.hands[0].level;
}

// The target from +0x3a4 (MOVSX) of cases 0x44, 0x46 and 0x47: the pool slot without a range check (0x4fe1ad and its
// copies). Outside the pool, MuRet cuts: the original would read other memory.
Core::Expected<Kernel::ActorState*, std::string> TargetOf(Kernel::ActorState& actor, std::size_t slot,
                                                          const Actors::CharacterStepContext& context,
                                                          std::uint32_t address)
{
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    if (target == NoTarget)
        return nullptr;
    auto actors = context.world.Actors();
    if (target < 0 || static_cast<std::size_t>(target) >= actors.size())
    {
        return Core::Unexpected(
            std::format("{:#x}: el blanco {} de +0x3a4 del lugar {} está fuera del pool", address, target, slot));
    }
    return &actors[static_cast<std::size_t>(target)];
}

// 0x4fdf2b..0x4fdfae, skill 0x2a: the weapon, 0x59fb80(0x2a) and 0x518cf0(0xfa, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a,
// slot, 0, 0, 0), the earthquake with state 0 (which its draw does with o's weapon, 0x52d43a); then sound 0x95. The
// prefix of 0x518cf0 sets +0x8a and +0x89 before the case (0x51a66d), which does not read them: MuRet writes them
// after the creation. With the table full, nothing (and the sound anyway).
Core::Expected<void, std::string> ShakeTheGround(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr std::uint8_t QuakeSkill = 0x2a;
    constexpr Audio::SoundId QuakeSound{0x95};
    KeepTheWeapon(actor);
    const std::int32_t index = OwnSkillIndex(context, QuakeSkill);
    Effects::ModelEffects& effects = context.world.Map().effects;
    const auto created = effects.CreateEarthquake(
        actor.position, actor.angles, actor.light, 0,
        Effects::ModelEffectOwner{.type = &actor.type, .effect = nullptr, .angles = &actor.angles},
        context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random);
    if (created.has_value())
    {
        Effects::ModelEffect& quake = effects.Effects()[*created];
        quake.disturbanceState = static_cast<std::uint16_t>(actor.word8a);
        quake.byte89 = static_cast<std::uint8_t>(index & 0xff);
        quake.byte1bd = 0;
        quake.ownerContext = &actor;
        quake.itemType = &actor.word8c;
        quake.itemLevel = &actor.byte8e;
    }
    RequestTheSound(context, QuakeSound);
    return {};
}

// 0x500496..0x500c0b, skill 0x05 (the Flame): the point of the skill's cell (+0x39c and +0x39d, plus 0.5f, times 100
// on the x87, each axis stored as a float) with the terrain height there (0x5f93c0), 0x59fb80(5) and 0x518cf0(0x5de,
// point, o+0x1c, o+0xf4, 0, o, o+0x8a, slot, 0, 0, 0): the jet with o as owner
// (Effects::ModelEffects::CreateOwnedFlameJet), whose step with the hero as owner sends the targets (0x52ae2a,
// Effects/GameModelEffects). With c the published hero ([0x743a170]), the own record opened, its count +0xf04 one more
// and closed (0x500539..0x500bfa, with its 3848 rand() if it is left at 0); then, with anyone, sound 0x79 (0x500c03).
// With the table full there is no jet (nor its creation's record) and the rest stays the same. Without the terrain
// heights, MuRet cuts before 0x59fb80.
Core::Expected<void, std::string> CastTheFlame(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr std::uint8_t FlameSkill = 5;
    constexpr Audio::SoundId FlameSound{0x79};
    constexpr double CellCentre = 0.5;                     // 0x6b882c
    constexpr double CellSize = Terrain::CellSizeAsDouble; // 0x6b86b4
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x5004ea: falta la altura del terreno (0x5f93c0) del chorro de la Flame"));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    const std::int32_t index = OwnSkillIndex(context, FlameSkill);
    const Kernel::ModelEffectArguments arguments{.type = Effects::FlameJetEffect,
                                                 .position = point,
                                                 .angles = actor.angles,
                                                 .light = actor.light,
                                                 .subtype = 0,
                                                 .owner = &actor,
                                                 .key = static_cast<std::uint16_t>(actor.word8a),
                                                 .byte89 = static_cast<std::uint8_t>(index & 0xff)};
    static_cast<void>(world.Map().effects.CreateOwnedFlameJet(arguments, runtime.ownRecordDepth,
                                                              world.OwnCharacter().bytef04, context.random));
    if (&actor == &world.Hero())
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        ++world.OwnCharacter().bytef04;
    }
    RequestTheSound(context, FlameSound);
    return {};
}

// 0x518cf0(type, point, o+0x1c, o+0xf4, 0, o, o+0x8a, `index`, 0, 0, 0): an effect of CL11's switch batch
// (Combat/BlastEffectCalls) with o as owner, as skill 0x29 (0x4fec9a), 0xd (0x500d7b and 0x500dba) and 0xe (0x5014da)
// request it. With the table full, nothing.
Core::Expected<void, std::string> CreateOwnedBlast(std::int16_t type, const glm::vec3& point, Kernel::ActorState& actor,
                                                   std::int32_t index, const Actors::CharacterStepContext& context)
{
    const Kernel::ModelEffectArguments arguments{.type = type,
                                                 .position = point,
                                                 .angles = actor.angles,
                                                 .light = actor.light,
                                                 .subtype = 0,
                                                 .owner = &actor,
                                                 .key = static_cast<std::uint16_t>(actor.word8a),
                                                 .byte89 = static_cast<std::uint8_t>(index & 0xff)};
    const auto made = Combat::CreateBlastEffect(arguments, &actor, context);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}

// The skill's cell (+0x39c and +0x39d, plus 0.5f, times 100 on the x87, each axis stored as a float) with the terrain
// height there (0x5f93c0, stored as a float), as 0x4ffcbe and 0x500cd4 read it. Without the heights, MuRet cuts at
// `address`.
Core::Expected<glm::vec3, std::string>
SkillCellPoint(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context, std::uint32_t address)
{
    constexpr double CellCentre = 0.5;                     // 0x6b882c
    constexpr double CellSize = Terrain::CellSizeAsDouble; // 0x6b86b4
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(
            std::format("{:#x}: falta la altura del terreno (0x5f93c0) de la celda de la skill", address));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    return point;
}

// 0x4ffcbe..0x4ffdb7, skill 0x27: the skill's cell with its height and ten turns: rand() % 50 times the turn over the
// height (FILD + FADD, stored as a float) and 0x518cf0(0x108, point, o+0x1c, the local light, 0, nobody, turn + 1, 0,
// 0, 0, 0), even with the table full (the rand() comes before the creation). The local light is (1, 1, 1) from
// 0x4fd81e, or (0.3, 0.5, 1) from 0x4fdb3f with actions 0x4d, 0x4f and 0x5a past frame 5.5; it does not matter: the
// creation of the 0x108/0 sets it to 0 (0x51bd37).
Core::Expected<void, std::string> DropTheIceStorm(const Kernel::ActorState& actor,
                                                  const Actors::CharacterStepContext& context)
{
    constexpr int Rounds = 10;
    constexpr int Rise = 50;
    constexpr std::int16_t StormEffect = 0x108;
    constexpr glm::vec3 White{1.0f, 1.0f,
                              1.0f}; // 0x4fd81e; the creation overrides the one of 0x4fdb3f anyway (0x51bd37)
    const auto cell = SkillCellPoint(actor, context, 0x4ffd1e);
    if (!cell.HasValue())
        return Core::Unexpected(cell.Error());
    const float height = cell.Value().z;
    for (int round = 0; round < Rounds; ++round)
    {
        glm::vec3 point = cell.Value();
        point.z =
            static_cast<float>(static_cast<double>(context.random.Next() % Rise * round) + static_cast<double>(height));
        const Kernel::ModelEffectArguments arguments{.type = StormEffect,
                                                     .position = point,
                                                     .angles = actor.angles,
                                                     .light = White,
                                                     .subtype = 0,
                                                     .owner = nullptr,
                                                     .key = static_cast<std::uint16_t>(round + 1)};
        if (auto made = Combat::CreateBlastEffect(arguments, nullptr, context); !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}

// 0x4fe190..0x4fe24e (skill 0x44, subtype 0) and 0x4fe442..0x4fe500 (skill 0x47, subtype 1): with a target that is not
// o, the joint 0x530860(0x632, p, p, the target's angles, subtype, target, 120, −1, 0, 0, −1) with p the target's
// position with z + 1200 (0x6ba198, on the x87 and stored as a float), which falls on it (StormJoints.cpp), and sound
// 0x267, whether it was created or not.
Core::Expected<void, std::string> BindTheTarget(Kernel::ActorState& actor, std::size_t slot,
                                                const Actors::CharacterStepContext& context, std::uint32_t read,
                                                std::uint32_t joint, std::int32_t subtype)
{
    const auto target = TargetOf(actor, slot, context, read);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr || target.Value() == &actor)
        return {};
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", joint));
    Kernel::ActorState& bound = *target.Value();
    constexpr double Height = 1200.0; // 0x6ba198
    constexpr float Width = 120.0f;   // 0x42f00000
    glm::vec3 from{bound.position.x, bound.position.y, F32(D(bound.position.z) + Height)};
    const Kernel::JointRequest request{.type = Joints::JointFamilies::StormJoint,
                                       .firstPosition = from,
                                       .secondPosition = from,
                                       .angles = bound.angles,
                                       .subtype = subtype,
                                       .ownerContext = &bound,
                                       .scale = Width,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random, Kernel::JointOwnerKind::Actor);
        !created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", joint, created.Error()));
    constexpr Audio::SoundId BindSound{0x267};
    RequestTheSound(context, BindSound);
    return {};
}

// 0x4fe383..0x4fe43d, skill 0x46: with a target, 0x534f90(0x10f, target, 4) and (…, 9), the 0x61e/6 decal over it
// (0x518cf0) and sound 0x269.
Core::Expected<void, std::string> FreeTheTarget(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const Actors::CharacterStepContext& context)
{
    const auto target = TargetOf(actor, slot, context, 0x4fe399);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr)
        return {};
    if (context.joints.empty())
        return Core::Unexpected(std::format("0x4fe3ce: faltan los joints del juego (0x534f90) del lugar {}", slot));
    Actors::KillJointsOf(context.joints, FlameJoint, target.Value(), 4);
    Actors::KillJointsOf(context.joints, FlameJoint, target.Value(), 9);
    constexpr Audio::SoundId FreeSound{0x269};
    const Kernel::ActorState& freed = *target.Value();
    if (auto made = CreateHighEffect({.type = Effects::ColoredDecalEffect,
                                      .position = freed.position,
                                      .angles = freed.angles,
                                      .light = freed.light,
                                      .subtype = Effects::BindingDecalSubtype,
                                      .owner = target.Value()},
                                     skill, slot, context);
        !made.HasValue())
        return made;
    RequestTheSound(context, FreeSound);
    return {};
}

// 0x4fe986..0x4feab6 (skill 0x40) and 0x502993..0x502aee (skill 0x4b): with something other than 0xa21 in the right
// hand, the 0x117/0 at its bone (+0x2bd, 0x4ec110 with the offset at 0); with something other than 0xa19 or
// 0xe12..0x1011 in the left, the 0x117/1 at its own (+0x2dd); both with o's angles and the light (1, 0.6, 0.3) of 0x40
// or (1, 1, 1) of 0x4b. Then 0x40 plays a sound (0x1cf, with o as origin) and 0x4b creates the 0x138 over o
// (0x502ae6) and plays a sound (0x272).
Core::Expected<void, std::string> SwingTheHands(const Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const Actors::CharacterStepContext& context, std::uint32_t right,
                                                std::uint32_t left)
{
    constexpr std::int32_t PlainRight = 0xa21;
    constexpr std::int32_t PlainLeft = 0xa19;
    constexpr std::int32_t FirstShield = 0xe12;
    constexpr std::int32_t PastShields = 0x1012;
    constexpr std::uint8_t RaiseSkill = 0x40;
    constexpr std::int16_t DarkLordSkill = 0x117, ManaRune = 0x138;
    const glm::vec3 handLight =
        skill == RaiseSkill ? glm::vec3{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)}
                            : glm::vec3(1.0f);
    const auto swing = [&](std::size_t hand, std::uint32_t address) -> Core::Expected<void, std::string>
    {
        const glm::vec3 point = Effects::ActorBonePoint(actor, actor.hands[hand].bone, glm::vec3(0.0f), context.world);
        if (auto made = CreateHighEffect({.type = DarkLordSkill,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = handLight,
                                          .subtype = static_cast<std::int32_t>(hand)},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
        return {};
    };
    if (HandOf(actor, 0) != PlainRight)
    {
        if (auto swung = swing(0, right); !swung.HasValue())
            return swung;
    }
    const std::int32_t held = HandOf(actor, 1);
    if (held != PlainLeft && !(held >= FirstShield && held < PastShields))
    {
        if (auto swung = swing(1, left); !swung.HasValue())
            return swung;
    }
    if (skill != RaiseSkill)
    {
        if (auto made = CreateHighEffect(
                {.type = ManaRune, .position = actor.position, .angles = actor.angles, .light = actor.light}, skill,
                slot, context);
            !made.HasValue())
            return Core::Unexpected("0x502ae6: " + made.Error());
    }
    constexpr std::int32_t RaiseSound = 0x1cf;
    constexpr std::int32_t OtherSwingSound = 0x272;
    // 0x4feaae: skill 0x40 keeps o as origin.
    RequestTheSound(context, Audio::SoundId{skill == RaiseSkill ? RaiseSound : OtherSwingSound},
                    skill == RaiseSkill ? &actor : nullptr);
    return {};
}

// 0x5023e4..0x502433 (and 0x5024d9..0x502528): the distance in cells from o to another, with dx stored as a float, dy
// on the x87 stack (53 bits) times its float copy, the root (0x6a1920) over 100 (0x6b86b4) and the quotient stored as a
// float.
float CellsBetween(const Kernel::ActorState& from, const Kernel::ActorState& to)
{
    constexpr double CellSize = Terrain::CellSizeAsDouble;
    const auto dx = static_cast<float>(static_cast<double>(from.position.x) - static_cast<double>(to.position.x));
    const double dy = static_cast<double>(from.position.y) - static_cast<double>(to.position.y);
    const auto storedDy = static_cast<float>(dy);
    const double squared = dy * static_cast<double>(storedDy) + static_cast<double>(dx) * static_cast<double>(dx);
    return static_cast<float>(std::sqrt(squared) / CellSize);
}

// 0x4f2230(o): the subtype of skill 0x4c's joints by byte +0x31b (the pet's +0x1b, Fenrir's excellent that 0x4c879d,
// 0x513c60 and 0x5bc9db copy): with 1, 0; with 2, 2; with any other, 1.
std::int32_t FenrirShade(const Kernel::ActorState& actor)
{
    constexpr std::uint8_t RedFenrir = 1, BlueFenrir = 2;
    if (actor.pet.flags1b == RedFenrir)
        return 0;
    return actor.pet.flags1b == BlueFenrir ? 2 : 1;
}

// 0x502352..0x502949, skill 0x4c: sound 0x27d and the loop over the 400 slots until ten targets are gathered
// (0x502569): a living monster (+0x88 at 2) not in action 6, or a living player (+0x88 at 1) outside action 0xc3 that
// 0x5588d0 lets be attacked; for each, the distance and the reach of 0x4c without an owner (0x5526f0(0x4c, 0), with the
// rand() of its entry), and it enters if the reach is not smaller (FCOMP with C0). Then rand() % 360, unused before the
// cut. With +0x434 at −1 or from 400 up, nothing more; otherwise two turns for the target of +0x434 and two more for
// each gathered one (0x5025d7..0x50284e): the point of 0x4f5b10(o, 0, −140, 130), no rand(), the angles (rand() % 360,
// 0, rand() % 360) and two 0x14f joints towards the target's position, with it as owner: width 100 with the subtype of
// 0x4f2230(o) and 80 with that plus 3. At the end (0x502853..0x502944), six ownerless 0x62c of width 60 with the
// subtype of 0x4f2230(o) plus 0xb, from the point of 0x4f5b10(o, 0, rand() % 40 − 20 + 10, 130) towards itself, with
// the angles (rand() % 360, 0, rand() % 360). With the pool full, 0x530860 creates nothing and draws no rand().
Core::Expected<void, std::string> StrikeAround(const Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                               const Actors::CharacterStepContext& context)
{
    constexpr Audio::SoundId AroundSound{0x27d};
    constexpr std::size_t MostTargets = 10;
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t MonsterObject = 2;
    constexpr std::uint8_t MonsterFallAction = 6;
    constexpr std::uint8_t PlayerFallAction = 0xc3;
    RequestTheSound(context, AroundSound);
    const HeroSkillContext ranges{.world = context.world, .random = context.random, .skills = context.skills};
    std::size_t gathered = 0;
    // [ebp−0x214]: the gathered ones, in their order (0x50245e, 0x502553).
    std::array<Kernel::ActorState*, MostTargets> found{};
    for (Kernel::ActorState& other : context.world.Actors().first<PoolSlots>())
    {
        bool candidate = other.alive && other.byte88 == MonsterObject && other.animation.action != MonsterFallAction;
        if (!candidate && other.alive && other.byte88 == PlayerObject && other.animation.action != PlayerFallAction)
        {
            const auto attackable = Combat::IsAttackableActor(context.world, other, context.controlDown);
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            candidate = attackable.Value();
        }
        if (candidate)
        {
            const float cells = CellsBetween(actor, other);
            const auto range = SkillRangeOf(skill, ranges);
            if (!range.HasValue())
                return Core::Unexpected(std::format("0x50243d: {} (la 0x4c del lugar {})", range.Error(), slot));
            if (range.Value() >= cells)
                found[gathered++] = &other;
        }
        if (gathered >= MostTargets)
            break;
    }
    static_cast<void>(context.random.Next());
    const std::int32_t target = actor.skillTarget4c;
    if (target == NoTarget || target >= static_cast<std::int32_t>(PoolSlots))
        return {};
    if (target < 0)
    {
        return Core::Unexpected(
            std::format("0x5025b8: el blanco {} de +0x434 del lugar {} está fuera del pool", target, slot));
    }
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x5026a4: faltan los joints del frame (0x530860)"));
    const std::int32_t shade = FenrirShade(actor);
    const auto strike = [&](Kernel::ActorState& struck, std::uint32_t first,
                            std::uint32_t second) -> Core::Expected<void, std::string>
    {
        constexpr int Rounds = 2, Turn = 360;
        constexpr std::int32_t PlasmaJoint = 0x14f, Paler = 3;
        constexpr float Wide = 100.0f, Narrow = 80.0f; // 0x42c80000 and 0x42a00000
        for (int round = 0; round < Rounds; ++round)
        {
            glm::vec3 from = TurnedPoint(actor, {0.0f, -140.0f, 130.0f}); // 0xc30c0000 and 0x43020000
            const auto pitch = static_cast<float>(context.random.Next() % Turn);
            const auto yaw = static_cast<float>(context.random.Next() % Turn);
            glm::vec3 angles{pitch, 0.0f, yaw};
            glm::vec3 to = struck.position;
            for (const auto& [address, subtype, width] :
                 {std::tuple{first, shade, Wide}, std::tuple{second, shade + Paler, Narrow}})
            {
                const Kernel::JointRequest request{.type = PlasmaJoint,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = &struck,
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
                if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random,
                                                            Kernel::JointOwnerKind::Actor);
                    !created.HasValue())
                    return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
            }
        }
        return {};
    };
    if (auto struck = strike(context.world.Actors()[static_cast<std::size_t>(target)], 0x5026a4, 0x5026ed);
        !struck.HasValue())
        return struck;
    for (std::size_t index = 0; index < gathered; ++index)
        if (auto struck = strike(*found[index], 0x5027f0, 0x502841); !struck.HasValue())
            return struck;
    constexpr int Spirals = 6, Spread = 40, Shift = 20, Turn = 360;
    constexpr std::int32_t SpiralJoint = 0x62c, SpiralShade = 0xb;
    constexpr double Lift = 10.0;  // 0x6b87a8
    constexpr float Width = 60.0f; // 0x42700000
    for (int spiral = 0; spiral < Spirals; ++spiral)
    {
        const auto height = F32(static_cast<double>(context.random.Next() % Spread - Shift) + Lift);
        glm::vec3 from = TurnedPoint(actor, {0.0f, height, 130.0f});
        const auto pitch = static_cast<float>(context.random.Next() % Turn);
        const auto yaw = static_cast<float>(context.random.Next() % Turn);
        glm::vec3 angles{pitch, 0.0f, yaw};
        const Kernel::JointRequest request{.type = SpiralJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = shade + SpiralShade,
                                           .ownerContext = nullptr,
                                           .scale = Width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
            return Core::Unexpected(std::format("0x50293c: {}", created.Error()));
    }
    return {};
}

// 0x4feabb..0x4feb36, skill 0x3f: 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 2, o, −1, …) and (0xcc, …, 3, no owner, −1,
// …), and sound 0x1cb from o.
Core::Expected<void, std::string> RaiseTheRings(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const Actors::CharacterStepContext& context)
{
    constexpr Audio::SoundId RingSound{0x1cb};
    if (auto inner = CreateWizard(
            0x4feae8,
            Effects::WizardArguments(Effects::InnerCircleEffect, actor.position, actor.angles, actor.light, 2, &actor),
            skill, slot, context);
        !inner.HasValue())
        return inner;
    if (auto outer = CreateWizard(
            0x4feb1b,
            Effects::WizardArguments(Effects::OuterCircleEffect, actor.position, actor.angles, actor.light, 3), skill,
            slot, context);
        !outer.HasValue())
        return outer;
    RequestTheSound(context, RingSound, &actor);
    return {};
}

// 0x4fecb5..0x4fed2a, skill 0x0a (the Hellfire): 0x59fb80(0xa) and 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 0, o,
// o+0x8a, slot, 0, 0, 0), whose creation with the hero as owner counts in the own record and sends the targets within
// 300 (0x51aa97); then 0x518cf0(0xcc, …, 0, no owner, −1, …). The Kalima part and sound 0x77 stay in the case.
Core::Expected<void, std::string> LightTheHellfire(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                   const Actors::CharacterStepContext& context)
{
    const std::int32_t index = OwnSkillIndex(context, skill);
    Kernel::ModelEffectArguments inner =
        Effects::WizardArguments(Effects::InnerCircleEffect, actor.position, actor.angles, actor.light, 0, &actor,
                                 static_cast<std::uint16_t>(actor.word8a));
    inner.byte89 = static_cast<std::uint8_t>(index & 0xff);
    if (auto created = CreateWizard(0x4fecf3, inner, skill, slot, context); !created.HasValue())
        return created;
    return CreateWizard(
        0x4fed25, Effects::WizardArguments(Effects::OuterCircleEffect, actor.position, actor.angles, actor.light, 0),
        skill, slot, context);
}

// 0x4ff51c..0x4ff5f2, skill 0x26 (the Meteorite): the point of the skill's cell (+0x39c and +0x39d, plus 0.5f, times
// 100 on the x87, each axis stored as a float) with the height of 0x5f93c0, the local light at (0.8f, 0.5, 0.1f) and
// two 0x518cf0(0xc2, point, o+0x1c, light, 6, no owner, 0, …); then sound 0x8e and the hero's volley (0x4ff5f5).
// Without the terrain heights, MuRet cuts before the first.
Core::Expected<void, std::string> RainTheMeteors(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr double CellCentre = 0.5;                     // 0x6b882c
    constexpr double CellSize = Terrain::CellSizeAsDouble; // 0x6b86b4
    constexpr glm::vec3 MeteorLight{std::bit_cast<float>(0x3f4ccccdu), 0.5f, std::bit_cast<float>(0x3dcccccdu)};
    constexpr std::int32_t MeteorSubtype = 6;
    constexpr std::uint16_t MeteorKey = 0;
    constexpr Audio::SoundId MeteorSound{0x8e};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x4ff570: falta la altura del terreno (0x5f93c0) del Meteorito"));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    for (const std::uint32_t address : {0x4ff5b2u, 0x4ff5dcu})
    {
        if (auto created = CreateWizard(address,
                                        Effects::WizardArguments(Effects::FireEffect, point, actor.angles, MeteorLight,
                                                                 MeteorSubtype, nullptr, MeteorKey),
                                        skill, slot, context);
            !created.HasValue())
            return created;
    }
    RequestTheSound(context, MeteorSound);
    CountHeroVolley(actor, context);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheHeadSkill(Kernel::ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                   const Actors::CharacterStepContext& context, const glm::vec3& light)
{
    switch (skill)
    {
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        // 0x4fdef2..0x4fdf26: 0x518cf0(0x61e, o+0x10, o+0x1c, o+0xf4, 3, o, −1, 0, 0, 0, 0), and nothing more. Its step
        // drops CL11's 0x619/3 joints (Effects/CharacterEffectSteps.h, StepPetSummon). With the table full, nothing.
        static_cast<void>(context.world.Map().effects.CreatePetSummon(actor.position, actor.angles, actor.light, &actor,
                                                                      Effects::SummonDecalSubtype));
        return {};
    case 0x2a:
        return ShakeTheGround(actor, context);
    case 0x37:
    case 0x38:
        // 0x4fdfb3 and 0x4fdfe4: the weapon, and nothing more.
        KeepTheWeapon(actor);
        return {};
    case 0x30:
        return RingTheSpirits(actor, slot, skill, context,
                              {.subtype = 2,
                               .jointAddress = 0x4fe0e4,
                               .decalSubtype = Effects::PulsingDecalSubtype,
                               .light = actor.light,
                               .sound = 0x7f});
    case 0x43:
        return CallTheStorm(actor, context);
    case 0x44:
        return BindTheTarget(actor, slot, context, 0x4fe1a6, 0x4fe235, 0);
    case 0x45:
        return RingTheSpirits(actor, slot, skill, context,
                              {.subtype = 0x15,
                               .jointAddress = 0x4fe322,
                               .decalSubtype = Effects::CoolPulsingDecalSubtype,
                               .light = CaseLight(actor),
                               .sound = 0x268});
    case 0x46:
        return FreeTheTarget(actor, slot, skill, context);
    case 0x47:
        return BindTheTarget(actor, slot, context, 0x4fe458, 0x4fe4e7, 1);
    case 0x48:
        return RaiseTheAuras(actor, context);
    case 0x35:
        return SpinTheEnergy(actor, context);
    case 0x40:
        return SwingTheHands(actor, slot, skill, context, 0x4fea10, 0x4fea9b);
    case 0x3f:
        return RaiseTheRings(actor, slot, skill, context);
    case 0x41:
        return RaiseTheSpirals(actor, context);
    case 0x31:
    {
        // 0x4febdb..0x4fec2b: 0x518cf0(0x6ac, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a, [0x5767948], 0, 0, 0) with no
        // 0x59fb80 before (Effects::FireBreathEffect: its forty 0x617/1 joints) and sound 0x72.
        constexpr Audio::SoundId BreathSound = Sound::KnightSkill3Sound;
        if (auto made =
                CreateHighEffect({.type = Effects::FireBreathEffect,
                                  .position = actor.position,
                                  .angles = actor.angles,
                                  .light = actor.light,
                                  .owner = &actor,
                                  .key = static_cast<std::uint16_t>(actor.word8a),
                                  .byte89 = static_cast<std::uint8_t>(context.world.Runtime().lastShotSkill & 0xffu)},
                                 skill, slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, BreathSound);
        return {};
    }
    case 0x29:
    {
        // 0x4fec30: the weapon, 0x59fb80(0x29), the 0xf3 over o (0x4fec9a) and sound 0x73.
        KeepTheWeapon(actor);
        const std::int32_t index = OwnSkillIndex(context, skill);
        if (auto made = CreateOwnedBlast(0xf3, actor.position, actor, index, context); !made.HasValue())
            return made;
        RequestTheSound(context, Audio::SoundId{0x73});
        return {};
    }
    case 0x0a:
        if (auto lit = LightTheHellfire(actor, slot, skill, context); !lit.HasValue())
            return lit;
        // 0x4fed2d..0x4ff4a8: Y, X and 0x437860(x, y, 2, -1500); a null 0x723c48 pointer is a no-op, not a boundary.
        if (Maps::IsKalimaMap(context.world.Runtime().map))
        {
            const Actors::LogicalCell cell = Actors::ReadLogicalCell(actor, context.random);
            constexpr std::int32_t WaveRadius = 2, WaveAmplitude = -1500;
            if (auto* kalima = context.world.Map().kalima.get(); kalima != nullptr)
            {
                const auto disturbed = kalima->Disturb(cell.x, cell.y, WaveRadius, WaveAmplitude, context.random);
                if (!disturbed.HasValue())
                    return Core::Unexpected(disturbed.Error());
            }
        }
        RequestTheSound(context, Audio::SoundId{0x77});
        return {};
    case 0x28:
        // 0x4ff4c3: 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 1, o, −1, …).
        if (auto circle = CreateWizard(0x4ff4ef,
                                       Effects::WizardArguments(Effects::InnerCircleEffect, actor.position,
                                                                actor.angles, actor.light, 1, &actor),
                                       skill, slot, context);
            !circle.HasValue())
            return circle;
        context.world.Runtime().StopSound(Audio::SoundId{0x92}, true); // 0x4ff4fe: rebobina antes de pedir 0x93.
        RequestTheSound(context, Audio::SoundId{0x93});
        return {};
    case 0x26:
        return RainTheMeteors(actor, slot, skill, context);
    case 0x27:
        if (auto storm = DropTheIceStorm(actor, context); !storm.HasValue())
            return storm;
        CountHeroVolley(actor, context);
        RequestTheSound(context, Audio::SoundId{0x90});
        return {};
    case 0x05:
        return CastTheFlame(actor, context);
    case 0x08:
    {
        // 0x500c10..0x500c5c (the Twister): 0x59fb80(8) and 0x518cf0(0xcf, o+0x10, o+0x1c, the local light, 0, o,
        // o+0x8a, slot, 0, 0, 0); then sound 0x74.
        constexpr Audio::SoundId TwisterSound{0x74};
        const std::int32_t index = OwnSkillIndex(context, skill);
        Kernel::ModelEffectArguments storm =
            Effects::WizardArguments(Effects::StormEffect, actor.position, actor.angles, light, 0, &actor,
                                     static_cast<std::uint16_t>(actor.word8a));
        storm.byte89 = static_cast<std::uint8_t>(index & 0xff);
        if (auto created = CreateWizard(0x500c49, storm, skill, slot, context); !created.HasValue())
            return created;
        RequestTheSound(context, TwisterSound);
        return {};
    }
    case 0x0c:
    {
        // 0x500c64..0x500ccf: the point of 0x4f5b10(o, −20, −90, 100), no rand(), 0x59fb80(0xc) and 0x518cf0(0x5e8,
        // point, o+0x1c, the step's light, 0, o, o+0x8a, slot, 0, 0, 0): the ray and, with the hero as owner, its four
        // 0x517a20. Then sound 0x7a.
        constexpr std::int16_t LightningRay = 0x5e8;
        constexpr glm::vec3 RayOffset{-20.0f, -90.0f, 100.0f};
        const glm::vec3 point = Effects::PointAround(actor, RayOffset);
        const std::int32_t index = OwnSkillIndex(context, skill);
        if (auto made = CreateHighEffect({.type = LightningRay,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = CaseLight(actor),
                                          .owner = &actor,
                                          .key = static_cast<std::uint16_t>(actor.word8a),
                                          .byte89 = static_cast<std::uint8_t>(index)},
                                         skill, slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, Sound::AquaFlashSound);
        return {};
    }
    case 0x0d:
    {
        // 0x500cd4: the cell with its height, 0x59fb80(0xd) and two 0xf5 there with o as owner (0x500d7b and 0x500dba).
        const auto cell = SkillCellPoint(actor, context, 0x500d28);
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const std::int32_t index = OwnSkillIndex(context, skill);
        for (int blast = 0; blast < 2; ++blast)
        {
            if (auto made = CreateOwnedBlast(0xf5, cell.Value(), actor, index, context); !made.HasValue())
                return made;
        }
        CountHeroVolley(actor, context);
        return {};
    }
    case 0x0e:
        if (auto burst = Effects::ScatterCombatBurstAt(actor.position, context); !burst.HasValue())
            return burst;
        // 0x5014da: the 0xf6 over o, with o as owner.
        if (auto made = CreateOwnedBlast(0xf6, actor.position, actor, OwnSkillIndex(context, skill), context);
            !made.HasValue())
            return made;
        CountHeroVolley(actor, context);
        return {};
    case 0x09:
    {
        // 0x501bab..0x501ca4: four turns with 0x59fb80(9); in each, two 0x613/0 joints hung from o from 100 higher,
        // with angles (0, 0, 90 · i): the one of width 80 with key +0x8a, the skill's index and +0x1bd (0x501c67), and
        // the one of width 20 with key −1 (0x501c9c).
        constexpr int Turns = 4;
        constexpr float WideWidth = 80.0f, ThinWidth = 20.0f; // 0x42a00000 and 0x41a00000
        const glm::vec3 point{actor.position.x, actor.position.y,
                              static_cast<float>(static_cast<double>(actor.position.z) + 100.0)};
        for (int turn = 0; turn < Turns; ++turn)
        {
            const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(static_cast<double>(turn) * 90.0)};
            const std::int32_t index = OwnSkillIndex(context, skill);
            if (auto wide = CreateTheSpirit(actor, context,
                                            {.address = 0x501c67,
                                             .from = point,
                                             .to = actor.position,
                                             .angles = angles,
                                             .width = WideWidth,
                                             .key = actor.word8a,
                                             .skill = index,
                                             .serial = actor.magicKey});
                !wide.HasValue())
                return wide;
            if (auto thin = CreateTheSpirit(actor, context,
                                            {.address = 0x501c9c,
                                             .from = point,
                                             .to = actor.position,
                                             .angles = angles,
                                             .width = ThinWidth,
                                             .key = -1});
                !thin.HasValue())
                return thin;
        }
        CountHeroVolley(actor, context);
        RequestTheSound(context, Audio::SoundId{0x75});
        return {};
    }
    case 0x4c:
        return StrikeAround(actor, slot, skill, context);
    case 0x2c:
    {
        // 0x50294e..0x50298e: 0x518cf0(0x13b, o+0x10, o+0x1c, the step's light, 0, o, −1, 0, 0, 0, 0), the siege flame
        // over o, and sound 0x26c.
        constexpr std::int16_t SwordForce = 0x13b;
        constexpr Audio::SoundId ForceSound{0x26c};
        if (auto made = CreateHighEffect({.type = SwordForce,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = CaseLight(actor),
                                          .owner = &actor},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected("0x502975: " + made.Error());
        RequestTheSound(context, ForceSound);
        return {};
    }
    case 0x4b:
        return SwingTheHands(actor, slot, skill, context, 0x502a23, 0x502ab4);
    default:
        // 0x502aff, as case 0.
        return {};
    }
}

// 0x504d87..0x504f51, skill 0x3d: the point (80, 0, 20) of o's first bone (0x4ec110) and three 0x116/0 with the target
// as owner and angles (0, 0, o's z + 90), (0, 0, z) and (0, 0, z − 90); the "light" (0, 0, the target's ceiling
// (+0x138) / 1) of the first and (0, 0, that − ceiling / 2) of the other two, which the creation stores as the start
// (the target's point they go to). Then the 0x117/0 and 0x117/1 there with o's angles and the light (1, 0.6, 0.3). No
// rand() or sound.
Core::Expected<void, std::string> LaunchThePierParts(const Kernel::ActorState& actor, Kernel::ActorState& victim,
                                                     std::size_t slot, std::uint8_t skill,
                                                     const Actors::CharacterStepContext& context)
{
    constexpr glm::vec3 LaunchOffset{80.0f, 0.0f, 20.0f}; // 0x42a00000 and 0x41a00000
    constexpr std::size_t LaunchBone = 0;
    constexpr double Turn = 90.0, Half = 2.0, Whole = 1.0; // 0x6b8948, 0x6b8818 and 0x6b87d4
    constexpr std::int16_t PierPart = 0x116, DarkLordSkill = 0x117;
    const glm::vec3 point = Effects::ActorBonePoint(actor, LaunchBone, LaunchOffset, context.world);
    const double yaw = static_cast<double>(actor.angles.z);
    glm::vec3 aim{0.0f, 0.0f, static_cast<float>(static_cast<double>(victim.boundsTop) / Whole)};
    const auto launch = [&](float z, std::uint32_t address) -> Core::Expected<void, std::string>
    {
        if (auto made = CreateHighEffect(
                {.type = PierPart, .position = point, .angles = {0.0f, 0.0f, z}, .light = aim, .owner = &victim}, skill,
                slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
        return {};
    };
    if (auto made = launch(static_cast<float>(yaw + Turn), 0x504e40); !made.HasValue())
        return made;
    aim.z = static_cast<float>(static_cast<double>(aim.z) - static_cast<double>(victim.boundsTop) / Half);
    if (auto made = launch(actor.angles.z, 0x504e9c); !made.HasValue())
        return made;
    if (auto made = launch(static_cast<float>(yaw - Turn), 0x504ee0); !made.HasValue())
        return made;
    const glm::vec3 handLight{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    for (const std::int32_t subtype : {0, 1})
    {
        if (auto made = CreateHighEffect({.type = DarkLordSkill,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = handLight,
                                          .subtype = subtype},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", subtype == 0 ? 0x504f1fu : 0x504f49u, made.Error()));
    }
    return {};
}

Core::Expected<void, std::string> StepTheTargetSkill(Kernel::ActorState& actor, Kernel::ActorState& victim,
                                                     std::size_t slot, std::uint8_t skill,
                                                     const Actors::CharacterStepContext& context,
                                                     const glm::vec3& light)
{
    switch (skill)
    {
    case 0x18:
    {
        // 0x504379..0x5043aa: 0x53e2b0(c, o, 0, 0x59fb80(+0x38e), 1, 0), the volley with no target; continues in 0x34
        // (0x5043b2).
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = Effects::FireTheArrow(actor, nullptr, slot, index, 1, 0, context); !fired.HasValue())
            return fired;
    }
        [[fallthrough]];
    case 0x34:
    case 0x33:
    {
        // 0x5043b2..0x5043fb and 0x504403..0x50444b: the first read is the skill of 0x53e2b0 and the second goes to
        // 0x59fb80; 0x53e2b0(c, o, 0, slot, 0, skill).
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        return Effects::FireTheArrow(actor, nullptr, slot, index, 0, shot, context);
    }
    case 0x2e:
    {
        // 0x504453..0x5044b4: the same with the target, 0x53e2b0(c, o, target, slot, 0, skill), and sound 0x26e.
        constexpr Audio::SoundId BlastSound{0x26e};
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = Effects::FireTheArrow(actor, &victim, slot, index, 0, shot, context); !fired.HasValue())
            return fired;
        RequestTheSound(context, BlastSound);
        return {};
    }
    case 0x1a:
        // 0x5044b9..0x5044f1: 0x518cf0(0x61e, target+0x10, target+0x1c, target+0xf4, 1, target, −1, 0, 0, 0, 0), the
        // decal that drops the 0x619/1 towards its owner (Effects/CharacterEffectSteps.h). With the table full,
        // nothing.
        static_cast<void>(
            context.world.Map().effects.CreatePetSummon(victim.position, victim.angles, victim.light, &victim));
        return {};
    case 0x1b:
        // 0x5044fe: with +0x38d and without 0x4000 in o+0x78, the 0x61e/2 decal over the target.
        if (actor.skillSucceeded == 0 || (actor.flags78 & QuietFlag) != 0)
            return {};
        if (auto made = CreateHighEffect({.type = Effects::ColoredDecalEffect,
                                          .position = victim.position,
                                          .angles = victim.angles,
                                          .light = victim.light,
                                          .subtype = 2,
                                          .owner = &victim},
                                         skill, slot, context);
            !made.HasValue())
            return made;
        {
            constexpr std::uint32_t BoundFlag = 8;
            const bool alreadyBound = (victim.flags78 & BoundFlag) != 0;
            if (!alreadyBound)
                victim.flags78 |= BoundFlag;
            const bool hasBinding = std::ranges::any_of(context.joints,
                                                        [&victim](const auto& joint)
                                                        {
                                                            return joint.alive && joint.type == FlameJoint &&
                                                                   joint.ownerContext == &victim &&
                                                                   (joint.subtype == 4 || joint.subtype == 9);
                                                        });
            // 0x5045b7/0x50466e: the target of the five is c's +0x3a4, signed (movsx).
            if (!alreadyBound || !hasBinding)
                return CreateFiveAuras(alreadyBound ? 0x5046b0 : 0x5045f9, victim, 4,
                                       std::bit_cast<std::int16_t>(actor.attackTarget), context);
        }
        return {};
    case 0x1c:
    {
        // 0x5046bf..0x50473b: without 0x4000 in o+0x78, 0x518cf0(0x61e, target+0x10, target+0x1c, target+0xf4, 3,
        // target, −1, 0, 0, 0, 0), whose step drops CL11's 0x619/3 joints (StepPetSummon), and with c's +0x38d, 4 in
        // the target's +0x78. With the table full, no decal and the rest the same.
        constexpr std::uint32_t SummonedFlag = 4;
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        static_cast<void>(context.world.Map().effects.CreatePetSummon(victim.position, victim.angles, victim.light,
                                                                      &victim, Effects::SummonDecalSubtype));
        if (actor.skillSucceeded != 0)
            victim.flags78 |= SummonedFlag;
        return {};
    }
    case 0x07:
    {
        // 0x504740..0x504800 (the Ice): 0x518cf0(0xc1, target+0x10, o+0x1c, the local light, 0, no owner, −1, …) and
        // five 0xca ice shards (target+0x10, o+0x1c, o+0xf4; ModelEffects::CreateIceShard, five rand() each); with
        // +0x38d, 2 in the target's +0x78, and sound 0x78.
        constexpr int Shards = 5;
        if (auto created = CreateWizard(
                0x504768, Effects::WizardArguments(Effects::IceEffect, victim.position, actor.angles, light, 0), skill,
                slot, context);
            !created.HasValue())
            return created;
        for (int shard = 0; shard < Shards; ++shard)
            static_cast<void>(
                context.world.Map().effects.CreateIceShard(victim.position, actor.angles, actor.light, context.random));
        constexpr std::uint32_t FrozenFlag = 2;
        constexpr Audio::SoundId IceSound{0x78};
        if (actor.skillSucceeded != 0)
            victim.flags78 |= FrozenFlag;
        RequestTheSound(context, IceSound);
        return {};
    }
    case 0x10:
    {
        // 0x504808..0x5048e5: the 0x189 sets 0x100 in its own +0x78 and leaves; with 0x4000, nothing; otherwise 0x100
        // in the target's +0x78, sound 0x84, 0x534f90(0x10f, target, 0) and five 0x10f/0 joints.
        constexpr std::uint16_t SelfFlagType = 0x189;
        constexpr std::uint32_t ShieldFlag = 0x100;
        constexpr Audio::SoundId ShieldSound = Sound::SoulBarrierSound;
        if (actor.type == SelfFlagType)
        {
            actor.flags78 |= ShieldFlag;
            return {};
        }
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        victim.flags78 |= ShieldFlag;
        RequestTheSound(context, ShieldSound);
        if (context.joints.empty())
            return Core::Unexpected(std::format("0x504876: faltan los joints del juego (0x534f90) del lugar {}", slot));
        Actors::KillJointsOf(context.joints, FlameJoint, &victim, 0);
        return CreateFiveAuras(0x5048db, victim, 0, -1, context);
    }
    case 0x01:
    {
        // 0x5048f1..0x50492b: the player creates the 0xc3 (target+0x10, o+0x1c, o+0xf4, 0, no owner, −1, …); then ALL
        // types, with the local light at (0.4f, 0.6f, 1), try ten 0x5f2/1 particles (0x50497e).
        constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
        if (actor.type == PlayerType)
        {
            if (auto created = CreateWizard(
                    0x504926,
                    Effects::WizardArguments(Effects::PoisonEffect, victim.position, actor.angles, actor.light, 0),
                    skill, slot, context);
                !created.HasValue())
                return created;
        }
        // 0x50492e..0x504986: ten 0x53fe70(0x5f2, the target's o + 0x10, c + 0x1c, (0.4f, 0.6f, 1), 1, 1.0f, 0), the
        // poison smoke (0x541eb6, seven rand() each while there is room).
        constexpr std::int32_t PoisonSmoke = 0x5f2;
        constexpr std::int32_t PoisonSmokeSubtype = 1;
        constexpr int PoisonSmokeCount = 10;
        const glm::vec3 poisonLight{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f19999au), 1.0f};
        if (context.particles == nullptr)
            return Core::Unexpected(std::format("0x50497e: falta el pool de partículas del lugar {}", slot));
        for (int smoke = 0; smoke < PoisonSmokeCount; ++smoke)
        {
            const auto created =
                context.particles->CreateChecked(PoisonSmoke, victim.position, actor.angles, poisonLight,
                                                 PoisonSmokeSubtype, 1.0f, context.worldClock, context.random);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("0x50497e: la partícula 0x5f2/1 del lugar {} falla con error {}",
                                                    slot, static_cast<unsigned>(created.Error())));
            }
        }
        if (actor.skillSucceeded != 0)
            victim.flags78 |= 1;
        RequestTheSound(context, Audio::SoundId{0x22});
        return {};
    }
    case 0x02:
        // 0x5049bf..0x5049fd (the Meteor): 0x518cf0(0xc2, target+0x10, target+0x1c, o+0xf4, 0, no owner, −1, …) and
        // sound 0x42.
        if (auto created = CreateWizard(
                0x5049ef, Effects::WizardArguments(Effects::FireEffect, victim.position, victim.angles, actor.light, 0),
                skill, slot, context);
            !created.HasValue())
            return created;
        RequestTheSound(context, FireSound);
        return {};
    case 0x2d:
    {
        // 0x504a0a..0x504abf: three 0x518cf0(0x139, o+0x10, o+0x1c, o+0xf4, subtype, target, −1, 0, 0, 0, 0), with
        // subtypes 0, 1 and 2 (Effects::JavelinEffect, one rand() each), and sound 0x26d.
        constexpr Audio::SoundId JavelinSound{0x26d};
        constexpr std::array<std::int32_t, 3> Subtypes{0, 1, 2};
        constexpr std::array<std::uint32_t, 3> Addresses{0x504a3a, 0x504a72, 0x504aa9};
        for (std::size_t index = 0; index < Subtypes.size(); ++index)
        {
            if (auto made = CreateHighEffect({.type = Effects::JavelinEffect,
                                              .position = actor.position,
                                              .angles = actor.angles,
                                              .light = actor.light,
                                              .subtype = Subtypes[index],
                                              .owner = &victim},
                                             skill, slot, context);
                !made.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", Addresses[index], made.Error()));
        }
        RequestTheSound(context, JavelinSound);
        return {};
    }
    case 0x49:
        return RaiseTheInferno(actor, context);
    case 0x4a:
    {
        // 0x504b51..0x504b99: the 0x116/2 over o with the target as owner and sound 0x271.
        constexpr std::int16_t PierPart = 0x116;
        constexpr std::int32_t DivingSubtype = 2;
        constexpr Audio::SoundId DiveSound{0x271};
        if (auto made = CreateHighEffect({.type = PierPart,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = actor.light,
                                          .subtype = DivingSubtype,
                                          .owner = &victim},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected("0x504b80: " + made.Error());
        RequestTheSound(context, DiveSound);
        return {};
    }
    case 0x04:
        // 0x504b9e..0x504bdc (the Fire Ball): 0x518cf0(0xc2, o+0x10, the angles towards the target, o+0xf4, 1, the
        // target as owner, −1, …) and sound 0x42.
        if (auto created =
                CreateWizard(0x504bce,
                             Effects::WizardArguments(Effects::FireEffect, actor.position,
                                                      TowardsTheTarget(actor, victim), actor.light, 1, &victim),
                             skill, slot, context);
            !created.HasValue())
            return created;
        RequestTheSound(context, FireSound);
        return {};
    case 0x0b:
    {
        // 0x504be9..0x504cce (the Power Wave): 0x518cf0(0xce, o+0x10, the angles towards the target, o+0xf4, 0, no
        // owner, −1, …); the 0x164 first creates two more, with the heading 10 more and then 20 less (fld/fadd/fstp on
        // the local), and returns it with 10 more. Then sound 0x76.
        constexpr std::uint16_t TripleType = 0x164;
        constexpr double SideTurn = 10.0;  // 0x6b87a8
        constexpr double BothSides = 20.0; // 0x6b86d0
        constexpr Audio::SoundId WaveSound = Sound::MagicSound;
        glm::vec3 angles = TowardsTheTarget(actor, victim);
        const auto wave = [&](std::uint32_t address)
        {
            return CreateWizard(address,
                                Effects::WizardArguments(Effects::MagicEffect, actor.position, angles, actor.light, 0),
                                skill, slot, context);
        };
        if (actor.type == TripleType)
        {
            angles.z = static_cast<float>(static_cast<double>(angles.z) + SideTurn);
            if (auto created = wave(0x504c38); !created.HasValue())
                return created;
            angles.z = static_cast<float>(static_cast<double>(angles.z) - BothSides);
            if (auto created = wave(0x504c7c); !created.HasValue())
                return created;
            angles.z = static_cast<float>(static_cast<double>(angles.z) + SideTurn);
        }
        if (auto created = wave(0x504cc0); !created.HasValue())
            return created;
        RequestTheSound(context, WaveSound);
        return {};
    }
    case 0x3c:
    {
        // 0x504cdb..0x504d7a: two 0x518cf0(0x114, o+0x10, o+0x1c, o+0xf4, 1, 0, −1, 0, 0, 0, 0) (0x504d05 and
        // 0x504d37), one with the 0x115/0 (0x504d69) and then sound 0x1c8. Each 0x114/1 drops two 0x6e8/0 joints
        // (0x51c25f).
        constexpr std::uint16_t Wave = 0x114, Piercing = 0x115;
        constexpr std::int32_t SwirlSubtype = 1;
        constexpr Audio::SoundId WaveSound{0x1c8};
        for (int wave = 0; wave < 2; ++wave)
            if (auto made = CreateHighEffect({.type = Wave,
                                              .position = actor.position,
                                              .angles = actor.angles,
                                              .light = actor.light,
                                              .subtype = SwirlSubtype},
                                             skill, slot, context);
                !made.HasValue())
                return made;
        if (auto made = CreateHighEffect(
                {.type = Piercing, .position = actor.position, .angles = actor.angles, .light = actor.light}, skill,
                slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, WaveSound);
        return {};
    }
    case 0x3d:
        return LaunchThePierParts(actor, victim, slot, skill, context);
    case 0x11:
    {
        // 0x504f56..0x504fd0: the +0x376 of the tables of 0x50561a and 0x505672 that go to 0x504fd7, and 0x76, do
        // nothing; the others (the players, with 0xffff) create the 0xd7 (type 0x15e) or the 0x5ca.
        constexpr std::array<std::uint16_t, 31> QuietKinds{
            0x25, 0x2e, 0x3d, 0x42, 0x45, 0x46, 0x49, 0x4b, 0x4d, 0x57, 0x59, 0x5d, 0x5f, 0x63,  0x70, 0x74,
            0x76, 0x7a, 0x7c, 0x80, 0x82, 0x8d, 0x8f, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0x125, 0x12f};
        constexpr std::uint16_t RayType = 0x15e;
        if (std::ranges::find(QuietKinds, actor.word376) != QuietKinds.end())
            return {};
        if (actor.type == RayType)
        {
            // 0x504fdc..0x505022: 0x518cf0(0xd7, o+0x10, the angles towards the target, o+0xf4, 0, the target as owner,
            // −1, …), no sound.
            return CreateWizard(0x50501a,
                                Effects::WizardArguments(Effects::SnowEffect, actor.position,
                                                         TowardsTheTarget(actor, victim), actor.light, 0, &victim),
                                skill, slot, context);
        }
        // 0x505024..0x505061: 0x518cf0(0x5ca, o+0x10, the angles towards the target, o+0xf4, 0, the target as owner,
        // −1, …) (Effects/SkillBurstEffects) and sound 0x76, also with the table full.
        if (auto made = CreateHighEffect({.type = Effects::EnergyBallEffect,
                                          .position = actor.position,
                                          .angles = TowardsTheTarget(actor, victim),
                                          .light = actor.light,
                                          .subtype = 0,
                                          .owner = &victim},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("0x505053: {}", made.Error()));
        RequestTheSound(context, Sound::MagicSound);
        return {};
    }
    default:
        // 0x505069: case 3 and the others.
        return {};
    }
}

Core::Expected<void, std::string> SparkTheTarget(const Kernel::ActorState& actor, const Kernel::ActorState& victim,
                                                 std::size_t slot, const Actors::CharacterStepContext& context)
{
    // 0x52e740..0x52e804: 0x53fe70(0x5c6, the target's o + 0x10, its angles, (1, 1, 1), 0, 1.0f, 0), the hit spark
    // (0x54058e..0x540592: the /0 goes to 0x54304c with only the prefix); the (0, 50, 0) turned by the hitter's angles
    // (0x5fc8f0 and 0x5fcbd0) and added to the position is read by nobody. Then, twenty times two rand() and
    // 0x53fe70(0x5c5, the same position and angles, (1, 1, 1), 0, 1.0f, 0), even if the pool is full.
    constexpr std::int32_t HitSpark = 0x5c6;
    constexpr std::int32_t FairySpark = 0x5c5;
    constexpr int FairySparks = 20;
    if (context.particles == nullptr)
        return Core::Unexpected(std::format("0x52e77b: falta el pool de partículas del lugar {}", slot));
    const auto fail = [&](std::uint32_t address, std::int32_t type, Particles::ParticleError error)
    {
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/0 de 0x52e740 del lugar {} (acción {:#x}) falla "
                                            "con error {}",
                                            address, type, slot, actor.animation.action, static_cast<unsigned>(error)));
    };
    const auto spark = context.particles->CreateChecked(HitSpark, victim.position, victim.angles, glm::vec3(1.0f), 0,
                                                        1.0f, context.worldClock, context.random);
    if (!spark.HasValue())
        return fail(0x52e77b, HitSpark, spark.Error());
    for (int fairy = 0; fairy < FairySparks; ++fairy)
    {
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        const auto created = context.particles->CreateChecked(
            FairySpark, victim.position, victim.angles, glm::vec3(1.0f), 0, 1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return fail(0x52e7f3, FairySpark, created.Error());
    }
    return {};
}
} // namespace Client::Skills
