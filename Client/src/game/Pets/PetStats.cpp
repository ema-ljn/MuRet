#include "Pets/PetStats.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkSpirit.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Hud/GamePanels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Items/OwnItemPlacement.h"
#include "Player/OwnStatHelpers.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Pets
{
namespace
{
// 0x420cc0: the pet window is open with 0x723911 at 2.
constexpr std::uint8_t PetWindowOpen = 2;
// 0x421f62..0x421fbf; the raven's slot is the left hand.
constexpr std::uint8_t RavenSlot = 1;
constexpr std::uint8_t HorseSlot = 8;
// 0x421f52..0x421f5c: the kinds that do something more with the hero's pet.
constexpr std::uint8_t PlainKind = 0;
constexpr std::uint8_t LevelUpKind = 0xfe;
constexpr std::uint8_t LastKind = 0xff;
// 0x4220bf: the hero's +8 is the kind + 3 (1 with 0xfe, 2 with 0xff).
constexpr std::uint8_t KindToRiderByte = 3;
// 0x421f00: with the slot under 12 (the equipment) and the trainer open, the label does not write the table.
constexpr std::uint8_t EquipmentSlots = 12;
constexpr std::uint16_t NoHoveredItem = 0xffff;
constexpr std::uint16_t DarkHorseItem = 0x1a04;
// The equipment slots of +0xbce (the raven's durability, 0x418170) and of +0xdac (the horse, 0x42214a).
constexpr std::size_t RavenEquipmentSlot = 1;
constexpr std::size_t HorseEquipmentSlot = 8;

// 0x4222fd and 0x418207, in 32 bits.
std::uint32_t NextExperience(std::uint32_t level)
{
    const std::uint32_t next = level + 1;
    return (level + 11) * next * next * next * 100;
}

// +0x36 + +0x20 of the own record, opened and closed around the read.
std::uint32_t Allowance(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    const Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    return static_cast<std::uint32_t>(own.word36) + own.leadership;
}

std::uint8_t OwnDurability(Kernel::WorldState& world, Crt::CrtRandom& random, std::size_t slot)
{
    const Models::LogicalAccess access(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return world.OwnCharacter().equipment[slot].durability;
}

// Whether 0x421db0 writes the pet's entry in the 0x723960 table: always with the window open (0x421e00); with it
// closed, with an item in the label and without the trainer open with the slot under 12 (0x421eee..0x421f16).
bool WritesTheTable(const Protocol::Pets::PetInfoReply& reply, Kernel::WorldState& world, Crt::CrtRandom& random,
                    bool windowOpen)
{
    if (windowOpen)
        return true;
    if (world.Runtime().hoveredItemCopy.id == NoHoveredItem)
        return false;
    if (reply.slot >= EquipmentSlots)
        return true;
    Hud::GamePanels panels(world, random);
    return !panels.Registry().IsOpen(Ui::TrainerPanel);
}

// 0x4e4cbd..0x4e4ce7: with the own record open (0x4e79a0 and 0x40f370), 0x550d10.
Core::Expected<void, std::string> RecalculateTheStats(const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return Player::RecalculateOwnStats(world.OwnCharacter(),
                                       Player::OwnStatsContext{.itemSetTables = resources.itemSetTables,
                                                               .itemSets = &world.ItemSets(),
                                                               .items = resources.items,
                                                               .publishedHero = world.Hero(),
                                                               .ownRecordAccesses = runtime.ownRecordDepth,
                                                               .random = random,
                                                               .held = Player::HeldItemOf(runtime),
                                                               .scan505820 = runtime.fullSetScan});
}

// 0x421db0 with the pet window open (0x421dc7..0x421ead).
Core::Expected<void, std::string> WithTheWindowOpen(const Protocol::Pets::PetInfoReply& reply,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    Kernel::ActorState& hero = world.Hero();
    if (hero.darkSpirit && reply.pet == Kernel::RavenPetStats)
        ApplyRavenInfo(*hero.darkSpirit, reply.level, reply.experience, world, random);
    // ApplyPetInfo already dropped a pet outside the table.
    Kernel::PetStats& entry = runtime.pets[reply.pet];
    entry.level = reply.level;
    entry.durability = OwnDurability(world, random, HorseEquipmentSlot);
    entry.experience = reply.experience;
    UpdatePetStats(reply.pet, world, random);
    return {};
}

// 0x421fb6..0x422196
void TheHorse(const Protocol::Pets::PetInfoReply& reply, const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    Kernel::PetStats& horse = runtime.pets[Kernel::DarkHorsePetStats];
    horse.level = reply.level;
    horse.durability = OwnDurability(world, random, HorseEquipmentSlot);
    horse.experience = reply.experience;
    if (reply.kind == LevelUpKind || reply.kind == LastKind)
        world.Hero().byte8 = static_cast<std::uint8_t>(reply.kind + KindToRiderByte);
    UpdatePetStats(Kernel::DarkHorsePetStats, world, random);
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    Kernel::ItemState& item = world.OwnCharacter().equipment[HorseEquipmentSlot];
    if (item.id == DarkHorseItem)
        Items::AddTheDarkHorseOption(item, resources);
}

// 0x418390 (five) and 0x418400 (fifteen), through the raven's vtable (+0x14 and +0x18): 0x530860(0x60f, p, q, angles,
// 0, raven, 40, key 2, 0, 0, −1) with the raven object (+0xc) as owner. Three rand() each (the raven's light, +0xf4;
// with width 40 its step does not read its action); with the joint table full, none, and it goes on. No rand() of their
// own.
Core::Expected<void, std::string> SparkTheRaven(Kernel::DarkSpirit& raven, int joints, bool turned,
                                                const Scenes::WorldEntryResources& resources)
{
    constexpr std::int32_t SparkleJoint = 0x60f, SparkleSubtype = 0, OrderKey = 2, NoTarget = -1;
    constexpr float OrderWidth = 40.0f; // 0x42200000
    std::span<Kernel::JointState> pool = resources.world.DrawTail().joints;
    glm::vec3 start = raven.position;
    glm::vec3 angles = turned ? raven.angles : glm::vec3(0.0f);
    glm::vec3& target = turned ? raven.position : start;
    for (int made = 0; made < joints; ++made)
    {
        const auto created = Joints::CreateFaunaJoint(
            pool, {SparkleJoint, start, target, angles, SparkleSubtype, &raven, OrderWidth, OrderKey, 0, 0, NoTarget},
            resources.cloth.random, Kernel::JointOwnerKind::Raven);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: {}", turned ? "0x418444" : "0x4183e8", created.Error()));
    }
    return {};
}

// 0x421db0 with the window closed (0x421ef2..0x422196).
Core::Expected<void, std::string> WithTheWindowClosed(const Protocol::Pets::PetInfoReply& reply,
                                                      const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    // 0x421f18..0x421f40. ApplyPetInfo already dropped a pet outside the table.
    if (WritesTheTable(reply, world, random, false))
    {
        Kernel::PetStats& entry = runtime.pets[reply.pet];
        entry.level = reply.level;
        entry.durability = runtime.hoveredItemCopy.durability;
        entry.experience = reply.experience;
    }
    UpdatePetStats(reply.pet, world, random);
    if (reply.kind != PlainKind && reply.kind != LevelUpKind && reply.kind != LastKind)
        return {};
    if (reply.pet == Kernel::RavenPetStats)
    {
        Kernel::ActorState& hero = world.Hero();
        if (reply.slot != RavenSlot || !hero.darkSpirit)
            return {};
        ApplyRavenInfo(*hero.darkSpirit, reply.level, reply.experience, world, random);
        // 0x421f8f..0x421fae: with 0xfe, vtable + 0x14 (0x418390); with 0xff, + 0x18 (0x418400).
        constexpr int LevelUpJoints = 5, LastJoints = 15;
        if (reply.kind == LevelUpKind)
            return SparkTheRaven(*hero.darkSpirit, LevelUpJoints, false, resources);
        if (reply.kind == LastKind)
            return SparkTheRaven(*hero.darkSpirit, LastJoints, true, resources);
        return {};
    }
    if (reply.pet == Kernel::DarkHorsePetStats && reply.slot == HorseSlot)
        TheHorse(reply, resources);
    return {};
}
} // namespace

void ResetThePets(Kernel::WorldRuntime& runtime)
{
    runtime.petTab = 0;
    runtime.panels.delegated.pet = 0;
    runtime.pets = {};
}

void UpdatePetStats(std::uint8_t pet, Kernel::WorldState& world, Crt::CrtRandom& random)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    UpdatePetStats(pet, world, runtime.pets, runtime.ownRecordDepth, random);
}

void UpdatePetStats(std::uint8_t pet, const Kernel::WorldState& world,
                    std::array<Kernel::PetStats, Kernel::PetStatsCount>& pets, std::uint8_t& ownRecordDepth,
                    Crt::CrtRandom& random)
{
    std::uint32_t allowance = 0;
    std::uint32_t strength = 0;
    std::uint32_t word2a = 0;
    {
        const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        const Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        strength = own.strength;
        allowance = static_cast<std::uint32_t>(own.word36) + own.leadership;
        word2a = own.word2a;
    }
    if (pet != Kernel::RavenPetStats && pet != Kernel::DarkHorsePetStats)
        return;
    Kernel::PetStats& stats = pets[pet];
    const std::uint32_t level = stats.level;
    stats.nextExperience = NextExperience(level);
    if (pet == Kernel::RavenPetStats)
    {
        // 0x42226a..0x4222c3: the raven's, like those of 0x417f30.
        stats.word0c = static_cast<std::uint16_t>((level + 12) * 15 + (allowance >> 3));
        stats.word0e = static_cast<std::uint16_t>((allowance >> 2) + 200 + level * 15);
    }
    else
    {
        // 0x4222f0..0x422381: the horse, with strength and +0x2a; +0xe is +0xc and a half, in 16 bits.
        const auto word0c = static_cast<std::uint16_t>(allowance / 10 + (word2a + strength) / 10 + level * 5);
        stats.word0c = word0c;
        stats.word0e = static_cast<std::uint16_t>((word0c >> 1) + word0c);
    }
    stats.word10 = static_cast<std::uint16_t>(allowance / 50 + (level << 2) / 5 + 20);
    stats.word12 = static_cast<std::uint16_t>(level * 16 + 1000);
}

void ApplyRavenInfo(Kernel::DarkSpirit& raven, std::uint8_t level, std::uint32_t experience, Kernel::WorldState& world,
                    Crt::CrtRandom& random)
{
    const std::uint32_t allowance = Allowance(world, random);
    raven.word1e2 = level;
    raven.level = level;
    raven.durability = OwnDurability(world, random, RavenEquipmentSlot);
    raven.experience = experience;
    raven.nextExperience = NextExperience(level);
    const std::uint32_t current = raven.level;
    raven.word456 = static_cast<std::uint16_t>((allowance >> 2) + 200 + current * 15);
    raven.word454 = static_cast<std::uint16_t>((current * 3 + 0x24) * 5 + (allowance >> 3));
    raven.word458 = static_cast<std::uint16_t>(20 + allowance / 50 + (current << 2) / 5);
    raven.word45a = static_cast<std::uint16_t>(current * 16 + 1000);
}

Core::Expected<void, std::string> ApplyPetInfo(const Protocol::Pets::PetInfoReply& reply,
                                               const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldRuntime& runtime = resources.world.Runtime();
    const bool windowOpen = runtime.panels.delegated.pet == PetWindowOpen;
    // 0x421dfd and 0x421f2c: a pet of 3 or more writes outside the three 0x723960 entries (0x14 bytes each) and goes
    // on. MuRet drops the whole A9, before any rand() (07, Q-C1208).
    if (reply.pet >= runtime.pets.size() && WritesTheTable(reply, resources.world, resources.cloth.random, windowOpen))
    {
        resources.log.Warning("se descarta un A9 con la mascota {}: el original escribe fuera de la tabla de 0x723960",
                              reply.pet);
        return {};
    }
    auto applied = windowOpen ? WithTheWindowOpen(reply, resources) : WithTheWindowClosed(reply, resources);
    if (!applied.HasValue())
        return applied;
    return RecalculateTheStats(resources);
}
} // namespace Client::Pets
