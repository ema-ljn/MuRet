#include "Skills/AttackStep.h"
#include "CastleSiege/CastleSiegeAttack.h"
#include "CastleSiege/SiegeVertexEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Tables/PlayerModels.h"
#include "Maps/MapId.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrowEffects.h"
#include "Skills/AttackFrameSkills.h"
#include "Skills/AttackSkillReads.h"
#include "Skills/AttackSkillCases.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Combat/Attackable.h"
#include "Monsters/MonsterAttackStep.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Kernel/ItemState.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

// 0x4fdb9a..0x50539e with 0x4f97b0 and 0x4f5b70, checked against the PE c32bbd5f: 06, "El paso del ataque".
namespace Client::Skills
{
namespace
{
using Crt::CrtRandom;
using Models::LogicalAccess;

constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
// 0x4fdbf1: the value +0x381 returns to when the skill runs, and the 15 of 0x4f97b0 (0x4fb0df).
constexpr std::uint8_t AttackDone = 15;
constexpr std::int16_t NoTarget = -1;

// 0x5051f1: the second's cases (skill − 0x10, the indices of 0x505730), which go to 0x50534f without the sound.
constexpr std::array<std::uint8_t, 24> QuietSkills{0x10, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f, 0x20, 0x21,
                                                   0x22, 0x23, 0x24, 0x28, 0x2b, 0x2f, 0x30, 0x35,
                                                   0x3f, 0x40, 0x43, 0x44, 0x45, 0x46, 0x48, 0x4b};
// 0x502f00..0x502fb5 and 0x50380e..0x503898: the 0x20a's ranged actions (the bow and the crossbow on foot, with and
// without wings, on Uniria or Dinorant and on Fenrir).
constexpr std::array<std::uint8_t, 8> RangedActions{0x2e, 0x2f, 0x30, 0x31, 0x36, 0x37, 0x5e, 0x61};
// 0x50378c..0x5037be and 0x50406f..0x504092: the types that shoot with 0x53e2b0 (the 0x180 only without a target).
constexpr std::array<std::uint16_t, 3> MissileTypes{0x168, 0x175, 0x17a};
constexpr std::uint16_t MissileTypeWithoutTarget = 0x180;

template <typename T, std::size_t N> bool IsOneOf(const std::array<T, N>& values, T value)
{
    return std::ranges::find(values, value) != values.end();
}

// MOVSX: a signed WORD.
std::int32_t TypeOf(const Kernel::ItemState& item)
{
    return std::bit_cast<std::int16_t>(item.id);
}

// 0x4f97b0(c, o). 0x4f9780(o) (0x4f97bd: the 0x20a with 0x25 or 0x26) does not draw a rand() and only the cases read
// it.
Core::Expected<void, std::string> StepTheAttackFrames(Kernel::ActorState& actor, std::size_t slot,
                                                      const Actors::CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x4f97cf..0x4f987a: +0x38e, opened and closed inline.
    const std::uint8_t skill = Skills::ReadTheSkill(actor, context.random);
    // 0x4f987d.
    runtime.skillThreshold = AttackDone;
    // 0x4f9899: the skill cases (the table at 0x4fb10c), which return; the rest go on at 0x4fab5d.
    auto handled = Skills::StepTheFrameSkill(actor, skill, slot, context);
    if (!handled.HasValue())
        return Core::Unexpected(handled.Error());
    if (handled.Value())
        return {};
    // 0x4fab5d..0x4fab7f: past frame 1 (0x6b87d4), the 0x20a with 0x3e ends (FCOMP: NaN does not pass).
    constexpr float FirstFrame = 1.0f;
    constexpr float StrikeFrame = 5.0f;
    constexpr std::uint8_t ShortAction = 0x3e;
    const float frame = actor.animation.frame;
    const std::uint8_t action = actor.animation.action;
    if (frame >= FirstFrame && actor.type == PlayerType && action == ShortAction)
    {
        actor.byte381 = AttackDone;
        return {};
    }
    // 0x4fab85..0x4fabda: before frame 5 (0x6b87f8), or with NaN, it returns.
    if (!(frame >= StrikeFrame))
        return {};
    const bool player = actor.type == PlayerType && action >= 0x22 && action <= 0x97;
    const bool monster = actor.type >= 0x152 && actor.type < 0x1ca && action >= 3 && action <= 4;
    if (!player && !monster)
        return {};
    // 0x4fabe0..0x4fb08b: the own record, opened and closed inline (0x40b4d0's 3848 rand() if it ends at 0), to read
    // the left hand (+0xbb4, MOVSX, without the cursor item).
    std::int32_t left = 0;
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        left = TypeOf(context.world.OwnCharacter().equipment[1]);
    }
    // 0x4fb08c..0x4fb0da: the bow 0x815 in the own record's left hand, whoever strikes: twenty 0x53fe70(0x5c6, o+0x10,
    // o+0x1c, o+0xf4, 12, 2.0f, 0), with six rand() each (none with the pool full).
    constexpr std::int32_t SparkBow = 0x815;
    constexpr int BowSparks = 20;
    constexpr float BowSparkScale = 2.0f;
    if (actor.byte381 >= 1 && left == SparkBow && actor.type == PlayerType)
    {
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x4fb0d1: falta el pool de partículas de las veinte 0x5c6/12 del lugar {}", slot));
        for (int spark = 0; spark < BowSparks; ++spark)
        {
            const auto created = context.particles->CreateChecked(Particles::SparkParticle, actor.position,
                                                                  actor.angles, actor.light, Particles::BowSparkSubtype,
                                                                  BowSparkScale, context.worldClock, context.random);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x4fb0d1: la 0x5c6/12 del lugar {} falla con error {}", slot,
                                                    static_cast<unsigned>(created.Error())));
        }
    }
    actor.byte381 = AttackDone;
    return {};
}

