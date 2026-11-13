#include "Scenes/GameMovement.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Tables/SkillTable.h"
#include "GameLogic/Path.h"
#include "GameLogic/PathCell.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/CameraTour.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameGates.h"
#include "Scenes/GameHeroFacing.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/WorldMap.h"
#include "World/OriginalFtol.h"
#include "World/WorldTerrain.h"
#include "World/TerrainHeight.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/PanelRegistry.h"
#include <array>
#include <cstddef>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cmath>
#include <optional>
#include <span>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
constexpr std::uint32_t MovementLockedFlag = 0x20;
// 0x56b693: con 3, la entrada del número de serie (el 94 con el tipo 2).
constexpr std::int32_t SerialEventWindow = 3;
// 0x56b785: lo que SendMove deja en 0x6c8dcc al cerrar la ventana de los eventos.
constexpr std::int32_t EventWindowUpdateTimeMax = 6;
// 0x56b397..0x56b4d6: las ventanas que cierra caminar, en este orden.
constexpr std::array<UI::PanelId, 9> PanelsClosedByWalking{
    UI::TradePanel, UI::WarehousePanel, UI::SecondGuildPanel, UI::ChaosBoxPanel, UI::QuestPanel,
    UI::Panel2000,  UI::NpcShopPanel,   UI::CastleSiegePanel, UI::EventNpcPanel};
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
constexpr float MinimumWalkingAlpha = 0.7f;
constexpr float TeleportFade = 0.1f;
constexpr float HoldSecondsPerMillisecond = 0.001f;
constexpr double MaximumHoldSeconds = 3600.0;

Core::Expected<void, std::string> CloseTheClickRecord(const HeroMovementContext& context)
{
    std::int16_t right = 0;
    std::int16_t left = 0;
    {
        const Render::Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        right = std::bit_cast<std::int16_t>(context.world.OwnCharacter().equipment[0].id);
        left = std::bit_cast<std::int16_t>(context.world.OwnCharacter().equipment[1].id);
    }
    // 0x59bd01..0x59bd38: con el cursor vacío, la mano derecha (+0xb6c) vacía y un arma de los grupos 0 a 2 o
    // del 5 en la izquierda (+0xbb4), el original pasa esa arma al cursor (0x59bdf6), viste al héroe
    // (0x512a30) y manda el C1 .. 24 que la lleva a la derecha (0x59bf1c). Con un arma en la derecha no
    // hace nada de eso y sigue con lo que hay bajo el mouse (0x59cd23).
    const bool weaponOnTheLeft = (left >= 0 && left < 0x600) || (left >= 0xa00 && left < 0xc00);
    if (context.world.Runtime().cursorItemCount <= 0 && right == -1 && weaponOnTheLeft)
    {
        static const InventoryServices NoServices{};
        return MoveTheLeftWeaponToTheRight(
            InventoryMoveContext{.world = context.world,
                                 .random = context.random,
                                 .services = context.inventory != nullptr ? *context.inventory : NoServices});
    }
    return {};
}

bool CanStartThePath(const ActorState& hero)
{
    const auto action = hero.animation.action;
    const bool restricted = action == 0xc2 || hero.byte80 == 1 || hero.byte80 == 2 ||
                            !(hero.alpha >= MinimumWalkingAlpha) || (action >= 0x22 && action <= 0x97);
    // 59bac6..59bb1f: estas tres familias atraviesan incluso las guardas anteriores.
    return !restricted || (action >= 0x8a && action <= 0x8c) || (action >= 0x48 && action <= 0x4b) ||
           (action >= 0x6a && action <= 0x7d);
}

// 0x59b1c2..0x59b1e2: el centro es la mitad del ancho del mundo (0x59b188), no 320 fijo.
Core::Expected<void, std::string> UpdateTheHead(ActorState& hero, const HeroMovementInput& input, std::int32_t center)
{
    const double angle = Render::Models::CreateAngle(
        static_cast<float>(center), 180.0f, static_cast<float>(input.mouseX), static_cast<float>(input.mouseY));
    const auto integer = World::OriginalFtolLow(angle + static_cast<double>(hero.angles.z));
    if (!integer)
        return Core::Unexpected(std::string("0x59b1f3: ángulo de la cabeza fuera del perfil ftol"));
    const std::int32_t shifted = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(*integer) + 315u);
    const std::int32_t target = std::clamp(shifted % 360, 120, 240);
    if (hero.animation.action == 0x3e)
    {
        hero.headTurn.target = {};
        return {};
    }
    hero.headTurn.target.x = static_cast<float>((target + 180) % 360);
    hero.headTurn.target.y =
        static_cast<float>((180.0 - static_cast<double>(input.mouseY)) * static_cast<double>(0.05f));
    return {};
}

