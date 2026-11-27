#include "Actors/ViewportPlayers.h"
#include "Maps/MapId.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Scenes/CastleBattleReceive.h"
#include "Scenes/SiegeBattleUi.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/CharacterClass.h"
#include "Pathfinding/PathFinder.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorCharacterSet.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActorEffectFlags.h"
#include "Actors/ActorEquipment.h"
#include "Kernel/ActorState.h"
#include "Scenes/BlastEffectCalls.h"
#include "Actors/CharacterStep.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/CrywolfContract.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/OwnItemPlacement.h"
#include "Actors/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Session/WorldTransitionSupport.h"
#include "UI/NameChecks.h"
#include "Camera/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/ParticleBurst.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "Terrain/TerrainCulling.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Actors
{
namespace
{
constexpr std::string_view HiddenName = "webzen"; // 0x6c8418 and 0x6c8420
constexpr float CellSize = 100.0f;                // 0x6b86b4
constexpr float CellCenter = 0.5f;                // 0x6b882c
constexpr float DirectionDegrees = 45.0f;         // 0x6b8848, with the 1 from 0x6b87d4

// The ten bytes of the name with the 0 at -0x4a (0x4c88b7) or at -0x12 (0x4c92e2) after them.
std::string NameText(const Protocol::FrontEnd::NameBytes& name)
{
    std::string text;
    for (const std::byte value : name)
    {
        if (value == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
    }
    return text;
}

bool IsHidden(const Protocol::FrontEnd::NameBytes& name)
{
    return UI::ContainsLikeTheOriginal(NameText(name), HiddenName);
}

std::uint8_t ByteOf(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

std::uint8_t PkOf(std::uint8_t directionAndPk)
{
    constexpr std::uint8_t PkMask = 0xf;
    return directionAndPk & PkMask;
}

// 0x4c8a36: states 2 to 4 with their action (the second one for class 2: & 7, the elf).
std::optional<std::uint8_t> StateAction(std::uint8_t state, std::uint8_t characterClass)
{
    const bool elf = Kernel::BaseClassOf(characterClass) == Kernel::CharacterClass::Elf;
    switch (state)
    {
    case 2:
        return elf ? std::uint8_t{0xc7} : std::uint8_t{0xc5};
    case 3:
        return elf ? std::uint8_t{0xcc} : std::uint8_t{0xcb};
    case 4:
        return elf ? std::uint8_t{0xca} : std::uint8_t{0xc9};
    default:
        return std::nullopt;
    }
}

// What state 1 keeps from the live character with the key (0x4c8919..0x4c8965).
struct Kept
{
    std::uint8_t byte1f2 = 0;
    std::uint8_t guildStatus = 0;
    std::uint8_t byte369 = 0;
    std::uint8_t byte370 = 0;
    std::uint8_t byte360 = 0;
    std::uint32_t flags78 = 0;
    std::uint8_t byte372 = 0;
};

Kept KeepFrom(const Kernel::ActorState& actor)
{
    return {.byte1f2 = static_cast<std::uint8_t>(actor.word1f2),
            .guildStatus = actor.guildStatus,
            .byte369 = actor.byte369,
            .byte370 = actor.byte370,
            .byte360 = actor.byte360,
            .flags78 = actor.flags78,
            .byte372 = actor.byte372};
}

// 0x4c89b4..0x4c89ed: the MOVZX leaves +0x1f2 with the low byte and the high one at 0.
void GiveBack(Kernel::ActorState& actor, const Kept& kept)
{
    actor.word1f2 = kept.byte1f2;
    actor.guildStatus = kept.guildStatus;
    actor.byte369 = kept.byte369;
    actor.byte370 = kept.byte370;
    actor.byte360 = kept.byte360;
    actor.flags78 = kept.flags78;
    actor.byte372 = kept.byte372;
}

using Maps::IsKalimaMap;

// 0x4c89f3..0x4c8a04: ((b & 0x10) | (b >> 4)) >> 1.
std::uint8_t ClassOf(std::uint8_t classAndState)
{
    constexpr std::uint8_t EvolutionBit = 0x10;
    constexpr int ClassShift = 4;
    return static_cast<std::uint8_t>(
        static_cast<std::uint8_t>((classAndState & EvolutionBit) | (classAndState >> ClassShift)) >> 1);
}

Core::Expected<void, std::string> ApplyPlayerRecord(const Protocol::Actors::PlayerRecord& record,
                                                    std::span<const std::byte> packet,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    constexpr std::uint8_t StateMask = 0xf;
    constexpr std::uint8_t TeleportState = 1;
    const std::uint8_t state = record.classAndState & StateMask;
    // 0x4c88e2..0x4c8965.
    std::optional<Kept> kept;
    if (state == TeleportState)
    {
        const std::size_t previous = Scenes::ActorWithKey(world, record.key);
        if (previous != CreatedActorSearch)
            kept = KeepFrom(world.Actors()[previous]);
    }
    // 0x4c8968..0x4c8992.
    const ActorConstructionContext construction{Scenes::TerrainOf(resources), runtime.map, Terrain::CullingScene::Game,
                                                resources.camera.fixedHeight};
    const auto created = CreateActor(world, record.key, Tables::PlayerSkeletonModel, {record.x, record.y, 0.0f},
                                     construction, resources.cloth);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    if (kept)
        GiveBack(actor, *kept);
    // 0x4c89f3..0x4c8a2a.
    actor.characterClass = ClassOf(record.classAndState);
    actor.skin = 0;
    actor.byte88 = 1;
    actor.pk = PkOf(record.directionAndPk);
    // 0x4c8a26..0x4c8c11.
    if (state == TeleportState)
    {
        if (auto teleported = Scenes::StartTheTeleportAction(actor, resources); !teleported.HasValue())
            return teleported;
        // 0x4c8a43..0x4c8ad4: the whole packet to the queue again (Q-C801).
        StoreViewportPacket(runtime.viewportPackets, packet);
    }
    else if (const auto action = StateAction(state, actor.characterClass))
    {
        const auto* skeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModel);
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4ea030: falta Player.bmd para la acción del 0x12"));
        Models::SetAction(actor.animation, *skeleton, *action);
    }
    // 0x4c8adc..0x4c8aec.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources); !flagged.HasValue())
        return flagged;
    WriteTheCells(actor, record.x, record.y, random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (record.isNew)
    {
        PutInTheCellCenter(actor, random);
        static_cast<void>(world.Map().effects.Create(World::ModelEffectArguments{.type = World::ArrivalEffect,
                                                                                 .position = actor.position,
                                                                                 .angles = actor.angles,
                                                                                 .light = actor.light,
                                                                                 .owner = &actor,
                                                                                 .key = 0xffff,
                                                                                 .scale = 0.0f}));
        actor.alpha = 0.0f;
    }
    else if (auto walked = WalkToTheTarget(actor, record.targetX, record.targetY, resources); !walked.HasValue())
    {
        return walked;
    }
    // 0x4c91aa..0x4c91e1: in Kalima (24..29 and 36), 0x530860(0x610, position, position, angles, 8, player, 20, −1, 0,
    // 0, −1), without rand(): the class of the Atlans fish with the ActorState as owner. With the joints full it
    // creates nothing and goes on.
    if (IsKalimaMap(runtime.map))
    {
        constexpr std::int32_t KalimaBeamJoint = 0x610, KalimaBeamSubtype = 8, NoKey = -1, NoTarget = -1;
        constexpr float KalimaBeamWidth = 20.0f;
        const auto beam = Scenes::CreateFaunaJoint(world.DrawTail().joints,
                                                   {KalimaBeamJoint, actor.position, actor.position, actor.angles,
                                                    KalimaBeamSubtype, &actor, KalimaBeamWidth, NoKey, 0, 0, NoTarget},
                                                   random, Scenes::JointOwnerKind::Actor);
        if (!beam.HasValue())
            return Core::Unexpected(std::format("0x4c91dc: {}", beam.Error()));
    }
    // 0x4c91e4..0x4c91fa.
    if (auto set = ApplyCharacterSet(Scenes::ActorWithKey(world, record.key), record.equipment, resources);
        !set.HasValue())
        return set;
    // 0x4c920b..0x4c9246.
    actor.byte1e3 = 0;
    actor.name = record.name;
    if (kept)
    {
        actor.byte360 = kept->byte360;
        actor.flags78 = kept->flags78;
    }
    // 0x4c9249..0x4c9256.
    ApplyCastleSiegeFlags(actor, record.effects, runtime.map);
    return Scenes::RefreshTheSiegePanel(runtime);
}

// 0x513340 with the whole nibble: from 8 to 15 it leaves through the JA with 0 (0x51334e).
std::uint8_t ShownLevelOf(std::uint8_t bits)
{
    constexpr std::uint8_t LastLevelBits = 7;
    return bits > LastLevelBits ? std::uint8_t{0} : Kernel::ItemLevelOf(bits);
}

constexpr std::uint16_t FirstItemModel = 0x212;

std::uint16_t ModelOf(std::uint16_t type)
{
    return static_cast<std::uint16_t>(type + FirstItemModel);
}

struct ShownItem
{
    std::uint16_t type = 0;
    std::uint8_t level = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
};

// 0x4c83ec..0x4c8486: the hands. An empty right hand does not touch the level; an empty left one releases the published
// hero's raven ([0x743a170], 0x4c8438).
Core::Expected<void, std::string> ShowInTheHand(Kernel::ActorState& actor, std::size_t hand, const ShownItem& item,
                                                const Scenes::WorldEntryResources& resources)
{
    Kernel::ActorItemPart& part = actor.hands[hand];
    if (item.type == Scenes::NoInventoryItem)
    {
        part.model = Kernel::NoItemModel;
        part.flags1b = 0;
        if (hand == 1)
            Scenes::PublishedHero(resources).darkSpirit.reset();
        return {};
    }
    part.model = ModelOf(item.type);
    part.level = item.level;
    part.flags1b = item.excellent;
    if (hand == 1)
        return Scenes::RecreateTransitionRaven(actor, resources);
    return {};
}

// 0x4c848b..0x4c8667: the five of the body; an empty one takes the class's part with the three bytes at 0.
void ShowOnTheBody(Kernel::ActorState& actor, std::size_t index, const ShownItem& item)
{
    Kernel::ActorItemPart& part = actor.body[index];
    if (item.type == Scenes::NoInventoryItem)
    {
        part.model = Kernel::ClassPartModel(static_cast<Kernel::BodyPart>(index), actor.characterClass);
        part.level = 0;
        part.flags1b = 0;
        part.flags1c = 0;
        return;
    }
    part.model = ModelOf(item.type);
    part.level = item.level;
    part.flags1b = item.excellent;
    part.flags1c = item.ancient;
}

// 0x4c8711..0x4c87d4: the pet and its helper (the table at 0x4c882c on the type − 0x1a00).
Core::Expected<void, std::string> ShowThePet(std::size_t index, const ShownItem& item,
                                             const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::ActorState& actor = world.Actors()[index];
    constexpr std::uint16_t Dinorant = 0x1c15;
    if (item.type == Scenes::NoInventoryItem)
    {
        actor.pet.model = Kernel::NoItemModel;
        actor.previousHad1c15 = false;
        Scenes::TurnOffHelpersOf(world.Helpers(), index);
        return {};
    }
    actor.pet.model = ModelOf(item.type);
    actor.previousHad1c15 = actor.pet.model == Dinorant;
    actor.pet.level = 0;
    constexpr std::uint16_t FirstPet = 0x1a00;
    std::uint16_t helper = 0;
    switch (item.type - FirstPet)
    {
    case 0:
        helper = 0x1c12;
        break;
    case 2:
        helper = 0xc6;
        break;
    case 3:
        helper = 0x110;
        break;
    case 4:
        helper = Scenes::DarkHorseModel;
        break;
    case 0x25:
    {
        // 0x4c879a..0x4c87ca: Fenrir, with the excellent bits (& 0x3f) at +0x31b.
        constexpr std::uint16_t RedFenrir = 0x14a;
        constexpr std::uint16_t BlueFenrir = 0x14c;
        constexpr std::uint16_t OtherFenrir = 0x14b;
        actor.pet.flags1b = item.excellent;
        helper = item.excellent == 1 ? RedFenrir : item.excellent == 2 ? BlueFenrir : OtherFenrir;
        break;
    }
    default:
        return {};
    }
    auto created = Scenes::CreateHelper(world, helper, actor.position, index, &resources.cloth.random);
    return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
}
} // namespace

// A write of +0x41c or +0x420: opens the entry, writes and closes it (4 rand() if no access is left open).
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    cell = value;
}

// A read: opens, reads and closes (0x40b800 or the inline close).
std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x4c8b0c..0x4c8f1b and 0x4c939e..0x4c9523: the protected cell and the target with the same values.
void WriteTheCells(Kernel::ActorState& actor, std::uint8_t x, std::uint8_t y, Crt::CrtRandom& random)
{
    WriteCell(x, actor.cellX, actor.cellXAccesses, random);
    WriteCell(y, actor.cellY, actor.cellYAccesses, random);
    actor.targetX = x;
    actor.targetY = y;
}

// 0x4c8f21..0x4c8f42 and 0x4c950c..0x4c9540: the high nibble of the direction.
float AngleOf(std::uint8_t directionAndPk)
{
    constexpr int DirectionShift = 4;
    return (static_cast<float>(directionAndPk >> DirectionShift) - 1.0f) * DirectionDegrees;
}

// 0x4c8f4b..0x4c905c: the X and then the Y, read from the table, to the center of the cell.
void PutInTheCellCenter(Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    actor.position.x = (static_cast<float>(x) + CellCenter) * CellSize;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    actor.position.y = (static_cast<float>(y) + CellCenter) * CellSize;
}

// 0x4c907c..0x4c91a3 and 0x4c9684..0x4c972c: the Y, the X and 0x4eac00(X, Y, target, +0x3e8, 0); +0x378 at 1 only if
// there is a path (without one it stays as it was).
Core::Expected<void, std::string> WalkToTheTarget(Kernel::ActorState& actor, std::uint8_t targetX, std::uint8_t targetY,
                                                  const Scenes::WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    const auto& attributes = Scenes::TerrainOf(resources).attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4eac00: faltan las paredes vigentes para el camino de otro jugador"));
    Pathfinding::PathFinder finder(attributes->cells);
    // 0x4eac0a..0x4eac41: with a Crywolf altar at 0x6c8e54, the A* flag also on another character's path.
    const auto path = finder.Find({x, y}, {targetX, targetY}, Scenes::CrywolfAltarGoal(resources.world));
    if (!path)
        return {};
    actor.path = *path;
    actor.movement = 1;
    return {};
}

Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Actors::Players& players,
                                                       const Scenes::WorldEntryResources& resources)
{
    // The session already made the dispatcher's copy (0x4e372e..0x4e37b8), before the reader (CopyToPacketRing).
    for (const Protocol::Actors::PlayerRecord& record : players.records)
    {
        // 0x4c88d2..0x4c88dc.
        if (IsHidden(record.name))
            continue;
        if (auto applied = ApplyPlayerRecord(record, players.packet, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

void ApplyViewportLeave(const Protocol::Actors::Leave& leave, Kernel::WorldState& world)
{
    for (const std::uint16_t word : leave.keys)
    {
        Scenes::ForgetTheSwitchHolder(world.Runtime(), word);
        // 0x4ca7e1..0x4ca7e8.
        const auto key = static_cast<std::int32_t>(word & Protocol::Actors::KeyMask);
        RemoveActor(world, key);
        // 0x4ca7f0..0x4ca809: the shop sign with the key, if there is one.
        Scenes::PersonalShopState& shops = world.Runtime().social.personalShop;
        if (const std::optional<std::size_t> owner = Scenes::ShopTitleWithKey(shops, key))
            Scenes::RemoveShopTitle(shops, *owner);
    }
}

Core::Expected<void, std::string> ApplyTransformed(const Protocol::Actors::Transformed& transformed,
                                                   const Scenes::WorldEntryResources& resources)
{
    for (const Protocol::Actors::TransformedRecord& record : transformed.records)
    {
        // 0x4c9307..0x4c9311.
        if (IsHidden(record.name))
            continue;
        // 0x4c9317..0x4c934c: the published hero's class before the creation and 0x5141e0(type, X, Y, key), without
        // looking at whether it returns 0.
        const std::uint8_t heroClass = Scenes::PublishedHero(resources).characterClass;
        const auto created = Scenes::CreateMonster(record.type, record.x, record.y, record.key, resources);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        if (auto finished = FinishTheTransformed(created.Value(), record, heroClass, resources); !finished.HasValue())
            return finished;
    }
    return {};
}

Core::Expected<void, std::string> FinishTheTransformed(std::size_t index,
                                                       const Protocol::Actors::TransformedRecord& record,
                                                       std::uint8_t heroClass,
                                                       const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    Kernel::ActorState& actor = world.Actors()[index];
    // 0x4c9356..0x4c9385.
    constexpr std::uint16_t SmallerWord376 = 7;
    if (actor.word376 == SmallerWord376)
        actor.scale = 0.8f;
    actor.characterClass = heroClass;
    actor.pk = PkOf(record.directionAndPk);
    actor.byte88 = 1;
    actor.byte3e3 = 1;
    // 0x4c938c..0x4c9396.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources); !flagged.HasValue())
        return flagged;
    ApplyCastleSiegeFlags(actor, record.effects, runtime.map);
    WriteTheCells(actor, record.x, record.y, random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (record.isNew)
    {
        // 0x4c9549..0x4c9652.
        PutInTheCellCenter(actor, random);
        actor.alpha = 0.0f;
        // 0x4c961f..0x4c967f: 0x518cf0(0xec, +0x10, +0x1c, +0xf4, 0, the character, −1, 0, 0, 0, 0), the magic circle
        // (World/BlastModelEffects), and 0x53fe70(0x5d5, +0x10, +0x1c, +0xf4, 2, 1.0f, the character), the ray that
        // follows it on its way; neither draws a rand(). With the table full, nothing.
        constexpr std::int16_t MagicCircle = 0xec;
        if (auto circle = Scenes::CreateBlastEffectOutsideTheStep({.type = MagicCircle,
                                                                   .position = actor.position,
                                                                   .angles = actor.angles,
                                                                   .light = actor.light,
                                                                   .subtype = 0,
                                                                   .key = 0xffff},
                                                                  &actor, world, random);
            !circle.HasValue())
            return Core::Unexpected(std::format("0x4c965c: {}", circle.Error()));
        const World::ParticleOwner owner{World::ParticleOwnerKind::Actor, index};
        if (auto bolt = world.Particles().CreateOwnedChecked(World::LightningParticle, actor.position, actor.angles,
                                                             actor.light, World::LightningFollowSubtype, 1.0f, 0.0f,
                                                             random, 0, owner);
            !bolt.HasValue())
            return Core::Unexpected(
                std::format("0x4c9674: la partícula 0x5d5/2, error {}", static_cast<unsigned>(bolt.Error())));
    }
    else if (auto walked = WalkToTheTarget(actor, record.targetX, record.targetY, resources); !walked.HasValue())
        return walked;
    // 0x4c9733..0x4c975d.
    actor.byte1e3 = 0;
    actor.name = record.name;
    return Scenes::DressTransitionHero(Scenes::PublishedHero(resources), resources);
}

Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Actors::EquipmentShown& shown,
                                                      const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    const std::size_t index = Scenes::ActorWithKey(world, shown.key);
    Kernel::ActorState& actor = world.Actors()[index];
    const std::span<const std::byte> bytes = shown.item;
    // 0x4c83b4..0x4c83dc: pkt[6] (byte 1), pkt[8] (byte 3) and pkt[9] (byte 4).
    constexpr std::uint8_t LevelMask = 0xf;
    constexpr int SlotShift = 4;
    constexpr std::uint8_t ExcellentMask = 0x3f;
    const std::uint8_t placeAndLevel = ByteOf(bytes, 1);
    const ShownItem item{.type = Scenes::InventoryItemType(shown.item),
                         .level = ShownLevelOf(placeAndLevel & LevelMask),
                         .excellent = static_cast<std::uint8_t>(ByteOf(bytes, 3) & ExcellentMask),
                         .ancient = ByteOf(bytes, 4)};
    const auto place = static_cast<std::uint8_t>(placeAndLevel >> SlotShift);
    constexpr std::uint8_t FirstBodyPlace = 2;
    constexpr std::uint8_t LastBodyPlace = 6;
    constexpr std::uint8_t WingsPlace = 7;
    constexpr std::uint8_t PetPlace = 8;
    if (place < FirstBodyPlace)
    {
        if (auto shownInHand = ShowInTheHand(actor, place, item, resources); !shownInHand.HasValue())
            return shownInHand;
    }
    else if (place <= LastBodyPlace)
    {
        ShowOnTheBody(actor, place - FirstBodyPlace, item);
    }
    else if (place == WingsPlace)
    {
        // 0x4c86e4..0x4c870c.
        if (item.type == Scenes::NoInventoryItem)
        {
            actor.wings.model = Kernel::NoItemModel;
        }
        else
        {
            actor.wings.level = 0;
            actor.wings.model = ModelOf(item.type);
        }
    }
    else if (place == PetPlace)
    {
        if (auto pet = ShowThePet(index, item, resources); !pet.HasValue())
            return pet;
    }
    // 0x4c87d7..0x4c87de.
    ArmForTheEventMaps(actor, world.Runtime().map);
    ApplyHeadAndScale(actor);
    return {};
}

Core::Expected<void, std::string> ApplyViewportPosition(const Protocol::Actors::Position& position,
                                                        const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::ActorState& actor = world.Actors()[Scenes::ActorWithKey(world, position.key)];
    auto& random = resources.cloth.random;
    constexpr std::uint16_t PushedType = 0xf1;
    if (actor.type == PushedType)
    {
        // 0x4c6b80..0x4c6ca3: +0xe4 at 20; the X read, (pkt[5] − X) · 400 at +0xd0; the Y read, (Y − pkt[6]) · 400 at
        // +0xcc (0x6b87b0); sound 0x1b and 0x51fe10 with the position.
        constexpr float PushSpeed = 20.0f;
        constexpr float PushScale = 400.0f;
        constexpr std::int32_t PushSound = 0x1b;
        actor.floatE4 = PushSpeed;
        const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
        actor.floatD0 =
            static_cast<float>(static_cast<std::int32_t>(position.x) - static_cast<std::int32_t>(x)) * PushScale;
        const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
        actor.floatCC =
            static_cast<float>(static_cast<std::int32_t>(y) - static_cast<std::int32_t>(position.y)) * PushScale;
        // 0x41ed10(0x1b, 0, 0).
        world.Runtime().RequestSound(PushSound);
        if (auto burst = World::CreatePushBurst(world.Particles(), actor.position, random); !burst.HasValue())
            return Core::Unexpected(std::format("0x51fe9e: falla la partícula 0x5c5/2 del empujón DF, error {}",
                                                static_cast<unsigned>(burst.Error())));
        return {};
    }
    // 0x4c6ca8..0x4c6dfe.
    WriteTheCells(actor, position.x, position.y, resources.cloth.random);
    actor.byte399 = 1;
    return {};
}
} // namespace Client::Actors
