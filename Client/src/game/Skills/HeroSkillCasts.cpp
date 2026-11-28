#include "Skills/HeroSkillCasts.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Actors/WalkPath.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Network/Delivery.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skills/Skill.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrowEffects.h"
#include "Actors/CharacterStep.h"
#include "Combat/CombatActionSpeeds.h"
#include "Effects/GameParticles.h"
#include "Movement/GameMovementState.h"
#include "Hud/GamePanels.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkills.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Kernel/ItemState.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Skills
{
namespace
{
constexpr std::size_t ManaOffset = 0x22;
constexpr std::size_t StaminaOffset = 0x24;
constexpr std::size_t RangeOffset = 0x26;
constexpr std::uint16_t DarkHorsePet = 0x1c16;
constexpr std::uint16_t FenrirPet = 0x1c37;
using Maps::IsBloodCastle;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});    // [0x6b86c0], 0.01
constexpr float ChosenRangeFactor = std::bit_cast<float>(std::uint32_t{0x3f99999a}); // [0x6b8bb0], 1.2
constexpr float BloodCastleRange = std::bit_cast<float>(std::uint32_t{0x3fe66666});  // 0x55969e, 1.8
constexpr float RumboFactor = std::bit_cast<float>(std::uint32_t{0x3cb60b61});       // [0x6b87d8], 1/45
constexpr std::string_view NoAttackName = "webzen";                                  // 0x6c8fa0 and 0x6c8fa8
constexpr std::uint32_t HiddenFlag20 = 0x20;
constexpr std::uint16_t WallAttribute = 4;
using Terrain::CellSize;                                                      // [0x6b86b4], 100
constexpr float HalfCell = std::bit_cast<float>(std::uint32_t{0x42480000});   // [0x6b86b0], 50
constexpr float HalfTurn = Math::HalfTurnAsFloat;                             // [0x6b86bc], 180
constexpr float OneOver360 = std::bit_cast<float>(std::uint32_t{0x3b360b61}); // [0x6ba444], 1/360
constexpr float ByteScale = std::bit_cast<float>(std::uint32_t{0x437f0000});  // [0x6b9f98], 255
constexpr float TurnToByte = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::uint16_t Byte38eCloseRandoms = 1;
constexpr std::size_t OwnSkillCount = 0x40;
constexpr std::uint16_t BlockedAttribute = 8;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

Models::LogicalAccess OwnAccess(const HeroSkillContext& context)
{
    return Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

Models::LogicalAccess EntryAccess(const HeroSkillContext& context, std::uint8_t skill)
{
    return Models::LogicalAccess(context.world.Runtime().movement.skillDepths[skill], context.random,
                                 static_cast<std::uint16_t>(Tables::SkillRecordSize));
}

// A logical cell (+0x41c or +0x420) with 0x40b570/0x412810 and 0x40b800: 4 rand() when closed at 0.
std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

// The name (+0x1d9) up to its '\0', as the inline search for "webzen" walks it.
bool NamedWebzen(const Kernel::ActorState& actor)
{
    std::string name;
    for (const std::byte letter : actor.name)
    {
        if (letter == std::byte{0})
            break;
        name.push_back(static_cast<char>(letter));
    }
    return name.find(NoAttackName) != std::string::npos;
}

Core::Expected<Kernel::ActorState*, std::string> ActorAt(Kernel::WorldState& world, std::int32_t index,
                                                         std::string_view where)
{
    if (index < 0 || static_cast<std::size_t>(index) >= world.Actors().size())
        return Core::Unexpected(std::format("{}: el personaje {} está fuera de la tabla", where, index));
    return &world.Actors()[static_cast<std::size_t>(index)];
}

// `actor`'s pool slot, for 0x558eb0, which compares the pointer with [0x743a170]. Outside the pool, none.
std::size_t SlotOf(const Kernel::WorldState& world, const Kernel::ActorState& actor)
{
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < actors.size(); ++slot)
    {
        if (&actors[slot] == &actor)
            return slot;
    }
    return actors.size();
}