std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x56b72f..0x56b77f: el lugar de SetCursorPos. Y = alto · y / 480 (MUL sin signo); X = (640 · x / 260, IMUL con
// signo) · ancho / 640 (MUL sin signo): el cursor queda en x · ancho / 260 y no en x · ancho / 640 (Q-C1330).
std::pair<std::int32_t, std::int32_t> EventWindowCursor(std::int32_t mouseX, std::int32_t mouseY, std::int32_t width,
                                                        std::int32_t height)
{
    constexpr std::uint64_t DivideBy480 = 0x88888889u; // >> 40
    constexpr std::int64_t DivideBy260 = 0x7e07e07f;   // >> 39
    constexpr std::uint64_t DivideBy640 = 0xcccccccdu; // >> 41
    constexpr std::uint32_t Width640 = 640;
    const auto rows = static_cast<std::uint32_t>(height) * static_cast<std::uint32_t>(mouseY);
    const auto y = static_cast<std::int32_t>((rows * DivideBy480) >> 40u);
    const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(mouseX) * Width640);
    auto quotient = static_cast<std::int32_t>((static_cast<std::int64_t>(scaled) * DivideBy260) >> 39);
    if (quotient < 0)
        ++quotient;
    const auto columns = static_cast<std::uint32_t>(quotient) * static_cast<std::uint32_t>(width);
    const auto x = static_cast<std::int32_t>((columns * DivideBy640) >> 41u);
    return {x, y};
}

// 0x56b4e8..0x56b78f: el C1 03 97 (sin cifrar, por el socket de [0x6c82a0]); con 3, la entrada del número de serie;
// 0x78abb00 en 0, SetCursorPos, 0x6c8dcc en 6 y el izquierdo suelto (0x7d36874).
Core::Expected<void, std::string> LeaveTheEventWindow(const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!context.sendEventWindowLeave)
        return Core::Unexpected(std::string("0x56b4e8: falta el servicio de envío del C1 03 97"));
    if (auto sent = context.sendEventWindowLeave(); !sent.HasValue())
        return sent;
    if (runtime.panels.field78abb00 == SerialEventWindow)
        return Core::Unexpected(std::string("0x56b6a9: cerrar la entrada del número de serie (0x6c8e74, 0x77f3b70, los "
                                            "diez buffers de 0x7798bf8, los EDIT de [0x5763504] y [0x5763508], "
                                            "0x77f3b6a, 0x77f3b6c, 0x78abacc y 0x77f3b6b) no está portado (P-C295)"));
    runtime.panels.field78abb00 = 0;
    // 0x56b77f: SetCursorPos mueve el cursor de Windows; queda pedido para la plataforma.
    const auto [x, y] =
        EventWindowCursor(context.input.mouseX, context.input.mouseY, context.screenWidth, context.screenHeight);
    runtime.movement.cursorWarpRequest = std::array<std::int32_t, 2>{x, y};
    runtime.movement.mouseUpdateTimeMax = EventWindowUpdateTimeMax;
    runtime.movement.leftHeld = false;
    return {};
}