// 0x4f5b70: the attack's work that runs while +0x381 rises.
Core::Expected<void, std::string> StepTheAttackHelpers(Kernel::ActorState& actor, std::size_t slot,
                                                       const Actors::CharacterStepContext& context)
{
    // 0x4f5b97: an unconditional rand() that nobody reads.
    static_cast<void>(context.random.Next());
    // The map helpers, the switch on +0x376 and the tail (MonsterAttackStep.h).
    return Monsters::StepTheAttackCases(actor, slot, context);
}

// 0x502b12..0x5037d5: +0x3a4 at −1.
Core::Expected<void, std::string> StrikeNothing(Kernel::ActorState& actor, std::size_t slot,
                                                const Actors::CharacterStepContext& context)
{
    // 0x502b12..0x502ef9: +0x38e; with 0x18, the flag (0x53e2b0's fifth argument) at 1.
    constexpr std::uint8_t FlaggedSkill = 0x18;
    const std::uint8_t flag = Skills::ReadTheSkill(actor, context.random) == FlaggedSkill ? 1 : 0;
    const bool shoots = actor.type == PlayerType ? IsOneOf(RangedActions, actor.animation.action) : actor.byte88 == 1;
    if (shoots)
    {
        // 0x502fbe..0x503760: two more reads; the first is 0x53e2b0's skill (0x5031e4) and the second goes to 0x59fb80
        // (0x50376a), which gives the slot. 0x50377d: 0x53e2b0(c, o, 0, slot, flag, skill).
        const std::uint8_t skill = Skills::ReadTheSkill(actor, context.random);
        const std::uint8_t second = Skills::ReadTheSkill(actor, context.random);
        const std::int32_t index = Skills::OwnSkillIndex(context, second);
        if (auto fired = Effects::FireTheArrow(actor, nullptr, slot, index, flag, skill, context); !fired.HasValue())
            return fired;
    }
    // 0x503785..0x5037d0: 0x53e2b0(c, o, 0, 0, 0, 0).
    if (IsOneOf(MissileTypes, actor.type) || actor.type == MissileTypeWithoutTarget)
        return Effects::FireTheArrow(actor, nullptr, slot, 0, 0, 0, context);
    return {};
}