Core::Expected<std::uint8_t, std::string> FtolByte(double value, std::string_view where)
{
    const auto whole = Math::OriginalFtolLow(value);
    if (!whole)
        return Core::Unexpected(std::format("{}: el valor fuera del perfil ftol", where));
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(*whole) & 0xffu);
}

// 0x562934..0x56298f and 0x563e0b..0x563e66: the distance from c to the center of the cell of 0x77e1ba8/0x77e1bac, on
// the x87, against the reach (0x5526f0, as a float) · 100. Outside (no C0 or C3), no; unordered, yes.
bool TheChosenCellIsInReach(const Kernel::ActorState& caster, const Movement::GameMovementState& state, float range)
{
    const auto cellX = static_cast<double>(std::bit_cast<std::int32_t>(state.targetX));
    const auto cellY = static_cast<double>(std::bit_cast<std::int32_t>(state.targetY));
    const double dx = static_cast<double>(caster.position.x) -
                      (cellX * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(caster.position.y) -
                      (cellY * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    return !(distance > static_cast<double>(range) * static_cast<double>(CellSize));
}

// 0x562a01..0x562a5b: the distance from c's logical cell to the target's, each axis between −8 and 7, in a byte.
std::uint8_t TowardsTheChosen(const Movement::GameMovementState& state, std::uint8_t x, std::uint8_t y)
{
    const std::int32_t dx = std::clamp(std::bit_cast<std::int32_t>(state.targetX) - std::int32_t{x}, -8, 7);
    const std::int32_t dy = std::clamp(std::bit_cast<std::int32_t>(state.targetY) - std::int32_t{y}, -8, 7);
    return static_cast<std::uint8_t>(((static_cast<std::uint32_t>(dx + 8) << 4u) & 0xf0u) |
                                     (static_cast<std::uint32_t>(dy - 8) & 0x0fu));
}

// 0x4ea030(character, action) with Player.bmd's actions.
Core::Expected<void, std::string> SetPlayerAction(Kernel::ActorState& actor, std::uint8_t action,
                                                  const HeroSkillContext& context, std::string_view where)
{
    if (context.playerSkeleton == nullptr)
        return Core::Unexpected(std::format("{}: 0x4ea030 sin las acciones de Player.bmd", where));
    Models::SetAction(actor.animation, *context.playerSkeleton, action);
    return {};
}

// 0x4f4a10 with the skills' context.
Core::Expected<void, std::string> AttackWith(Kernel::ActorState& actor, const HeroSkillContext& context)
{
    return Combat::SetActorAttack(actor, Combat::ActorCombatContext{.world = context.world,
                                                                    .random = context.random,
                                                                    .playerSkeleton = context.playerSkeleton,
                                                                    .models = context.models,
                                                                    .items = context.items,
                                                                    .terrain = context.terrain,
                                                                    .dynamicLighting = context.dynamicLighting,
                                                                    .faunaModels = context.faunaModels});
}

// 0x563b8f..0x564f37: 0x4f4a10 on c; a player (0x20a in +2) or, with skill 0x18, one with 1 in +0x88, nothing more.
// Otherwise, +0x38e (one rand() on closing it), its slot in the own record's skills (opened by the caller) and the
// arrow of 0x53e2b0(c, c, 0, slot, `count`, 0) (Effects::FireTheArrow): 1 with 0x18 (0x564f28) and 0x36 − 0x34 with
// 0x36 (0x563df1).
Core::Expected<void, std::string> ShootTheArrow(Kernel::ActorState& caster, std::uint8_t skill, std::uint8_t count,
                                                const HeroSkillContext& context)
{
    if (auto attacked = AttackWith(caster, context); !attacked.HasValue())
        return attacked;
    if (caster.type == Tables::PlayerSkeletonModel || (skill == 0x18 && caster.byte88 == 1))
        return {};
    std::uint8_t carried = 0;
    {
        const Models::LogicalAccess access(caster.byte38eAccesses, context.random, Byte38eCloseRandoms);
        carried = caster.byte38e;
    }
    std::size_t slot = 0;
    {
        const auto own = OwnAccess(context);
        for (std::size_t index = 0; index < OwnSkillCount; ++index)
        {
            if (Kernel::OwnSkillByte(context.world.OwnCharacter(), index) == carried)
            {
                slot = index;
                break;
            }
        }
    }
    // 0x564f2f (P-C269 c): 0x53e2b0(c, c, 0, slot, `count`, 0), with the sounds in WorldRuntime::lastSoundId (CL10).
    Kernel::WorldState& world = context.world;
    const Actors::CharacterStepContext shot{.world = world,
                                            .random = context.random,
                                            .playerSkeleton = context.playerSkeleton,
                                            .items = context.items,
                                            .terrain = context.terrain,
                                            .soundRequest = [&world](Audio::SoundId sound)
                                            { world.Runtime().RequestSound(sound); },
                                            .soundAt = [&world](Audio::SoundId sound, const glm::vec3& origin)
                                            { world.Runtime().RequestSound(sound, origin); },
                                            .controlDown = context.controlDown,
                                            .dynamicLighting = context.dynamicLighting,
                                            .faunaModels = context.faunaModels};
    return Effects::FireTheArrow(caster, nullptr, SlotOf(world, caster), static_cast<std::int32_t>(slot), count, 0,
                                 shot);
}

// 0x55bd12..0x55bdd8: a player's action by skill (table 0x55cb78 by skill − 0x2b, into 0x55cb60).
std::optional<std::uint8_t> PlayerActionOf(std::uint8_t skill, const Kernel::ActorState& caster,
                                           const Kernel::WorldRuntime& runtime)
{
    switch (skill)
    {
    case 0x2b:
        return std::uint8_t{0x43};
    case 0x2c:
        return std::uint8_t{0x85};
    case 0x2f:
        return caster.pet.model == FenrirPet ? std::uint8_t{0x5f} : std::uint8_t{0x42};
    case 0x31:
        return runtime.map == 8 || runtime.map == 10 ? std::uint8_t{0x41} : std::uint8_t{0x40};
    case 0x3c:
    case 0x3d:
    case 0x4a:
        return std::nullopt;
    default:
        // 0x55bda6..0x55bdd8: with Fenrir outside the safe zone, 0x5d; otherwise 0x7758584 + 0x25 (the slot or the
        // skill left there, not the skill read).
        if (caster.pet.model == FenrirPet && caster.safeZone == 0)
            return std::uint8_t{0x5d};
        return static_cast<std::uint8_t>(runtime.movement.weaponSkill.slot + 0x25u);
    }
}

// 0x55c680..0x55c72a: the target's cell moved one step along `hero`'s heading (ftol(+0x24 · 1/45), absolute value,
// from 0 to 7; with more, it stays): table 0x55cb98.
void StepTheCellByTheRumbo(std::uint8_t& x, std::uint8_t& y, std::int32_t rumbo)
{
    switch (rumbo)
    {
    case 0:
        ++y;
        break;
    case 1:
        --x;
        ++y;
        break;
    case 2:
        --x;
        break;
    case 3:
        --x;
        --y;
        break;
    case 4:
        --y;
        break;
    case 5:
        ++x;
        --y;
        break;
    case 6:
        ++x;
        break;
    case 7:
        ++x;
        ++y;
        break;
    default:
        break;
    }
}

// 0x55be0d..0x55be49: 0x53fe70(0x5fe, +0x10, +0x1c, (1, 1, 1), 0, 0, `hero`), with the owner in the pool, and sound
// 0x41ed10(rand() % 2 + 0x3c, 0, 0), which draws its rand() even with the pool full (CL10: it stays in the last sound).
// 0x53fdb0 puts the shine on a bone with the scale of the owner type's shared model (+0x68 of 0x5769ad8 + type ·
// 0xe0), which MuRet has only for Player.bmd (WorldState::PlayerModel): with another type, the hero of a
// transformation ring, it cuts before creating it (08, P-C358).
Core::Expected<void, std::string> ShineOnTheHero(const Kernel::ActorState& hero, const HeroSkillContext& context)
{
    constexpr std::int32_t FirstShineSound = 0x3c;
    Kernel::WorldState& world = context.world;
    const std::size_t slot = SlotOf(world, hero);
    if (slot >= world.Actors().size())
        return Core::Unexpected(std::string("0x55be23: el dueño del brillo 0x5fe no está en el pool de personajes"));
    const auto created = world.Particles().CreateFollowingChecked(
        Particles::SkillShineParticle, hero.position, hero.angles, glm::vec3(1.0f), 0, 0.0f,
        Particles::ParticleOwner{.kind = Particles::ParticleOwnerKind::Actor, .identity = slot},
        Effects::ParticleOwnerBonesOf(world));
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x55be23: el brillo 0x5fe (0x53fe70) falla con el error {}",
                                            static_cast<unsigned>(created.Error())));
    world.Runtime().RequestSound(
        Audio::SoundId{static_cast<std::int32_t>(context.random.Next() % 2) + FirstShineSound});
    return {};
}