Core::Expected<void, std::string> SendThePath(ActorState& hero, GameLogic::PathCell target,
                                              const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    GamePanels panels(context.world, context.random);
    panels.LendTheWindowClose(context.sendWindowClose);
    // 0x56acf7: con el NPC de la caja abierto (0x8000), nada.
    if (panels.Registry().IsOpen(UI::TrainerPanel) || (hero.flags78 & MovementLockedFlag) != 0)
        return {};
    const auto maximum = Protocol::Movement::WalkUpdateTimeMax(hero.path.count);
    if (!maximum.HasValue())
        return Core::Unexpected(std::string("0x56acd0: cantidad de camino inválida"));
    state.mouseUpdateTimeMax = static_cast<std::int32_t>(maximum.Value());
    std::array<Protocol::Movement::Cell, GameLogic::MaxPathCells> cells{};
    for (std::size_t i = 0; i < hero.path.count; ++i)
        cells[i] = {hero.path.cells[i].x, hero.path.cells[i].y};
    if (!context.sendWalk)
        return Core::Unexpected(std::string("0x56acd0: falta el servicio de envío D3"));
    if (const auto sent =
            context.sendWalk(std::span(cells).first(hero.path.count), {target.x, target.y}, hero.angles.z);
        !sent.HasValue())
        return sent;
    // 0x56b387: con el aviso de la desconexión (0x70) el héroe no camina.
    if (runtime.dialogs.current != UI::DisconnectDialog)
        hero.movement = 1;
    // 0x56b397..0x56b4d6: cada una abierta se cierra; las del comercio y la tienda de NPC sacan un rand() aunque
    // estén cerradas.
    for (const UI::PanelId id : PanelsClosedByWalking)
    {
        if (!panels.Registry().IsOpen(id))
            continue;
        if (auto closed = panels.Registry().Close(id); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    // 0x56b4db: con la ventana de los eventos (0x78abb00), el C1 03 97 (CL9 hito 11).
    if (runtime.panels.field78abb00 == 0)
        return {};
    return LeaveTheEventWindow(context);
}

Core::Expected<void, std::string> UseGroundClick(ActorState& hero, const HeroMovementContext& context)
{
    if (context.input.shiftDown || !context.pickTarget)
        return {};
    const auto picked = context.pickTarget();
    if (!picked.HasValue())
        return Core::Unexpected(picked.Error());
    if (!picked.Value())
        return {};
    World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x4eac00: falta TerrainWall para caminar"));
    GameMovementState& state = context.world.Runtime().movement;
    if (hero.movement != 0)
    {
        const auto visibleX = World::OriginalFtolLow(static_cast<double>(hero.position.x) * CellReciprocal);
        const auto visibleY = World::OriginalFtolLow(static_cast<double>(hero.position.y) * CellReciprocal);
        if (!visibleX || !visibleY)
            return Core::Unexpected(std::string("0x59deb3: posición fuera del perfil ftol"));
        if (std::abs(static_cast<int>(ReadCell(hero.cellXAccesses, hero.cellX, context.random)) - *visibleX) >= 2)
            return {};
        if (std::abs(static_cast<int>(ReadCell(hero.cellYAccesses, hero.cellY, context.random)) - *visibleY) >= 2)
            return {};
    }
    const auto target = *picked.Value();
    state.targetX = target.x;
    state.targetY = target.y;
    const auto x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    if (x == target.x && ReadCell(hero.cellYAccesses, hero.cellY, context.random) == target.y && hero.movement != 0)
    {
        state.mouseUpdateTime = 0;
        return {};
    }
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    const auto path = GameLogic::PathFinder(terrain.attributes->cells).Find({startX, startY}, target);
    if (!path)
    {
        state.mouseUpdateTime = 0;
        return {};
    }
    hero.path = *path;
    hero.movementType = 0;
    return SendThePath(hero, target, context);
}

Core::Expected<void, std::string> ProcessInput(ActorState& hero, const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    // 0x59b846..0x59b85a: el modo del editor (0x77f3b18) y el mouse de la UI (0x775857c) saltan también la suma.
    if (state.inputBlocked || runtime.ui.mouseOnUi != 0)
        return {};
    if (state.mouseUpdateTime >= state.mouseUpdateTimeMax && !state.movementBlocked)
    {
        if (!state.pendingClick && !state.leftHeld)
        {
            state.heldSince = context.input.worldTime;
            state.heldElapsed = 0.0f;
        }
        else
        {
            state.pendingClick = false;
            const double elapsed = (static_cast<double>(context.input.worldTime) - state.heldSince) *
                                   static_cast<double>(HoldSecondsPerMillisecond);
            state.heldElapsed = static_cast<float>(elapsed);
            if (elapsed >= MaximumHoldSeconds)
                state.leftHeld = false;
            else if (CanStartThePath(hero))
            {
                if (const auto closed = CloseTheClickRecord(context); !closed.HasValue())
                    return closed;
                state.mouseUpdateTime = 0;
                if (const auto clicked = UseGroundClick(hero, context); !clicked.HasValue())
                    return clicked;
            }
        }
    }
    // El contador nativo es un DWORD: conservar el wrap sin overflow con signo en C++.
    state.mouseUpdateTime = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.mouseUpdateTime) + 1u);
    return {};
}

