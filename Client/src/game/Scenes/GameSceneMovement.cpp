#include "Pathfinding/PathCell.h"
#include "Scenes/GameScene.h"
#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Outgoing.h"
#include "Input/MouseMessageState.h"
#include "Fauna/FaunaStep.h"
#include "Fauna/FaunaEffects.h"
#include "Assets/FaunaModels.h"
#include "Movement/GameMovement.h"
#include "Hud/Dialogs/GameDialogSteps.h"
#include "Tables/GameData.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Chat/MacroFile.h"
#include "Core/Log.h"
#include "Items/InventoryMoves.h"
#include "Weather/GameLeaves.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/HeroMovementHooks.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Widget.h"
#include "Ui/GameCursor.h"
#include "Terrain/TerrainPicking.h"
#include "Terrain/TerrainPickingState.h"
#include "Terrain/TerrainPickingTarget.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/WorldTerrain.h"
#include <glm/ext/vector_float3.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>
#include <string>
#include <utility>

namespace Client::Scenes
{
Core::Expected<void, std::string> GameScene::StepTheHero(const Input::InputState& input)
{
    Kernel::WorldState& world = *m_context.world;
    const Ui::Point mouse = Ui::MouseIn640x480(input, m_screen);
    const auto ticks =
        m_context.clock ? std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count() : 0;
    // The click's edge is already at 0x7d366d4 when the step starts (StepTheDialogs), and the UI could use it: MoveHero
    // does not set it again.
    Movement::HeroMovementContext context{
        .world = world,
        .random = m_context.cloth->random,
        .playerSkeleton = m_models ? m_models->SkeletonOf(Tables::PlayerSkeletonModelId) : nullptr,
        .items = m_items,
        .skills = m_skills,
        .gates = m_gates,
        .input = {mouse.x, mouse.y, false, input.MouseMessages().leftDown, input.IsDown(Input::Key::Shift),
                  static_cast<float>(ticks), static_cast<std::uint32_t>(ticks), input.IsDown(Input::Key::Ctrl)},
        .fixedHeight = CameraState().fixedHeight,
        .models = m_models};
    context.terrain = &MapTerrain();
    context.dynamicLighting = &DynamicLight().Lighting();
    context.faunaModels = &LoadedFaunaModels();
    const Session::HeroMovementHooks hooks{};
    context.hooks = &hooks;
    context.saveOptionsAndMacros = [this, &world]() -> Core::Expected<void, std::string>
    {
        std::optional<std::uint8_t> chosenSlot;
        if (m_session != nullptr)
            if (const auto* chosen = m_session->ChosenCharacter(); chosen != nullptr)
                chosenSlot = chosen->slot;
        auto body = Hud::Dialogs::OptionBodyOf(world, m_context.cloth->random, chosenSlot);
        if (!body.HasValue())
            return Core::Unexpected(std::move(body).Error());
        // 0x61c210 ignores send's return: even if it fails, 0x59ad45 goes on with Macro.txt.
        if (m_session == nullptr)
            m_context.log.Warning("no se manda el F3 30: no hay servidor");
        else if (auto sent = m_session->SendOptionData(body.Value()); !sent.HasValue())
            m_context.log.Warning("no se manda el F3 30: {}", sent.Error());
        return Tables::WriteMacroFile(m_context.dataRoot, Chat::WrittenMacros(world.Runtime().chat.input.macros));
    };
    context.pickTarget = [this] { return GroundUnderTheMouse(); };
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
    // The D7 to attack on arrival (0x5650c0).
    if (m_session)
        context.sendAttack = [&](std::uint16_t key, std::uint8_t action, std::uint8_t direction)
        { return m_session->SendAttack(key, action, direction); };
    if (m_session)
        context.sendElfCell = [&](std::uint8_t x, std::uint8_t y) { return m_session->SendElfCell(x, y); };
    if (m_session)
    {
        // The C1 03 97 goes through the session; SetCursorPos stays requested in GameMovementState::cursorWarpRequest.
        context.sendEventWindowLeave = [&]() { return m_session->SendEventWindowLeave(); };
        context.sendWindowClose = [&]() { m_session->SendWindowClose(); };
        context.sendGamePacket = [&](Protocol::Outgoing outgoing) { m_session->SendGamePacket(std::move(outgoing)); };
        context.sendNpcWindow = NpcWindowSenderOf(m_session);
        context.requestQuestList = [&]() { m_session->RequestQuestList(); };
        context.sendTalk = [&](std::int16_t key) { m_session->SendTalk(key); };
        context.sendAction = [&](std::uint8_t direction, std::uint8_t action)
        { m_session->SendAction(direction, action); };
    }
    // 0x57ceea: the skill without a chosen target aims at the terrain under the mouse: 0x5fc260(1), the sweep, and
    // 0x5fac90, the retry on the chosen cell; the point is the one the sweep leaves (0x7d366e0..0x7d366e8). *Inferred*:
    // the same pair as the click (PickTerrainTarget) after a PickTerrain of the step.
    context.pickTerrainPoint = [&]() -> Core::Expected<std::optional<glm::vec3>, std::string>
    {
        if (!m_mouseRay)
            return std::optional<glm::vec3>{};
        const auto& terrain = MapTerrain();
        Terrain::TerrainView view;
        view.mapping = terrain.mapping.get();
        view.heights = terrain.heights.get();
        view.attributes = terrain.attributes.get();
        view.lighting = &DynamicLight().Lighting();
        view.map = static_cast<std::uint8_t>(world.Runtime().map);
        view.fixedHeight = CameraState().fixedHeight;
        if (auto swept =
                Terrain::PickTerrain(m_picking, view, m_terrainRange, m_context.states->camera.trapezoid, *m_mouseRay);
            !swept.HasValue())
            return Core::Unexpected(std::move(swept).Error());
        Terrain::TerrainPickingTarget target;
        const auto picked = Terrain::PickTerrainTarget(m_picking, target, view, m_terrainRange,
                                                       m_context.states->camera.trapezoid, *m_mouseRay);
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        if (!picked.Value())
            return std::optional<glm::vec3>{};
        return std::optional{m_picking.hit};
    };
    if (m_session)
    {
        context.sendSkill = [session = m_session](Protocol::Outgoing packet) { session->SendChat(std::move(packet)); };
        context.playerKey = m_session->PlayerIndex();
    }
    context.texts = m_texts;
    context.measure = m_context.textMeasure;
    context.screen = m_screen;
    const Items::InventoryServices inventory = InventoryContext();
    context.inventory = &inventory;
    return Movement::StepHeroMovement(context);
}

Core::Expected<std::optional<Pathfinding::PathCell>, std::string> GameScene::GroundUnderTheMouse()
{
    if (!m_mouseRay)
        return std::optional<Pathfinding::PathCell>{};
    Kernel::WorldState& world = *m_context.world;
    const auto& terrain = MapTerrain();
    Terrain::TerrainView view;
    view.mapping = terrain.mapping.get();
    view.heights = terrain.heights.get();
    view.attributes = terrain.attributes.get();
    view.lighting = &DynamicLight().Lighting();
    view.map = static_cast<std::uint8_t>(world.Runtime().map);
    view.fixedHeight = CameraState().fixedHeight;
    Terrain::TerrainPickingTarget target;
    const auto picked = Terrain::PickTerrainTarget(m_picking, target, view, m_terrainRange,
                                                   m_context.states->camera.trapezoid, *m_mouseRay);
    if (!picked.HasValue())
        return Core::Unexpected(picked.Error());
    if (!picked.Value())
        return std::optional<Pathfinding::PathCell>{};
    return std::optional{
        Pathfinding::PathCell{static_cast<std::uint8_t>(target.x), static_cast<std::uint8_t>(target.y)}};
}

Core::Expected<void, std::string> GameScene::StepTheFauna()
{
    Kernel::WorldState& world = *m_context.world;
    if (auto loaded = LoadTheObjectModels(world.Runtime().map); !loaded.HasValue())
        return loaded;
    auto& hero = world.Hero();
    const float time =
        m_context.clock ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                        : 0;
    Fauna::FaunaStepContext context{world.Fauna(),
                                    world.Runtime().map,
                                    world.Runtime().storm,
                                    hero.position,
                                    hero.animation.action,
                                    hero.safeZone,
                                    StepTerrain(),
                                    m_context.states->terrain.dynamicLight.Lighting(),
                                    LoadedFaunaModels(),
                                    *m_context.cloth,
                                    time};
    context.fixedHeight = CameraState().fixedHeight;
    context.prefixModel = LoadedFaunaModels().PrefixModel();
    context.runtime = &world.Runtime();
    auto* slots = m_worldModels.TextureSlots();
    if (!slots)
        return Core::Unexpected(std::string("612d90: falta la tabla de texturas del mundo"));
    const Assets::FaunaModelLoadContext resources{
        m_context.dataRoot,           m_context.textures,   m_context.log, *slots, &LoadedFaunaModels().TextureTable(),
        &world.Runtime().modelSounds, world.Runtime().audio};
    context.effects = Fauna::BindFaunaStepEffects(LoadedFaunaModels(), resources, world, world.Map().effects,
                                                  m_context.cloth->random);
    return Fauna::StepGameFauna(context);
}

const Terrain::WorldTerrain& GameScene::StepTerrain() const
{
    if (m_context.world->Runtime().map == 54 && m_context.selection && m_context.selection->Terrain())
        return *m_context.selection->Terrain();
    return MapTerrain();
}

const Terrain::WorldTerrain& GameScene::MapTerrain() const
{
    return m_context.states->terrain.current;
}

Terrain::DynamicWorldLight& GameScene::DynamicLight() const
{
    return m_context.states->terrain.dynamicLight;
}

Assets::FaunaModels& GameScene::LoadedFaunaModels() const
{
    return m_context.states->assets.fauna;
}

Core::Expected<void, std::string> GameScene::StepTheLeaves()
{
    Kernel::WorldState& world = *m_context.world;
    const auto& terrain = StepTerrain();
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x530490: falta terreno para el paso de hojas"));
    auto& hero = world.Hero();
    const float time =
        m_context.clock ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                        : 0;
    Weather::GameLeafContext context{world.Leaves(),
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
    // 0x5304f6: the Crywolf weather (0x5763358), rolled by 0x430510.
    context.crywolfState = &world.Runtime().crywolf.weather;
    return Weather::StepGameLeaves(context);
}
} // namespace Client::Scenes
