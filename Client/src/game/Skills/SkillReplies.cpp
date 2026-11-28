#include "Skills/SkillReplies.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Tables/SkillRecord.h"
#include "Protocol/Skills/Skill.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Actors/ActorActions.h"
#include "Combat/ActorCombat.h"
#include "Actors/ActorEffectFlags.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Combat/BlastEffectCalls.h"
#include "Combat/CombatActionSpeeds.h"
#include "Kernel/DrawTailState.h"
#include "Effects/HighModelEffects.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Effects/SkullEffects.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Ui/GameUiState.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Skills
{
namespace
{
// 0x5110d0 with nobody holding the key.
constexpr std::size_t NoActor = 400;
// +0x38e and +0x42e: one-byte entries of the protection table, one rand() when closed at 0 (0x40b3f0 and inline).
constexpr std::uint16_t ActorByteCloseRandoms = 1;
// The mounts of +0x318 (the item's model).
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
// 0x4cd3a5: the type that makes no sound with skill 3.
constexpr std::uint16_t SilentSkill3Type = 0x1a5;
// 0x4cd369: with +0x376 at 0x4d, the 0x6f does not sound.
constexpr std::uint16_t SilentWord376 = 0x4d;
// 0x53d8d0 and 0x53d930: the flash of the one leaving and of the one arriving.
constexpr std::int16_t SparkEffect = 0x5c6;

using Protocol::Skills::ActorEffect;
using Protocol::Skills::DurationMagic;
using Protocol::Skills::MagicAttack;
using Protocol::Skills::MagicCancel;
using Protocol::Skills::SkillList;
using Protocol::Skills::SkillTarget;
using Protocol::Skills::SummonLife;

bool Riding(const Kernel::ActorState& actor, std::uint16_t mount)
{
    return actor.pet.model == mount && actor.safeZone == 0;
}

bool RidingAPegasus(const Kernel::ActorState& actor)
{
    return actor.pet.model >= UniriaModel && actor.pet.model <= DarkHorseModel && actor.safeZone == 0;
}

bool IsPlayer(const Kernel::ActorState& actor)
{
    return actor.type == Tables::PlayerSkeletonModel;
}

// 0x4ea030 with the model of the character's type (+2): without it in MuRet, cuts.
Core::Expected<void, std::string> Act(Kernel::ActorState& actor, std::uint8_t action,
                                      const Scenes::WorldEntryResources& resources)
{
    const Models::Skeleton* skeleton = resources.models.SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: MuRet no tiene el modelo {:#x} del personaje", actor.type));
    Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

// 0x4f4a10 (Combat::SetActorAttack).
Core::Expected<void, std::string> AttackAction(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    return Combat::SetActorAttack(actor, Combat::CombatContextOf(resources));
}

// 0x4cd68e and 0x4cdd2e: 0x4f4a10, the frame at 0 and +0x381 at 1 (0x4cde62).
Core::Expected<void, std::string> AttackAndSettle(Kernel::ActorState& actor,
                                                  const Scenes::WorldEntryResources& resources)
{
    if (auto attacked = AttackAction(actor, resources); !attacked.HasValue())
        return attacked;
    actor.animation.frame = 0.0f;
    actor.byte381 = 1;
    return {};
}

// 0x41ed10(id, …): the caller dispatches the sound after checking the optional.
void Sound(const Scenes::WorldEntryResources& resources, Audio::SoundId id)
{
    resources.world.Runtime().RequestSound(id);
}

// 0x4f5290: on a player, the speeds (0x4f3f80) and the hit action by mount, class or a rand(); on another, 3 and 4
// alternating every three with +0x38f, which goes up.
Core::Expected<void, std::string> HitAction(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    if (!IsPlayer(actor))
    {
        const std::uint8_t action = actor.attackCount % 3 == 0 ? std::uint8_t{3} : std::uint8_t{4};
        if (auto acted = Act(actor, action, resources); !acted.HasValue())
            return acted;
        ++actor.attackCount;
        return {};
    }
    Combat::RefreshCombatActionSpeeds(resources);
    if (Riding(actor, UniriaModel) || Riding(actor, DinorantModel))
        return Act(actor, 0x97, resources);
    if (Riding(actor, FenrirModel))
        return Act(actor, 0x5d, resources);
    if (Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf)
        return Act(actor, 0x92, resources);
    const int bit = resources.cloth.random.Next() & 1;
    return Act(actor, static_cast<std::uint8_t>(0x8e + bit), resources);
}

// The actions of 0x4cc110 and 0x4cc1d0, by hands: the left (+0x2d8) with a staff, or the right (+0x2b8) with another
// weapon, and the winged elf (+0x2f8) with the second of each pair.
struct HandActions
{
    std::uint8_t left = 0;
    std::uint8_t leftWinged = 0;
    std::uint8_t right = 0;
    std::uint8_t rightWinged = 0;
};
constexpr HandActions StaffHands{0x2e, 0x30, 0x2f, 0x31};  // 0x4cc110
constexpr HandActions RepeatHands{0x7e, 0x80, 0x7f, 0x81}; // 0x4cc1d0

Core::Expected<void, std::string> ActByTheHands(Kernel::ActorState& actor, const HandActions& actions,
                                                const Scenes::WorldEntryResources& resources)
{
    // The signed WORD CMPs: the 0xffff with no item is −1.
    const auto left = static_cast<std::int16_t>(actor.hands[1].model);
    const auto right = static_cast<std::int16_t>(actor.hands[0].model);
    const bool winged = Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf &&
                        actor.wings.model != Kernel::NoItemModel;
    if ((left >= 0xa12 && left < 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27)
        return Act(actor, winged ? actions.leftWinged : actions.left, resources);
    if ((right >= 0xa1a && right < 0xa21) || right == 0xa22 || (right >= 0xa24 && right < 0xc12))
        return Act(actor, winged ? actions.rightWinged : actions.right, resources);
    return {};
}

// 0x4e9c50(by, on): the action of `on` by the hands of `by`: both 0x63, the right 0x64, the left 0x65 and none 0x62.
Core::Expected<void, std::string> ActByBothHands(const Kernel::ActorState& by, Kernel::ActorState& on,
                                                 const Scenes::WorldEntryResources& resources)
{
    const bool right = by.hands[0].model != Kernel::NoItemModel;
    const bool left = by.hands[1].model != Kernel::NoItemModel;
    if (right && left)
        return Act(on, 0x63, resources);
    if (right)
        return Act(on, 0x64, resources);
    if (left)
        return Act(on, 0x65, resources);
    return Act(on, 0x62, resources);
}

// 0x53d8d0 and 0x53d930: the one leaving (alpha to 0, +0x80 at 1) and the one arriving (frame 5, +0x80 at 3, alpha to
// 1), with the speeds, action 0x93, the 0x5c6 flash and sound 0x76.
Core::Expected<void, std::string> Vanish(Kernel::ActorState& actor, bool arriving,
                                         const Scenes::WorldEntryResources& resources)
{
    Combat::RefreshCombatActionSpeeds(resources);
    if (auto acted = Act(actor, 0x93, resources); !acted.HasValue())
        return acted;
    if (arriving)
    {
        actor.animation.frame = 5.0f;
        actor.byte80 = 3;
        actor.alphaTarget = 1.0f;
    }
    else
    {
        actor.alphaTarget = 0.0f;
        actor.byte80 = 1;
    }
    static_cast<void>(
        resources.world.Map().effects.Create(SparkEffect, actor.position, actor.angles, actor.light, 0.0f));
    Sound(resources, Sound::MagicSound);
    return {};
}

// 0x4f2250 on `actor`, with the Fenrir sound it requests.
Core::Expected<void, std::string> Stop(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    const auto context = Scenes::TransitionStopContext(actor, resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    const auto stopped = Actors::SetActorStop(actor, context.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    return {};
}

// The action by mount of 0x4cc530's branches 0x43 to 0x48: Dark Horse, Uniria, Dinorant and Fenrir, outside the safe
// zone; otherwise, `otherwise`.
std::uint8_t MountedAction(const Kernel::ActorState& actor, std::uint8_t otherwise)
{
    if (Riding(actor, DarkHorseModel))
        return 0x52;
    if (Riding(actor, UniriaModel))
        return 0x40;
    if (Riding(actor, DinorantModel))
        return 0x41;
    if (Riding(actor, FenrirModel))
        return 0x5d;
    return otherwise;
}

// That of branches 0x4b and 0x40 (0x4cd81a and 0x4cd841): the Dark Horse, Fenrir or 0x8e.
std::uint8_t ChargeAction(const Kernel::ActorState& actor)
{
    if (Riding(actor, DarkHorseModel))
        return 0x52;
    if (Riding(actor, FenrirModel))
        return 0x5d;
    return 0x8e;
}

// That of branches 0x3c, 0x3d (0x4cd455) and 0x4a (0x4cd7b7): with a pegasus (in 0x4a, only the Dark Horse) 0x4e,
// with Fenrir 0x58 and otherwise 0x4c.
std::uint8_t ShoutAction(const Kernel::ActorState& actor, bool onlyTheDarkHorse)
{
    const bool pegasus = onlyTheDarkHorse ? Riding(actor, DarkHorseModel) : RidingAPegasus(actor);
    if (pegasus)
        return 0x4e;
    if (Riding(actor, FenrirModel))
        return 0x58;
    return 0x4c;
}

// A 0x4cc530 in progress: the source, the target and whether the source is the hero (0x743a170).
struct Cast
{
    const MagicAttack& attack;
    Kernel::ActorState& source;
    Kernel::ActorState& target;
    bool sourceIsHero;
    const Scenes::WorldEntryResources& resources;
};

// 0x4cde62: +0x381 at 1 and returns.
Core::Expected<void, std::string> Settle(const Cast& cast)
{
    cast.source.byte381 = 1;
    return {};
}

// 0x4cd381, branch 0x10: except on the hero, 0x4f5290 and the frame at 0.
Core::Expected<void, std::string> HitUnlessHero(const Cast& cast)
{
    if (!cast.sourceIsHero)
    {
        if (auto hit = HitAction(cast.source, cast.resources); !hit.HasValue())
            return hit;
        cast.source.animation.frame = 0.0f;
    }
    return Settle(cast);
}

// 0x4cd3e0, branches 1, 2, 4, 7, 0xb and 0x11: except on the hero, 0x4f5290 on a player and 0x4f4a10 on another.
Core::Expected<void, std::string> StrikeUnlessHero(const Cast& cast)
{
    if (cast.sourceIsHero)
        return Settle(cast);
    if (!IsPlayer(cast.source))
        return AttackAndSettle(cast.source, cast.resources);
    if (auto hit = HitAction(cast.source, cast.resources); !hit.HasValue())
        return hit;
    cast.source.animation.frame = 0.0f;
    return Settle(cast);
}

// An action, +0x381 at 1 and a sound, and returns (0x4cd4df, 0x4cd879 and the like).
Core::Expected<void, std::string> ActAndSound(const Cast& cast, std::uint8_t action, std::int32_t sound)
{
    if (auto acted = Act(cast.source, action, cast.resources); !acted.HasValue())
        return acted;
    cast.source.byte381 = 1;
    Sound(cast.resources, Audio::SoundId{sound});
    return {};
}

// An action and 0x4cde62.
Core::Expected<void, std::string> ActAndSettle(const Cast& cast, std::uint8_t action)
{
    if (auto acted = Act(cast.source, action, cast.resources); !acted.HasValue())
        return acted;
    return Settle(cast);
}

Core::Expected<void, std::string> CastTheSkill(const Cast& cast)
{
    Kernel::ActorState& source = cast.source;
    Kernel::ActorState& target = cast.target;
    const Scenes::WorldEntryResources& resources = cast.resources;
    Kernel::WorldState& world = resources.world;
    const std::uint8_t skill = cast.attack.skill;
    const bool succeeded = source.skillSucceeded != 0;
    switch (skill)
    {
    // 0x4cd316, 0x4cd34b and 0x4cd35b.
    case 0xc9:
        source.flags78 |= 0x100;
        source.byte381 = 1;
        return AttackAction(source, resources);
    case 0xca:
        source.byte381 = 1;
        source.flags78 |= 0x8;
        return AttackAction(source, resources);
    case 0xc8:
        return AttackAction(source, resources);
    // 0x4cd369: plays the sound and continues like 0x10.
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        if (source.word376 != SilentWord376)
            Sound(resources, Audio::SoundId{0x6f});
        return HitUnlessHero(cast);
    case 0x10:
        return HitUnlessHero(cast);
    // 0x4cd3a5: plays the sound, stops the target if it is in one of four actions and continues like 1.
    case 0x3:
    {
        if (source.type != SilentSkill3Type)
            Sound(resources, Audio::SoundId{0x50});
        const std::uint8_t action = target.animation.action;
        if (action == 0xcb || action == 0xcc || action == 0xc5 || action == 0xc7)
        {
            if (auto stopped = Stop(target, resources); !stopped.HasValue())
                return stopped;
        }
        return StrikeUnlessHero(cast);
    }
    case 0x1:
    case 0x2:
    case 0x4:
    case 0x7:
    case 0xb:
    case 0x11:
        return StrikeUnlessHero(cast);
    // 0x4cd410: the target leaves and the source arrives. Returns through 0x4cde69 (0x4cd442), skipping the +0x381 of
    // 0x4cde62.
    case 0xf:
    {
        if (auto gone = Vanish(target, false, resources); !gone.HasValue())
            return gone;
        if (auto came = Vanish(source, true, resources); !came.HasValue())
            return came;
        if (cast.sourceIsHero)
            world.Runtime().movement.gateRequestBlocked = 0;
        world.Runtime().RequestSound(Audio::SoundId{0x83}, source.position); // 0x4cd43a
        return {};
    }
    case 0x6:
        return Vanish(source, false, resources);
    case 0x3c:
        return ActAndSound(cast, ShoutAction(source, false), 0x70);
    case 0x3d:
        return ActAndSound(cast, ShoutAction(source, false), 0x1c9);
    case 0x4a:
        return ActAndSound(cast, ShoutAction(source, true), 0x1c9);
    // 0x4cd4df..0x4cd59f: the action is the skill + 0x25.
    case 0x13:
        return ActAndSound(cast, 0x38, 0x70);
    case 0x14:
        return ActAndSound(cast, 0x39, 0x71);
    case 0x15:
        return ActAndSound(cast, 0x3a, 0x72);
    case 0x16:
        return ActAndSound(cast, 0x3b, 0x73);
    case 0x17:
    {
        const std::uint8_t action = source.attackCount % 2 == 0 ? std::uint8_t{0x3c} : std::uint8_t{0x29};
        if (auto acted = Act(source, action, resources); !acted.HasValue())
            return acted;
        ++source.attackCount;
        source.byte381 = 1;
        Sound(resources, Audio::SoundId{0x73});
        return {};
    }
    case 0x38:
        return ActAndSound(cast, 0x8d, 0x73);
    case 0x39:
        return ActAndSound(cast, 0x84, 0x71);
    case 0x2c:
        return ActAndSound(cast, 0x85, 0x71);
    case 0x2f:
        return ActAndSettle(cast, source.pet.model == FenrirModel ? std::uint8_t{0x5f} : std::uint8_t{0x42});
    case 0x2b:
    {
        if (auto acted = Act(source, 0x43, resources); !acted.HasValue())
            return acted;
        if (!cast.sourceIsHero && IsPlayer(source))
            source.animation.frame = 0.0f;
        return Settle(cast);
    }
    case 0x18:
    case 0x34:
    case 0x36:
    {
        if (auto acted = ActByTheHands(source, StaffHands, resources); !acted.HasValue())
            return acted;
        return Settle(cast);
    }
    // 0x4cd617: with the skill succeeded, turns off the target's 0x628/0 and creates another (Effects::PutTheSkullOn;
    // with the table full, none). Then sound 0x85 on the target (0x4cd669..0x4cd671) and its 0x40 (0x4cd676..0x4cd67e).
    case 0x37:
        if (succeeded)
        {
            if (auto skull = Effects::PutTheSkullOn(target, world, resources.cloth.random, 0x4cd664); !skull.HasValue())
                return skull;
            world.Runtime().RequestSound(Audio::SoundId{0x85}, target.position);
            target.flags78 |= 0x40;
        }
        if (IsPlayer(source))
            return {};
        return AttackAndSettle(source, resources);
    // 0x4cd6a5: with the skill succeeded, the target's ice (0x4cd6bc..0x4cd730: turns off its 0xc1/1 and creates the
    // 0xc1/1 and the /2, Actors::WrapInIce; with the table full, nothing). Then (0x4cd733..0x4cd748): the target's
    // +0x378 at 0, 0x4f2250 on it and its 0x20.
    case 0x33:
    {
        if (auto acted = ActByTheHands(source, StaffHands, resources); !acted.HasValue())
            return acted;
        if (!succeeded)
            return Settle(cast);
        if (auto frozen = Actors::WrapInIce(target, resources, Actors::IceCalls{.first = 0x4cd6ff, .second = 0x4cd72b});
            !frozen.HasValue())
            return frozen;
        target.movement = 0;
        if (auto stopped = Stop(target, resources); !stopped.HasValue())
            return stopped;
        target.flags78 |= 0x20;
        return Settle(cast);
    }
    case 0x2d:
        if (!cast.sourceIsHero && IsPlayer(source))
        {
            if (auto hit = HitAction(source, resources); !hit.HasValue())
                return hit;
            source.animation.frame = 0.0f;
        }
        source.animation.frame = 0.0f;
        return Settle(cast);
    case 0x49:
    {
        if (auto acted = Act(source, 0x86, resources); !acted.HasValue())
            return acted;
        source.animation.frame = 0.0f;
        return Settle(cast);
    }
    case 0x2e:
    {
        if (auto acted = ActByTheHands(source, RepeatHands, resources); !acted.HasValue())
            return acted;
        return Settle(cast);
    }
    case 0x4b:
        target.flags78 |= 0x200;
        return ActAndSettle(cast, ChargeAction(source));
    case 0x43:
        return ActAndSettle(cast, MountedAction(source, 0x3f));
    case 0x44:
    {
        if (auto acted = Act(source, MountedAction(source, 0x87), resources); !acted.HasValue())
            return acted;
        if (succeeded)
            if (auto removed = Actors::RemoveActorFlag(0x1000, target, world, resources.effectHooks);
                !removed.HasValue())
                return removed;
        return Settle(cast);
    }
    case 0x46:
    {
        if (auto acted = Act(source, MountedAction(source, 0x3f), resources); !acted.HasValue())
            return acted;
        if (succeeded && (target.flags78 & 0x4000u) != 0x4000u && &source != &target)
            target.flags78 |= 0x4000;
        return Settle(cast);
    }
    case 0x47:
    {
        if (auto acted = Act(source, MountedAction(source, 0x87), resources); !acted.HasValue())
            return acted;
        if (succeeded)
            if (auto removed = Actors::RemoveActorFlag(0x4000, target, world, resources.effectHooks);
                !removed.HasValue())
                return removed;
        return Settle(cast);
    }
    // 0x4cdad7..0x4cdb7a: with the skill succeeded, 0x2000 in the source and 0x518cf0(0x138, its position, its angles,
    // its light, 0, no owner, −1, 0, 0, 0, 0): the mana rune (Effects/HighModelEffects, no rand()).
    case 0x45:
    {
        if (auto acted = Act(source, MountedAction(source, 0x3f), resources); !acted.HasValue())
            return acted;
        if (!succeeded)
            return Settle(cast);
        source.flags78 |= 0x2000;
        constexpr std::int16_t ManaRune = 0x138;
        const auto rune = Effects::CreateHighModelEffect(
            {.type = ManaRune, .position = source.position, .angles = source.angles, .light = source.light},
            {.world = world, .random = resources.cloth.random});
        if (!rune.HasValue())
            return Core::Unexpected(std::format("0x4cdb72: {}", rune.Error()));
        return Settle(cast);
    }
    case 0x48:
        if (succeeded)
        {
            for (const std::uint32_t mask : {0x4u, 0x8u, 0x10u, 0x100u, 0x800u, 0x2000u})
                if (auto removed = Actors::RemoveActorFlag(mask, target, world, resources.effectHooks);
                    !removed.HasValue())
                    return removed;
        }
        return ActAndSettle(cast, MountedAction(source, 0x3f));
    // 0x4cdc37..0x4cdc9c: without 0x400 in the target, turns off its 0x5ac/2 and its 0x619/9 joints and creates another
    // 0x5ac/2 with it as owner (0x4cdc88, Effects::ModelEffects::CreateSkillFlare; with the table full, none). Then
    // 0x400 and it continues.
    case 0x35:
        if ((target.flags78 & 0x400u) != 0x400u)
        {
            Effects::KillModelEffectsOf(world.Map().effects, Effects::IcarusFlareEffect, &target,
                                        Effects::GuardFlareSubtype);
            Actors::KillJointsOf(world.DrawTail().joints, 0x619, &target, 9);
            static_cast<void>(world.Map().effects.CreateSkillFlare(target.position, target.angles, target.light,
                                                                   Effects::GuardFlareSubtype, &target));
        }
        target.flags78 |= 0x400;
        return HitUnlessHero(cast);
    case 0x40:
        Combat::RefreshCombatActionSpeeds(resources);
        target.flags78 |= 0x800;
        return ActAndSettle(cast, ChargeAction(source));
    case 0x3f:
    {
        std::uint8_t action = 0x4d;
        if (RidingAPegasus(source))
            action = 0x4f;
        else if (Riding(source, FenrirModel))
            action = 0x5a;
        if (auto acted = Act(source, action, resources); !acted.HasValue())
            return acted;
        if (!IsPlayer(source))
            return AttackAndSettle(source, resources);
        return Settle(cast);
    }
    // 0x4cdd45: without 0x10 in the target, turns off its 0x5ac/1 and creates another with it as owner (0x4cdd84,
    // CreateSkillFlare; with the table full, none), whose step drops CL11's 0x5ac/4 particles (0x52a866,
    // StepSkillFlare). Then 0x10 and it continues.
    case 0x30:
        if ((target.flags78 & 0x10u) != 0x10u)
        {
            Effects::KillModelEffectsOf(world.Map().effects, Effects::IcarusFlareEffect, &target,
                                        Effects::SwellFlareSubtype);
            static_cast<void>(world.Map().effects.CreateSkillFlare(target.position, target.angles, target.light,
                                                                   Effects::SwellFlareSubtype, &target));
        }
        target.flags78 |= 0x10;
        return ActAndSettle(cast, 0x3f);
    // 0x4cdda4: on maps 8 and 10 (Atlans and the sky), 0x41.
    case 0x31:
    {
        const std::int32_t map = world.Runtime().map;
        return ActAndSettle(cast, map == 8 || map == 10 ? std::uint8_t{0x41} : std::uint8_t{0x40});
    }
    // 0x4cddd3..0x4cde03: 0x518cf0(0x112, +0x10, +0x1c, +0xf4, 0, no owner, −1, 0, 0, 0, 0) on the source, with its
    // sixty 0x5ac/0 joints (Effects/BlastModelEffects), and sound 0x94.
    case 0x3b:
    {
        constexpr std::int16_t ComboEffect = 0x112;
        const auto made = Combat::CreateBlastEffectOutsideTheStep({.type = ComboEffect,
                                                                   .position = source.position,
                                                                   .angles = source.angles,
                                                                   .light = source.light,
                                                                   .subtype = 0,
                                                                   .key = 0xffff},
                                                                  nullptr, world, resources.cloth.random);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x4cddf5: {}", made.Error()));
        Sound(resources, Audio::SoundId{0x94});
        return {};
    }
    case 0x3a:
    {
        if (auto acted = Act(source, 0x44, resources); !acted.HasValue())
            return acted;
        Sound(resources, Audio::SoundId{0x92});
        source.byte1bc = 0;
        return {};
    }
    // 0x4cde2f: on the hero, turns off the right button (0x4cde43..0x4cde58): the release (0x7d36840), the click
    // (0x7d36880), the down state (0x7d3685c) and the charge frames (0x7d368a4), so it is not released again.
    case 0x28:
    {
        if (auto acted = Act(source, 0x45, resources); !acted.HasValue())
            return acted;
        if (cast.sourceIsHero)
        {
            Ui::GameUiState& ui = world.Runtime().ui;
            ui.rightReleased = false;
            ui.rightClick = false;
            ui.rightDown = false;
            ui.rightHoldFrames = 0;
        }
        return Settle(cast);
    }
    case 0x32:
        return Settle(cast);
    default:
        return {};
    }
}

// 0x4ce000: another player's action by the duration skill (the table of 0x4ced44).
Core::Expected<void, std::string> DurationAction(Kernel::ActorState& actor, std::uint8_t skill,
                                                 const Scenes::WorldEntryResources& resources)
{
    switch (skill)
    {
    // 0x4cea88 and 0x4ceaad.
    case 0x18:
    case 0x34:
    case 0x36:
        return AttackAction(actor, resources);
    case 0xc:
        return Act(actor, 0x94, resources);
    case 0xa:
        return Act(actor, 0x96, resources);
    case 0xe:
        return Act(actor, 0x95, resources);
    case 0x41:
        return Act(actor, actor.pet.model == FenrirModel ? std::uint8_t{0x5b} : std::uint8_t{0x94}, resources);
    case 0x3e:
    {
        if (auto acted = Act(actor, 0x53, resources); !acted.HasValue())
            return acted;
        Sound(resources, Audio::SoundId{0x1ce});
        return {};
    }
    case 0x2a:
        return Act(actor, 0x3e, resources);
    case 0x2f:
        return Act(actor, actor.pet.model == FenrirModel ? std::uint8_t{0x5f} : std::uint8_t{0x42}, resources);
    case 0x29:
    case 0x37:
        return Act(actor, 0x3d, resources);
    case 0x38:
        return Act(actor, 0x8d, resources);
    case 0x2b:
        return Act(actor, 0x43, resources);
    case 0x43:
    case 0x46:
        return Act(actor, 0x3f, resources);
    case 0x4c:
        return ActByBothHands(actor, actor, resources);
    default:
        return HitAction(actor, resources);
    }
}

Core::Expected<void, std::string> LevelUpEffect(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    // 0x4dbfad..0x4dc004 and 0x4d319d..0x4d31f4: fifteen tries, the /0 decal and the sound even with the pools full.
    constexpr int Sparkles = 15;
    for (int sparkle = 0; sparkle < Sparkles; ++sparkle)
    {
        const auto created = Joints::CreateFaunaJoint(
            resources.world.DrawTail().joints,
            {0x60f, actor.position, actor.position, actor.angles, 0, &actor, 40.0f, 2, 0, 0, -1},
            resources.cloth.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x4dbfc5: {}", created.Error()));
    }
    static_cast<void>(resources.world.Map().effects.CreatePetSummon(actor.position, actor.angles, actor.light, &actor,
                                                                    Effects::LevelUpDecalSubtype));
    Sound(resources, Audio::SoundId{0x98});
    return {};
}
} // namespace

Core::Expected<void, std::string> ApplyMagicAttack(const MagicAttack& attack,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const std::size_t sourceIndex = Kernel::ActorWithKey(world, attack.source);
    const std::size_t targetIndex = Kernel::ActorWithKey(world, attack.target);
    if (targetIndex == NoActor)
        return {};
    runtime.lastAttacker = static_cast<std::int32_t>(sourceIndex);
    const auto actors = world.Actors();
    Kernel::ActorState& source = actors[sourceIndex];
    Kernel::ActorState& target = actors[targetIndex];
    const bool sourceIsHero = sourceIndex == runtime.hero;
    // 0x4ccea1: 0x3b goes straight to the table.
    if (attack.skill != 0x3b)
    {
        if (!sourceIsHero && attack.skill != 0x6 && attack.skill != 0xf && target.seen)
            source.angles.z = static_cast<float>(
                Math::CreateAngle(source.position.x, source.position.y, target.position.x, target.position.y));
        source.attackTarget = static_cast<std::uint16_t>(targetIndex);
        if (attack.skill == 0x4c)
            source.skillTarget4c = static_cast<std::int32_t>(targetIndex);
        source.skillSucceeded = attack.succeeded ? std::uint8_t{1} : std::uint8_t{0};
        // 0x4ccf07..0x4cd2ef: the skill in +0x38e, opened and closed inline.
        const Models::LogicalAccess access(source.byte38eAccesses, resources.cloth.random, ActorByteCloseRandoms);
        source.byte38e = attack.skill;
    }
    // 0x4cce50: without the source, the original continues through the skill's branch on slot 400, with whatever is
    // there (the model type of 0x4ea030, the mount, the hands). MuRet leaves the above in 400 and drops the branch (07,
    // Q-C1237).
    if (sourceIndex == NoActor)
    {
        resources.log.Debug("se descarta la rama de la skill {:#x} del 19: la fuente {:#x} no está", attack.skill,
                            attack.source);
        return {};
    }
    return CastTheSkill(Cast{attack, source, target, sourceIsHero, resources});
}

Core::Expected<void, std::string> ApplyDurationMagic(const DurationMagic& magic,
                                                     const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    const std::size_t index = Kernel::ActorWithKey(world, magic.key);
    Kernel::ActorState& actor = world.Actors()[index];
    {
        const Models::LogicalAccess access(actor.byte38eAccesses, resources.cloth.random, ActorByteCloseRandoms);
        actor.byte38e = magic.skill;
    }
    // 0x4cea3e..0x4cea4d: FILD, times 1/255 (0x6b88f8) and times 360 (0x6b869c), exact in double (Q-C1201).
    actor.angles.z = static_cast<float>(static_cast<double>(magic.direction) * static_cast<double>(1.0f / 255.0f) *
                                        static_cast<double>(360.0f));
    // 0x4ce76c: without the character, slot 400. Its action comes from the type there (0x4cea38); both branches set the
    // frame to 0, because 400 is never the hero. MuRet drops the action and keeps the frame (07, Q-C1204).
    if (index == NoActor)
    {
        resources.log.Debug("se descarta la acción de la skill {:#x} del 1E: la clave {:#x} no está", magic.skill,
                            magic.key);
        actor.animation.frame = 0.0f;
    }
    // 0x4cece8: other than a player, 0x4f4a10 and the frame at 0, the hero too.
    else if (!IsPlayer(actor))
    {
        if (auto attacked = AttackAction(actor, resources); !attacked.HasValue())
            return attacked;
        actor.animation.frame = 0.0f;
    }
    else if (index != world.Runtime().hero)
    {
        if (auto acted = DurationAction(actor, magic.skill, resources); !acted.HasValue())
            return acted;
        actor.animation.frame = 0.0f;
    }
    // 0x4cecfe..: the cell, +0x381, +0x3a4 and +0x380.
    actor.skillCellY = magic.y;
    actor.skillCellX = magic.x;
    actor.byte381 = 1;
    actor.attackTarget = 0xffff;
    actor.byte380 = 0;
    return {};
}

Core::Expected<void, std::string> ApplyMagicCancel(const MagicCancel& cancel,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::ActorState& actor = world.Actors()[Kernel::ActorWithKey(world, cancel.key)];
    std::uint32_t mask = 0;
    switch (cancel.skill)
    {
    case 0x1:
    case 0x26:
        mask = 0x1;
        break;
    case 0x7:
    case 0x27:
        mask = 0x2;
        break;
    case 0x1c:
        mask = 0x4;
        break;
    case 0x1b:
        mask = 0x8;
        break;
    case 0x30:
        mask = 0x10;
        break;
    case 0x33:
        mask = 0x20;
        break;
    case 0x37:
        mask = 0x40;
        break;
    case 0x10:
        mask = 0x100;
        break;
    case 0x4b:
        mask = 0x200;
        break;
    case 0x35:
        mask = 0x400;
        break;
    case 0x40:
        mask = 0x800;
        break;
    case 0x43:
        mask = 0x1000;
        break;
    case 0x45:
        mask = 0x2000;
        break;
    case 0x46:
        mask = 0x4000;
        break;
    // 0x4cbfc8 and 0x4cbfdd: 0x439d40 first.
    case 0xc9:
        Actors::SettleMonsterFlags(actor);
        mask = 0x8;
        break;
    case 0xca:
        Actors::SettleMonsterFlags(actor);
        mask = 0x100;
        break;
    default:
        return {};
    }
    return Actors::RemoveActorFlag(mask, actor, world, resources.effectHooks);
}

Core::Expected<void, std::string> ApplyActorState(const Protocol::Skills::ActorState& state,
                                                  const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::ActorState& actor = world.Actors()[Kernel::ActorWithKey(world, state.key)];
    const bool lit = (actor.flags78 & state.mask) == state.mask;
    if (state.type == 1)
    {
        // 0x4cbe47: without all of them, 0x4e6380(mask, character) (Actors::ApplyEffectFlags).
        if (!lit)
            return Actors::ApplyEffectFlags(actor, state.mask, &actor == &world.Hero(), resources);
        // 0x4cbe52..0x4cbe72: the NEG/SBB leaves the whole +0x7c at 0x200000 if it had it and at 0 if not (Q-C1202).
        constexpr std::uint32_t Sitting = 0x200000;
        actor.flags7c = (actor.flags7c & Sitting) == Sitting ? Sitting : 0u;
        return {};
    }
    if (lit)
        return Actors::RemoveActorFlag(state.mask, actor, world, resources.effectHooks);
    return {};
}

Core::Expected<void, std::string> ApplySkillTarget(const SkillTarget& target,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    const std::size_t sourceIndex = Kernel::ActorWithKey(world, target.source);
    const std::size_t targetIndex = Kernel::ActorWithKey(world, target.target);
    if (targetIndex == NoActor)
        return {};
    world.Runtime().lastAttacker = static_cast<std::int32_t>(sourceIndex);
    Kernel::ActorState& source = world.Actors()[sourceIndex];
    {
        const Models::LogicalAccess access(source.byte42eAccesses, resources.cloth.random, ActorByteCloseRandoms);
        source.byte42e = target.skill;
    }
    source.attackTarget = static_cast<std::uint16_t>(targetIndex);
    // 0x4cc2a9: SAR 0xf of the whole target; with the bit, 0x5110d0 did not find it and already returned.
    source.skillSucceeded = (target.target >> 15) != 0 ? std::uint8_t{1} : std::uint8_t{0};
    // 0x4cc503..0x4cc51c: 0x4f4a10, the frame at 0 and +0x381 at 1.
    if (target.skill < 1 || target.skill > 0xd)
        return {};
    // 0x4cc2b0: without the source, slot 400, whose 0x4f4a10 comes from the type there. MuRet drops it and keeps the
    // frame and +0x381 (07, Q-C1237).
    if (sourceIndex == NoActor)
    {
        resources.log.Debug("se descarta la acción de la skill {:#x} del 69: la fuente {:#x} no está", target.skill,
                            target.source);
        source.animation.frame = 0.0f;
        source.byte381 = 1;
        return {};
    }
    return AttackAndSettle(source, resources);
}

Core::Expected<void, std::string> ApplyActorEffect(const ActorEffect& effect,
                                                   const Scenes::WorldEntryResources& resources)
{
    const std::size_t index = Kernel::ActorWithKey(resources.world, effect.key);
    if (index >= NoActor)
        return {};
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t LevelUpKind = 0x10;
    if (resources.world.Actors()[index].byte88 != PlayerObject || effect.kind != LevelUpKind)
        return {};
    return LevelUpEffect(resources.world.Actors()[index], resources);
}

Core::Expected<void, std::string> ApplyLevelUp(const Protocol::Skills::LevelUp& reply,
                                               const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    {
        const Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random,
                                           Models::OwnRecordCloseRandoms);
        Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = reply.level;
        own.levelUpPoints = reply.levelUpPoints;
        own.life = reply.maxLife;
        own.maxLife = reply.maxLife;
        own.mana = reply.maxMana;
        own.maxMana = reply.maxMana;
        own.maxBp = reply.maxBp;
        own.addedPoints = reply.addedPoints;
        own.maxAddedPoints = reply.maxAddedPoints;
        own.word48 = reply.word48;
        own.word4a = reply.word4a;
        // 0x550ca0: DWORD arithmetic, overflow included.
        const std::uint32_t level = reply.level;
        own.nextExperience = 10u * (level + 9u) * level * level;
        if (level > 255u)
        {
            const std::uint32_t extra = level - 255u;
            own.nextExperience += 1000u * (extra + 9u) * extra * extra;
        }
    }
    return LevelUpEffect(world.Hero(), resources);
}

Core::Expected<void, std::string> ApplySkillList(const SkillList& list, const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    Crt::CrtRandom& random = resources.cloth.random;
    {
        const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        if (list.count == Protocol::Skills::SkillListRemoveOne)
            own.SetSkillAt(list.slot, 0);
        else if (list.count == Protocol::Skills::SkillListAddOne)
            own.SetSkillAt(list.slot, list.skill);
        // 0x4c52a6..0x4c52c2: the same slot `count` times.
        else if (list.list == Protocol::Skills::SkillListClearSlot)
        {
            if (list.count > 0)
                own.SetSkillAt(list.slot, 0);
        }
        else
        {
            // 0x4c535a..0x4c53b7: with the list at 0, all 256 at 0; then the entries and the Dark Lord's four.
            if (list.list == 0)
            {
                own.words6c.fill(0);
                own.skillsAc.fill(0);
            }
            for (const Protocol::Skills::SkillListEntry& entry : list.entries)
                own.SetSkillAt(entry.slot, entry.skill);
            if (Kernel::BaseClassOf(world.Hero().characterClass) == Kernel::CharacterClass::DarkLord)
            {
                for (std::uint8_t order = 0; order < 4; ++order)
                    own.SetSkillAt(0x78u + order, static_cast<std::uint8_t>(0x78 + order));
            }
        }
    }
    // 0x4c53f6..0x4c5595: open again, the counts and the hero's +0x425.
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    own.byte6a = 0;
    own.byte6b = 0;
    constexpr std::size_t CountedSlots = 0x40;
    for (std::size_t slot = 0; slot < CountedSlots; ++slot)
    {
        const std::uint8_t skill = Kernel::OwnSkillByte(own, slot);
        if (skill == 0)
            continue;
        ++own.byte6a;
        if (resources.skills == nullptr)
            return Core::Unexpected(std::string("0x4c54ed: el F3 11 lee Skill.bmd y MuRet no lo cargó"));
        const auto record = resources.skills->Record(skill);
        if (!record)
            return Core::Unexpected(std::format("0x4c54ed: Skill.bmd no tiene la skill {}", skill));
        std::uint8_t hidden = 0;
        {
            // 0x4c549a..0x4c54fa: opens the entry and closes it with 0x4e7c40.
            const Models::LogicalAccess entry(runtime.movement.skillDepths[skill], random,
                                              static_cast<std::uint16_t>(Tables::SkillRecordSize));
            constexpr std::size_t HiddenOffset = Tables::SkillRecordLayout::Origin;
            hidden = std::to_integer<std::uint8_t>((*record)[HiddenOffset]);
        }
        if (hidden == 1)
            ++own.byte6b;
    }
    Kernel::ActorState& hero = world.Hero();
    if (hero.skillIndex >= own.byte6a)
        hero.skillIndex = 0;
    if (own.byte6a == 1)
        hero.skillIndex = 0;
    if (Kernel::OwnSkillByte(own, hero.skillIndex) == 0)
        hero.skillIndex = 0;
    // 0x4c5557..0x4c5588: skips 0x43 to 0x48, up to 256 times, with the byte wrapping.
    constexpr int MaximumSkips = 0x100;
    for (int skipped = 0; skipped < MaximumSkips; ++skipped)
    {
        const std::uint8_t skill = Kernel::OwnSkillByte(own, hero.skillIndex);
        if (skill < 0x43 || skill > 0x48)
            break;
        hero.skillIndex = static_cast<std::uint8_t>(hero.skillIndex + 1);
    }
    return {};
}

void ApplySummonLife(const SummonLife& summon, const Scenes::WorldEntryResources& resources)
{
    resources.world.Runtime().hud.summonLife = summon.life;
}
} // namespace Client::Skills
