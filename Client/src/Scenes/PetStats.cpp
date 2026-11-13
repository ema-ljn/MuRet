#include "Scenes/PetStats.h"

#include "Core/Expected.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x420cc0: la ventana de la mascota abierta es 0x723911 en 2.
constexpr std::uint8_t PetWindowOpen = 2;
// 0x421f62..0x421fbf: la casilla del cuervo (la mano izquierda) y la del caballo.
constexpr std::uint8_t RavenSlot = 1;
constexpr std::uint8_t HorseSlot = 8;
// 0x421f52..0x421f5c: las clases que hacen algo más con la mascota del héroe.
constexpr std::uint8_t PlainKind = 0;
constexpr std::uint8_t LevelUpKind = 0xfe;
constexpr std::uint8_t LastKind = 0xff;
// 0x4220bf: el +8 del héroe es la clase + 3 (1 con 0xfe, 2 con 0xff).
constexpr std::uint8_t KindToRiderByte = 3;
// 0x421f00: con la casilla debajo de 12 (el equipo) y el entrenador abierto, el cartel no escribe la tabla.
constexpr std::uint8_t EquipmentSlots = 12;
constexpr std::uint16_t NoHoveredItem = 0xffff;
constexpr std::uint16_t DarkHorseItem = 0x1a04;
// El lugar del equipo de +0xbce (la durabilidad del cuervo, 0x418170) y de +0xdac (el caballo, 0x42214a).
constexpr std::size_t RavenEquipmentSlot = 1;
constexpr std::size_t HorseEquipmentSlot = 8;

// (nivel + 11) · (nivel + 1)³ · 100, en 32 bits (0x4222fd y 0x418207).
std::uint32_t NextExperience(std::uint32_t level)
{
    const std::uint32_t next = level + 1;
    return (level + 11) * next * next * next * 100;
}

// +0x36 + +0x20 del registro propio, con el registro abierto y cerrado.
std::uint32_t Allowance(WorldState& world, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                               Render::Models::OwnRecordCloseRandoms);
    const OwnCharacterRecord& own = world.OwnCharacter();
    return static_cast<std::uint32_t>(own.word36) + own.leadership;
}

// El byte de la durabilidad del lugar `slot` del equipo propio, con el registro abierto y cerrado.
std::uint8_t OwnDurability(WorldState& world, Render::Models::CrtRandom& random, std::size_t slot)
{
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, random,
                                               Render::Models::OwnRecordCloseRandoms);
    return world.OwnCharacter().equipment[slot].durability;
}

Core::Expected<PetStats*, std::string> EntryOf(WorldRuntime& runtime, std::uint8_t pet)
{
    if (pet >= runtime.pets.size())
        return Core::Unexpected(
            std::format("0x421dfd: la mascota {} escribe fuera de las tres de 0x723960 (0x14 bytes cada una)", pet));
    return &runtime.pets[pet];
}

// 0x4e4cbd..0x4e4ce7: con el registro propio abierto (0x4e79a0 y 0x40f370), 0x550d10.
Core::Expected<void, std::string> RecalculateTheStats(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    return RecalculateOwnStats(world.OwnCharacter(), OwnStatsContext{.itemSetTables = resources.itemSetTables,
                                                                     .itemSets = &world.ItemSets(),
                                                                     .items = resources.items,
                                                                     .publishedHero = world.Actors()[runtime.hero],
                                                                     .ownRecordAccesses = runtime.ownRecordDepth,
                                                                     .random = random,
                                                                     .held = HeldItemOf(runtime),
                                                                     .scan505820 = runtime.fullSetScan});
}

// 0x421db0 con la ventana de la mascota abierta (0x421dc7..0x421ead).
Core::Expected<void, std::string> WithTheWindowOpen(const Protocol::Pet::PetInfoReply& reply,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    ActorState& hero = world.Actors()[runtime.hero];
    if (hero.darkSpirit && reply.pet == RavenPetStats)
        ApplyRavenInfo(*hero.darkSpirit, reply.level, reply.experience, world, random);
    auto entry = EntryOf(runtime, reply.pet);
    if (!entry.HasValue())
        return Core::Unexpected(std::move(entry).Error());
    entry.Value()->level = reply.level;
    entry.Value()->durability = OwnDurability(world, random, HorseEquipmentSlot);
    entry.Value()->experience = reply.experience;
    UpdatePetStats(reply.pet, world, random);
    return {};
}

// 0x421fb6..0x422196: el caballo del héroe, en la casilla 8.
void TheHorse(const Protocol::Pet::PetInfoReply& reply, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    PetStats& horse = runtime.pets[DarkHorsePetStats];
    horse.level = reply.level;
    horse.durability = OwnDurability(world, random, HorseEquipmentSlot);
    horse.experience = reply.experience;
    if (reply.kind == LevelUpKind || reply.kind == LastKind)
        world.Actors()[runtime.hero].byte8 = static_cast<std::uint8_t>(reply.kind + KindToRiderByte);
    UpdatePetStats(DarkHorsePetStats, world, random);
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    World::ItemState& item = world.OwnCharacter().equipment[HorseEquipmentSlot];
    if (item.id == DarkHorseItem)
        AddTheDarkHorseOption(item, resources);
}

