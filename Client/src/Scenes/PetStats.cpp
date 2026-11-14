#include "Scenes/PetStats.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
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

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

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

// Si 0x421db0 escribe la entrada de la mascota en la tabla de 0x723960: con la ventana abierta, siempre (0x421e00); con
// la ventana cerrada, con un ítem en el cartel y sin el entrenador abierto con la casilla debajo de 12
// (0x421eee..0x421f16).
bool WritesTheTable(const Protocol::Pet::PetInfoReply& reply, WorldState& world, Render::Models::CrtRandom& random,
                    bool windowOpen)
{
    if (windowOpen)
        return true;
    if (world.Runtime().hoveredItemCopy.id == NoHoveredItem)
        return false;
    if (reply.slot >= EquipmentSlots)
        return true;
    GamePanels panels(world, random);
    return !panels.Registry().IsOpen(UI::TrainerPanel);
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
    // ApplyPetInfo ya descartó la mascota de fuera de la tabla.
    PetStats& entry = runtime.pets[reply.pet];
    entry.level = reply.level;
    entry.durability = OwnDurability(world, random, HorseEquipmentSlot);
    entry.experience = reply.experience;
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

// 0x418390 (cinco) y 0x418400 (quince), por la vtable del cuervo (+0x14 y +0x18): 0x530860(0x60f, p, q, ángulos, 0,
// cuervo, 40, la clave 2, 0, 0, −1) con el objeto del cuervo (+0xc) de dueño. 0x418390 pasa una copia de su posición
// en p y q y los ángulos en 0; 0x418400, la copia en p, su posición en q y sus ángulos. Son los 60f/0 de
// Scenes::CreateFaunaJoint (la luz del cuervo, +0xf4, y con el ancho de 40 su paso no lee su acción): tres rand() cada
// uno. Con la tabla de joints llena, ninguno y sigue. Sin rand() propios.
Core::Expected<void, std::string> SparkTheRaven(DarkSpirit& raven, int joints, bool turned,
                                                const WorldEntryResources& resources)
{
    constexpr std::int32_t SparkleJoint = 0x60f, SparkleSubtype = 0, OrderKey = 2, NoTarget = -1;
    constexpr float OrderWidth = 40.0f; // 0x42200000
    std::span<JointState> pool = resources.world.DrawTail().joints;
    glm::vec3 start = raven.position;
    glm::vec3 angles = turned ? raven.angles : glm::vec3(0.0f);
    glm::vec3& target = turned ? raven.position : start;
    for (int made = 0; made < joints; ++made)
    {
        const auto created = CreateFaunaJoint(
            pool, {SparkleJoint, start, target, angles, SparkleSubtype, &raven, OrderWidth, OrderKey, 0, 0, NoTarget},
            resources.cloth.random, JointOwnerKind::Raven);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: {}", turned ? "0x418444" : "0x4183e8", created.Error()));
    }
    return {};
}

// 0x421db0 con la ventana cerrada (0x421ef2..0x422196).
Core::Expected<void, std::string> WithTheWindowClosed(const Protocol::Pet::PetInfoReply& reply,
                                                      const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Render::Models::CrtRandom& random = resources.cloth.random;
    // 0x421f18..0x421f40. ApplyPetInfo ya descartó la mascota de fuera de la tabla.
    if (WritesTheTable(reply, world, random, false))
    {
        PetStats& entry = runtime.pets[reply.pet];
        entry.level = reply.level;
        entry.durability = runtime.hoveredItemCopy.durability;
        entry.experience = reply.experience;
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
        // 0x421f8f..0x421fae: con 0xfe, la vtable + 0x14 (0x418390); con 0xff, la + 0x18 (0x418400).
        constexpr int LevelUpJoints = 5, LastJoints = 15;
        if (reply.kind == LevelUpKind)
            return SparkTheRaven(*hero.darkSpirit, LevelUpJoints, false, resources);
        if (reply.kind == LastKind)
            return SparkTheRaven(*hero.darkSpirit, LastJoints, true, resources);
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
    // 0x421dfd y 0x421f2c: una mascota de 3 o más escribe fuera de las tres entradas de 0x723960 (0x14 bytes cada una)
    // y sigue. MuRet descarta el A9 entero, antes de cualquier rand() (07, Q-C1208).
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
} // namespace Client::Scenes
