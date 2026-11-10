#include "Scenes/ActorMotion.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/CharacterPosition.h"
#include "GameLogic/MovePath.h"
#include "GameLogic/MovementSpeed.h"
#include "GameLogic/PathFinder.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorCooldownServices.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/WorldRuntime.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t PathlessType = 0x159;
constexpr std::uint16_t SinkingDeadType = 0x154;
constexpr float DirectionDegrees = 45.0f;

std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}

void RetryPath(ActorState& actor, Render::Models::CrtRandom& random, Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    if (actor.movementCooldown != 0 || actor.type == PathlessType)
    {
        return;
    }
    const auto x = ReadCell(actor.cellXAccesses, actor.cellX, random);
    // 0x508c84: la primera X distinta saltea la lectura comparativa de Y.
    if (x == actor.targetX && ReadCell(actor.cellYAccesses, actor.cellY, random) == actor.targetY)
    {
        return;
    }
    const auto startY = ReadCell(actor.cellYAccesses, actor.cellY, random);
    const auto startX = ReadCell(actor.cellXAccesses, actor.cellX, random);
    const auto path = GameLogic::PathFinder(walls).Find({startX, startY}, {actor.targetX, actor.targetY});
    if (path)
    {
        actor.path = *path;
        actor.movement = 1;
    }
}
} // namespace

bool MoveActorPath(ActorState& actor, std::uint8_t rotate, Render::Models::CrtRandom& random)
{
    std::uint32_t x = actor.cellX;
    std::uint32_t y = actor.cellY;
    std::optional<Render::Models::LogicalAccess> xAccess;
    std::optional<Render::Models::LogicalAccess> yAccess;
    const GameLogic::LogicalCellEffects effects{
        .onStore = [&](GameLogic::LogicalAxis axis, GameLogic::LogicalStoreStage stage, std::uint32_t& cell)
        {
            const bool before = stage == GameLogic::LogicalStoreStage::BeginningBefore ||
                                stage == GameLogic::LogicalStoreStage::ArrivalBefore;
            auto& access = axis == GameLogic::LogicalAxis::X ? xAccess : yAccess;
            auto& depth = axis == GameLogic::LogicalAxis::X ? actor.cellXAccesses : actor.cellYAccesses;
            if (before)
            {
                access.emplace(depth, random, Render::Models::SmallLogicalCloseRandoms);
                return;
            }
            auto& stored = axis == GameLogic::LogicalAxis::X ? actor.cellX : actor.cellY;
            stored = static_cast<std::uint8_t>(cell);
            access.reset();
        }};
    return GameLogic::MovePath({actor.path, actor.position, actor.angles.z, x, y}, rotate, effects);
}

Core::Expected<void, std::string> UpdateActorPosition(ActorState& actor, const ActorMotionContext& context)
{
    const auto speed = [&]()
    {
        const GameLogic::MovementSpeedInput input{.type = actor.type,
                                                  .baseSpeed = actor.baseMovementSpeed,
                                                  .item = actor.pet.model,
                                                  .wings = actor.wings.model,
                                                  .movementMode = actor.byte88,
                                                  .itemVariant = actor.pet.flags1b,
                                                  .safeZone = actor.safeZone,
                                                  .flags = actor.flags78,
                                                  .map = context.runtime.map};
        return static_cast<double>(GameLogic::UpdateMovementSpeed(actor.run, context.runtime.movementSpeed, input));
    };
    const bool updated = GameLogic::TryUpdateCharacterPosition(
        {actor.position, actor.angles, actor.movementPhase, std::cref(actor.type), std::cref(actor.pet.model),
         std::cref(actor.safeZone), std::cref(context.runtime.map)},
        speed, context.height);
    if (!updated)
    {
        return Core::Unexpected(std::string("0x508a80: falta la consulta de altura 0x5f93c0"));
    }
    return {};
}

Core::Expected<void, std::string> StepOtherActorMotion(ActorState& actor, const ActorWalkContext& walk,
                                                       const ActorMotionContext& motion,
                                                       Data::Terrain::TerrainLayer<std::uint16_t>& walls)
{
    if (actor.dying != 0)
    {
        if (actor.type != SinkingDeadType)
        {
            return {};
        }
        if (!motion.height)
        {
            return Core::Unexpected(std::string("0x508e89: falta la altura del personaje 0x154 que se muere"));
        }
        actor.position.z = static_cast<float>(motion.height(actor.position.x, actor.position.y));
        return {};
    }
    if (actor.movement == 0)
    {
        RetryPath(actor, motion.random, walls);
        return {};
    }
    const auto walked = SetActorWalk(actor, walk);
    if (!walked.HasValue())
    {
        return Core::Unexpected(walked.Error());
    }
    if (MoveActorPath(actor, 1, motion.random))
    {
        actor.movement = 0;
        const auto stopped = SetActorStop(actor, walk.stop);
        if (!stopped.HasValue())
        {
            return Core::Unexpected(stopped.Error());
        }
        actor.angles.z = static_cast<float>(static_cast<int>(actor.direction) - 1) * DirectionDegrees;
    }
    return UpdateActorPosition(actor, motion);
}
Core::Expected<void, std::string> StepActorCooldown(ActorState& actor, Render::Models::CrtRandom& random,
                                                    const ActorCooldownServices& services)
{
    if (actor.movementCooldown == 0)
    {
        return {};
    }
    --actor.movementCooldown;
    constexpr int EffectCount = 20;
    constexpr int CreatorChance = 10;
    constexpr std::int32_t FirstArrivalEffect = 0xc8;
    for (int effect = 0; effect < EffectCount; ++effect)
    {
        actor.light = glm::vec3(1.0f);
        const auto x = static_cast<float>(static_cast<double>(actor.position.x) + (random.Next() % 64 - 32));
        const auto y = static_cast<float>(static_cast<double>(actor.position.y) + (random.Next() % 64 - 32));
        const auto z = static_cast<float>(static_cast<double>(actor.position.z) + (random.Next() % 32 - 16));
        if (random.Next() % CreatorChance == 0)
        {
            if (!services.particle)
            {
                return Core::Unexpected(std::string("0x506a73: falta crear la partícula 0x5f3/1 del freno"));
            }
            const auto particle = services.particle({x, y, z}, actor);
            if (!particle.HasValue())
            {
                return Core::Unexpected(particle.Error());
            }
        }
        if (random.Next() % CreatorChance == 0)
        {
            const auto type = FirstArrivalEffect + (random.Next() & 1);
            if (!services.effect)
            {
                return Core::Unexpected(std::string("0x506ab7: falta crear el efecto 0xc8/0xc9 del freno"));
            }
            const auto arrival = services.effect(type, actor);
            if (!arrival.HasValue())
            {
                return Core::Unexpected(arrival.Error());
            }
        }
    }
    return {};
}
} // namespace Client::Scenes