// 0x421db0 con la ventana cerrada (0x421ef2..0x422196).
Core::Expected<void, std::string> WithTheWindowClosed(const Protocol::Pet::PetInfoReply& reply,
                                                      const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    const World::ItemState& hovered = runtime.hoveredItemCopy;
    if (hovered.id != NoHoveredItem)
    {
        bool trainer = false;
        if (reply.slot < EquipmentSlots)
        {
            GamePanels panels(world, random);
            trainer = panels.Registry().IsOpen(UI::TrainerPanel);
        }
        if (!trainer)
        {
            auto entry = EntryOf(runtime, reply.pet);
            if (!entry.HasValue())
                return Core::Unexpected(std::move(entry).Error());
            entry.Value()->level = reply.level;
            entry.Value()->durability = hovered.durability;
            entry.Value()->experience = reply.experience;
        }
    }
    UpdatePetStats(reply.pet, world, random);
    if (reply.kind != PlainKind && reply.kind != LevelUpKind && reply.kind != LastKind)
        return {};
    if (reply.pet == RavenPetStats)
    {
        ActorState& hero = world.Actors()[runtime.hero];
        if (reply.slot != RavenSlot || !hero.darkSpirit)
            return {};
        ApplyRavenInfo(*hero.darkSpirit, reply.level, reply.experience, world, random);
        if (reply.kind == LevelUpKind)
            return Core::Unexpected(std::string("0x421f9a: los cinco joints 0x60f/2 del cuervo (0x418390) son de la "
                                                "CL11"));
        if (reply.kind == LastKind)
            return Core::Unexpected(std::string("0x421fae: los quince joints 0x60f/2 del cuervo (0x418400) son de la "
                                                "CL11"));
        return {};
    }
    if (reply.pet == DarkHorsePetStats && reply.slot == HorseSlot)
        TheHorse(reply, resources);
    return {};
}
} // namespace

void ResetThePets(WorldRuntime& runtime)
{
    runtime.petTab = 0;
    runtime.panels.delegated.pet = 0;
    runtime.pets = {};
}

void UpdatePetStats(std::uint8_t pet, WorldState& world, Render::Models::CrtRandom& random)
{
    WorldRuntime& runtime = world.Runtime();
    UpdatePetStats(pet, world, runtime.pets, runtime.ownRecordDepth, random);
}

void UpdatePetStats(std::uint8_t pet, const WorldState& world, std::array<PetStats, PetStatsCount>& pets,
                    std::uint8_t& ownRecordDepth, Render::Models::CrtRandom& random)
{
    std::uint32_t allowance = 0;
    std::uint32_t strength = 0;
    std::uint32_t word2a = 0;
    {
        const Render::Models::LogicalAccess access(ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& own = world.OwnCharacter();
        strength = own.strength;
        allowance = static_cast<std::uint32_t>(own.word36) + own.leadership;
        word2a = own.word2a;
    }
    if (pet != RavenPetStats && pet != DarkHorsePetStats)
        return;
    PetStats& stats = pets[pet];
    const std::uint32_t level = stats.level;
    stats.nextExperience = NextExperience(level);
    if (pet == RavenPetStats)
    {
        // 0x42226a..0x4222c3: los del cuervo, como los de 0x417f30.
        stats.word0c = static_cast<std::uint16_t>((level + 12) * 15 + (allowance >> 3));
        stats.word0e = static_cast<std::uint16_t>((allowance >> 2) + 200 + level * 15);
    }
    else
    {
        // 0x4222f0..0x422381: el caballo, con la fuerza y +0x2a; +0xe es +0xc y medio, en 16 bits.
        const auto word0c = static_cast<std::uint16_t>(allowance / 10 + (word2a + strength) / 10 + level * 5);
        stats.word0c = word0c;
        stats.word0e = static_cast<std::uint16_t>((word0c >> 1) + word0c);
    }
    stats.word10 = static_cast<std::uint16_t>(allowance / 50 + (level << 2) / 5 + 20);
    stats.word12 = static_cast<std::uint16_t>(level * 16 + 1000);
}

void ApplyRavenInfo(DarkSpirit& raven, std::uint8_t level, std::uint32_t experience, WorldState& world,
                    Render::Models::CrtRandom& random)
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

Core::Expected<void, std::string> ApplyPetInfo(const Protocol::Pet::PetInfoReply& reply,
                                               const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    const bool windowOpen = runtime.panels.delegated.pet == PetWindowOpen;
    auto applied = windowOpen ? WithTheWindowOpen(reply, resources) : WithTheWindowClosed(reply, resources);
    if (!applied.HasValue())
        return applied;
    return RecalculateTheStats(resources);
}
} // namespace Client::Scenes
