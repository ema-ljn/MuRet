#include "GameLogic/Path.h"
#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "Core/IClock.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Terrain/TerrainMapping.h"
#include "GameLogic/GateCheck.h"
#include "GameLogic/GateCheckState.h"
#include "GameLogic/GateDecision.h"
#include "GameLogic/PathFinder.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Movement.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameData.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameCursor.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"
#include "World/GameCamera.h"
#include "World/TerrainCells.h"
#include "World/TerrainPicking.h"
#include "World/WorldTerrain.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ratio>

namespace Client::Scenes
{
namespace
{
std::uint32_t ReadCell(ActorState& actor, bool x, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(x ? actor.cellXDepth : actor.cellYDepth, random,
                                               Render::Models::SmallLogicalCloseRandoms);
    return x ? actor.cellX : actor.cellY;
}

bool LessThanTwo(std::uint32_t logical, std::int32_t visual)
{
    const auto difference = logical - std::bit_cast<std::uint32_t>(visual);
    const auto sign = (difference & 0x80000000u) != 0 ? 0xffffffffu : 0u;
    return std::bit_cast<std::int32_t>((difference ^ sign) - sign) < 2;
}
} // namespace

// 59b188..59b295: el blanco precede al movimiento; no interpola el yaw del cuerpo.
void GameScene::UpdateHeroHeadTarget(const Input::InputState& input)
{
    WorldState& world = *m_context.world;
    WorldRuntime& state = world.Runtime();
    const ActorState& actor = world.Actors()[state.hero];
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    constexpr std::int32_t WorldWidth = 640; // CL7: viewport lógico sin ventanas laterales deCL8.
    const bool allow = (actor.objectFlags & 0x1000u) == 0;
    const double angle = allow ? Render::Models::CreateAngle(static_cast<float>(WorldWidth / 2), 180.0f,
                                                             static_cast<float>(mouse.x), static_cast<float>(mouse.y))
                               : 0.0;
    const auto whole = World::OriginalFtolLow(angle + world.Actors()[state.hero].angles.z);
    if (!whole)
    {
        m_failure = "cabeza: ángulo fuera del dominio portable de OriginalFtolLow";
        return;
    }
    const auto wrapped = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(*whole) + 315u);
    const auto clamped = std::clamp(wrapped % 360, 120, 240);
    const auto heading = (clamped + 180) % 360;
    ActorState& destination = world.Actors()[state.hero];
    destination.headTargetZ = 0.0f;
    if (allow && actor.animation.body.action != 0x3e &&
        (state.headTargetLock == 0 || state.targets[4] == -1 || state.map == 6 || (state.map >= 18 && state.map <= 23)))
    {
        destination.head.target.x = static_cast<float>(heading);
        destination.head.target.y = static_cast<float>(static_cast<double>(180 - mouse.y) *
                                                       static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)));
        return;
    }
    destination.head.target.x = 0.0f;
    destination.head.target.y = 0.0f;
}

