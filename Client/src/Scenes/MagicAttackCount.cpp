#include "Scenes/MagicAttackCount.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorShock.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DamageNumbers.h"
#include "Scenes/HandShine.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

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
constexpr std::int32_t MagicAttackSound = 0x76;
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
void HitTheHero(WorldState& world, std::uint16_t damage, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random,
                                            Render::Models::OwnRecordCloseRandoms);
    OwnCharacterRecord& record = world.OwnCharacter();
    record.life = damage >= record.life ? std::uint16_t{0} : static_cast<std::uint16_t>(record.life - damage);
}
} // namespace

Core::Expected<void, std::string> ApplyMagicAttackCount(const Protocol::Viewport::MagicAttackCount& attack,
                                                        std::uint16_t playerKey, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    // 0x4cedec..0x4cedf7.
    const std::size_t casterSlot = ActorWithKey(world, attack.key);
    runtime.lastAttacker = static_cast<std::int32_t>(casterSlot);
    ActorState& caster = world.Actors()[casterSlot];
    // 0x4cee0f.
    if (auto raised = RaiseTheHandShine(world.Actors(), runtime.hero, casterSlot, world.Particles(), random);
        !raised.HasValue())
        return raised;
    runtime.lastSoundId = MagicAttackSound;
    // 0x4cee23: 0x4ea030 lee la cuenta de acciones del modelo del que pega (+0x26 de 0x5769ad8): el de un monstruo o
    // un NPC, el que cargó su alta (el GS 0.97 también manda el 1A de un monstruo, user.cpp:11887). Sin nadie con la
    // clave, el 400 (Q-C1125): su tipo es memoria que MuRet no modela y la acción se descarta con un aviso; lo demás
    // va al 400 como en el original.
    if (casterSlot == NoActor)
        resources.log.Debug("se descarta la acción 0x96 del 1A: la clave {:#x} del que pega no está", attack.key);
    else if (auto set = SetMonsterAction(caster, MagicAttackAction, 0x4cee23, resources); !set.HasValue())
        return set;
    // 0x4cee28..0x4cefd7: pkt[5] en +0x38e, con la entrada de la tabla abierta.
    {
        const Render::Models::LogicalAccess skill(caster.byte38eAccesses, random, Byte38eCloseRandoms);
        caster.byte38e = attack.magic;
    }
    caster.byte381 = SkillActive;
    // 0x4cefeb..0x4cf196: cada blanco, con su clave entera (ESI).
    for (const std::uint16_t key : attack.targets)
    {
        ActorState& target = world.Actors()[ActorWithKey(world, key)];
        if (random.Next() % ShockOneIn == 0)
        {
            if (auto shocked = SetActorShock(target, target.damage, resources); !shocked.HasValue())
                return Core::Unexpected(std::format("0x4cf042: {}", shocked.Error()));
        }
        if (target.damage == 0)
            continue;
        CreateDamageNumber(world, target.position, target.damage, DamageLight, DamageScale, DamageRises);
        if (key == playerKey)
            HitTheHero(world, target.damage, random);
    }
    return {};
}
} // namespace Client::Scenes