// 0x505224..0x50534a: by the own record's hands (whoever strikes), or the cursor item in place of one.
void PlayTheStrikeSound(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x50522f and 0x50534a: 0x4e79a0 and 0x40f370, the 3848 rand() if the close leaves it at 0.
    const LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const auto& equipment = context.world.OwnCharacter().equipment;
    const Kernel::ItemState* right = &equipment[0];
    const Kernel::ItemState* left = &equipment[1];
    // 0x505258..0x5052ab: [0x7879fbc] > 0 (JLE) and [0x78ab9b8] == 0x78a9bb0; the slot [0x78a6758], 0 the right and 1
    // the left.
    if (runtime.cursorItemCount > 0 && runtime.cursorGrid == Kernel::CursorGrid::Inventory)
    {
        if (runtime.slot78a6758 == 0)
            right = &runtime.cursorItem;
        else if (runtime.slot78a6758 == 1)
            left = &runtime.cursorItem;
    }
    // 0x5052b5..0x505337: both from 0x800 to 0x9ff, 0x4b; otherwise 0x46; plus rand() & 3 (signed, 0x80000003).
    const auto tiro = [](std::int32_t type) { return type >= 0x800 && type < 0xa00; };
    const std::int32_t base = tiro(TypeOf(*right)) && tiro(TypeOf(*left)) ? 0x4b : 0x46;
    const auto sound = static_cast<std::int32_t>(context.random.Next() & 3) + base;
    // 0x505312/0x505337: the origin is o, even though the hands are the own record's.
    if (context.soundAt)
        context.soundAt(Audio::SoundId{sound}, actor.position);
    else if (context.soundRequest)
        context.soundRequest(Audio::SoundId{sound});
}

// 0x5040df..0x5041a4: ten times, three rand() & 63 (the target's x and y − 32 and its z + 0x5a, each sum from the fild
// in the x87) and 0x53fe70(0x5e4, point, o+0x1c, [EBP − 0x30], 0, 1.0f, 0). The 0x5e4's creation overwrites the local's
// light (0x542be9); with the pool full, the 0x5e4 does not draw its four rand() and the point's three are drawn anyway.
Core::Expected<void, std::string> BleedOnTheTarget(const Kernel::ActorState& actor, const Kernel::ActorState& victim,
                                                   std::int16_t target, const Actors::CharacterStepContext& context)
{
    constexpr int Drops = 10;
    constexpr int Spread = 63;
    constexpr int Center = 32;
    constexpr int Height = 0x5a;
    const auto add = [](int offset, float base)
    { return static_cast<float>(static_cast<double>(offset) + static_cast<double>(base)); };
    for (int drop = 0; drop < Drops; ++drop)
    {
        const int x = (context.random.Next() & Spread) - Center;
        const int y = (context.random.Next() & Spread) - Center;
        const int z = (context.random.Next() & Spread) + Height;
        const glm::vec3 point{add(x, victim.position.x), add(y, victim.position.y), add(z, victim.position.z)};
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x50419c: falta el pool de partículas de la sangre del blanco {}", target));
        const auto created =
            context.particles->CreateChecked(Particles::BloodParticle, point, actor.angles, glm::vec3(1.0f), 0, 1.0f,
                                             context.worldClock, context.random, 0);
        if (!created.HasValue())
        {
            return Core::Unexpected(std::format("0x50419c: la sangre 0x5e4 del blanco {} falla con error {}", target,
                                                static_cast<unsigned>(created.Error())));
        }
    }
    return {};
}

