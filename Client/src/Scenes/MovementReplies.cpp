#include "Scenes/MovementReplies.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/PlayerKey.h"
#include "Tables/PlayerModels.h"
#include "Pathfinding/PathFinder.h"
#include "Protocol/Movement/Movement.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorShock.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/BlastEffectCalls.h"
#include "Scenes/CrywolfContract.h"
#include "Scenes/DamageNumbers.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SceneId.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t SearchActorCount = 400;
constexpr std::uint16_t JumpingActor = 0x186;
constexpr std::uint16_t NoRandomShock = 0x113;
constexpr float CellSize = 100.0f;
constexpr float NormalDamageScale = 15.0f;
constexpr float HighlightDamageScale = 50.0f;
constexpr float DuplicateHeight = 10.0f;
constexpr float DuplicateScale = 5.0f;
constexpr float FirstDuplicateLight = 0.4f;
constexpr float SecondDuplicateLight = 0.2f;
ActorState* FindActor(WorldState& world, std::uint16_t key)
{
    for (std::size_t slot = 0; slot < SearchActorCount; ++slot)
    {
        ActorState& actor = world.Actors()[slot];
        if (actor.alive && actor.key == key)
            return &actor;
    }
    return nullptr;
}
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    cell = value;
}
Core::Expected<ActorStopContext, std::string> StopContext(const ActorState& actor, const WorldEntryResources& resources)
{
    const auto* skeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModel);
    if (skeleton == nullptr && actor.type == Tables::PlayerSkeletonModel)
        return Core::Unexpected(std::string("0x4c6790: falta Player.bmd para la acción del personaje"));
    static const Models::Skeleton EmptySkeleton;
    const Models::Skeleton* playerSkeleton = skeleton != nullptr ? skeleton : &EmptySkeleton;
    const Models::Skeleton* actorSkeleton =
        actor.type == Tables::PlayerSkeletonModel ? nullptr : MonsterSkeletonOf(resources, actor.type);
    return ActorStopContext{.runtime = resources.world.Runtime(),
                            .scene = SceneId::Game,
                            .playerSkeleton = *playerSkeleton,
                            .items = resources.items,
                            .random = resources.cloth.random,
                            .terrainWalls = resources.world.Map().terrain.attributes
                                                ? &resources.world.Map().terrain.attributes->cells
                                                : nullptr,
                            .actorSkeleton = actorSkeleton,
                            .modelSounds = resources.world.FaunaResources().SoundActions(actor.type)};
}
Core::Expected<void, std::string> ApplyOtherWalk(ActorState& actor, const WorldEntryResources& resources)
{
    if (actor.movementCooldown != 0)
        return {};
    auto& random = resources.cloth.random;
    {
        const Models::LogicalAccess y(actor.cellYAccesses, random, Models::SmallLogicalCloseRandoms);
    }
    {
        const Models::LogicalAccess x(actor.cellXAccesses, random, Models::SmallLogicalCloseRandoms);
    }
    auto& attributes = resources.world.Map().terrain.attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4eac00: faltan las paredes vigentes para el D3 recibido"));
    Pathfinding::PathFinder finder(attributes->cells);
    // 0x4eac0a..0x4eac41: con un altar de Crywolf en 0x6c8e54, la bandera del A* también en el D3 de otro.
    const auto path =
        finder.Find({actor.cellX, actor.cellY}, {actor.targetX, actor.targetY}, CrywolfAltarGoal(resources.world));
    actor.movement = path.has_value() ? 1 : 0;
    if (path)
    {
        actor.path = *path;
        return {};
    }
    const auto context = StopContext(actor, resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    const auto stopped = SetActorStop(actor, context.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    return {};
}
Core::Expected<void, std::string> ApplyJumpingWalk(ActorState& actor, const WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    {
        const Models::LogicalAccess x(actor.cellXAccesses, random, Models::SmallLogicalCloseRandoms);
        const Models::LogicalAccess y(actor.cellYAccesses, random, Models::SmallLogicalCloseRandoms);
        actor.cellX = actor.targetX;
        actor.cellY = actor.targetY;
        actor.angles.z = static_cast<float>(Math::CreateAngle(actor.position.x, actor.position.y,
                                                              static_cast<float>(actor.cellX) * CellSize,
                                                              static_cast<float>(actor.cellY) * CellSize));
    }
    const auto context = StopContext(actor, resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    // 0x4f37c0 → 0x4ea030 con el modelo del personaje: el 0x186 es un monstruo (el GS 0.97 manda su D3 desde
    // gObjMonster, que llama a CGMoveRecv), con su modelo en WorldState::FaunaResources (MonsterSkeletonOf).
    const auto walked = SetActorWalk(actor, {context.Value(), resources.world.ActionSpeeds(), false,
                                             context.Value().actorSkeleton, context.Value().modelSounds});
    if (!walked.HasValue())
        return Core::Unexpected(walked.Error());
    actor.byte399 = 1;
    return {};
}
void CreateRisingDamage(WorldState& world, const glm::vec3& position, std::int32_t amount, const glm::vec3& light,
                        float scale)
{
    CreateDamageNumber(world, position, amount, light, scale, true);
}
void SubtractHeroLife(std::uint16_t amount, const WorldEntryResources& resources)
{
    const Models::LogicalAccess access(resources.world.Runtime().ownRecordDepth, resources.cloth.random,
                                       Models::OwnRecordCloseRandoms);
    auto& life = resources.world.OwnCharacter().life;
    life = amount >= life ? 0 : static_cast<std::uint16_t>(life - amount);
}
glm::vec3 DamageLight(std::uint8_t type, bool hero)
{
    switch (type)
    {
    case 0:
        return {1.0f, hero ? 0.0f : 0.6f, 0.0f};
    case 1:
        return {0.0f, 1.0f, 1.0f};
    case 2:
        return {0.0f, 1.0f, 0.6f};
    case 3:
        return {0.0f, 0.6f, 1.0f};
    case 4:
        return {1.0f, 0.0f, 1.0f};
    case 5:
        return {0.0f, 1.0f, 0.0f};
    default:
        return {1.0f, 1.0f, 1.0f};
    }
}
// 0x4cb30b..0x4cb3b0: el héroe 0x20a con +0x78 & 0x100 (el escudo de la skill del mago, 0x4e641a) que recibe un golpe
// sin sacudida. El personaje del lugar [0x5767964] (WorldRuntime::lastAttacker: el del último 18 con 0x78 o 0x79, o la
// fuente del último 19, 69 o 1A, con el 400, el lugar que sigue a los 400 de la tabla, si no estaba; 0 antes del
// primero) es el que pega: 0x4e93f0 da el rumbo desde él hasta el héroe y, si difiere del suyo (+0x24) en menos de 10
// (0x6b8d30, un double; también sin orden, con NaN), 0x518cf0(0x109, +0x10 del héroe, (0, 0, rumbo + 180), +0xf4, 0,
// héroe, −1, 0, 0, 0, 0): el escudo, que mira al que pega. El original no mira si ese lugar está vivo: lee el que haya.
// Sin rand(); con la tabla llena, nada.
Core::Expected<void, std::string> RaiseTheShield(ActorState& hero, const WorldEntryResources& resources)
{
    constexpr std::int16_t ShieldEffect = 0x109;
    constexpr double FacingSpan = 10.0; // 0x6b8d30
    constexpr double Behind = 180.0;    // 0x6b86bc
    WorldState& world = resources.world;
    const std::int32_t source = world.Runtime().lastAttacker;
    const auto actors = world.Actors();
    if (source < 0 || static_cast<std::size_t>(source) >= actors.size())
        return Core::Unexpected(
            std::format("0x4cb347: el lugar {} de [0x5767964] cae fuera de la tabla de personajes", source));
    const ActorState& striker = actors[static_cast<std::size_t>(source)];
    const double heading = Math::CreateAngle(striker.position.x, striker.position.y, hero.position.x, hero.position.y);
    // 0x4cb361..0x4cb376: fabs y fcomp con test ah,1: C0 también sin orden.
    if (std::fabs(heading - static_cast<double>(striker.angles.z)) >= FacingSpan)
        return {};
    const World::ModelEffectArguments shield{.type = ShieldEffect,
                                             .position = hero.position,
                                             .angles = {0.0f, 0.0f, static_cast<float>(heading + Behind)},
                                             .light = hero.light,
                                             .subtype = 0,
                                             .key = 0xffff};
    if (auto made = CreateBlastEffectOutsideTheStep(shield, &hero, world, resources.cloth.random); !made.HasValue())
        return Core::Unexpected(std::format("0x4cb3b0: {}", made.Error()));
    return {};
}
void CreateOrdinaryDamage(const Protocol::Movement::Damage& reply, ActorState& actor, bool hero,
                          const WorldEntryResources& resources)
{
    if (reply.amount == 0)
    {
        CreateRisingDamage(resources.world, actor.position, -1, glm::vec3(hero ? 1.0f : 0.5f), NormalDamageScale);
        return;
    }
    const glm::vec3 light = DamageLight(reply.type, hero);
    const float scale = (reply.flag || (reply.type >= 1 && reply.type <= 3)) ? HighlightDamageScale : NormalDamageScale;
    const int duplicates = (reply.flag || reply.doubleHit) ? 2 : 0;
    glm::vec3 position = actor.position;
    for (int index = 0; index < duplicates; ++index)
    {
        const float dim = index == 0 ? FirstDuplicateLight : SecondDuplicateLight;
        const float enlarged = index == 0 ? scale : scale + DuplicateScale;
        CreateRisingDamage(resources.world, position, reply.amount, light - glm::vec3(dim), enlarged);
        position.z = static_cast<float>(static_cast<double>(position.z) + DuplicateHeight);
    }
    // 0x4cb614 salta a 0x4cb6c1 y cae después en 0x4cb6c9: bit7 también emite el normal final.
    if (reply.flag)
        CreateRisingDamage(resources.world, position, reply.amount, light, scale + DuplicateHeight);
    CreateRisingDamage(resources.world, actor.position, reply.amount, light, scale);
}
} // namespace
Core::Expected<void, std::string> ApplyWalkReply(const Protocol::Movement::Walk& reply, Kernel::PlayerKey player,
                                                 const WorldEntryResources& resources)
{
    ActorState* actor = FindActor(resources.world, reply.characterId);
    if (actor == nullptr || actor->dying != 0)
        return {};
    actor->direction = reply.direction;
    if (reply.characterId == player.Value())
    {
        if (actor->movement != 0)
            return {};
        WriteCell(reply.cell.x, actor->cellX, actor->cellXAccesses, resources.cloth.random);
        WriteCell(reply.cell.y, actor->cellY, actor->cellYAccesses, resources.cloth.random);
        return {};
    }
    actor->targetX = reply.cell.x;
    actor->targetY = reply.cell.y;
    return actor->type == JumpingActor ? ApplyJumpingWalk(*actor, resources) : ApplyOtherWalk(*actor, resources);
}
Core::Expected<void, std::string> ApplyDamageReply(const Protocol::Movement::Damage& reply, Kernel::PlayerKey player,
                                                   const WorldEntryResources& resources)
{
    ActorState* actor = FindActor(resources.world, reply.characterId);
    if (actor == nullptr)
        return {};
    const bool hero = reply.characterId == player.Value();
    if (reply.shock)
    {
        if (auto shocked = SetActorShock(*actor, reply.amount, resources); !shocked.HasValue())
            return shocked;
        CreateRisingDamage(resources.world, actor->position, reply.amount, {1.0f, 0.0f, 0.0f}, NormalDamageScale);
        if (hero)
            SubtractHeroLife(reply.amount, resources);
    }
    else
    {
        if (hero)
        {
            SubtractHeroLife(reply.amount, resources);
            constexpr std::uint32_t DefensiveEffectFlag = 0x100;
            if ((actor->flags78 & DefensiveEffectFlag) != 0 && actor->type == Tables::PlayerSkeletonModel)
                if (auto shielded = RaiseTheShield(*actor, resources); !shielded.HasValue())
                    return shielded;
        }
        else if (actor->word376 != NoRandomShock && (resources.cloth.random.Next() & 1) == 0)
        {
            if (auto shocked = SetActorShock(*actor, reply.amount, resources); !shocked.HasValue())
                return shocked;
        }
        CreateOrdinaryDamage(reply, *actor, hero, resources);
    }
    actor->damage = reply.amount;
    return {};
}
} // namespace Client::Scenes