std::uint16_t WordAt(std::span<const std::byte, Tables::SkillRecordSize> record, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(record[offset]) |
                                      (std::to_integer<std::uint16_t>(record[offset + 1]) << 8));
}
} // namespace

Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                   std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: 0x551c70 sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    const Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                      static_cast<std::uint16_t>(Tables::SkillRecordSize));
    return SkillCost{.mana = WordAt(*record, ManaOffset), .stamina = WordAt(*record, StaminaOffset)};
}

Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells(context.world.Inventory().cells);
    const auto found = Items::FindQuickSlotItem(runtime.hud.quickSlots, cells, Items::ManaPotionSlot,
                                                runtime.movement.gateRequestBlocked != 0);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (!found.Value())
        return {};
    Items::InventoryServices inventory = context.inventory != nullptr ? *context.inventory : Items::InventoryServices{};
    if (!inventory.sendUseItem)
    {
        // 0x5af520 draws the padding and the serial before the send. UseInventoryItem already keeps those two rand(),
        // even with the socket at −1: here only the send is lent, without going through SendTheSkillPacket again.
        inventory.sendUseItem = [&context](const Protocol::Items::UseItem& use) -> Core::Expected<void, std::string>
        {
            if (context.send)
                context.send(Protocol::Items::WriteUseItem(use));
            return {};
        };
    }
    Hud::GamePanels panels(context.world, context.random);
    const auto used = Items::UseInventoryItem({context.world, context.random, inventory}, panels.Registry(),
                                              static_cast<std::int32_t>(*found.Value()));
    if (!used.HasValue())
        return Core::Unexpected(std::format("{}: {}", where, used.Error()));
    return {};
}

Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const Kernel::ActorState& caster,
                                                const HeroSkillContext& context)
{
    auto range = SkillRangeOf(skill, context);
    if (range.HasValue() && caster.pet.model == DarkHorsePet)
        range.Value() += 2.0f;
    return range;
}

Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const HeroSkillContext& context)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x5526f0: falta Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::string("0x5526f0: falta el registro de habilidad"));
    const auto entry = EntryAccess(context, skill);
    return static_cast<float>(std::to_integer<std::uint8_t>((*record)[RangeOffset]));
}

