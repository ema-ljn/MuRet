#include "Scenes/ActorMotion.h"

#include "Core/Expected.h"
#include "GameLogic/CharacterPosition.h"
#include "GameLogic/MovePath.h"
#include "GameLogic/MovementSpeed.h"
#include "GameLogic/Path.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
Core::Expected<bool, std::string> MoveActorAlongPath(ActorState& actor, WorldState& world, const SceneContext& context,
                                                     const GameData& data, const IModelLibrary& models,
                                                     std::uint8_t rotate)
{
    if (context.cloth == nullptr || context.camera == nullptr || world.Resources() == nullptr ||
        !world.Resources()->terrain.IsLoaded())
        return Core::Unexpected(std::string("faltan RNG, cámara o terreno para mover el actor"));
    if (actor.path.count > GameLogic::MaxPathCells || actor.path.progress > 3)
        return Core::Unexpected(std::string("ruta fuera del dominio tipado deMovePath"));
    const auto walking = SetActorWalk(actor, world, data, models, *context.cloth);
    if (!walking.HasValue())
        return Core::Unexpected(walking.Error());
    using Render::Models::LogicalAccess;
    std::array<std::optional<LogicalAccess>, 2> access;
    const GameLogic::LogicalCellEffects effects{
        .onStore = [&](GameLogic::LogicalAxis axis, GameLogic::LogicalStoreStage stage, std::uint32_t&)
        {
            const std::size_t index = axis == GameLogic::LogicalAxis::X ? 0u : 1u;
            if (stage == GameLogic::LogicalStoreStage::BeginningBefore ||
                stage == GameLogic::LogicalStoreStage::ArrivalBefore)
                access[index].emplace(axis == GameLogic::LogicalAxis::X ? actor.cellXDepth : actor.cellYDepth,
                                      context.cloth->random, Render::Models::SmallLogicalCloseRandoms);
            else
                access[index].reset();
        }};
    const bool arrived =
        GameLogic::MovePath({actor.path, actor.position, actor.angles.z, actor.cellX, actor.cellY}, rotate, effects);
    if (arrived)
    {
        actor.moving = false;
        const auto stopped = SetActorStop(actor, world, data, models, *context.cloth);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        return true;
    }
    actor.characterFlags &= ~std::uint32_t{0x200000};
    auto& runtime = world.Runtime();
    const auto speed = [&]() -> double
    {
        return GameLogic::UpdateMovementSpeed(actor.run, runtime.movementSpeed,
                                              {.type = actor.type,
                                               .baseSpeed = actor.baseSpeed,
                                               .item = actor.pet.model,
                                               .wings = actor.wings.model,
                                               .movementMode = actor.kind,
                                               .itemVariant = actor.mountVariant,
                                               .safeZone = actor.safeZone,
                                               .flags = actor.objectFlags,
                                               .map = runtime.map});
    };
    const auto height = [&](float x, float y)
    {
        const auto& terrain = *world.Resources()->terrain.Terrain();
        return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, context.camera->fixedHeight);
    };
    GameLogic::UpdateCharacterPosition(
        {actor.position, actor.angles, actor.movementPhase, actor.type, actor.pet.model, actor.safeZone, runtime.map},
        speed, height);
    return false;
}
} // namespace Client::Scenes
