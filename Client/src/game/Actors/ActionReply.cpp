#include "Actors/ActionReply.h"

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/Skeleton.h"
#include "Actors/ActionSend.h"
#include "Actors/ActorActions.h"
#include "Scenes/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/SceneId.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Assets/FaunaModels.h"
#include "Terrain/TerrainDefines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors
{
namespace
{
// 0x4cb781: the dark horse at +0x318.
constexpr std::uint16_t DarkHorsePet = 0x1c16;
// 0x4cb7b9..0x4cb7fa: (pkt[5] − 1.0) · 45.0 (0x6b87d4 and 0x6b8848) and the cell · 100.0 + 50.0 (0x6b86b4 and
// 0x6b86b0).
constexpr double DirectionBase = 1.0;
constexpr double DegreesPerDirection = 45.0;
constexpr double CellSize = Terrain::CellSizeAsDouble;
constexpr double HalfCell = 50.0;
// 0x4cb853..0x4cb864: action 0x12 sets 0x98 and requests sound 0x6f.
constexpr std::uint8_t ShieldAction = 0x12;
constexpr std::uint8_t ShieldAnimation = 0x98;
constexpr std::int32_t ShieldSound = 0x6f;
// 0x4cb820: 0x78 and 0x79 go to 0x4f4a10 (Strike).
constexpr std::uint8_t FirstStrikeAction = 0x78;
constexpr std::uint8_t LastStrikeAction = 0x79;
// 0x4cb812: 0x7a and 0x7b go to 0x4f2250.
constexpr std::uint8_t FirstStopAction = 0x7a;
constexpr std::uint8_t LastStopAction = 0x7b;
// 0x4cbc92: 0x7e sets 5.
constexpr std::uint8_t SalutedAction = 0x7e;
constexpr std::uint8_t SalutedAnimation = 5;
// 0x4cb871..0x4cbcc8: from 0x80 to 0x9b, the animation of each action and the elf's (+0x1d4 & 7 == 2).
constexpr std::uint8_t FirstTableAction = 0x80;
constexpr std::uint8_t LastTableAction = 0x9b;

struct TableAnimation
{
    std::uint8_t other = 0;
    std::uint8_t elf = 0;
};

constexpr TableAnimation Both(std::uint8_t animation)
{
    return TableAnimation{animation, animation};
}

// Per action from 0x80, with the block of each one (those from 0x92 on do not look at the class).
constexpr std::array<TableAnimation, LastTableAction - FirstTableAction + 1> Animations{{
    {0xc5, 0xc7}, // 0x80, 0x4cb871
    {0xcb, 0xcc}, // 0x81, 0x4cb8a3
    {0xc9, 0xca}, // 0x82, 0x4cb8d7
    {0x99, 0x9a}, // 0x83, 0x4cb90b
    {0x9b, 0x9c}, // 0x84, 0x4cb93d
    {0x9d, 0x9e}, // 0x85, 0x4cb971
    {0xa3, 0xa4}, // 0x86, 0x4cb9a5
    {0xa1, 0xa2}, // 0x87, 0x4cb9d7
    {0xa5, 0xa6}, // 0x88, 0x4cba0b
    {0xa7, 0xa8}, // 0x89, 0x4cba3f
    {0x9f, 0xa0}, // 0x8a, 0x4cbad9
    {0xa9, 0xaa}, // 0x8b, 0x4cba71
    {0xab, 0xac}, // 0x8c, 0x4cbaa5
    {0xad, 0xae}, // 0x8d, 0x4cbb0b
    {0xaf, 0xb0}, // 0x8e, 0x4cbb3f
    {0xb1, 0xb2}, // 0x8f, 0x4cbb73
    {0xb3, 0xb4}, // 0x90, 0x4cbba5
    {0xb5, 0xb6}, // 0x91, 0x4cbbd9
    Both(0xb7),   // 0x92, 0x4cbc0d
    Both(0xb8),   // 0x93, 0x4cbc20
    Both(0xc0),   // 0x94, 0x4cbc33
    Both(0xb9),   // 0x95, 0x4cbc46
    Both(0xba),   // 0x96, 0x4cbc59
    Both(0xbb),   // 0x97, 0x4cbc6c
    Both(0xbc),   // 0x98, 0x4cbc7f
    Both(0xbd),   // 0x99, 0x4cbca2
    Both(0xbe),   // 0x9a, 0x4cbcb5
    Both(0xbf),   // 0x9b, 0x4cbcc8
}};

// The animation 0x4cb750 passes to 0x4ea030 (0x4cbcdb: any other, the action itself).
std::uint8_t AnimationOf(const Kernel::ActorState& actor, std::uint8_t action)
{
    if (action == ShieldAction)
        return ShieldAnimation;
    if (action == SalutedAction)
        return SalutedAnimation;
    return TableAnimationOf(actor.characterClass, action);
}

Core::Expected<void, std::string> StopTheActor(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources,
                                               const ActionSender& sendAction)
{
    const auto* player = resources.models.SkeletonOf(Tables::PlayerSkeletonModel);
    if (player == nullptr && actor.type == Tables::PlayerSkeletonModel)
        return Core::Unexpected(std::string("0x4cb813: falta Player.bmd para 0x4f2250"));
    static const Models::Skeleton EmptySkeleton;
    const Models::Skeleton* playerSkeleton = player != nullptr ? player : &EmptySkeleton;
    const Models::Skeleton* actorSkeleton =
        actor.type == Tables::PlayerSkeletonModel ? nullptr : Scenes::MonsterSkeletonOf(resources, actor.type);
    const ActorStopContext context{
        .runtime = resources.world.Runtime(),
        .scene = Scenes::SceneId::Game,
        .playerSkeleton = *playerSkeleton,
        .items = resources.items,
        .random = resources.cloth.random,
        .terrainWalls =
            Scenes::TerrainOf(resources).attributes ? &Scenes::TerrainOf(resources).attributes->cells : nullptr,
        .sendAction = sendAction,
        .actorSkeleton = actorSkeleton,
        .modelSounds = Assets::FaunaModelsOrEmpty(Scenes::FaunaModelsOf(resources)).SoundActions(actor.type)};
    const auto stopped = SetActorStop(actor, context);
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    return {};
}
// 0x4cb820..0x4cb852: 0x4f4a10 (SetActorAttack, with its rand() calls and its sound) and then +0x381 at 1, the frame
// (+0x114) at 0, the target (+0x3a4) with the low WORD of [0x57678b0] (the hero's slot, heroSlot) and [0x5767964]
// with the index. A stop in 0x4f4a10 leaves what came before done and writes none of this.
Core::Expected<void, std::string> Strike(Kernel::ActorState& actor, std::size_t index,
                                         const Scenes::WorldEntryResources& resources)
{
    if (auto attacked = Scenes::SetActorAttack(actor, Scenes::CombatContextOf(resources)); !attacked.HasValue())
        return attacked;
    Kernel::WorldRuntime& runtime = resources.world.Runtime();
    actor.byte381 = 1;
    actor.animation.frame = 0.0f;
    actor.attackTarget = static_cast<std::uint16_t>(runtime.heroSlot);
    runtime.lastAttacker = static_cast<std::int32_t>(index);
    return {};
}
} // namespace

std::uint8_t TableAnimationOf(std::uint8_t characterClass, std::uint8_t action)
{
    if (action < FirstTableAction || action > LastTableAction)
        return action;
    const TableAnimation& animation = Animations[static_cast<std::size_t>(action - FirstTableAction)];
    return Kernel::BaseClassOf(characterClass) == Kernel::CharacterClass::Elf ? animation.elf : animation.other;
}

Core::Expected<void, std::string> ApplyActionReply(const Protocol::Movement::Action& reply,
                                                   const Scenes::WorldEntryResources& resources,
                                                   const ActionSender& sendAction)
{
    // 0x4cb769: with nobody alive with the key, the original returns slot 400 and the C1 18 writes to it too (Q-C778).
    const std::size_t index = Scenes::ActorWithKey(resources.world, reply.key);
    Kernel::ActorState& actor = resources.world.Actors()[index];
    if (actor.pet.model == DarkHorsePet)
        return {};
    // 0x4cb7b2..0x4cb7fa: Movement at 0, the angle and the position at the center of the target cell (Q-C779). All on
    // the x87 and stored as float; with bytes the arithmetic is exact.
    actor.movement = 0;
    actor.angles.z = static_cast<float>((static_cast<double>(reply.direction) - DirectionBase) * DegreesPerDirection);
    actor.position.x = static_cast<float>(static_cast<double>(actor.targetX) * CellSize + HalfCell);
    actor.position.y = static_cast<float>(static_cast<double>(actor.targetY) * CellSize + HalfCell);
    const std::uint8_t action = reply.action;
    if (action >= FirstStopAction && action <= LastStopAction)
        return StopTheActor(actor, resources, sendAction);
    if (action >= FirstStrikeAction && action <= LastStrikeAction)
        return Strike(actor, index, resources);
    // 0x4ea030 reads the actions of the character's model (0x5769ad8 + type · 0xe0): for a monster or an NPC, the one
    // its creation loaded; a model that is not there is the record without actions and sets nothing (0x4ea056).
    // Without Player.bmd, the player is a contract guard.
    if (actor.type == Tables::PlayerSkeletonModel)
    {
        const auto* player = resources.models.SkeletonOf(Tables::PlayerSkeletonModel);
        if (player == nullptr)
            return Core::Unexpected(std::format("0x4ea030: la acción {:#x} sin Player.bmd", action));
        Models::SetAction(actor.animation, *player, AnimationOf(actor, action));
    }
    else if (auto set = Scenes::SetMonsterAction(actor, AnimationOf(actor, action), 0x4cbcdd, resources);
             !set.HasValue())
    {
        return set;
    }
    // 0x4cb864: 0x41ed10(0x6f, 0, 0).
    if (action == ShieldAction)
        resources.world.Runtime().RequestSound(ShieldSound);
    return {};
}
} // namespace Client::Actors
