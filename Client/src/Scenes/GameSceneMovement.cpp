#include "GameLogic/PathCell.h"
#include "Scenes/GameScene.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Scenes/FaunaStep.h"
#include "Scenes/FaunaEffects.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameMovement.h"
#include "Scenes/GameLeaves.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "UI/GameCursor.h"
#include "World/TerrainPicking.h"
#include "World/TerrainPickingTarget.h"
#include "World/TerrainCells.h"
#include "World/WorldTerrain.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> GameScene::StepTheHero(const Input::InputState& input)
{
    WorldState& world = *m_context.world;
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    const auto ticks =
        m_context.clock ? std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count() : 0;
    HeroMovementContext context{
        .world = world,
        .random = m_context.cloth->random,
        .playerSkeleton = m_models ? m_models->SkeletonOf(Data::Models::PlayerSkeletonModel) : nullptr,
        .items = m_items,
        .skills = m_skills,
        .gates = m_gates,
        .input = {mouse.x, mouse.y, input.WasPressed(Input::MouseButton::Left), input.IsDown(Input::MouseButton::Left),
                  input.IsDown(Input::Key::Shift), static_cast<float>(ticks), static_cast<std::uint32_t>(ticks)},
        .fixedHeight = CameraState().fixedHeight};
    context.pickTarget = [&]() -> Core::Expected<std::optional<GameLogic::PathCell>, std::string>
    {
        if (!m_mouseRay)
            return std::optional<GameLogic::PathCell>{};
        const auto& terrain = world.Map().terrain;
        World::TerrainView view;
        view.mapping = terrain.mapping.get();
        view.heights = terrain.heights.get();
        view.attributes = terrain.attributes.get();
        view.lighting = &world.DynamicLighting().Lighting();
        view.map = static_cast<std::uint8_t>(world.Runtime().map);
        view.fixedHeight = CameraState().fixedHeight;
        World::TerrainPickingTarget target;
        const auto picked = World::PickTerrainTarget(m_picking, target, view, m_terrainRange,
                                                     world.Runtime().terrainPolygon, *m_mouseRay);
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        if (!picked.Value())
            return std::optional<GameLogic::PathCell>{};
        return std::optional{
            GameLogic::PathCell{static_cast<std::uint8_t>(target.x), static_cast<std::uint8_t>(target.y)}};
    };
    context.sendWalk = [&](auto cells, auto target, float angle) -> Core::Expected<void, std::string>
    {
        if (!m_session)
            return Core::Unexpected(std::string("0x56acd0: no hay sesión para enviar D3"));
        return m_session->SendWalk(cells, target, angle);
    };
    if (m_session)
        context.requestGate = [&](std::uint8_t gate) { m_session->RequestGate(gate); };
    if (m_session)
        context.sendFacing = [&](std::uint8_t direction) { return m_session->SendFacing(direction); };
    return StepHeroMovement(context);
}

Core::Expected<void, std::string> GameScene::StepTheFauna()
{
    WorldState& world = *m_context.world;
    if (auto loaded = LoadTheObjectModels(world.Runtime().map); !loaded.HasValue())
        return loaded;
    auto& hero = world.Actors()[world.Runtime().hero];
    const float time =
        m_context.clock ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                        : 0;
    FaunaStepContext context{world.Fauna(),
                             world.Runtime().map,
                             world.Runtime().storm,
                             hero.position,
                             hero.animation.action,
                             hero.safeZone,
                             StepTerrain(),
                             world.DynamicLighting().Lighting(),
                             world.FaunaResources(),
                             *m_context.cloth,
                             time};
    context.fixedHeight = CameraState().fixedHeight;
    context.prefixModel = world.FaunaResources().PrefixModel();
    auto* slots = m_worldModels.TextureSlots();
    if (!slots)
        return Core::Unexpected(std::string("612d90: falta la tabla de texturas del mundo"));
    const FaunaModelLoadContext resources{m_context.dataRoot, m_context.textures, m_context.log, *slots,
                                          &world.FaunaResources().TextureTable()};
    context.effects = BindFaunaStepEffects(world.FaunaResources(), resources, world.DrawTail().joints,
                                           world.Map().effects, m_context.cloth->random);
    return StepGameFauna(context);
}

const World::WorldTerrain& GameScene::StepTerrain() const
{
    if (m_context.world->Runtime().map == 54 && m_context.selection && m_context.selection->Terrain())
        return *m_context.selection->Terrain();
    return m_context.world->Map().terrain;
}

Core::Expected<void, std::string> GameScene::StepTheLeaves()
{
    WorldState& world = *m_context.world;
    const auto& terrain = StepTerrain();
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x530490: falta terreno para el paso de hojas"));
    auto& hero = world.Actors()[world.Runtime().hero];
    const float time =
        m_context.clock ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                        : 0;
    GameLeafContext context{world.Leaves(),
                            world.Runtime().leafWeather,
                            *terrain.heights,
                            *terrain.attributes,
                            hero.position,
                            m_drawnCamera.position.y,
                            world.Runtime().map,
                            world.Runtime().heroCellLayer,
                            m_context.cloth->random,
                            time,
                            CameraState().fixedHeight,
                            &world.Particles()};
    context.cloudHero = &hero;
    context.movementSpeed = &world.Runtime().movementSpeed;
    // 0x5304f6: el clima de Crywolf (0x5763358), que sortea 0x430510.
    context.crywolfState = &world.Runtime().crywolf.weather;
    return StepGameLeaves(context);
}
} // namespace Client::Scenes
