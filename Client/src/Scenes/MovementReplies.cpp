#include "Scenes/MovementReplies.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorShock.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DamageNumbers.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
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
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    cell = value;
}
Core::Expected<ActorStopContext, std::string> StopContext(const WorldEntryResources& resources)
{
    const auto* skeleton = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4c6790: falta Player.bmd para la acción del personaje"));
    return ActorStopContext{resources.world.Runtime(),
                            SceneId::Game,
                            *skeleton,
                            resources.items,
                            resources.cloth.random,
                            resources.world.Map().terrain.attributes ? &resources.world.Map().terrain.attributes->cells
                                                                     : nullptr};
}
Core::Expected<void, std::string> ApplyOtherWalk(ActorState& actor, const WorldEntryResources& resources)
{
    if (actor.movementCooldown != 0)
        return {};
    auto& random = resources.cloth.random;
    {
        const Render::Models::LogicalAccess y(actor.cellYAccesses, random, Render::Models::SmallLogicalCloseRandoms);
    }
    {
        const Render::Models::LogicalAccess x(actor.cellXAccesses, random, Render::Models::SmallLogicalCloseRandoms);
    }
    auto& attributes = resources.world.Map().terrain.attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4eac00: faltan las paredes vigentes para el D3 recibido"));
    GameLogic::PathFinder finder(attributes->cells);
    const auto path = finder.Find({actor.cellX, actor.cellY}, {actor.targetX, actor.targetY});
    actor.movement = path.has_value() ? 1 : 0;
    if (path)
    {
        actor.path = *path;
        return {};
    }
    const auto context = StopContext(resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    const auto stopped = SetActorStop(actor, context.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    if (stopped.Value().sound)
        resources.world.Runtime().lastSoundId = stopped.Value().sound;
    return {};
}
Core::Expected<void, std::string> ApplyJumpingWalk(ActorState& actor, const WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    {
        const Render::Models::LogicalAccess x(actor.cellXAccesses, random, Render::Models::SmallLogicalCloseRandoms);
        const Render::Models::LogicalAccess y(actor.cellYAccesses, random, Render::Models::SmallLogicalCloseRandoms);
        actor.cellX = actor.targetX;
        actor.cellY = actor.targetY;
        actor.angles.z = static_cast<float>(Render::Models::CreateAngle(actor.position.x, actor.position.y,
                                                                        static_cast<float>(actor.cellX) * CellSize,
                                                                        static_cast<float>(actor.cellY) * CellSize));
    }
    const auto context = StopContext(resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    // 0x4f37c0 → 0x4ea030 con el modelo del personaje: el 0x186 es un monstruo (el GS 0.97 manda su D3 desde
    // gObjMonster, que llama a CGMoveRecv), con su modelo en WorldState::FaunaResources (MonsterSkeletonOf).
    const auto walked = SetActorWalk(
        actor, {context.Value(), resources.world.ActionSpeeds(), false, MonsterSkeletonOf(resources, actor.type)});
    if (!walked.HasValue())
        return Core::Unexpected(walked.Error());
    if (walked.Value().sound)
        resources.world.Runtime().lastSoundId = walked.Value().sound;
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
    const Render::Models::LogicalAccess access(resources.world.Runtime().ownRecordDepth, resources.cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
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
Core::Expected<void, std::string> ApplyWalkReply(const Protocol::Movement::Walk& reply, std::uint16_t playerKey,
                                                 const WorldEntryResources& resources)
{
    ActorState* actor = FindActor(resources.world, reply.characterId);
    if (actor == nullptr || actor->dying != 0)
        return {};
    actor->direction = reply.direction;
    if (reply.characterId == playerKey)
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
Core::Expected<void, std::string> ApplyDamageReply(const Protocol::Movement::Damage& reply, std::uint16_t playerKey,
                                                   const WorldEntryResources& resources)
{
    ActorState* actor = FindActor(resources.world, reply.characterId);
    if (actor == nullptr)
        return {};
    const bool hero = reply.characterId == playerKey;
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
            if ((actor->flags78 & DefensiveEffectFlag) != 0 && actor->type == Data::Models::PlayerSkeletonModel)
                return Core::Unexpected(std::string("0x4cb32f: el efecto defensivo necesita el atacante seleccionado"));
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
