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
#include "Scenes/ChatBubbles.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameGates.h"
#include "Scenes/GameHeroFacing.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/NpcTalk.h"
#include "Scenes/WorldMap.h"
#include "World/OriginalFtol.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"
#include "World/TerrainHeight.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include <array>
#include <cstddef>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cmath>
#include <format>
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
// 0x56b397..0x56b4d6: las ventanas que cierra caminar, en este orden.
constexpr std::array<UI::PanelId, 9> PanelsClosedByWalking{
    UI::TradePanel, UI::WarehousePanel, UI::SecondGuildPanel, UI::ChaosBoxPanel, UI::QuestPanel,
    UI::Panel2000,  UI::NpcShopPanel,   UI::CastleSiegePanel, UI::EventNpcPanel};
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
constexpr float MinimumWalkingAlpha = 0.7f;
constexpr float TeleportFade = 0.1f;
constexpr float HoldSecondsPerMillisecond = 0.001f;
constexpr double MaximumHoldSeconds = 3600.0;

// Cómo termina el clic de MoveHero: sigue hasta el final (0x59e248) o sale sin él (0x59e2c0).
enum class MoveHeroEnd : std::uint8_t
{
    Continue,
    Leave,
};

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
Core::Expected<void, std::string> UpdateTheHead(ActorState& hero, const HeroMovementInput& input, std::int32_t center,
                                                bool keepsTheHead)
{
    const double angle = Render::Models::CreateAngle(
        static_cast<float>(center), 180.0f, static_cast<float>(input.mouseX), static_cast<float>(input.mouseY));
    const auto integer = World::OriginalFtolLow(angle + static_cast<double>(hero.angles.z));
    if (!integer)
        return Core::Unexpected(std::string("0x59b1f3: ángulo de la cabeza fuera del perfil ftol"));
    const std::int32_t shifted = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(*integer) + 315u);
    const std::int32_t target = std::clamp(shifted % 360, 120, 240);
    // 0x59b22f..0x59b261: con el ataque automático y el otro elegido (0x6c8e38) distinto de −1 fuera del mapa 6 y de
    // Chaos Castle, o con la acción 0x3e, la cabeza va a 0 y el héroe quieto no gira (BL en 0, 0x59b3c4).
    if (keepsTheHead || hero.animation.action == 0x3e)
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
    // 0x56b4db: con 0x78abb00, el C1 03 97 del asedio.
    if (runtime.panels.field78abb00 != 0)
        return Core::Unexpected(std::string("0x56b4e8: el C1 03 97 de 0x78abb00 es de la CL9.5"));
    return {};
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

// 0x59b23d..0x59b258, 0x59b8a7..0x59b8b9 y 0x57d1bc..0x57d1d7: el ataque automático (0x6c8e3c), fuera del mapa 6 y de
// Chaos Castle (18 a 23).
bool AutoAttacksHere(const WorldRuntime& runtime)
{
    constexpr std::int32_t SpectatorMap = 6;
    constexpr std::int32_t FirstChaosCastleMap = 18;
    constexpr std::int32_t LastChaosCastleMap = 23;
    return runtime.ui.autoAttack && runtime.map != SpectatorMap &&
           (runtime.map < FirstChaosCastleMap || runtime.map > LastChaosCastleMap);
}

// Lo que necesita 0x5650c0 desde MoveHero (0x59b2d2 y 0x59d670): el héroe es un jugador, sin la biblioteca de modelos.
HeroArrivalContext ArrivalContextOf(const HeroMovementContext& context)
{
    return HeroArrivalContext{.combat = {.world = context.world,
                                         .random = context.random,
                                         .playerSkeleton = context.playerSkeleton,
                                         .models = nullptr,
                                         .items = context.items},
                              .sendWalk = context.sendWalk,
                              .sendAttack = context.sendAttack};
}