void PublishHeroLayer(const ActorState& hero, WorldRuntime& runtime, const World::WorldTerrain& terrain)
{
    if (!terrain.mapping)
        return;
    const auto x = World::OriginalFtolLow(hero.position.x);
    const auto y = World::OriginalFtolLow(hero.position.y);
    if (!x || !y)
        return;
    const auto raw = static_cast<std::int64_t>(*y / 100) * 256 + *x / 100;
    const auto index = static_cast<std::size_t>(std::clamp<std::int64_t>(raw, 0, 65535));
    runtime.heroCellLayer = terrain.mapping->layer1[index];
}
std::int32_t StepTheCounter(WorldRuntime& runtime, Render::Models::CrtRandom& random)
{
    {
        const Render::Models::LogicalAccess access(runtime.warpDepth, random, Render::Models::SmallLogicalCloseRandoms);
        if (runtime.warpCounter > 0)
            --runtime.warpCounter;
    }
    const Render::Models::LogicalAccess access(runtime.warpDepth, random, Render::Models::SmallLogicalCloseRandoms);
    return runtime.warpCounter;
}
Core::Expected<void, std::string> MoveTheHero(ActorState& hero, const HeroMovementContext& context)
{
    if (hero.movement == 0)
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    const World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!context.playerSkeleton)
        return Core::Unexpected(std::string("0x4f37c0: falta Player.bmd"));
    const ActorStopContext stop{
        runtime,       SceneId::Game,  *context.playerSkeleton,
        context.items, context.random, terrain.attributes ? &terrain.attributes->cells : nullptr};
    if ((hero.flags78 & MovementLockedFlag) != 0)
    {
        const auto stopped = SetActorStop(hero, stop);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        if (stopped.Value().sound)
            runtime.lastSoundId = stopped.Value().sound;
        return {};
    }
    const auto walked = SetActorWalk(hero, {stop, context.world.ActionSpeeds(), true});
    if (!walked.HasValue())
        return Core::Unexpected(walked.Error());
    if (walked.Value().sound)
        runtime.lastSoundId = walked.Value().sound;
    if (MoveActorPath(hero, 1, context.random))
    {
        hero.movement = 0;
        const auto stopped = SetActorStop(hero, stop);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        if (stopped.Value().sound)
            runtime.lastSoundId = stopped.Value().sound;
        const auto yaw = World::OriginalFtolLow(hero.angles.z);
        if (!yaw)
            return Core::Unexpected(std::string("0x59b2d2: ángulo de llegada fuera del perfil ftol"));
        runtime.movement.arrivalYaw = *yaw;
        runtime.movement.movementTarget = 0;
        // 5650ef..565100: MovementType0 sale por la tabla, sin interacción ni RNG.
        if (hero.movementType != 0)
            return Core::Unexpected(std::string("0x5650c0: la interacción de llegada requiere CL9"));
        return {};
    }
    hero.flags7c &= ~std::uint32_t{0x200000};
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x508a80: falta terreno para mover al héroe"));
    return UpdateActorPosition(hero, {runtime, context.random, [&](float x, float y) {
                                          return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y,
                                                                        context.fixedHeight);
                                      }});
}

