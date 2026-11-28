#include "Movement/GameMovement.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Kernel/MouseState.h"
#include "Kernel/CharacterClass.h"
#include "Pathfinding/Path.h"
#include "Pathfinding/PathCell.h"
#include "Actors/WalkPath.h"
#include "Maps/MapId.h"
#include "Protocol/Movement/Movement.h"
#include "Models/IModelLibrary.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorMotion.h"
#include "Actors/ActorWalk.h"
#include "Chat/ChatBubbles.h"
#include "Items/FloorItemActions.h"
#include "Items/FloorItemPackets.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/SceneId.h"
#include "Movement/GameMovementState.h"
#include "Movement/GameGates.h"
#include "Movement/GameHeroFacing.h"
#include "Items/InventoryMoves.h"
#include "Movement/HeroMovementHooks.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowSteps.h"
#include "Math/Ftol.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainHeight.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Assets/FaunaModels.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <glm/ext/vector_float3.hpp>

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
#include <string_view>
#include <utility>

namespace Client::Movement
{
namespace
{
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
constexpr std::uint32_t MovementLockedFlag = 0x20;
// 0x56b785: what SendMove leaves in 0x6c8dcc when it closes the event window.
constexpr std::int32_t EventWindowUpdateTimeMax = 6;
// 0x56b397..0x56b4d6: the windows that walking closes, in this order.
constexpr std::array<Ui::PanelId, 9> PanelsClosedByWalking{
    Ui::TradePanel, Ui::WarehousePanel, Ui::SecondGuildPanel, Ui::ChaosBoxPanel, Ui::QuestPanel,
    Ui::Panel2000,  Ui::NpcShopPanel,   Ui::CastleSiegePanel, Ui::EventNpcPanel};
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
constexpr float MinimumWalkingAlpha = 0.7f;
constexpr float HoldSecondsPerMillisecond = 0.001f;
constexpr double MaximumHoldSeconds = 3600.0;
// 0x5650ef: case 1 of 0x5650c0, picking up the floor item (0x59dc6e and 0x5ac99a).
constexpr std::uint8_t PickUpMovement = 1;
constexpr std::string_view NoHooks = "0x59adf0: no hero movement hooks (Session::HeroMovementHooks)";

// How MoveHero's click ends: it runs to the end (0x59e248) or leaves without it (0x59e2c0).
enum class MoveHeroEnd : std::uint8_t
{
    Continue,
    Leave,
};

Core::Expected<Actors::ActorStopContext, std::string> StopContextForActor(const Kernel::ActorState& actor,
                                                                          const HeroMovementContext& context,
                                                                          const Terrain::WorldTerrain& terrain,
                                                                          std::string_view missingPlayer)
{
    static const Models::Skeleton EmptySkeleton;
    const Models::Skeleton* playerSkeleton = context.playerSkeleton;
    if (actor.type == Tables::PlayerSkeletonModel && playerSkeleton == nullptr)
        return Core::Unexpected(std::string(missingPlayer));
    if (playerSkeleton == nullptr)
        playerSkeleton = &EmptySkeleton;

    const Models::Skeleton* actorSkeleton = nullptr;
    const Assets::FaunaModels& faunaModels = Assets::FaunaModelsOrEmpty(context.faunaModels);
    const std::span<const std::int16_t> modelSounds = faunaModels.SoundActions(actor.type);
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        actorSkeleton = faunaModels.SkeletonOf(Models::ModelId{actor.type});
        if (actorSkeleton == nullptr && context.models != nullptr)
            actorSkeleton = context.models->SkeletonOf(Models::ModelId{actor.type});
    }

    return Actors::ActorStopContext{.runtime = context.world.Runtime(),
                                    .scene = Kernel::SceneId::Game,
                                    .playerSkeleton = *playerSkeleton,
                                    .items = context.items,
                                    .random = context.random,
                                    .terrainWalls = terrain.attributes ? &terrain.attributes->cells : nullptr,
                                    .sendAction = context.sendAction,
                                    .actorSkeleton = actorSkeleton,
                                    .modelSounds = modelSounds};
}

Core::Expected<void, std::string> CloseTheClickRecord(const HeroMovementContext& context)
{
    std::int16_t right = 0;
    std::int16_t left = 0;
    {
        const Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                        Models::OwnRecordCloseRandoms);
        right = std::bit_cast<std::int16_t>(context.world.OwnCharacter().equipment[0].id);
        left = std::bit_cast<std::int16_t>(context.world.OwnCharacter().equipment[1].id);
    }
    // 0x59bd01..0x59bd38: with the cursor empty, the right hand (+0xb6c) empty and a weapon of groups 0 to 2 or 5 in
    // the left (+0xbb4), the original moves that weapon to the cursor (0x59bdf6), dresses the hero (0x512a30) and sends
    // the C1 .. 24 that takes it to the right (0x59bf1c). With a weapon in the right it does none of that and goes on
    // with what is under the mouse (0x59cd23).
    const bool weaponOnTheLeft = (left >= 0 && left < 0x600) || (left >= 0xa00 && left < 0xc00);
    if (context.world.Runtime().cursorItemCount <= 0 && right == -1 && weaponOnTheLeft)
    {
        static const Items::InventoryServices NoServices{};
        return Items::MoveTheLeftWeaponToTheRight(
            Items::InventoryMoveContext{.world = context.world,
                                        .random = context.random,
                                        .services = context.inventory != nullptr ? *context.inventory : NoServices});
    }
    return {};
}