// Las manos del registro propio (+0xb6c y +0xbb4, MOVSX), en un acceso.
std::pair<std::int32_t, std::int32_t> OwnHands(const HeroMovementContext& context)
{
    const Render::Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    const auto& equipment = context.world.OwnCharacter().equipment;
    return {std::bit_cast<std::int16_t>(equipment[0].id), std::bit_cast<std::int16_t>(equipment[1].id)};
}

bool InRange(std::int32_t value, std::int32_t first, std::int32_t end)
{
    return value >= first && value < end;
}

// 0x59cd49..0x59d3d7: el elfo (+0x1d4 & 7 en 2) que camina (+0x378) sin interacción (+0x379 en 0) y con una ballesta en
// la derecha (0x808..0x80e o 0x810..0x9ff) o un arco en la izquierda (0x800..0x806 o 0x811, sin el 0x814 ni el 0x815)
// manda su celda lógica, la Y (+0x420) leída antes que la X (+0x41c): C1 05 DF X Y. Vacío, no manda.
Core::Expected<void, std::string> SendTheElfCell(ActorState& hero, const HeroMovementContext& context)
{
    constexpr std::uint8_t ClassMask = 7;
    constexpr std::uint8_t ElfClass = 2;
    if (hero.movement == 0 || hero.movementType != 0 || (hero.characterClass & ClassMask) != ElfClass)
        return {};
    const auto [right, left] = OwnHands(context);
    const bool shoots =
        InRange(right, 0x808, 0x80f) || InRange(right, 0x810, 0xa00) || InRange(left, 0x800, 0x807) || left == 0x811;
    if (!shoots)
        return {};
    const auto y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    if (!context.sendElfCell)
        return {};
    return context.sendElfCell(x, y);
}

