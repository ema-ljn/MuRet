#include "Scenes/MagicAttackCount.h"
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
#include "Scenes/DamageNumbers.h"
#include "Scenes/HandShine.h"
#include "Scenes/MonsterCreation.h"
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

namespace Client::Scenes
{
namespace
{
// 0x4cee16: 0x41ed10(0x76, 0, 0).
constexpr Audio::SoundId MagicAttackSound = Sound::MagicSound;
// 0x4cee1d: 0x4ea030(el que pega, 0x96).
constexpr std::uint8_t MagicAttackAction = 0x96;
// 0x5110d0 sin nadie con la clave: el lugar de después de los 400.
constexpr std::size_t NoActor = 400;
// 0x40b3f0: un rand() por el byte de +0x38e al cerrarse.
constexpr std::uint16_t Byte38eCloseRandoms = 1;
// 0x4cefe4: +0x381 en 1.
constexpr std::uint8_t SkillActive = 1;
// 0x4cf024..0x4cf035: rand() par.
constexpr int ShockOneIn = 2;
// 0x4cf05a..0x4cf07d: el color (1; 0,6; 0) (0x3f19999a), la escala 15 (0x41700000) y que sube.
constexpr glm::vec3 DamageLight(1.0f, 0.6f, 0.0f);
constexpr float DamageScale = 15.0f;
constexpr bool DamageRises = true;

// 0x4cf099..0x4cf17d: el registro propio abierto alrededor de la resta; un golpe de la vida o más la deja en 0.
void HitTheHero(Kernel::WorldState& world, std::uint16_t damage, Crt::CrtRandom& random)
{
    const Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    Kernel::OwnCharacterRecord& record = world.OwnCharacter();
    record.life = damage >= record.life ? std::uint16_t{0} : static_cast<std::uint16_t>(record.life - damage);
}
} // namespace

Core::Expected<void, std::string> ApplyMagicAttackCount(const Protocol::Actors::MagicAttackCount& attack,
                                                        Kernel::PlayerKey player, const WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    // 0x4cedec..0x4cedf7.
    const std::size_t casterSlot = Kernel::ActorWithKey(world, attack.key);
    runtime.lastAttacker = static_cast<std::int32_t>(casterSlot);
    Kernel::ActorState& caster = world.Actors()[casterSlot];
    // 0x4cee0f.
    if (auto raised = RaiseTheHandShine(world.Actors(), runtime.hero, casterSlot, world.Particles(), random);
        !raised.HasValue())
        return raised;
    runtime.RequestSound(MagicAttackSound);
    // 0x4cee23: 0x4ea030 lee la cuenta de acciones del modelo del que pega (+0x26 de 0x5769ad8): el de un monstruo o
    // un NPC, el que cargó su alta (el GS 0.97 también manda el 1A de un monstruo, user.cpp:11887). Sin nadie con la
    // clave, el 400 (Q-C1125): su tipo es memoria que MuRet no modela y la acción se descarta, con una línea de
    // depuración (con el viewport del GS, que no es el del cliente, pasa jugando); lo demás va al 400 como en el
    // original.
    if (casterSlot == NoActor)
        resources.log.Debug("se descarta la acción 0x96 del 1A: la clave {:#x} del que pega no está", attack.key);
    else if (auto set = Actors::SetMonsterAction(caster, MagicAttackAction, 0x4cee23, resources); !set.HasValue())
        return set;
    // 0x4cee28..0x4cefd7: pkt[5] en +0x38e, con la entrada de la tabla abierta.
    {
        const Models::LogicalAccess skill(caster.byte38eAccesses, random, Byte38eCloseRandoms);
        caster.byte38e = attack.magic;
    }
    caster.byte381 = SkillActive;
    // 0x4cefeb..0x4cf196: cada blanco, con su clave entera (ESI).
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
} // namespace Client::Scenes