bool CanStartThePath(const Kernel::ActorState& hero)
{
    const auto action = hero.animation.action;
    const bool restricted = action == 0xc2 || hero.byte80 == 1 || hero.byte80 == 2 ||
                            !(hero.alpha >= MinimumWalkingAlpha) || (action >= 0x22 && action <= 0x97);
    // 0x59bac6..0x59bb1f: these three families pass even the earlier guards.
    return !restricted || (action >= 0x8a && action <= 0x8c) || (action >= 0x48 && action <= 0x4b) ||
           (action >= 0x6a && action <= 0x7d);
}

// 0x59b1c2..0x59b1e2: the center is half the world width (0x59b188), not a fixed 320.
Core::Expected<void, std::string> UpdateTheHead(Kernel::ActorState& hero, const HeroMovementInput& input,
                                                std::int32_t center, bool keepsTheHead)
{
    const double angle = Math::CreateAngle(static_cast<float>(center), 180.0f, static_cast<float>(input.mouseX),
                                           static_cast<float>(input.mouseY));
    const auto integer = Math::OriginalFtolLow(angle + static_cast<double>(hero.angles.z));
    if (!integer)
        return Core::Unexpected(std::string("0x59b1f3: ángulo de la cabeza fuera del perfil ftol"));
    const std::int32_t shifted = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(*integer) + 315u);
    const std::int32_t target = std::clamp(shifted % 360, 120, 240);
    // 0x59b22f..0x59b261: with the auto attack and the other chosen (0x6c8e38) other than −1 outside map 6 and Chaos
    // Castle, or with action 0x3e, the head goes to 0 and the idle hero does not turn (BL at 0, 0x59b3c4).
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

std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x56b72f..0x56b77f: the SetCursorPos place. Y = height · y / 480 (unsigned MUL); X = (640 · x / 260, signed IMUL) ·
// width / 640 (unsigned MUL): the cursor lands at x · width / 260, not at x · width / 640 (Q-C1330).
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

// 0x56b4e8..0x56b78f: the C1 03 97 (unencrypted, through the [0x6c82a0] socket); with 3, the serial number entry;
// 0x78abb00 at 0, SetCursorPos, 0x6c8dcc at 6 and the left button released (0x7d36874).
Core::Expected<void, std::string> LeaveTheEventWindow(const HeroMovementContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!context.sendEventWindowLeave)
        return Core::Unexpected(std::string("0x56b4e8: falta el servicio de envío del C1 03 97"));
    if (auto sent = context.sendEventWindowLeave(); !sent.HasValue())
        return sent;
    // 0x56b6a9..0x56b72a.
    context.hooks->CloseTheSerialEntry(runtime);
    runtime.panels.field78abb00 = 0;
    // 0x56b77f: SetCursorPos moves the Windows cursor; it stays requested for the platform.
    const auto [x, y] =
        EventWindowCursor(context.input.mouseX, context.input.mouseY, context.screen.width, context.screen.height);
    runtime.movement.cursorWarpRequest = std::array<std::int32_t, 2>{x, y};
    runtime.mouse.updateTimeMax = EventWindowUpdateTimeMax;
    runtime.mouse.leftHeld = false;
    return {};
}

Core::Expected<void, std::string> SendThePath(Kernel::ActorState& hero, Pathfinding::PathCell target,
                                              const HeroMovementContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    const IHeroMovementHooks& hooks = *context.hooks;
    // 0x56acf7: with the box NPC open (0x8000), nothing.
    if (hooks.IsPanelOpen(context, Ui::TrainerPanel) || (hero.flags78 & MovementLockedFlag) != 0)
        return {};
    const auto maximum = Protocol::Movement::WalkUpdateTimeMax(hero.path.count);
    if (!maximum.HasValue())
        return Core::Unexpected(std::string("0x56acd0: cantidad de camino inválida"));
    mouse.updateTimeMax = static_cast<std::int32_t>(maximum.Value());
    std::array<Protocol::Movement::Cell, Pathfinding::MaxPathCells> cells{};
    for (std::size_t i = 0; i < hero.path.count; ++i)
        cells[i] = {hero.path.cells[i].x, hero.path.cells[i].y};
    if (!context.sendWalk)
        return Core::Unexpected(std::string("0x56acd0: falta el servicio de envío D3"));
    if (const auto sent =
            context.sendWalk(std::span(cells).first(hero.path.count), {target.x, target.y}, hero.angles.z);
        !sent.HasValue())
        return sent;
    // 0x56b387: with the disconnect notice (0x70) the hero does not walk.
    if (runtime.dialogs.current != Ui::DisconnectDialog)
        hero.movement = 1;
    // 0x56b397..0x56b4d6: each open one closes; the trade and NPC shop ones draw a rand() even when closed.
    for (const Ui::PanelId id : PanelsClosedByWalking)
    {
        if (!hooks.IsPanelOpen(context, id))
            continue;
        if (auto closed = hooks.ClosePanel(context, id); !closed.HasValue())
            return closed;
    }
    // 0x56b4db: with the event window (0x78abb00), the C1 03 97 (CL9 milestone 11).
    if (runtime.panels.field78abb00 == 0)
        return {};
    return LeaveTheEventWindow(context);
}