// 0x59d3de..0x59d8be: el clic sobre un personaje atacable. El blanco a 0x6c8ebc, el otro elegido (0x6c8e38) en 1 y
// +0x379 en 3; la celda del blanco, ftol(posición · 0,01f), a 0x77e1ba8 y 0x77e1bac. Si no se ve desde la celda lógica
// (0x558800), nada más. Si no, PathFinding2 hasta ella (en +0x3e8 si sale): con camino y sin arma de tiro (ni +0x376 en
// 9), SendMove (0x56acd0); si no, 0x561c50 y, con flechas, 0x5650c0(héroe, héroe, 1), que no lee el tercero. Sin
// flechas, MoveHero sale ahí (Leave).
Core::Expected<MoveHeroEnd, std::string> AttackTheChosen(ActorState& hero, const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    if (auto sent = SendTheElfCell(hero, context); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    // IsAttackable ya miró que el elegido esté en el pool (0x558bce).
    const std::int32_t chosen = runtime.selectedTargets[2];
    state.attackTarget = chosen;
    runtime.selectedTargets[4] = 1;
    hero.movementType = 3;
    const ActorState& victim = context.world.Actors()[static_cast<std::size_t>(chosen)];
    const auto targetX = World::OriginalFtolLow(static_cast<double>(victim.position.x) * CellReciprocal);
    const auto targetY = World::OriginalFtolLow(static_cast<double>(victim.position.y) * CellReciprocal);
    if (!targetX || !targetY)
        return Core::Unexpected(std::string("0x59d421: la celda del elegido fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*targetX);
    state.targetY = std::bit_cast<std::uint32_t>(*targetY);
    World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver al elegido"));
    // 0x59d439..0x59d531: la Y y la X, cada una en su acceso.
    const auto fromY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto fromX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    const auto seen = World::CanSeeTheCell(terrain.attributes->cells, fromX, fromY, *targetX, *targetY);
    if (!seen)
        return Core::Unexpected(std::string("0x55886d: la línea hasta el elegido sale de TerrainWall"));
    if (!*seen)
        return MoveHeroEnd::Continue;
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    constexpr std::int32_t LastCell = 0xff;
    if (*targetX < 0 || *targetX > LastCell || *targetY < 0 || *targetY > LastCell)
        return Core::Unexpected(std::format("0x59d653: la celda del elegido ({}, {}) fuera de la grilla, que "
                                            "PathFinding2 toma como índice lineal",
                                            *targetX, *targetY));
    const GameLogic::PathCell target{static_cast<std::uint8_t>(*targetX), static_cast<std::uint8_t>(*targetY)};
    const auto path = GameLogic::PathFinder(terrain.attributes->cells).Find({startX, startY}, target);
    if (path)
    {
        hero.path = *path;
        // 0x59d67d..0x59d8a6: las manos, en otro acceso; la ballesta de la derecha o el arco de la izquierda (con el
        // 0x814 y el 0x815) no caminan.
        const auto [right, left] = OwnHands(context);
        const bool shoots = InRange(right, 0x808, 0x80f) || InRange(right, 0x810, 0xa00) ||
                            InRange(left, 0x800, 0x807) || left == 0x811 || left == 0x814 || left == 0x815;
        constexpr std::uint16_t ShootingWord376 = 9;
        if (!shoots && hero.word376 != ShootingWord376)
        {
            if (auto sent = SendThePath(hero, target, context); !sent.HasValue())
                return Core::Unexpected(sent.Error());
            return MoveHeroEnd::Continue;
        }
    }
    // 0x59d65f..0x59d678.
    static const InventoryServices NoServices{};
    const auto arrows = CheckTheArrows(
        InventoryMoveContext{.world = context.world,
                             .random = context.random,
                             .services = context.inventory != nullptr ? *context.inventory : NoServices});
    if (!arrows.HasValue())
        return Core::Unexpected(arrows.Error());
    if (!arrows.Value())
        return MoveHeroEnd::Leave;
    if (auto arrived = InteractOnArrival(hero, ArrivalContextOf(context)); !arrived.HasValue())
        return Core::Unexpected(arrived.Error());
    return MoveHeroEnd::Continue;
}

// 0x5650c0(héroe, héroe, n), la llegada: por +0x379 (MOVSX, del 1 al 5 por la tabla 0x56ab20; con otro, nada en
// 0x5650fa). El 2 le habla al NPC elegido (NpcTalk.h, hito 2), que necesita el contexto de MoveHero; los demás van por
// InteractOnArrival (HeroAttack.h): el 3 pega (hito 3) y el 1, el 4 y el 5 cortan.
Core::Expected<void, std::string> ArriveAtTheTarget(ActorState& hero, const HeroMovementContext& context)
{
    if (static_cast<std::int8_t>(hero.movementType) == static_cast<std::int8_t>(NpcTalkMovement))
        return TalkToTheNpc(hero, context);
    return InteractOnArrival(hero, ArrivalContextOf(context));
}

// 0x59daaf..0x59dc59 (CL9 hito 2): +0x379 en 2 salvo con el bit 0x10000000 de +0x7c; el NPC en 0x6c8ebc, su celda
// como destino (0x77e1ba8 y 0x77e1bac, la posición · 0,01 con ftol), el NPC, su tipo y su ángulo en 0x6c8e54,
// 0x7798bf0 y 0x77f36cc; las celdas del héroe (y después x) y el camino (0x4eac00): con él, 0x56acd0; sin él, la
// llegada en el lugar (0x5650c0(héroe, héroe, 1)).
Core::Expected<void, std::string> WalkToTheNpc(ActorState& hero, const HeroMovementContext& context, std::int32_t npc)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    if (npc < 0 || static_cast<std::size_t>(npc) >= context.world.Actors().size())
        return Core::Unexpected(std::format("0x59dad9: el NPC elegido {} está fuera de la tabla de personajes", npc));
    if ((hero.flags7c & NpcTalkBlockedFlag) != NpcTalkBlockedFlag)
        hero.movementType = NpcTalkMovement;
    state.attackTarget = npc;
    const ActorState& target = context.world.Actors()[static_cast<std::size_t>(npc)];
    const auto x = World::OriginalFtolLow(static_cast<double>(target.position.x) * CellReciprocal);
    const auto y = World::OriginalFtolLow(static_cast<double>(target.position.y) * CellReciprocal);
    if (!x || !y)
        return Core::Unexpected(std::string("0x59daef: la posición del NPC fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*x);
    state.targetY = std::bit_cast<std::uint32_t>(*y);
    state.chosenNpc = npc;
    state.chosenNpcType = static_cast<std::int16_t>(target.type);
    state.chosenNpcAngle = std::bit_cast<std::uint32_t>(target.angles.z);
    World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x4eac00: falta TerrainWall para caminar"));
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    // 0x4eac0a..0x4eac41: con Crywolf activo (0x435a00 y el mapa 34) y un altar en 0x6c8e54 (0x204..0x208), el A*
    // (0x4ead70) va con una bandera que MuRet no tiene.
    constexpr std::int32_t CrywolfMap = 34;
    constexpr std::uint8_t ThirdStage = 3;
    constexpr std::uint8_t FourthStage = 4;
    constexpr std::int16_t FirstAltar = 0x204;
    constexpr std::int16_t LastAltar = 0x208;
    const bool crywolf =
        (runtime.crywolf.stage == ThirdStage || runtime.crywolf.stage == FourthStage) && runtime.map == CrywolfMap;
    const auto model = static_cast<std::int16_t>(target.type);
    if (crywolf && model >= FirstAltar && model <= LastAltar)
        return Core::Unexpected(std::string("0x4eac41: el camino a un altar de Crywolf (la bandera del A* de 0x4ead70) "
                                            "es del hito 11"));
    const GameLogic::PathCell goal{static_cast<std::uint8_t>(*x & 0xff), static_cast<std::uint8_t>(*y & 0xff)};
    if (*x != goal.x || *y != goal.y)
        return Core::Unexpected(std::format("0x59dc32: el destino ({}, {}) del NPC sale del mapa", *x, *y));
    if (const auto path = GameLogic::PathFinder(terrain.attributes->cells).Find({startX, startY}, goal))
    {
        hero.path = *path;
        return SendThePath(hero, goal, context);
    }
    return ArriveAtTheTarget(hero, context);
}

Core::Expected<MoveHeroEnd, std::string> ProcessInput(ActorState& hero, const HeroMovementContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    // 0x59b846..0x59b85a: el modo del editor (0x77f3b18) y el mouse de la UI (0x775857c) saltan también la suma.
    if (state.inputBlocked || runtime.ui.mouseOnUi != 0)
        return MoveHeroEnd::Continue;
    if (state.mouseUpdateTime >= state.mouseUpdateTimeMax && !state.movementBlocked)
    {
        // 0x59b882..0x59b8cd: el clic pendiente (que se gasta), el botón apretado o el ataque automático con el otro
        // elegido (0x6c8e38) en 1, lo que deja el clic de ataque, y un elegido (0x6c8e30).
        bool clicked = state.leftHeld;
        if (state.pendingClick)
        {
            state.pendingClick = false;
            clicked = true;
        }
        if (AutoAttacksHere(runtime) && runtime.selectedTargets[4] == 1 && runtime.selectedTargets[2] != -1)
            clicked = true;
        if (!clicked)
        {
            state.heldSince = context.input.worldTime;
            state.heldElapsed = 0.0f;
        }
        else
        {
            const double elapsed = (static_cast<double>(context.input.worldTime) - state.heldSince) *
                                   static_cast<double>(HoldSecondsPerMillisecond);
            state.heldElapsed = static_cast<float>(elapsed);
            if (elapsed >= MaximumHoldSeconds)
                state.leftHeld = false;
            else if (CanStartThePath(hero))
            {
                if (const auto closed = CloseTheClickRecord(context); !closed.HasValue())
                    return Core::Unexpected(closed.Error());
                state.mouseUpdateTime = 0;
                // 0x59cd2e..0x59cd43: fuera de la zona segura, 0x558bb0 dice si el personaje elegido se ataca (sin
                // elegido, o con un ítem en el cursor, 0x7879fbc, da 0 y sigue en 0x59d8c3, como acá).
                bool attacks = false;
                if (hero.safeZone == 0)
                {
                    const auto attackable = IsAttackable(context.world, context.input.controlDown);
                    if (!attackable.HasValue())
                        return Core::Unexpected(attackable.Error());
                    attacks = attackable.Value();
                }
                // 0x59da75..0x59daa9: sin atacar, el NPC elegido (0x6c8e2c), sin la tienda de NPC (un rand() del byte
                // protegido) ni el almacén abiertos, va a hablarle y sigue en 0x59e248; si no, sigue en el terreno.
                bool toTheNpc = false;
                if (!attacks && runtime.selectedTargets[1] != -1)
                {
                    GamePanels panels(context.world, context.random);
                    toTheNpc =
                        !panels.Registry().IsOpen(UI::NpcShopPanel) && !panels.Registry().IsOpen(UI::WarehousePanel);
                }
                if (attacks)
                {
                    const auto attacked = AttackTheChosen(hero, context);
                    if (!attacked.HasValue())
                        return Core::Unexpected(attacked.Error());
                    if (attacked.Value() == MoveHeroEnd::Leave)
                        return MoveHeroEnd::Leave;
                }
                else if (toTheNpc)
                {
                    if (const auto walked = WalkToTheNpc(hero, context, runtime.selectedTargets[1]); !walked.HasValue())
                        return Core::Unexpected(walked.Error());
                }
                else if (const auto walked = UseGroundClick(hero, context); !walked.HasValue())
                {
                    return Core::Unexpected(walked.Error());
                }
            }
        }
    }
    // 0x59e248: el contador nativo es un DWORD: conservar el wrap sin overflow con signo en C++.
    state.mouseUpdateTime = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.mouseUpdateTime) + 1u);
    return MoveHeroEnd::Continue;
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
        runtime,           SceneId::Game,  *context.playerSkeleton,
        context.items,     context.random, terrain.attributes ? &terrain.attributes->cells : nullptr,
        context.sendAction};
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
        // 0x59b2f7: 0x5650c0(héroe, héroe, 0). Con +0x379 en 0 sale por la tabla, sin interacción ni RNG; el 2 le habla
        // al NPC (CL9 hito 2) y el 3 pega (CL9 hito 3).
        return ArriveAtTheTarget(hero, context);
    }
    hero.flags7c &= ~std::uint32_t{0x200000};
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x508a80: falta terreno para mover al héroe"));
    return UpdateActorPosition(hero, {runtime, context.random, [&](float x, float y) {
                                          return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y,
                                                                        context.fixedHeight);
                                      }});
}

