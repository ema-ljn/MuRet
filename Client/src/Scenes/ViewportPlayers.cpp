#include "Scenes/ViewportPlayers.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorCharacterSet.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorEffectFlags.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/ViewportPacketRing.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "UI/NameChecks.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::string_view HiddenName = "webzen"; // 0x6c8418 y 0x6c8420
constexpr float CellSize = 100.0f;                // 0x6b86b4
constexpr float CellCenter = 0.5f;                // 0x6b882c
constexpr float DirectionDegrees = 45.0f;         // 0x6b8848, con el 1 de 0x6b87d4

using Name = std::array<std::byte, Protocol::Character::NameSize>;

// Los diez bytes del nombre con el 0 de -0x4a (0x4c88b7) o de -0x12 (0x4c92e2) detrás.
std::string NameText(const Name& name)
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

bool IsHidden(const Name& name)
{
    return UI::ContainsLikeTheOriginal(NameText(name), HiddenName);
}

std::uint8_t ByteOf(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

// Una escritura de +0x41c o +0x420: abre la entrada, escribe y la cierra (4 rand() si queda sin accesos).
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    cell = value;
}

// Una lectura: abre, lee y cierra (0x40b800 o el cierre en línea).
std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x4c8b0c..0x4c8f1b y 0x4c939e..0x4c9523: la celda protegida y el blanco con los mismos valores.
void WriteTheCells(ActorState& actor, std::uint8_t x, std::uint8_t y, Render::Models::CrtRandom& random)
{
    WriteCell(x, actor.cellX, actor.cellXAccesses, random);
    WriteCell(y, actor.cellY, actor.cellYAccesses, random);
    actor.targetX = x;
    actor.targetY = y;
}

// 0x4c8f21..0x4c8f42 y 0x4c950c..0x4c9540: el nibble de arriba de la dirección.
float AngleOf(std::uint8_t directionAndPk)
{
    constexpr int DirectionShift = 4;
    return (static_cast<float>(directionAndPk >> DirectionShift) - 1.0f) * DirectionDegrees;
}

std::uint8_t PkOf(std::uint8_t directionAndPk)
{
    constexpr std::uint8_t PkMask = 0xf;
    return directionAndPk & PkMask;
}

// 0x4c8f4b..0x4c905c: la X y después la Y, leídas de la tabla, al centro de la celda.
void PutInTheCellCenter(ActorState& actor, Render::Models::CrtRandom& random)
{
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    actor.position.x = (static_cast<float>(x) + CellCenter) * CellSize;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    actor.position.y = (static_cast<float>(y) + CellCenter) * CellSize;
}

// 0x4c907c..0x4c91a3 y 0x4c9684..0x4c972c: la Y, la X y 0x4eac00(X, Y, destino, +0x3e8, 0); +0x378 en 1 solo si hay
// camino (sin él queda como estaba).
Core::Expected<void, std::string> WalkToTheTarget(ActorState& actor, std::uint8_t targetX, std::uint8_t targetY,
                                                  const WorldEntryResources& resources)
{
    auto& random = resources.cloth.random;
    const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
    const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
    auto& attributes = resources.world.Map().terrain.attributes;
    if (!attributes)
        return Core::Unexpected(std::string("0x4eac00: faltan las paredes vigentes para el camino de otro jugador"));
    GameLogic::PathFinder finder(attributes->cells);
    const auto path = finder.Find({x, y}, {targetX, targetY});
    if (!path)
        return {};
    actor.path = *path;
    actor.movement = 1;
    return {};
}