void GameScene::CheckGates()
{
    if (m_data == nullptr || !m_data->gates || m_context.cloth == nullptr)
        return;
    auto& world = *m_context.world;
    auto& state = world.Runtime();
    const auto& hero = world.Actors()[state.hero];
    const auto& own = world.OwnCharacter();
    const auto tick = m_context.clock != nullptr
                          ? static_cast<std::uint32_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                          : 0u;
    // Snapshot de la decisión pura; los accesos propios del callee599ef0 no se inventan aquí.
    const GameLogic::GateCheckState input{.x = std::bit_cast<std::int32_t>(hero.cellX),
                                          .y = std::bit_cast<std::int32_t>(hero.cellY),
                                          .map = state.map,
                                          .level = own.level,
                                          .characterClass = own.characterClass,
                                          .mount = std::bit_cast<std::int16_t>(own.equipment[8].id),
                                          .wings = std::bit_cast<std::int16_t>(own.equipment[7].id),
                                          .alternateMount = state.alternateMount,
                                          .alternateMountActive = state.alternateMountActive > 0,
                                          .warpCounter = state.warpCounter,
                                          .elapsedMs = tick - state.lastTeleportTick,
                                          .heroGateBlocked = hero.gateBlocked,
                                          .requestBlocked = state.gateRequestBlocked,
                                          .warpPending = state.warpPending};
    const auto decision = GameLogic::GateCheck(*m_data->gates, input);
    state.gateWarnings = decision.warnings; // La presentación del área de mensajes esCL8.
    if (decision.clearWarpPending)
        state.warpPending = false;
    if (decision.waitSteps != 0)
    {
        const Render::Models::LogicalAccess access(state.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        state.warpCounter = decision.waitSteps;
    }
    if (!decision.request)
        return;
    const auto gate = Data::Tables::ReadGateRecord(*m_data->gates, *decision.request);
    const auto destination = gate ? Data::Tables::ReadGateRecord(*m_data->gates, gate->target) : std::nullopt;
    if (destination && (destination->map == 30 || destination->map == 31))
    {
        m_failure = "gate: faltan macros61c170/61c210 de mapas30/31";
        return;
    }
    if (m_session != nullptr)
        m_session->RequestGate(*decision.request);
    {
        const Render::Models::LogicalAccess access(state.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        state.warpCounter = 9'999'999;
    }
    state.warpPending = true;
    state.targets.fill(-1);
}

void GameScene::ProcessHeroInput(const Input::InputState& input)
{
    auto& state = m_context.world->Runtime();
    auto& hero = m_context.world->Actors()[state.hero];
    CheckGates();
    if (m_failure)
        return;
    const UI::Point mouse = UI::MouseIn640x480(input, m_screen);
    if (state.uiMovementBlocked || mouse.y > 431)
    {
        RefreshHeroTerrainLayer();
        return;
    }
    const bool rawDown = input.IsDown(Input::MouseButton::Left);
    if (!rawDown)
        state.forcedLeftRelease = false;
    bool activate = false;
    if (std::bit_cast<std::int32_t>(state.mouseUpdateTime) >= std::bit_cast<std::int32_t>(state.mouseUpdateTimeMax) &&
        !state.inputActionBlocked)
    {
        activate = state.pendingLeftPress || (rawDown && !state.forcedLeftRelease);
        state.pendingLeftPress = false;
    }
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    if (!activate || (hero.objectFlags & 0x1000u) != 0)
    {
        state.mouseHoldDuration = 0.0f;
        state.mouseHoldStart = clock;
    }
    else
    {
        const double duration = (static_cast<double>(clock) - state.mouseHoldStart) *
                                static_cast<double>(std::bit_cast<float>(0x3a83126fu));
        state.mouseHoldDuration = static_cast<float>(duration);
        if (duration >= 3600.0)
        {
            state.pendingLeftPress = false;
            state.forcedLeftRelease = true;
        }
        else
        {
            const int action = hero.animation.body.action;
            const bool ordinary = action != 0xc2 && hero.interactionState != 1 && hero.interactionState != 2 &&
                                  hero.alpha >= 0.7f && (action < 0x22 || action > 0x97);
            if (ordinary || (action >= 0x48 && action <= 0x4b) || (action >= 0x6a && action <= 0x7d) ||
                (action >= 0x8a && action <= 0x8c))
            {
                state.mouseUpdateTime = 0;
                if (state.targets[0] == -1 && state.targets[1] == -1 && state.targets[2] == -1 &&
                    state.targets[3] == -1)
                    UseGroundClick(input);
            }
        }
    }
    ++state.mouseUpdateTime;
    RefreshHeroTerrainLayer();
}

void GameScene::UseGroundClick(const Input::InputState& input)
{
    auto& world = *m_context.world;
    auto& state = world.Runtime();
    auto* resources = world.Resources();
    if (input.IsDown(Input::Key::Shift) || resources == nullptr || !resources->terrain.IsLoaded() ||
        !resources->pickingRay)
        return;
    const auto& terrain = *resources->terrain.Terrain();
    const World::TerrainView view{.heights = terrain.heights.get(),
                                  .attributes = terrain.attributes.get(),
                                  .fixedHeight = m_context.camera != nullptr ? m_context.camera->fixedHeight
                                                                             : World::FixedCornerHeight};
    const auto selected =
        World::PickTerrainTarget(resources->terrainPicking, resources->terrainTarget, view, resources->terrainRange,
                                 resources->terrainPolygon, *resources->pickingRay);
    if (!selected.HasValue())
    {
        m_failure = selected.Error();
        return;
    }
    if (!selected.Value())
        return;
    auto& actor = world.Actors()[state.hero];
    constexpr float Cells = std::bit_cast<float>(0x3c23d70au);
    const auto px = World::OriginalFtolLow(static_cast<double>(actor.position.x) * Cells);
    const auto py = World::OriginalFtolLow(static_cast<double>(actor.position.y) * Cells);
    if (!px || !py)
    {
        m_failure = "posición fuera del dominioftol de MoveHero";
        return;
    }
    auto& random = m_context.cloth->random;
    if (actor.moving)
    {
        if (!LessThanTwo(ReadCell(actor, true, random), *px))
            return;
        if (!LessThanTwo(ReadCell(actor, false, random), *py))
            return;
    }
    if (ReadCell(actor, true, random) == resources->terrainTarget.x &&
        ReadCell(actor, false, random) == resources->terrainTarget.y && actor.moving)
    {
        state.mouseUpdateTime = 0;
        return;
    }
    const auto y = ReadCell(actor, false, random);
    const auto x = ReadCell(actor, true, random);
    GameLogic::PathFinder finder(resources->walls);
    auto path = finder.Find(
        {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)},
        {static_cast<std::uint8_t>(resources->terrainTarget.x), static_cast<std::uint8_t>(resources->terrainTarget.y)});
    if (!path)
    {
        state.mouseUpdateTime = 0;
        return;
    }
    actor.path = *path;
    actor.movementIntent = 0;
    if (state.window8000Open || (actor.objectFlags & 0x20u) != 0)
        return;
    const auto maximum = Protocol::Movement::WalkUpdateTimeMax(actor.path.count);
    if (!maximum.HasValue())
    {
        m_failure = "cantidad de celdas fuera del dominioSendMove";
        return;
    }
    state.mouseUpdateTimeMax = maximum.Value();
    std::array<Protocol::Movement::Cell, GameLogic::MaxPathCells> cells{};
    for (std::size_t i = 0; i < actor.path.count; ++i)
        cells[i] = {actor.path.cells[i].x, actor.path.cells[i].y};
    if (m_session != nullptr)
    {
        const auto sent = m_session->SendWalk(std::span(cells).first(actor.path.count),
                                              {static_cast<std::uint8_t>(resources->terrainTarget.x),
                                               static_cast<std::uint8_t>(resources->terrainTarget.y)},
                                              actor.angles.z);
        if (!sent.HasValue())
        {
            m_failure = "SendMove rechazó ángulo o ruta fuera de su dominio";
            return;
        }
    }
    if (state.actionCommand != 0x70)
        actor.moving = true;
}

void GameScene::RefreshHeroTerrainLayer()
{
    auto& world = *m_context.world;
    const auto* resources = world.Resources();
    if (resources == nullptr || !resources->terrain.IsLoaded())
        return;
    const auto& hero = world.Actors()[world.Runtime().hero];
    const auto y = World::OriginalFtolLow(hero.position.y);
    const auto x = World::OriginalFtolLow(hero.position.x);
    if (!x || !y)
    {
        m_failure = "epílogo deMoveHero fuera del dominioftol";
        return;
    }
    const auto bits = std::bit_cast<std::uint32_t>(*x / 100) + (std::bit_cast<std::uint32_t>(*y / 100) << 8);
    const auto index = std::clamp<std::int32_t>(std::bit_cast<std::int32_t>(bits), 0, 65535);
    world.Runtime().heroTerrainLayer1 = resources->terrain.Terrain()->mapping->layer1[static_cast<std::size_t>(index)];
    // 57cfc0 anterior sigue frontera separada: no se simula pagando3848 sin ejecutar su estado.
}
} // namespace Client::Scenes