// 0x59afdf..0x59b185, entre el contador y la cabeza: el clic derecho (0x7d36880) sobre el personaje elegido
// (0x6c8e30, el picking del frame). Nada de esto saca rand().
Core::Expected<void, std::string> NameTheChosen(WorldState& world)
{
    WorldRuntime& runtime = world.Runtime();
    // 0x59afdf..0x59b033: con la ventana de una carta (+0xf0 de [0x5763514]) y el derecho, el nombre del elegido
    // (+0x1d9, sin mirar el −1) va al campo de la ventana (+0xb4, los métodos +0x4c y +0x30). MuRet no la abre hasta el
    // hito 10 (CL9.4): `letter` queda en 0.
    if (runtime.oldWindows.letter != 0 && runtime.ui.rightClick &&
        UI::FindTheOldWindow(runtime.oldWindows, runtime.oldWindows.letter) != nullptr)
        return Core::Unexpected(
            std::string("0x59b003: el nombre del elegido en la ventana de la carta es del hito 10"));
    // 0x59b036..0x59b086: el chat abierto (0x6c8e68), el héroe en la zona segura (+0x3e2), un elegido, el derecho y
    // +0x376 del elegido en 0xff (solo lo escribe 0x5140c0, la cola del alta de un monstruo, con el tipo 255).
    const std::int32_t chosen = runtime.selectedTargets[2];
    const ActorState& hero = world.Actors()[runtime.hero];
    if (!runtime.ui.chatOpen || hero.safeZone == 0 || chosen == -1 || !runtime.ui.rightClick)
        return {};
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= world.Actors().size())
        return Core::Unexpected(std::string("0x59b06d: el elegido está fuera de la tabla de personajes"));
    constexpr std::uint16_t WhisperableWord376 = 0xff;
    const ActorState& target = world.Actors()[static_cast<std::size_t>(chosen)];
    if (target.word376 != WhisperableWord376)
        return {};
    // 0x59b092: el derecho se gasta también en Chaos Castle (18 a 23), donde no sigue.
    runtime.ui.rightClick = false;
    constexpr std::int32_t FirstChaosCastleMap = 18;
    constexpr std::int32_t LastChaosCastleMap = 23;
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
        return {};
    UI::ChatInputState& input = runtime.chat.input;
    UI::ChatBuffer& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
    // 0x59b0a7..0x59b0ca: 0x4694c0(0, 0, 1, lugar) con 0x6c7b8c en 1, antes de las copias: el EDIT del susurro toma lo
    // que había en el lugar de ahora del historial (0x77f31cc + 0x6c8ea8 · 0x100), no el nombre nuevo (Q-C804).
    UI::SetTheChatFields(input, std::nullopt, UI::CStringOf(slot));
    // 0x59b0cf..0x59b16b: strcpy del nombre (+0x1d9) al buffer del susurro (0x7798cf8), del buffer al lugar, el buffer
    // en 0 entero (0x100 bytes) y otra vez del lugar al buffer; 0x59b17a, su largo (0x7758594).
    UI::CopyIntoTheBuffer(input.whisperBuffer, ChatNameOf(target));
    UI::CopyIntoTheBuffer(slot, UI::CStringOf(input.whisperBuffer));
    input.whisperBuffer.fill('\0');
    UI::CopyIntoTheBuffer(input.whisperBuffer, UI::CStringOf(slot));
    input.whisperLength = static_cast<std::int32_t>(UI::CStringOf(input.whisperBuffer).size());
    // 0x59b180: el sonido 0x19.
    runtime.lastSoundId = UI::ChatButtonSound;
    return {};
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
    if (runtime.movement.cameraTour)
    {
        if (!context.playerSkeleton)
            return Core::Unexpected(std::string("0x59ae24: falta Player.bmd para parar durante el recorrido"));
        const auto& terrain = context.world.Map().terrain;
        const auto stopped =
            SetActorStop(hero, {runtime, SceneId::Game, *context.playerSkeleton, context.items, context.random,
                                terrain.attributes ? &terrain.attributes->cells : nullptr, context.sendAction});
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
    if (auto named = NameTheChosen(context.world); !named.HasValue())
        return named;
    // 0x59b188: el ancho del mundo, con sus consultas (2 rand() con el inventario abierto); la mitad, con signo.
    GamePanels panels(context.world, context.random);
    const std::int32_t center = panels.Registry().WorldWidth() / 2;
    const bool keepsTheHead = AutoAttacksHere(runtime) && runtime.selectedTargets[4] != -1;
    if (auto head = UpdateTheHead(hero, context.input, center, keepsTheHead); !head.HasValue())
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
                                            .headTurningAllowed = !keepsTheHead && hero.animation.action != 0x3e,
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
    const auto input = ProcessInput(hero, context);
    if (!input.HasValue())
        return Core::Unexpected(input.Error());
    // 0x59d666: sin flechas o virotes (0x561c50 en 0), MoveHero sale (0x59e2c0) sin la suma, 0x57cfc0 ni la capa.
    if (input.Value() == MoveHeroEnd::Leave)
        return {};
    if (const auto closed = CloseTheHeroActionPrefix(hero, context); !closed.HasValue())
        return closed;
    PublishHeroLayer(hero, runtime, context.world.Map().terrain);
    return {};
}
} // namespace Client::Scenes