Core::Expected<void, std::string> UseGroundClick(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    if (context.input.shiftDown || !context.pickTarget)
        return {};
    const auto picked = context.pickTarget();
    if (!picked.HasValue())
        return Core::Unexpected(picked.Error());
    if (!picked.Value())
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x4eac00: falta TerrainWall para caminar"));
    GameMovementState& state = context.world.Runtime().movement;
    Kernel::MouseState& mouse = context.world.Runtime().mouse;
    if (hero.movement != 0)
    {
        const auto visibleX = Math::OriginalFtolLow(static_cast<double>(hero.position.x) * CellReciprocal);
        const auto visibleY = Math::OriginalFtolLow(static_cast<double>(hero.position.y) * CellReciprocal);
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
        mouse.updateTime = 0;
        return {};
    }
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    // 0x4eac0a..0x4eac41: with a Crywolf altar in 0x6c8e54, a click on the ground also sets the goal to 0.
    const auto path = Actors::FindThePath(context.world, terrain.attributes->cells, {startX, startY}, target);
    if (!path)
    {
        mouse.updateTime = 0;
        return {};
    }
    hero.path = *path;
    hero.movementType = 0;
    return SendThePath(hero, target, context);
}

// 0x59b23d..0x59b258, 0x59b8a7..0x59b8b9 and 0x57d1bc..0x57d1d7: the auto attack (0x6c8e3c), outside map 6 and Chaos
// Castle (18 to 23).
bool AutoAttacksHere(const Kernel::WorldRuntime& runtime)
{
    constexpr std::int32_t SpectatorMap = 6;
    using Maps::FirstChaosCastleMap;
    using Maps::LastChaosCastleMap;
    return runtime.ui.autoAttack && runtime.map != SpectatorMap &&
           (runtime.map < FirstChaosCastleMap || runtime.map > LastChaosCastleMap);
}

// The own record's hands (+0xb6c and +0xbb4, MOVSX), in one access.
std::pair<std::int32_t, std::int32_t> OwnHands(const HeroMovementContext& context)
{
    const Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                    Models::OwnRecordCloseRandoms);
    const auto& equipment = context.world.OwnCharacter().equipment;
    return {std::bit_cast<std::int16_t>(equipment[0].id), std::bit_cast<std::int16_t>(equipment[1].id)};
}

bool InHalfOpenRange(std::int32_t value, std::int32_t first, std::int32_t end)
{
    return value >= first && value < end;
}

// 0x59cd49..0x59d3d7: the elf (+0x1d4 & 7 at 2) that walks (+0x378) without interaction (+0x379 at 0) and with a
// crossbow in the right (0x808..0x80e or 0x810..0x9ff) or a bow in the left (0x800..0x806 or 0x811, not 0x814 or
// 0x815) sends its logical cell, the Y (+0x420) read before the X (+0x41c): C1 05 DF X Y. Empty, it sends nothing.
Core::Expected<void, std::string> SendTheElfCell(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    if (hero.movement == 0 || hero.movementType != 0 ||
        Kernel::BaseClassOf(hero.characterClass) != Kernel::CharacterClass::Elf)
        return {};
    const auto [right, left] = OwnHands(context);
    const bool shoots = InHalfOpenRange(right, 0x808, 0x80f) || InHalfOpenRange(right, 0x810, 0xa00) ||
                        InHalfOpenRange(left, 0x800, 0x807) || left == 0x811;
    if (!shoots)
        return {};
    const auto y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    if (!context.sendElfCell)
        return {};
    return context.sendElfCell(x, y);
}