// 0x4c8a36: los estados 2 a 4 con su acción (la segunda, de la clase 2: & 7, el elfo).
std::optional<std::uint8_t> StateAction(std::uint8_t state, std::uint8_t characterClass)
{
    constexpr std::uint8_t ClassMask = 7;
    constexpr std::uint8_t Elf = 2;
    const bool elf = (characterClass & ClassMask) == Elf;
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

// Lo que el estado 1 conserva del vivo con la clave (0x4c8919..0x4c8965).
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

Kept KeepFrom(const ActorState& actor)
{
    return {.byte1f2 = static_cast<std::uint8_t>(actor.word1f2),
            .guildStatus = actor.guildStatus,
            .byte369 = actor.byte369,
            .byte370 = actor.byte370,
            .byte360 = actor.byte360,
            .flags78 = actor.flags78,
            .byte372 = actor.byte372};
}

// 0x4c89b4..0x4c89ed: el MOVZX deja +0x1f2 con el byte de abajo y el de arriba en 0.
void GiveBack(ActorState& actor, const Kept& kept)
{
    actor.word1f2 = kept.byte1f2;
    actor.guildStatus = kept.guildStatus;
    actor.byte369 = kept.byte369;
    actor.byte370 = kept.byte370;
    actor.byte360 = kept.byte360;
    actor.flags78 = kept.flags78;
    actor.byte372 = kept.byte372;
}

bool IsKalima(std::int32_t map)
{
    constexpr std::int32_t FirstKalima = 24;
    constexpr std::int32_t LastKalima = 29;
    constexpr std::int32_t SeventhKalima = 36;
    return (map >= FirstKalima && map <= LastKalima) || map == SeventhKalima;
}

// 0x4c89f3..0x4c8a04: ((b & 0x10) | (b >> 4)) >> 1.
std::uint8_t ClassOf(std::uint8_t classAndState)
{
    constexpr std::uint8_t EvolutionBit = 0x10;
    constexpr int ClassShift = 4;
    return static_cast<std::uint8_t>(
        static_cast<std::uint8_t>((classAndState & EvolutionBit) | (classAndState >> ClassShift)) >> 1);
}

Core::Expected<void, std::string> ApplyPlayerRecord(const Protocol::Viewport::PlayerRecord& record,
                                                    std::span<const std::byte> packet,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    constexpr std::uint8_t StateMask = 0xf;
    constexpr std::uint8_t TeleportState = 1;
    const std::uint8_t state = record.classAndState & StateMask;
    // 0x4c88e2..0x4c8965.
    std::optional<Kept> kept;
    if (state == TeleportState)
    {
        const std::size_t previous = ActorWithKey(world, record.key);
        if (previous != CreatedActorSearch)
            kept = KeepFrom(world.Actors()[previous]);
    }
    // 0x4c8968..0x4c8992.
    const ActorConstructionContext construction{world.Map().terrain, runtime.map, World::CullingScene::Game,
                                                resources.camera.fixedHeight};
    const auto created = CreateActor(world, record.key, Data::Models::PlayerSkeletonModel, {record.x, record.y, 0.0f},
                                     construction, resources.cloth);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    ActorState& actor = world.Actors()[index];
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
        if (auto teleported = StartTheTeleportAction(actor, resources); !teleported.HasValue())
            return teleported;
        // 0x4c8a43..0x4c8ad4: el paquete entero otra vez a la cola (Q-C801).
        StoreViewportPacket(runtime.viewportPackets, packet);
    }
    else if (const auto action = StateAction(state, actor.characterClass))
    {
        const auto* skeleton = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4ea030: falta Player.bmd para la acción del 0x12"));
        Render::Models::SetAction(actor.animation, *skeleton, *action);
    }
    // 0x4c8adc..0x4c8aec.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId);
        !flagged.HasValue())
        return flagged;
    WriteTheCells(actor, record.x, record.y, random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (record.isNew)
    {
        PutInTheCellCenter(actor, random);
        static_cast<void>(
            world.Map().effects.Create(World::ArrivalEffect, actor.position, actor.angles, actor.light, 0.0f));
        actor.alpha = 0.0f;
    }
    else if (auto walked = WalkToTheTarget(actor, record.targetX, record.targetY, resources); !walked.HasValue())
    {
        return walked;
    }
    // 0x4c91aa..0x4c91e1.
    if (IsKalima(runtime.map))
        return Core::Unexpected(std::string("0x4c91dc: el joint 0x610/8 (0x530860) del jugador que entra en Kalima es "
                                            "de la CL11"));
    // 0x4c91e4..0x4c91fa.
    if (auto set = ApplyCharacterSet(ActorWithKey(world, record.key), record.equipment, resources); !set.HasValue())
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
    return StepTheSiegePanel(runtime);
}