Core::Expected<void, std::string> CloseTheHeroActionPrefix(ActorState& hero, const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x57cfe2..0x57cff9.
    if (runtime.movement.inputBlocked || runtime.ui.mouseOnUi != 0)
        return {};
    if (hero.byte80 == 1)
    {
        // 57d02b..57d04a: fsts conserva el resultado extendido para la comparación.
        const double faded = static_cast<double>(hero.alpha) - static_cast<double>(TeleportFade);
        hero.alpha = static_cast<float>(faded);
        if (faded < TeleportFade)
            hero.byte80 = 2;
    }
    std::uint8_t skill = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        const auto index = static_cast<std::size_t>(hero.skillIndex);
        const auto& words = context.world.OwnCharacter().words6c;
        if (index >= sizeof(words))
            return Core::Unexpected(std::string("0x57d0d5: el índice de habilidad excede los bytes acreditados"));
        skill = static_cast<std::uint8_t>((words[index / 4] >> ((index % 4) * 8)) & 0xffu);
    }
    if (!context.skills)
        return Core::Unexpected(std::string("0x5526f0: falta Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::string("0x5526f0: falta el registro de habilidad"));
    {
        const Render::Models::LogicalAccess access(runtime.movement.skillDepths[skill], context.random,
                                                   static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
        runtime.movement.selectedSkillRange = static_cast<float>(std::to_integer<std::uint8_t>((*record)[0x26]));
    }
    if (hero.pet.model == 0x1c16)
        runtime.movement.selectedSkillRange += 2.0f;
    // 57d261..57d271: el bloqueo no evita leer Skill.bmd, pero sin clic conserva el reloj.
    if (runtime.movement.movementBlocked && !runtime.movement.pendingClick && !runtime.movement.leftHeld)
        return {};
    runtime.movement.actionInputTime = context.input.worldTime;
    runtime.movement.actionInputElapsed = 0.0f;
    return {};
}
} // namespace
Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    ActorState& hero = context.world.Actors()[runtime.hero];
    LatchTheLeftButton(runtime.movement, context.input.leftPressed, context.input.leftDown);
    // 0x59ae24: 0x4033d0, el recorrido de la cámara (Scenes/CameraTour.h).
    if (CameraTourRunning(runtime.events.cameraTour))
    {
        if (!context.playerSkeleton)
            return Core::Unexpected(std::string("0x59ae24: falta Player.bmd para parar durante el recorrido"));
        const auto& terrain = context.world.Map().terrain;
        const auto stopped =
            SetActorStop(hero, {runtime, SceneId::Game, *context.playerSkeleton, context.items, context.random,
                                terrain.attributes ? &terrain.attributes->cells : nullptr});
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        if (stopped.Value().sound)
            runtime.lastSoundId = stopped.Value().sound;
        return {};
    }
    if (runtime.selectedTargets[2] != -1 && (hero.flags7c & 0x200000u) != 0)
        return {};
    if (hero.dying != 0 || (hero.flags78 & FrozenHeroFlag) != 0)
        return {};
    if (StepTheCounter(runtime, context.random) > WaitDrawThreshold)
        return {};
    // 0x59b188: el ancho del mundo, con sus consultas (2 rand() con el inventario abierto); la mitad, con signo.
    GamePanels panels(context.world, context.random);
    const std::int32_t center = panels.Registry().WorldWidth() / 2;
    if (auto head = UpdateTheHead(hero, context.input, center); !head.HasValue())
        return head;
    if (hero.movement == 0)
    {
        // 0x59b357: IsOpen(0x40000) no saca rand().
        const auto facing = StepHeroFacing({.hero = hero,
                                            .movement = runtime.movement,
                                            .secondGuildOpen = panels.Registry().IsOpen(UI::SecondGuildPanel),
                                            .mouseOnUi = runtime.ui.mouseOnUi,
                                            .mouseX = context.input.mouseX,
                                            .mouseY = context.input.mouseY,
                                            .centerX = center,
                                            .headTurningAllowed = hero.animation.action != 0x3e,
                                            .sendFacing = context.sendFacing});
        if (!facing.HasValue())
            return facing;
    }
    if (const auto moved = MoveTheHero(hero, context); !moved.HasValue())
        return moved;
    if (context.gates)
    {
        const auto gate =
            CheckGameGates({context.world, *context.gates, context.random, context.input.ticksMs, context.requestGate});
        if (!gate.HasValue())
            return Core::Unexpected(gate.Error());
    }
    if (const auto input = ProcessInput(hero, context); !input.HasValue())
        return input;
    if (const auto closed = CloseTheHeroActionPrefix(hero, context); !closed.HasValue())
        return closed;
    PublishHeroLayer(hero, runtime, context.world.Map().terrain);
    return {};
}
} // namespace Client::Scenes