void SendTheSkillPacket(const HeroSkillContext& context, Protocol::Outgoing packet)
{
    if (packet.delivery == Network::Delivery::Encrypted)
        context.random.Discard(2);
    if (context.send)
        context.send(std::move(packet));
}

Core::Expected<bool, std::string> PassesTheSkillBrake(std::int32_t skill, const HeroSkillContext& context,
                                                      std::string_view where)
{
    if (!context.tickCount)
        return Core::Unexpected(std::format("{}: el freno de 300 ms (0x576791c) sin GetTickCount", where));
    return Combat::PassesTheActionBrake(context.world.Runtime().movement.lastActionTick, skill, context.tickCount);
}

Core::Expected<bool, std::string> UseTheSkillOnTheChosen(Kernel::ActorState& caster, Kernel::ActorState& hero,
                                                         std::uint8_t skill, const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Movement::GameMovementState& state = runtime.movement;
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen <= -1)
        return false;
    const auto target = ActorAt(world, chosen, "0x55952d");
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    // 0x55953e..0x55956c: the chosen target's cell.
    const auto cellX =
        Math::OriginalFtolLow(static_cast<double>(target.Value()->position.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        Math::OriginalFtolLow(static_cast<double>(target.Value()->position.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x55954b: la celda del elegido fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*cellX);
    state.targetY = std::bit_cast<std::uint32_t>(*cellY);
    state.weaponSkill = Movement::WeaponSkillRequest{.active = 0, .slot = skill, .target = chosen};
    if (context.skills == nullptr || !context.skills->Record(skill))
        return Core::Unexpected(std::format("0x5595b5: Skill.bmd no tiene la skill {}", skill));
    // 0x5595b5..0x559720 and 0x55982d..0x55987e: the skill's entry, open until the end.
    const auto entry = EntryAccess(context, skill);
    const auto range = SkillRangeOf(skill, caster, context);
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    float reach = static_cast<float>(static_cast<double>(range.Value()) * static_cast<double>(ChosenRangeFactor));
    if (IsBloodCastle(runtime.map) && InRange(skill, 0x13, 0x17))
        reach = BloodCastleRange;
    // 0x5596a5..0x5596f2: the distance from `hero` to the cell's center, on the x87; less than or equal to the reach ·
    // 100 (no C0) is in reach.
    const double dx = static_cast<double>(hero.position.x) - (static_cast<double>(*cellX) * 100.0 + 50.0);
    const double dy = static_cast<double>(hero.position.y) - (static_cast<double>(*cellY) * 100.0 + 50.0);
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(reach) * 100.0;
    if (!(limit < distance) && !std::isnan(distance))
    {
        if (auto cast = CastTheSkillAtTheTarget(caster, hero, context); !cast.HasValue())
            return Core::Unexpected(cast.Error());
        return true;
    }
    // 0x559725..0x559822: out of reach, the path from c's logical cells.
    const std::uint8_t y = ReadCell(caster.cellYAccesses, caster.cellY, context.random);
    const std::uint8_t x = ReadCell(caster.cellXAccesses, caster.cellX, context.random);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x55980f: falta TerrainWall para el camino de la skill"));
    constexpr std::int32_t LastCell = 0xff;
    if (*cellX < 0 || *cellX > LastCell || *cellY < 0 || *cellY > LastCell)
        return Core::Unexpected(std::format("0x55980f: la celda del elegido ({}, {}) fuera de la grilla, que "
                                            "PathFinding2 toma como índice lineal",
                                            *cellX, *cellY));
    // 0x55980f: PathFinding2 with the Crywolf altar flag (0x4eac0a..0x4eac41), which does not count with a nonzero
    // reach (0x4eadd4).
    const auto path =
        Actors::FindThePath(world, terrain.attributes->cells, {x, y},
                            {static_cast<std::uint8_t>(*cellX), static_cast<std::uint8_t>(*cellY)}, reach);
    if (!path)
        return false;
    caster.path = *path;
    caster.movement = 1;
    caster.movementType = 5;
    return true;
}

Core::Expected<void, std::string> CastTheSkillAtTheTarget(Kernel::ActorState& caster, Kernel::ActorState& hero,
                                                          const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Movement::WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x55b714..0x55b7fe: with 0x7758580, the skill of slot 0x7758584 in the own record; otherwise 0x7758584 is the
    // skill.
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = request.active != 0 ? Kernel::OwnSkillByte(world.OwnCharacter(), request.slot)
                                    : static_cast<std::uint8_t>(request.slot & 0xffu);
    }
    // 0x55b803..0x55bc0c: C1 06 D3 X Y (heading << 4) with the published hero's logical cell, X before Y, through the
    // socket.
    Kernel::ActorState& published = world.Hero();
    const std::uint8_t x = ReadCell(published.cellXAccesses, published.cellX, context.random);
    const std::uint8_t y = ReadCell(published.cellYAccesses, published.cellY, context.random);
    const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
    auto turn = Protocol::Movement::WriteWalk(cell, cell[0], published.angles.z);
    if (!turn.HasValue())
        return Core::Unexpected(std::string("0x55baac: el rumbo del héroe fuera del perfil ftol"));
    SendTheSkillPacket(context, std::move(turn.Value()));
    // 0x55bcf2..0x55bde3.
    caster.movement = 0;
    if (hero.type == Tables::PlayerSkeletonModel)
    {
        Combat::RefreshCombatActionSpeeds(world, context.random);
        if (const auto action = PlayerActionOf(skill, caster, runtime))
        {
            if (auto set = SetPlayerAction(hero, *action, context, "0x55bdd3"); !set.HasValue())
                return set;
        }
    }
    else if (auto attacked = AttackWith(caster, context); !attacked.HasValue())
    {
        return attacked;
    }
    // 0x55bde6..0x55be49: except for 0x3c, 0x3d and 0x4a, the 0x5fe shine over `hero` and sound 0x3c or 0x3d.
    if (skill != 0x3c && skill != 0x3d && skill != 0x4a)
    {
        if (auto shone = ShineOnTheHero(hero, context); !shone.HasValue())
            return shone;
    }
    // 0x55be4c..0x55beb5: the target's position in c's +0x3a8 and `hero`'s heading towards it.
    const auto target = ActorAt(world, request.target, "0x55be4c");
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    caster.attackPosition = target.Value()->position;
    hero.angles.z = static_cast<float>(
        Math::CreateAngle(hero.position.x, hero.position.y, caster.attackPosition.x, caster.attackPosition.y));
    // 0x55be9a..0x55c622: with "webzen" in the published hero's name or without passing the brake, the 19 is not sent.
    if (!NamedWebzen(published))
    {
        const auto passes = PassesTheSkillBrake(skill, context, "0x55bf17");
        if (!passes.HasValue())
            return Core::Unexpected(passes.Error());
        if (passes.Value())
            SendTheSkillPacket(context, Protocol::Skills::WriteMagicAttack(skill, target.Value()->key));
    }
    // 0x55c62c..0x55c662.
    caster.byte381 = 1;
    if ((hero.flags78 & HiddenFlag20) == HiddenFlag20 || caster.pet.model == DarkHorsePet)
        return {};
    const auto cellX =
        Math::OriginalFtolLow(static_cast<double>(caster.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        Math::OriginalFtolLow(static_cast<double>(caster.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x55c662: la celda del blanco fuera del perfil ftol"));
    auto dfX = static_cast<std::uint8_t>(*cellX & 0xff);
    auto dfY = static_cast<std::uint8_t>(*cellY & 0xff);
    const std::uint16_t monster = target.Value()->word376;
    if (IsBloodCastle(runtime.map) || skill == 0x3c || skill == 0x39 || skill == 0x2c || monster == 0x115 ||
        monster == 0x11b || monster == 0x116 || monster == 0x120)
    {
        const auto rumbo = Math::OriginalFtolLow(static_cast<double>(hero.angles.z) * static_cast<double>(RumboFactor));
        if (!rumbo)
            return Core::Unexpected(std::string("0x55c6e0: el rumbo del héroe fuera del perfil ftol"));
        StepTheCellByTheRumbo(dfX, dfY, std::abs(*rumbo));
    }
    // 0x55c72d..0x55c79b: 0x5f8290 (y · 256 + x) in TerrainWall's attributes.
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x55c745: falta TerrainWall para la celda del DF"));
    const std::uint16_t attribute = terrain.attributes->cells[std::size_t{dfY} * 256u + dfX];
    if ((attribute & WallAttribute) == WallAttribute || (attribute & BlockedAttribute) == BlockedAttribute)
        return {};
    if (skill == 0x2f || skill == 0x2b || skill == 0x31 || skill == 0x3c || skill == 0x3d || skill == 0x4a)
        return {};
    // 0x55c7a1..0x55cb48: C1 05 DF X Y, through the socket.
    SendTheSkillPacket(context, Protocol::Combat::WriteElfCell(dfX, dfY));
    return {};
}

Core::Expected<bool, std::string> UseTheListedWeaponSkill(Kernel::ActorState& hero, const Kernel::ItemState& item,
                                                          std::uint8_t skill, const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    const auto cost = SkillCostOf(skill, context, "0x559b0b");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    const Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    if (own.mana < cost.Value().mana)
    {
        if (auto potion = UseAManaPotion(context, "0x559b41"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        return false;
    }
    // 0x55a292..0x55a2b3.
    if (cost.Value().stamina > own.bp)
        return false;
    const auto ready =
        TryStartSkillDelay(static_cast<std::uint8_t>(world.Runtime().movement.weaponSkill.slot & 0xffu),
                           SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
    if (!ready.HasValue())
        return Core::Unexpected(ready.Error());
    if (!ready.Value())
        return false;
    const auto attackable = Combat::IsAttackable(world, context.controlDown);
    if (!attackable.HasValue())
        return Core::Unexpected(attackable.Error());
    if (!attackable.Value())
        return UseTheWeaponOptions(hero, item, context);
    return UseTheSkillOnTheChosen(hero, hero, skill, context);
}

Core::Expected<bool, std::string> UseTheWeaponOptions(Kernel::ActorState& hero, const Kernel::ItemState& item,
                                                      const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    const auto own = OwnAccess(context);
    bool used = false;
    for (std::uint8_t option = 0; option < item.optionCount; ++option)
    {
        const std::uint8_t skill = item.OptionTag(option);
        const Kernel::ActorState& published = world.Hero();
        if (Kernel::OwnSkillByte(world.OwnCharacter(), published.skillIndex) != skill)
            continue;
        // 0x55a368..0x55a3b0: the cost and, without mana, the potion or the next option.
        const auto cost = SkillCostOf(skill, context, "0x55a37a");
        if (!cost.HasValue())
            return Core::Unexpected(cost.Error());
        if (world.OwnCharacter().mana < cost.Value().mana)
        {
            if (auto potion = UseAManaPotion(context, "0x55a3b0"); !potion.HasValue())
                return Core::Unexpected(potion.Error());
            continue;
        }
        // 0x55abb5..0x55abf3: the delay of the published hero's slot and table 0x55b6c8 by option − 0x12.
        const auto ready =
            TryStartSkillDelay(published.skillIndex,
                               SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
        if (!ready.HasValue())
            return Core::Unexpected(ready.Error());
        if (!ready.Value())
            continue;
        if (skill == 0x12)
        {
            // 0x55abfa..0x55b45a: the action, and the 19 over the published hero.
            hero.movement = 0;
            if (hero.type == Tables::PlayerSkeletonModel)
            {
                if (auto set = SetPlayerAction(hero, 0x98, context, "0x55ac12"); !set.HasValue())
                    return Core::Unexpected(set.Error());
            }
            else if (auto attacked = AttackWith(hero, context); !attacked.HasValue())
            {
                return Core::Unexpected(attacked.Error());
            }
            const Kernel::ActorState& self = world.Hero();
            if (!NamedWebzen(self))
            {
                const auto passes = PassesTheSkillBrake(skill, context, "0x55ac98");
                if (!passes.HasValue())
                    return Core::Unexpected(passes.Error());
                if (passes.Value())
                    SendTheSkillPacket(context, Protocol::Skills::WriteMagicAttack(skill, self.key));
            }
            used = true;
        }
        else if (InRange(skill, 0x13, 0x17) || skill == 0x31)
        {
            // 0x55b460..0x55b484.
            const auto attackable = Combat::IsAttackable(world, context.controlDown);
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            if (!attackable.Value())
                continue;
            const auto chosen = UseTheSkillOnTheChosen(hero, hero, skill, context);
            if (!chosen.HasValue())
                return chosen;
            used = chosen.Value();
        }
    }
    return used;
}

Core::Expected<bool, std::string> UseTheElfWeaponOptions(Kernel::ActorState& hero, const Kernel::ItemState& item,
                                                         const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Movement::GameMovementState& state = runtime.movement;
    const auto own = OwnAccess(context);
    bool used = false;
    for (std::uint8_t option = 0; option < item.optionCount; ++option)
    {
        const std::uint8_t skill = item.OptionTag(option);
        const Kernel::ActorState& published = world.Hero();
        if (Kernel::OwnSkillByte(world.OwnCharacter(), published.skillIndex) != skill)
            continue;
        // 0x562024..0x562092: the cost and, without mana, the potion or the next option.
        const auto cost = SkillCostOf(skill, context, "0x56203e");
        if (!cost.HasValue())
            return Core::Unexpected(cost.Error());
        if (world.OwnCharacter().mana < cost.Value().mana)
        {
            if (auto potion = UseAManaPotion(context, "0x562075"); !potion.HasValue())
                return Core::Unexpected(potion.Error());
            continue;
        }
        // 0x5628c3..0x5628ef and 0x56500b: with too much stamina or without the delay, 0 even if another option used
        // it.
        if (cost.Value().stamina > world.OwnCharacter().bp)
            return false;
        const auto ready =
            TryStartSkillDelay(published.skillIndex,
                               SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
        if (!ready.HasValue())
            return Core::Unexpected(ready.Error());
        if (!ready.Value())
            return false;
        // 0x5628f5..0x562919: the reach (0x5526f0) of any option; only 0x18 and 0x36 continue.
        const auto range = SkillRangeOf(skill, hero, context);
        if (!range.HasValue())
            return Core::Unexpected(range.Error());
        if (skill != 0x18 && skill != 0x36)
            continue;
        // 0x56291f..0x56292f and 0x563df6..0x563e06: without arrows (0x561c50), the options end.
        static const Items::InventoryServices NoServices{};
        const auto arrows = Items::CheckTheArrows(
            Items::InventoryMoveContext{.world = world,
                                        .random = context.random,
                                        .services = context.inventory != nullptr ? *context.inventory : NoServices});
        if (!arrows.HasValue())
            return Core::Unexpected(arrows.Error());
        if (!arrows.Value())
            break;
        if (!TheChosenCellIsInReach(hero, state, range.Value()))
            continue;
        // 0x562995..0x5629b0 and 0x563e6c..0x563e92: c's heading towards +0x3a8.
        hero.angles.z = static_cast<float>(
            Math::CreateAngle(hero.position.x, hero.position.y, hero.attackPosition.x, hero.attackPosition.y));
        Protocol::Skills::DurationMagicSend send{.skill = skill};
        if (skill == 0x36)
        {
            // 0x5629b6..0x562a69: c's logical cell (Y before X), towards the target and the back.
            const std::uint8_t y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
            const std::uint8_t x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
            send.towards = TowardsTheChosen(state, x, y);
            const auto back = FtolByte((static_cast<double>(hero.angles.z) + static_cast<double>(HalfTurn)) *
                                           static_cast<double>(OneOver360) * static_cast<double>(ByteScale),
                                       "0x562a64");
            if (!back.HasValue())
                return Core::Unexpected(back.Error());
            send.back = back.Value();
        }
        // 0x562a6c..0x562a87 and 0x563e8a..0x563eab: the key of 0x558eb0 with 0x7758588, or 0xffff without it.
        std::int32_t key = 0xffff;
        if (state.weaponSkill.target != -1)
        {
            const auto chosen =
                Combat::ChosenKeyOf(world, SlotOf(world, hero), state.weaponSkill.target, context.controlDown);
            if (!chosen.HasValue())
                return Core::Unexpected(chosen.Error());
            key = chosen.Value();
        }
        send.key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(key) & 0xffffu);
        // 0x562a8a..0x562af3 and 0x563eae..0x563f17: with "webzen" in the published hero's name, it is not sent (no
        // brake).
        if (!NamedWebzen(world.Hero()))
        {
            // 0x562af5..0x563b78 and 0x563f19..0x564e84: 0x5767948, the logical cell again and the 1E.
            context.world.Runtime().lastShotSkill = skill;
            send.y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
            send.x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
            const auto direction =
                skill == 0x36
                    ? FtolByte(static_cast<double>(hero.angles.z) * static_cast<double>(OneOver360) *
                                   static_cast<double>(ByteScale),
                               "0x563022")
                    : FtolByte(static_cast<double>(hero.angles.z) * static_cast<double>(TurnToByte), "0x5643f1");
            if (!direction.HasValue())
                return Core::Unexpected(direction.Error());
            send.direction = direction.Value();
            SendTheSkillPacket(context, Protocol::Skills::WriteDurationMagic(send));
        }
        if (auto shot =
                ShootTheArrow(hero, skill, skill == 0x36 ? std::uint8_t{0x36 - 0x34} : std::uint8_t{1}, context);
            !shot.HasValue())
            return Core::Unexpected(shot.Error());
        used = true;
    }
    return used;
}
} // namespace Client::Skills