// 0x513340 con el nibble entero: de 8 a 15 sale por el JA con 0 (0x51334e).
std::uint8_t ShownLevelOf(std::uint8_t bits)
{
    constexpr std::uint8_t LastLevelBits = 7;
    return bits > LastLevelBits ? std::uint8_t{0} : GameLogic::ItemLevelOf(bits);
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

// 0x4c83ec..0x4c8486: las manos. La derecha vacía no toca el nivel; la izquierda vacía suelta el cuervo del héroe
// publicado ([0x743a170], 0x4c8438).
Core::Expected<void, std::string> ShowInTheHand(ActorState& actor, std::size_t hand, const ShownItem& item,
                                                const WorldEntryResources& resources)
{
    ActorItemPart& part = actor.hands[hand];
    if (item.type == NoInventoryItem)
    {
        part.model = NoItemModel;
        part.flags1b = 0;
        if (hand == 1)
            PublishedHero(resources).darkSpirit.reset();
        return {};
    }
    part.model = ModelOf(item.type);
    part.level = item.level;
    part.flags1b = item.excellent;
    if (hand == 1)
        return RecreateTransitionRaven(actor, resources);
    return {};
}

// 0x4c848b..0x4c8667: las cinco del cuerpo; vacías, la de la clase con los tres bytes en 0.
void ShowOnTheBody(ActorState& actor, std::size_t index, const ShownItem& item)
{
    ActorItemPart& part = actor.body[index];
    if (item.type == NoInventoryItem)
    {
        part.model = GameLogic::ClassPartModel(static_cast<GameLogic::BodyPart>(index), actor.characterClass);
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

// 0x4c8711..0x4c87d4: la mascota y su ayudante (la tabla de 0x4c882c sobre el tipo − 0x1a00).
Core::Expected<void, std::string> ShowThePet(std::size_t index, const ShownItem& item,
                                             const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& actor = world.Actors()[index];
    constexpr std::uint16_t Dinorant = 0x1c15;
    if (item.type == NoInventoryItem)
    {
        actor.pet.model = NoItemModel;
        actor.previousHad1c15 = false;
        TurnOffHelpersOf(world.Helpers(), index);
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
        helper = DarkHorseModel;
        break;
    case 0x25:
    {
        // 0x4c879a..0x4c87ca: Fenrir, con el excelente (& 0x3f) en +0x31b.
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
    auto created = CreateHelper(world, helper, actor.position, index, &resources.cloth.random);
    return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
}
} // namespace

Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Viewport::Players& players,
                                                       const WorldEntryResources& resources)
{
    // La copia del dispatcher (0x4e372e..0x4e37b8) ya la hizo la sesión, antes del lector (CopyToPacketRing).
    for (const Protocol::Viewport::PlayerRecord& record : players.records)
    {
        // 0x4c88d2..0x4c88dc.
        if (IsHidden(record.name))
            continue;
        if (auto applied = ApplyPlayerRecord(record, players.packet, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

void ApplyViewportLeave(const Protocol::Viewport::Leave& leave, WorldState& world)
{
    auto& siege = world.Runtime().castleSiegeInfo;
    for (const std::uint16_t word : leave.keys)
    {
        // 0x4ca799..0x4ca7cd.
        if (siege)
        {
            constexpr std::uint16_t SiegeKey = 0x13d;
            const std::size_t first = word == SiegeKey ? 4 : 0x1f;
            for (std::size_t offset = first; offset < first + 3; ++offset)
                (*siege)[offset] = 0;
        }
        // 0x4ca7e1..0x4ca7e8.
        RemoveActor(world, static_cast<std::int32_t>(word & Protocol::Viewport::KeyMask));
    }
}

Core::Expected<void, std::string> ApplyTransformed(const Protocol::Viewport::Transformed& transformed,
                                                   const WorldEntryResources& resources)
{
    static_cast<void>(resources);
    for (const Protocol::Viewport::TransformedRecord& record : transformed.records)
    {
        // 0x4c9307..0x4c9311.
        if (IsHidden(record.name))
            continue;
        return Core::Unexpected(std::string("0x4c934c: 0x5141e0, el alta del transformado o del monstruo de tipo +4, "
                                            "es de la hito 2"));
    }
    return {};
}

Core::Expected<void, std::string> FinishTheTransformed(std::size_t index,
                                                       const Protocol::Viewport::TransformedRecord& record,
                                                       std::uint8_t heroClass, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    ActorState& actor = world.Actors()[index];
    // 0x4c9356..0x4c9385.
    constexpr std::uint16_t SmallerWord376 = 7;
    if (actor.word376 == SmallerWord376)
        actor.scale = 0.8f;
    actor.characterClass = heroClass;
    actor.pk = PkOf(record.directionAndPk);
    actor.byte88 = 1;
    actor.byte3e3 = 1;
    // 0x4c938c..0x4c9396.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId);
        !flagged.HasValue())
        return flagged;
    ApplyCastleSiegeFlags(actor, record.effects, runtime.map);
    WriteTheCells(actor, record.x, record.y, random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (record.isNew)
    {
        // 0x4c9549..0x4c9652.
        PutInTheCellCenter(actor, random);
        actor.alpha = 0.0f;
        return Core::Unexpected(std::string("0x4c965c: el efecto 0xec (0x518cf0) y el joint 0x5d5/2 (0x53fe70) del "
                                            "transformado que llega son de la CL11"));
    }
    if (auto walked = WalkToTheTarget(actor, record.targetX, record.targetY, resources); !walked.HasValue())
        return walked;
    // 0x4c9733..0x4c975d.
    actor.byte1e3 = 0;
    actor.name = record.name;
    return DressTransitionHero(PublishedHero(resources), resources);
}

Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Viewport::EquipmentShown& shown,
                                                      const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const std::size_t index = ActorWithKey(world, shown.key);
    ActorState& actor = world.Actors()[index];
    const std::span<const std::byte> bytes = shown.item;
    // 0x4c83b4..0x4c83dc: pkt[6] (el byte 1), pkt[8] (el 3) y pkt[9] (el 4).
    constexpr std::uint8_t LevelMask = 0xf;
    constexpr int SlotShift = 4;
    constexpr std::uint8_t ExcellentMask = 0x3f;
    const std::uint8_t placeAndLevel = ByteOf(bytes, 1);
    const ShownItem item{.type = InventoryItemType(shown.item),
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
        if (item.type == NoInventoryItem)
        {
            actor.wings.model = NoItemModel;
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

Core::Expected<void, std::string> ApplyViewportPosition(const Protocol::Viewport::Position& position,
                                                        const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& actor = world.Actors()[ActorWithKey(world, position.key)];
    constexpr std::uint16_t PushedType = 0xf1;
    if (actor.type == PushedType)
        return Core::Unexpected(std::string("0x4c6b80: el empujón del tipo 0xf1 (+0xe4, +0xd0 y +0xcc), el sonido 0x1b "
                                            "y los joints 0x5c5 de 0x51fe10 son de la hito 2 y la CL11"));
    // 0x4c6ca8..0x4c6dfe.
    WriteTheCells(actor, position.x, position.y, resources.cloth.random);
    actor.byte399 = 1;
    return {};
}
} // namespace Client::Scenes
