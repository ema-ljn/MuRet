#include "Combat/MagicAttackCount.h"
#include "Actors/ActionReply.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/PlayerKey.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorShock.h"
#include "Kernel/ActorState.h"
#include "Combat/DamageNumbers.h"
#include "Effects/HandShine.h"
#include "Monsters/MonsterCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Combat
{
namespace
{
// 0x4cee16: 0x41ed10(0x76, 0, 0).
constexpr Audio::SoundId MagicAttackSound = Sound::MagicSound;
// 0x4cee1d: 0x4ea030(striker, 0x96).
constexpr std::uint8_t MagicAttackAction = 0x96;
// 0x5110d0 when nobody has the key: the slot after the 400.
constexpr std::size_t NoActor = 400;
// 0x40b3f0: one rand() for the +0x38e byte when it closes.
constexpr std::uint16_t Byte38eCloseRandoms = 1;
// 0x4cefe4: +0x381 at 1.
constexpr std::uint8_t SkillActive = 1;
// 0x4cf024..0x4cf035: rand() par.
constexpr int ShockOneIn = 2;
// 0x4cf05a..0x4cf07d: the color (1; 0.6; 0) (0x3f19999a), scale 15 (0x41700000) and rising.
constexpr glm::vec3 DamageLight(1.0f, 0.6f, 0.0f);
constexpr float DamageScale = 15.0f;
constexpr bool DamageRises = true;

// 0x4cf099..0x4cf17d: the own record open around the subtraction; a hit of the life or more leaves it at 0.
void HitTheHero(Kernel::WorldState& world, std::uint16_t damage, Crt::CrtRandom& random)
{
    const Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    Kernel::OwnCharacterRecord& record = world.OwnCharacter();
    record.life = damage >= record.life ? std::uint16_t{0} : static_cast<std::uint16_t>(record.life - damage);
}
} // namespace

Core::Expected<void, std::string> ApplyMagicAttackCount(const Protocol::Actors::MagicAttackCount& attack,
                                                        Kernel::PlayerKey player,
                                                        const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    // 0x4cedec..0x4cedf7.
    const std::size_t casterSlot = Kernel::ActorWithKey(world, attack.key);
    runtime.lastAttacker = static_cast<std::int32_t>(casterSlot);
    Kernel::ActorState& caster = world.Actors()[casterSlot];
    // 0x4cee0f.
    if (auto raised = Effects::RaiseTheHandShine(world.Actors(), runtime.hero, casterSlot, world.Particles(), random);
        !raised.HasValue())
        return raised;
    runtime.RequestSound(MagicAttackSound);
    // 0x4cee23: 0x4ea030 reads the action count of the striker's model (+0x26 of 0x5769ad8): a monster's or an NPC's,
    // the one its creation loaded (GS 0.97 also sends a monster's 1A, user.cpp:11887). With nobody holding the key, the
    // 400 (Q-C1125): its type is memory MuRet does not model and the action is discarded, with a debug line (with the
    // GS's viewport, which is not the client's, it happens in play); the rest goes to the 400 as in the original.
    if (casterSlot == NoActor)
        resources.log.Debug("se descarta la acción 0x96 del 1A: la clave {:#x} del que pega no está", attack.key);
    else if (auto set = Actors::SetMonsterAction(caster, MagicAttackAction, 0x4cee23, resources); !set.HasValue())
        return set;
    // 0x4cee28..0x4cefd7: pkt[5] in +0x38e, with the table entry open.
    {
        const Models::LogicalAccess skill(caster.byte38eAccesses, random, Byte38eCloseRandoms);
        caster.byte38e = attack.magic;
    }
    caster.byte381 = SkillActive;
    // 0x4cefeb..0x4cf196: each target, with its whole key (ESI).
    for (const std::uint16_t key : attack.targets)
    {
        Kernel::ActorState& target = world.Actors()[Kernel::ActorWithKey(world, key)];
        if (random.Next() % ShockOneIn == 0)
        {
            if (auto shocked = Actors::SetActorShock(target, target.damage, resources); !shocked.HasValue())
                return Core::Unexpected(std::format("0x4cf042: {}", shocked.Error()));
        }
        if (target.damage == 0)
            continue;
        CreateDamageNumber(world, target.position, target.damage, DamageLight, DamageScale, DamageRises);
        if (key == player.Value())
            HitTheHero(world, target.damage, random);
    }
    return {};
}
} // namespace Client::Combat