// 0x59d3de..0x59d8be: the click on an attackable character. The target to 0x6c8ebc, the other chosen (0x6c8e38) at 1
// and +0x379 at 3; the target's cell, ftol(position · 0.01f), to 0x77e1ba8 and 0x77e1bac. If it is not seen from the
// logical cell (0x558800), nothing else. Otherwise, PathFinding2 to it (in +0x3e8 if found): with a path and no ranged
// weapon (nor +0x376 at 9), SendMove (0x56acd0); otherwise, 0x561c50 and, with arrows, 0x5650c0(hero, hero, 1), which
// does not read the third. Without arrows, MoveHero leaves there (Leave).
Core::Expected<MoveHeroEnd, std::string> AttackTheChosen(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    if (auto sent = SendTheElfCell(hero, context); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    // IsAttackable already checked that the chosen one is in the pool (0x558bce).
    const std::int32_t chosen = runtime.selectedTargets[2];
    state.attackTarget = chosen;
    runtime.selectedTargets[4] = 1;
    hero.movementType = 3;
    const Kernel::ActorState& victim = context.world.Actors()[static_cast<std::size_t>(chosen)];
    const auto targetX = Math::OriginalFtolLow(static_cast<double>(victim.position.x) * CellReciprocal);
    const auto targetY = Math::OriginalFtolLow(static_cast<double>(victim.position.y) * CellReciprocal);
    if (!targetX || !targetY)
        return Core::Unexpected(std::string("0x59d421: la celda del elegido fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*targetX);
    state.targetY = std::bit_cast<std::uint32_t>(*targetY);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver al elegido"));
    // 0x59d439..0x59d531: the Y and the X, each in its own access.
    const auto fromY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto fromX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    // A line that leaves TerrainWall (0x55886d reads the memory next to it, 0x7d0d348 + 2 · index): MuRet discards the
    // click as unseen, with the rand() of both reads (07, Q-C1179).
    const auto seen = Pathfinding::CanSeeTheCell(terrain.attributes->cells, fromX, fromY, *targetX, *targetY);
    if (!seen || !*seen)
        return MoveHeroEnd::Continue;
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    // The chosen one's cell outside the grid, which the A* takes as a linear index (the next row, or outside the 65536
    // WORDs): MuRet discards the click, with the rand() of the four reads (07, Q-C1179).
    constexpr std::int32_t LastCell = 0xff;
    if (*targetX < 0 || *targetX > LastCell || *targetY < 0 || *targetY > LastCell)
        return MoveHeroEnd::Continue;
    const Pathfinding::PathCell target{static_cast<std::uint8_t>(*targetX), static_cast<std::uint8_t>(*targetY)};
    // 0x59d653: PathFinding2 with the Crywolf altar flag (0x4eac0a..0x4eac41): no distance; with an altar chosen, the
    // goal in the TerrainWall stays at 0.
    const auto path = Actors::FindThePath(context.world, terrain.attributes->cells, {startX, startY}, target);
    if (path)
    {
        hero.path = *path;
        // 0x59d67d..0x59d8a6: the hands, in another access; the crossbow in the right or the bow in the left (with
        // 0x814 and 0x815) do not walk.
        const auto [right, left] = OwnHands(context);
        const bool shoots = InHalfOpenRange(right, 0x808, 0x80f) || InHalfOpenRange(right, 0x810, 0xa00) ||
                            InHalfOpenRange(left, 0x800, 0x807) || left == 0x811 || left == 0x814 || left == 0x815;
        constexpr std::uint16_t ShootingWord376 = 9;
        if (!shoots && hero.word376 != ShootingWord376)
        {
            if (auto sent = SendThePath(hero, target, context); !sent.HasValue())
                return Core::Unexpected(sent.Error());
            return MoveHeroEnd::Continue;
        }
    }
    // 0x59d65f..0x59d678.
    static const Items::InventoryServices NoServices{};
    const auto arrows = Items::CheckTheArrows(
        Items::InventoryMoveContext{.world = context.world,
                                    .random = context.random,
                                    .services = context.inventory != nullptr ? *context.inventory : NoServices});
    if (!arrows.HasValue())
        return Core::Unexpected(arrows.Error());
    if (!arrows.Value())
        return MoveHeroEnd::Leave;
    if (auto arrived = context.hooks->InteractOnArrival(hero, context); !arrived.HasValue())
        return Core::Unexpected(arrived.Error());
    return MoveHeroEnd::Continue;
}

// 0x5650c0(hero, hero, n), the arrival: by +0x379 (MOVSX, 1 to 5 through table 0x56ab20; with another, nothing at
// 0x5650fa). 2 talks to the chosen NPC (NpcTalk.h, milestone 2) and 1 (0x5650ef..0x565100, milestone 5) picks up the
// chosen floor item, which need MoveHero's context; the others go through InteractOnArrival (HeroAttack.h): 3 hits
// (milestone 3) and 4 and 5 stop.
Core::Expected<void, std::string> ArriveAtTheTarget(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    if (static_cast<std::int8_t>(hero.movementType) == static_cast<std::int8_t>(NpcTalkMovement))
        return context.hooks->TalkToTheNpc(hero, context);
    if (static_cast<std::int8_t>(hero.movementType) == static_cast<std::int8_t>(PickUpMovement))
    {
        static const Items::InventoryServices NoServices{};
        return Items::PickUpTheFloorItem(
            Items::InventoryMoveContext{.world = context.world,
                                        .random = context.random,
                                        .services = context.inventory != nullptr ? *context.inventory : NoServices},
            hero.position, Terrain::TerrainOrEmpty(context.terrain), context.fixedHeight);
    }
    return context.hooks->InteractOnArrival(hero, context);
}

// 0x59daaf..0x59dc59 (CL9 milestone 2): +0x379 at 2 except with bit 0x10000000 of +0x7c; the NPC in 0x6c8ebc, its cell
// as the destination (0x77e1ba8 and 0x77e1bac, the position · 0.01 with ftol), the NPC, its type and its angle in
// 0x6c8e54, 0x7798bf0 and 0x77f36cc; the hero's cells (y, then x) and the path (0x4eac00): with it, 0x56acd0; without
// it, the arrival in place (0x5650c0(hero, hero, 1)).
Core::Expected<void, std::string> WalkToTheNpc(Kernel::ActorState& hero, const HeroMovementContext& context,
                                               std::int32_t npc)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    if (npc < 0 || static_cast<std::size_t>(npc) >= context.world.Actors().size())
        return Core::Unexpected(std::format("0x59dad9: el NPC elegido {} está fuera de la tabla de personajes", npc));
    if ((hero.flags7c & NpcTalkBlockedFlag) != NpcTalkBlockedFlag)
        hero.movementType = NpcTalkMovement;
    state.attackTarget = npc;
    const Kernel::ActorState& target = context.world.Actors()[static_cast<std::size_t>(npc)];
    const auto x = Math::OriginalFtolLow(static_cast<double>(target.position.x) * CellReciprocal);
    const auto y = Math::OriginalFtolLow(static_cast<double>(target.position.y) * CellReciprocal);
    if (!x || !y)
        return Core::Unexpected(std::string("0x59daef: la posición del NPC fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*x);
    state.targetY = std::bit_cast<std::uint32_t>(*y);
    runtime.chosenNpc.index = npc;
    runtime.chosenNpc.type = static_cast<std::int16_t>(target.type);
    runtime.chosenNpc.angle = std::bit_cast<std::uint32_t>(target.angles.z);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x4eac00: falta TerrainWall para caminar"));
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    const Pathfinding::PathCell goal{static_cast<std::uint8_t>(*x & 0xff), static_cast<std::uint8_t>(*y & 0xff)};
    if (*x != goal.x || *y != goal.y)
        return Core::Unexpected(std::format("0x59dc32: el destino ({}, {}) del NPC sale del mapa", *x, *y));
    // 0x4eac0a..0x4eac41: with Crywolf active (0x435a00 and map 34) and the altar just chosen in 0x6c8e54
    // (0x204..0x208), the A* (0x4ead70) sets its cell's word to 0 and the path reaches the altar.
    if (const auto path = Actors::FindThePath(context.world, terrain.attributes->cells, {startX, startY}, goal))
    {
        hero.path = *path;
        return SendThePath(hero, goal, context);
    }
    return ArriveAtTheTarget(hero, context);
}

// 0x59dc5e..0x59ddea (CL9 milestone 5): with a chosen floor item (0x6c8e28), +0x379 at 1, the chosen one (0x77f27b0)
// and its cell as the destination (0x77e1ba8 and 0x77e1bac, the position · 0.01 without 0x59de66's mask), the hero's
// cells (y, then x) and the path (0x4eac00): with it, 0x56acd0; without it, 0x5650c0(hero, hero, 1) and +0x379 at 0.
Core::Expected<void, std::string> WalkToTheFloorItem(Kernel::ActorState& hero, const HeroMovementContext& context,
                                                     std::int32_t index)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    if (index < 0 || index >= Items::FloorItemCount)
        return Core::Unexpected(std::format("0x59dc82: el ítem del piso {} está fuera de la tabla 0x77f46c8", index));
    hero.movementType = PickUpMovement;
    runtime.floor.chosen = index;
    const glm::vec3& position = context.world.DrawTail().floorItems[static_cast<std::size_t>(index)].position;
    const auto x = Math::OriginalFtolLow(static_cast<double>(position.x) * CellReciprocal);
    const auto y = Math::OriginalFtolLow(static_cast<double>(position.y) * CellReciprocal);
    if (!x || !y)
        return Core::Unexpected(std::string("0x59dc8e: la posición del ítem del piso fuera del perfil ftol"));
    state.targetX = static_cast<std::uint32_t>(*x);
    state.targetY = static_cast<std::uint32_t>(*y);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x4eac00: falta TerrainWall para caminar"));
    const auto startY = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
    const auto startX = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
    const Pathfinding::PathCell target{static_cast<std::uint8_t>(*x & 0xff), static_cast<std::uint8_t>(*y & 0xff)};
    if (*x != target.x || *y != target.y)
        return Core::Unexpected(std::format("0x59ddbc: el destino ({}, {}) del ítem del piso sale del mapa", *x, *y));
    // 0x59ddbc: PathFinding2 with the Crywolf altar flag (0x4eac0a..0x4eac41, Q-C1351): with an altar still in
    // 0x6c8e54, the item's cell stays at 0 in TerrainWall.
    if (const auto path = Actors::FindThePath(context.world, terrain.attributes->cells, {startX, startY}, target))
    {
        hero.path = *path;
        return SendThePath(hero, target, context);
    }
    static const Items::InventoryServices NoServices{};
    auto picked = Items::PickUpTheFloorItem(
        Items::InventoryMoveContext{.world = context.world,
                                    .random = context.random,
                                    .services = context.inventory != nullptr ? *context.inventory : NoServices},
        hero.position, Terrain::TerrainOrEmpty(context.terrain), context.fixedHeight);
    hero.movementType = 0;
    return picked;
}

Core::Expected<MoveHeroEnd, std::string> ProcessInput(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    Kernel::MouseState& mouse = runtime.mouse;
    // 0x59b846..0x59b85a: the editor mode (0x77f3b18) and the UI mouse (0x775857c) also skip the sum.
    if (state.inputBlocked || runtime.ui.mouseOnUi != 0)
        return MoveHeroEnd::Continue;
    if (mouse.updateTime >= mouse.updateTimeMax && !state.movementBlocked)
    {
        // 0x59b882..0x59b8cd: the pending click (which is consumed), the held button or the auto attack with the other
        // chosen (0x6c8e38) at 1, which the attack click leaves, and a chosen one (0x6c8e30).
        bool clicked = mouse.leftHeld;
        if (mouse.pendingClick)
        {
            mouse.pendingClick = false;
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
                mouse.leftHeld = false;
            else if (CanStartThePath(hero))
            {
                if (const auto closed = CloseTheClickRecord(context); !closed.HasValue())
                    return Core::Unexpected(closed.Error());
                mouse.updateTime = 0;
                // 0x59cd2e..0x59cd43: outside the safe zone, 0x558bb0 says whether the chosen character is attacked
                // (with none chosen, or with an item on the cursor, 0x7879fbc, it gives 0 and goes on at 0x59d8c3, as
                // here).
                bool attacks = false;
                if (hero.safeZone == 0)
                {
                    const auto attackable = context.hooks->IsAttackable(context.world, context.input.controlDown);
                    if (!attackable.HasValue())
                        return Core::Unexpected(attackable.Error());
                    attacks = attackable.Value();
                }
                // 0x59d8c3..0x59da70: without attacking, the chosen object (0x6c8e34). 0x59eb90 is not ported and
                // leaves it at −1.
                if (!attacks && runtime.selectedTargets[3] != -1)
                    return Core::Unexpected(
                        std::string("0x59d8d1: ir al objeto elegido (0x6c8e34, 0x59eb90) no está portado"));
                // 0x59da75..0x59daa9: without attacking, the chosen NPC (0x6c8e2c), with neither the NPC shop (a rand()
                // of the protected byte) nor the warehouse open, goes to talk to it and goes on at 0x59e248; otherwise,
                // it goes on with the floor item.
                bool toTheNpc = false;
                if (!attacks && runtime.selectedTargets[1] != -1)
                {
                    toTheNpc = !context.hooks->IsPanelOpen(context, Ui::NpcShopPanel) &&
                               !context.hooks->IsPanelOpen(context, Ui::WarehousePanel);
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
                // 0x59dc5e..0x59ddea: with a chosen floor item, walk to it (CL9 milestone 5); otherwise, the terrain.
                else if (runtime.selectedTargets[0] != -1)
                {
                    if (const auto walked = WalkToTheFloorItem(hero, context, runtime.selectedTargets[0]);
                        !walked.HasValue())
                        return Core::Unexpected(walked.Error());
                }
                else if (const auto walked = UseGroundClick(hero, context); !walked.HasValue())
                {
                    return Core::Unexpected(walked.Error());
                }
            }
        }
    }
    // 0x59e248: the native counter is a DWORD: keep the wrap without signed overflow in C++.
    mouse.updateTime = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(mouse.updateTime) + 1u);
    return MoveHeroEnd::Continue;
}

void PublishHeroLayer(const Kernel::ActorState& hero, Kernel::WorldRuntime& runtime,
                      const Terrain::WorldTerrain& terrain)
{
    if (!terrain.mapping)
        return;
    const auto x = Math::OriginalFtolLow(hero.position.x);
    const auto y = Math::OriginalFtolLow(hero.position.y);
    if (!x || !y)
        return;
    const auto raw = static_cast<std::int64_t>(*y / 100) * 256 + *x / 100;
    const auto index = static_cast<std::size_t>(std::clamp<std::int64_t>(raw, 0, 65535));
    runtime.heroCellLayer = terrain.mapping->layer1[index];
}
std::int32_t StepTheCounter(Kernel::WorldRuntime& runtime, Crt::CrtRandom& random)
{
    {
        const Models::LogicalAccess access(runtime.warpDepth, random, Models::SmallLogicalCloseRandoms);
        if (runtime.warpCounter > 0)
            --runtime.warpCounter;
    }
    const Models::LogicalAccess access(runtime.warpDepth, random, Models::SmallLogicalCloseRandoms);
    return runtime.warpCounter;
}
Core::Expected<void, std::string> MoveTheHero(Kernel::ActorState& hero, const HeroMovementContext& context)
{
    if (hero.movement == 0)
        return {};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    const auto stopContext = StopContextForActor(hero, context, terrain, "0x4f37c0: falta Player.bmd");
    if (!stopContext.HasValue())
        return Core::Unexpected(stopContext.Error());
    const Actors::ActorStopContext& stop = stopContext.Value();
    if ((hero.flags78 & MovementLockedFlag) != 0)
    {
        const auto stopped = Actors::SetActorStop(hero, stop);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        return {};
    }
    const auto walked =
        Actors::SetActorWalk(hero, {stop, context.world.ActionSpeeds(), true, stop.actorSkeleton, stop.modelSounds});
    if (!walked.HasValue())
        return Core::Unexpected(walked.Error());
    if (Actors::MoveActorPath(hero, 1, context.random))
    {
        hero.movement = 0;
        const auto stopped = Actors::SetActorStop(hero, stop);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        const auto yaw = Math::OriginalFtolLow(hero.angles.z);
        if (!yaw)
            return Core::Unexpected(std::string("0x59b2d2: ángulo de llegada fuera del perfil ftol"));
        runtime.movement.arrivalYaw = *yaw;
        runtime.movement.movementTarget = 0;
        // 0x59b2e4..0x59b2f7: 0x5650c0(hero, hero, 0). With +0x379 at 0 it leaves through the table, without
        // interaction or rand(); 1 picks up the chosen floor item (CL9 milestone 5), 2 talks to the NPC (CL9 milestone
        // 2) and 3 hits (CL9 milestone 3).
        return ArriveAtTheTarget(hero, context);
    }
    hero.flags7c &= ~std::uint32_t{0x200000};
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x508a80: falta terreno para mover al héroe"));
    return Actors::UpdateActorPosition(hero, {runtime, context.random, [&](float x, float y) {
                                                  return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes,
                                                                                  x, y, context.fixedHeight);
                                              }});
}

// 0x59afdf..0x59b185, between the counter and the head: the right click (0x7d36880) on the chosen character
// (0x6c8e30, the frame's picking). None of this draws rand().
Core::Expected<void, std::string> NameTheChosen(Kernel::WorldState& world, const IHeroMovementHooks& hooks)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    // 0x59afdf..0x59b033: with the add-friend box (+0xf0 of [0x5763514], which only 0x49d0b0 writes and 0x490200 sets
    // back to 0; JBE: unsigned) and the right click, if 0x490a00 finds it, the chosen one's name (+0x1d9, without
    // checking −1) goes to its EDIT (+0xb4): the vtable + 0x4c (0x467ee0: SetWindowTextA with fewer than 0x100 bytes)
    // and + 0x30 with 1 (0x4682a0: the focus, the EDIT id in 0x5762ec0 and EM_SETSEL(−2, −1), the cursor at the end).
    // It does not consume the right click and goes on at 0x59b036.
    if (runtime.oldWindows.letter != 0 && runtime.ui.rightClick)
    {
        if (Ui::OldWindow* box = Ui::FindTheOldWindow(runtime.oldWindows, runtime.oldWindows.letter); box != nullptr)
        {
            if (box->kind != Ui::OldWindowKind::AddFriendBox)
                return Core::Unexpected(std::string("0x59b00f: +0xf0 es una ventana que no es la caja de agregar un "
                                                    "amigo: su +0xb4 no es el EDIT"));
            const std::int32_t named = runtime.selectedTargets[2];
            if (named < 0 || static_cast<std::size_t>(named) >= world.Actors().size())
                return Core::Unexpected(
                    std::format("0x59b020: el nombre del elegido {} se lee de +0x1d9 de su registro, "
                                "fuera de la tabla de personajes ([0x743a16c] + elegido · 0x438): "
                                "lo que hay ahí depende del heap (VM)",
                                named));
            constexpr std::size_t SetTextLimit = 0x100;
            Ui::OldEdit& edit = box->add.edit;
            const std::string name = Actors::ChatNameOf(world.Actors()[static_cast<std::size_t>(named)]);
            if (name.size() < SetTextLimit)
                edit.text.SetText(name);
            Ui::Steps::FocusTheEdit(runtime.oldWindows, edit, hooks.OldWindowGlobalsOf(runtime));
            edit.text.Deselect();
        }
    }
    // 0x59b036..0x59b086: the chat open (0x6c8e68), the hero in the safe zone (+0x3e2), a chosen one, the right click
    // and the chosen one's +0x376 at 0xff (only 0x5140c0 writes it, the tail of a monster's creation, with type 255).
    const std::int32_t chosen = runtime.selectedTargets[2];
    const Kernel::ActorState& hero = world.Hero();
    if (!runtime.ui.chatOpen || hero.safeZone == 0 || chosen == -1 || !runtime.ui.rightClick)
        return {};
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= world.Actors().size())
        return Core::Unexpected(std::string("0x59b06d: el elegido está fuera de la tabla de personajes"));
    constexpr std::uint16_t WhisperableWord376 = 0xff;
    const Kernel::ActorState& target = world.Actors()[static_cast<std::size_t>(chosen)];
    if (target.word376 != WhisperableWord376)
        return {};
    // 0x59b092: the right click is consumed also in Chaos Castle (18 to 23), where it does not go on.
    runtime.ui.rightClick = false;
    using Maps::FirstChaosCastleMap;
    using Maps::LastChaosCastleMap;
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
        return {};
    Chat::ChatInputState& input = runtime.chat.input;
    Chat::ChatBuffer& slot = input.whisperNames[static_cast<std::size_t>(input.whisperNameAt)];
    // 0x59b0a7..0x59b0ca: 0x4694c0(0, 0, 1, slot) with 0x6c7b8c at 1, before the copies: the whisper EDIT takes what
    // was in the history's current slot (0x77f31cc + 0x6c8ea8 · 0x100), not the new name (Q-C804).
    Chat::SetTheChatFields(input, std::nullopt, Chat::CStringOf(slot));
    // 0x59b0cf..0x59b16b: strcpy of the name (+0x1d9) to the whisper buffer (0x7798cf8), from the buffer to the slot,
    // the whole buffer zeroed (0x100 bytes) and again from the slot to the buffer; 0x59b17a, its length (0x7758594).
    Chat::CopyIntoTheBuffer(input.whisperBuffer, Actors::ChatNameOf(target));
    Chat::CopyIntoTheBuffer(slot, Chat::CStringOf(input.whisperBuffer));
    input.whisperBuffer.fill('\0');
    Chat::CopyIntoTheBuffer(input.whisperBuffer, Chat::CStringOf(slot));
    input.whisperLength = static_cast<std::int32_t>(Chat::CStringOf(input.whisperBuffer).size());
    // 0x59b180: sound 0x19.
    runtime.RequestSound(Chat::ChatButtonSound);
    return {};
}

} // namespace

Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context)
{
    if (context.hooks == nullptr)
        return Core::Unexpected(std::string(NoHooks));
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::ActorState& hero = context.world.Hero();
    // In the game, StepTheDialogs already did it this step: here nothing changes.
    Kernel::LatchTheLeftButton(runtime.mouse, context.input.leftPressed, context.input.leftDown);
    // 0x59ae24: 0x4033d0, the camera tour (Events/Crywolf/CameraTour.h).
    if (context.hooks->CameraTourRunning(runtime))
    {
        const auto& terrain = Terrain::TerrainOrEmpty(context.terrain);
        const auto stop =
            StopContextForActor(hero, context, terrain, "0x59ae24: falta Player.bmd para parar durante el recorrido");
        if (!stop.HasValue())
            return Core::Unexpected(stop.Error());
        const auto stopped = Actors::SetActorStop(hero, stop.Value());
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        return {};
    }
    if (runtime.selectedTargets[2] != -1 && (hero.flags7c & 0x200000u) != 0)
        return {};
    if (hero.dying != 0 || (hero.flags78 & FrozenHeroFlag) != 0)
        return {};
    if (StepTheCounter(runtime, context.random) > Kernel::WaitDrawThreshold)
        return {};
    if (auto named = NameTheChosen(context.world, *context.hooks); !named.HasValue())
        return named;
    // 0x59b188: the world width, with its queries (2 rand() with the inventory open); half of it, signed.
    const std::int32_t center = context.hooks->WorldWidth(context) / 2;
    const bool keepsTheHead = AutoAttacksHere(runtime) && runtime.selectedTargets[4] != -1;
    if (auto head = UpdateTheHead(hero, context.input, center, keepsTheHead); !head.HasValue())
        return head;
    if (hero.movement == 0)
    {
        // 0x59b357: IsOpen(0x40000) no saca rand().
        const auto facing =
            StepHeroFacing({.hero = hero,
                            .movement = runtime.movement,
                            .secondGuildOpen = context.hooks->IsPanelOpen(context, Ui::SecondGuildPanel),
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
        const auto gate = CheckGameGates({.world = context.world,
                                          .gates = *context.gates,
                                          .random = context.random,
                                          .ticksMs = context.input.ticksMs,
                                          .requestGate = context.requestGate,
                                          .saveOptionsAndMacros = context.saveOptionsAndMacros});
        if (!gate.HasValue())
            return Core::Unexpected(gate.Error());
    }
    const auto input = ProcessInput(hero, context);
    if (!input.HasValue())
        return Core::Unexpected(input.Error());
    // 0x59d666: without arrows or bolts (0x561c50 at 0), MoveHero leaves (0x59e2c0) without the sum, 0x57cfc0 or the
    // layer.
    if (input.Value() == MoveHeroEnd::Leave)
        return {};
    // 0x59e255: 0x57cfc0, use the skill (CL9 milestone 4).
    if (auto used = context.hooks->UseTheHeroSkill(hero, context); !used.HasValue())
        return used;
    PublishHeroLayer(hero, runtime, Terrain::TerrainOrEmpty(context.terrain));
    return {};
}
} // namespace Client::Movement