// 0x5037dd..0x50534a
Core::Expected<void, std::string> StrikeTheTarget(Kernel::ActorState& actor, std::size_t slot, std::int16_t target,
                                                  const Actors::CharacterStepContext& context, const glm::vec3& light)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    auto actors = context.world.Actors();
    // 0x5037e0..0x5037f2: the pool slot, without checking the range.
    if (target < 0 || static_cast<std::size_t>(target) >= actors.size())
    {
        return Core::Unexpected(
            std::format("0x5037e0: el blanco {} de +0x3a4 del lugar {} está fuera del pool", target, slot));
    }
    Kernel::ActorState& victim = actors[static_cast<std::size_t>(target)];
    if (actor.type == PlayerType && IsOneOf(RangedActions, actor.animation.action))
    {
        // 0x5038a1..0x504043: two reads of +0x38e; the first is 0x53e2b0's skill (0x503ac7) and the second goes to
        // 0x59fb80 (0x504048), which gives the slot. 0x504060: 0x53e2b0(c, o, the target, slot, 0, skill), and it goes
        // on.
        const std::uint8_t skill = Skills::ReadTheSkill(actor, context.random);
        const std::uint8_t second = Skills::ReadTheSkill(actor, context.random);
        const std::int32_t index = Skills::OwnSkillIndex(context, second);
        if (auto fired = Effects::FireTheArrow(actor, &victim, slot, index, 0, skill, context); !fired.HasValue())
            return fired;
    }
    // 0x504068..0x5040a9: 0x53e2b0(c, o, the target, 0, 0, 0).
    if (IsOneOf(MissileTypes, actor.type))
    {
        if (auto fired = Effects::FireTheArrow(actor, &victim, slot, 0, 0, 0, context); !fired.HasValue())
            return fired;
    }
    // 0x5040b9: +0x384, the damage of the last 0x15.
    if (victim.damage != 0)
    {
        constexpr std::uint16_t NoSparksType = 0x159;
        constexpr std::uint16_t SmokeType = 0x18e;
        if (victim.type != NoSparksType)
        {
            if (auto bled = BleedOnTheTarget(actor, victim, target, context); !bled.HasValue())
                return bled;
        }
        if (victim.type == SmokeType)
        {
            // 0x5041bf..0x50426f: five rand() & 1; with 0, the target's point with z at (z + 50) + rand() % 30 and
            // 0x518cf0(0x10b, point, o+0x1c, o+0xf4, 0, ownerless, −1, 0, 0, 0, 0) (0x504267, the stone; 0x51bafb).
            constexpr int Puffs = 5;
            constexpr int HeightSpan = 30;
            constexpr double Lift = 50.0; // 0x6b86b0
            for (int i = 0; i < Puffs; ++i)
            {
                if ((context.random.Next() & 1) != 0)
                    continue;
                const float lifted = static_cast<float>(static_cast<double>(victim.position.z) + Lift);
                const int rise = context.random.Next() % HeightSpan;
                const glm::vec3 point{victim.position.x, victim.position.y,
                                      static_cast<float>(static_cast<double>(rise) + static_cast<double>(lifted))};
                static_cast<void>(context.world.Map().effects.CreateDebris(Effects::DebrisEffect, point, actor.angles,
                                                                           actor.light, context.random));
            }
            // 0x50427d: 0x41ed10(0x88, 0, 0).
            constexpr Audio::SoundId SmokeSound = Sound::HitCristalSound;
            if (context.soundRequest)
                context.soundRequest(SmokeSound);
        }
        // 0x50428c: 0x42ea00(target).
        constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
        if (runtime.map == CastleMap && (victim.type == 0x19b || victim.type == 0x19c))
        {
            const auto heroFlags = context.world.Hero().flags78;
            if (auto hit = CastleSiege::EmitTheCastleAttackHit(
                    victim, heroFlags, context.random,
                    [&](const Kernel::ModelEffectArguments& arguments) -> Core::Expected<void, std::string>
                    {
                        auto& effects = context.world.Map().effects;
                        if (arguments.type == 0x114)
                            static_cast<void>(effects.CreateWaveEffect(arguments));
                        else if (arguments.type == Effects::DebrisEffect)
                            static_cast<void>(effects.CreateDebris(arguments.type, arguments.position, arguments.angles,
                                                                   arguments.light, context.random));
                        else
                            static_cast<void>(CastleSiege::CreateSiegeVertexEffect(arguments, effects, context.random));
                        return {};
                    });
                !hit.HasValue())
                return hit;
        }
    }
    // 0x50429f..0x5042ce: 0x52e740 (AttackSkillCases.h).
    if (actor.animation.action > 0x37 && actor.animation.action < 0x3d)
    {
        if (auto sparked = Skills::SparkTheTarget(actor, victim, slot, context); !sparked.HasValue())
            return sparked;
    }
    // 0x5042d3..0x50431c: the angles with 0x4e93f0's heading toward the target, which only the cases read (no rand();
    // StepTheTargetSkill builds them). 0x50432a..0x504372: +0x38e and its switch (AttackSkillCases.h).
    const std::uint8_t first = Skills::ReadTheSkill(actor, context.random);
    if (auto cased = Skills::StepTheTargetSkill(actor, victim, slot, first, context, light); !cased.HasValue())
        return cased;
    // 0x505069..0x505107: the target's point, the flag of 0x25 and 0x26 and the light, which the skill cases read. With
    // +0x380 at 2 (the hero's 16, it killed), 0x530860(0x618, target+0x10, target+0x10, o+0x1c, 0 and 1, o, 20.0f, −1,
    // 0, 0, −1) (0x505169, 0x5051a4), without rand().
    constexpr std::uint8_t KillerOnRecord = 2;
    if (actor.byte380 == KillerOnRecord)
    {
        if (context.joints.empty())
        {
            return Core::Unexpected(
                std::format("0x505169: faltan los joints de los 0x618 del lugar {} hacia el blanco {}", slot, target));
        }
        constexpr float KillBeamWidth = 20.0f;
        glm::vec3 from = victim.position;
        glm::vec3 to = victim.position;
        glm::vec3 angles = actor.angles;
        for (const std::int32_t subtype : {0, 1})
        {
            const Kernel::JointRequest request{.type = Joints::KillBeamJoint,
                                               .firstPosition = from,
                                               .secondPosition = to,
                                               .angles = angles,
                                               .subtype = subtype,
                                               .ownerContext = &actor,
                                               .scale = KillBeamWidth,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
            if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", subtype == 0 ? 0x505169 : 0x5051a4, created.Error()));
        }
    }
    // 0x5051bb
    const std::uint8_t second = Skills::ReadTheSkill(actor, context.random);
    if (IsOneOf(QuietSkills, second) || (actor.word376 >= 0x44 && actor.word376 <= 0x4b))
        return {};
    PlayTheStrikeSound(actor, context);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheAttack(Kernel::ActorState& actor, std::size_t slot,
                                                const Actors::CharacterStepContext& context, const glm::vec3& light)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x4fdb9a..0x4fdbd4.
    if (actor.byte381 != 0)
    {
        if (auto frames = StepTheAttackFrames(actor, slot, context); !frames.HasValue())
            return frames;
        if (auto helpers = StepTheAttackHelpers(actor, slot, context); !helpers.HasValue())
            return helpers;
        ++actor.byte381;
    }
    // 0x4fdbda..0x4fdbeb: JL, signed.
    if (static_cast<std::int32_t>(actor.byte381) < runtime.skillThreshold)
        return {};
    // 0x4fdbf1..0x4fdc10.
    actor.byte381 = 0;
    const auto key = Combat::ChosenKeyOf(context.world, slot, context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    actor.word8a = static_cast<std::int16_t>(key.Value());
    // 0x4fdc17..0x4fdeeb: +0x38e and the head's switch.
    const std::uint8_t skill = Skills::ReadTheSkill(actor, context.random);
    if (auto cased = Skills::StepTheHeadSkill(actor, slot, skill, context, light); !cased.HasValue())
        return cased;
    // 0x502aff..0x502b0c: +0x3a4 (MOVSX) at −1.
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    auto struck =
        target == NoTarget ? StrikeNothing(actor, slot, context) : StrikeTheTarget(actor, slot, target, context, light);
    if (!struck.HasValue())
        return struck;
    // 0x505356..0x505388: 0x40ae60, +0x38e at 0 and 0x40b0a0, a write through the table.
    {
        const LogicalAccess access(actor.byte38eAccesses, context.random, Skills::SkillCloseRandoms);
        actor.byte38e = 0;
    }
    // 0x50538b and 0x505397.
    actor.word382 = 0;
    actor.byte380 = 0;
    return {};
}
} // namespace Client::Skills
