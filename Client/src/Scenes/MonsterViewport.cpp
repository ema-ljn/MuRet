#include "Scenes/MonsterViewport.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCharacterSet.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorEffectFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t CastleSiegeMap = 30;
constexpr std::uint16_t SiegeGateKind = 0x115;
constexpr std::uint32_t GateOpenFlag = 0x20000;

// 0x4ea030 sobre un monstruo: la cuenta de acciones de su modelo, que MuRet solo tiene del jugador.
Core::Expected<void, std::string> SetMonsterAction(ActorState& actor, std::uint8_t action, std::uint32_t address,
                                                   const WorldEntryResources& resources)
{
    const auto* skeleton = resources.models.SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: 0x4ea030 necesita el modelo {:#x}, que MuRet no carga (P-C176)", address, actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

// 0x4c9790, los casos con la acción 1, +0x112 en 1, el alpha en 1 y, si hay, el sonido.
Core::Expected<void, std::string> RiseWithActionOne(ActorState& actor, std::uint32_t address,
                                                    std::optional<std::int32_t> sound,
                                                    const WorldEntryResources& resources)
{
    if (auto set = SetMonsterAction(actor, 1, address, resources); !set.HasValue())
        return set;
    actor.animation.previousAction = 1;
    actor.alpha = 1.0f;
    if (sound)
        resources.world.Runtime().lastSoundId = *sound; // (CL10)
    return {};
}

// 0x4c9ed2..0x4c9f44 y 0x4e0b96..0x4e0c10: la puerta de Castle Siege.
Core::Expected<void, std::string> CheckTheSiegeGate(const ActorState& actor, const WorldRuntime& runtime,
                                                    std::uint32_t address)
{
    if (runtime.map != CastleSiegeMap || actor.word376 != SiegeGateKind)
        return {};
    if ((actor.flags78 & GateOpenFlag) == GateOpenFlag || actor.flags78 == 0)
        return Core::Unexpected(
            std::format("{:#x}: la puerta del asedio (0x4ea030 y 0x42a110) es de la CL9.5", address));
    return {};
}

// 0x4c9cf8..0x4c9d3c: el multiplicador del ángulo, 45 o, con el modelo 0x1b5, los de la celda. Los DWORD de +0x420 y
// +0x41c se leen sin abrir su entrada: lo que dejó el último cierre de 0x511290 (Q-C849).
float AngleStepOf(const ActorState& actor)
{
    constexpr std::uint16_t RotatedModel = 0x1b5;
    float step = 45.0f; // 0x6b8848
    if (actor.type != RotatedModel)
        return step;
    if (actor.cellYLeft == 0x5a)
        step = 0.0f; // 0x6b86a0
    if (actor.cellXLeft == 0x3e)
        step = -135.0f; // 0x6ba100
    if (actor.cellXLeft == 0xb7)
        step = 90.0f; // 0x6b8948
    return step;
}

Core::Expected<void, std::string> ApplyMonsterRecord(const Protocol::Viewport::MonsterRecord& record,
                                                     const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const auto created = CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    ActorState& actor = world.Actors()[index];
    // 0x4c9cee..0x4c9cf3.
    if (auto flagged =
            ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId, world.Map().effects);
        !flagged.HasValue())
        return flagged;
    constexpr int DirectionShift = 4;
    const float step = AngleStepOf(actor);
    actor.angles.z = (static_cast<float>(record.direction >> DirectionShift) - 1.0f) * step;
    // 0x4c9d67..0x4c9ee9.
    WriteTheCells(actor, record.x, record.y, random);
    constexpr std::uint8_t HighBit = 0x80;
    constexpr std::uint8_t StanceMask = 7;
    actor.byte42d = record.typeHigh & HighBit;
    actor.byte93 = (record.typeHigh >> DirectionShift) & StanceMask;
    if (auto gate = CheckTheSiegeGate(actor, runtime, 0x4c9f0f); !gate.HasValue())
        return gate;
    // 0x4c9f4c..0x4ca07a.
    if (record.isNew)
        return ArriveAsNew(actor, resources);
    return WalkToTheTarget(actor, record.targetX, record.targetY, resources);
}

// 0x517400(dueño, clave, tipo, X, Y, recién llegado).
Core::Expected<std::size_t, std::string> CreateSummon(const Protocol::Viewport::SummonRecord& record,
                                                      const WorldEntryResources& resources)
{
    const auto created = CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return created;
    ActorState& actor = resources.world.Actors()[created.Value()];
    constexpr std::uint16_t FirstSummonLevel = 0x97;
    actor.level = static_cast<std::uint16_t>(record.type - FirstSummonLevel);
    // 0x51743f: wsprintfA(local, el nombre de +0x1d9 como formato, el dueño). Los casos de 0x98..0x9e dejan la cadena
    // vacía de 0x6e57a4: sin '%', el resultado es el formato.
    const std::string format = ActorText(actor);
    if (format.find('%') != std::string::npos)
        return Core::Unexpected(std::string("0x51743f: wsprintfA con un formato en el nombre, sin portar"));
    constexpr std::uint16_t LastSummonLevel = 7;
    if (actor.level == LastSummonLevel)
        actor.subtype = 1;
    // 0x51745c..0x517466: los 24 bytes del local a +0x1d9; después del 0, la pila sin iniciar (Q-C848): MuRet escribe
    // la cadena con su 0 y deja el resto (P-C177).
    std::vector<std::byte> bytes(std::as_bytes(std::span(format)).begin(), std::as_bytes(std::span(format)).end());
    bytes.push_back(std::byte{0});
    if (auto put = PutActorText(actor, bytes, 0x51745c); !put.HasValue())
        return Core::Unexpected(put.Error());
    if (record.isNew)
        return Core::Unexpected(std::string("0x51747c: los cuatro rand() y los joints 0x615 (0x530860) del invocado "
                                            "que llega son de la CL11"));
    return created;
}

// 0x4ca516..0x4ca5bf: el dueño (diez bytes, con +0x1e3 en 0 antes), el Text 485 y el nombre que tenía.
Core::Expected<void, std::string> NameAfterTheOwner(ActorState& actor, const Protocol::Viewport::SummonRecord& record,
                                                    const WorldEntryResources& resources)
{
    // 0x76e0a38, el registro 485 de Text.bmd.
    constexpr std::size_t OwnerText = 485;
    const std::string monster = ActorText(actor);
    actor.byte1e3 = 0;
    actor.name = record.owner;
    if (resources.texts == nullptr)
        return Core::Unexpected(std::string("0x4ca570: falta Text.bmd para el nombre del invocado (el 485)"));
    const auto text = resources.texts->Text(OwnerText);
    if (!text)
        return Core::Unexpected(std::string("0x4ca570: Text.bmd no tiene el 485"));
    // Los dos strcat, cada uno con su 0: el Text 485 (0x4ca57a..0x4ca592) y el nombre de antes (0x4ca5a7..0x4ca5bf).
    const auto concatenate = [&actor](std::string_view added, std::uint32_t address)
    {
        const std::string_view cut = added.substr(0, added.find('\0'));
        std::vector<std::byte> bytes(std::as_bytes(std::span(cut)).begin(), std::as_bytes(std::span(cut)).end());
        bytes.push_back(std::byte{0});
        return PutActorText(actor, bytes, address, ActorText(actor).size());
    };
    if (auto added = concatenate(*text, 0x4ca58b); !added.HasValue())
        return added;
    return concatenate(monster, 0x4ca5b8);
}

bool IsSummonType(std::uint16_t type)
{
    constexpr std::uint16_t FirstSummon = 0x98;
    constexpr std::uint16_t LastSummon = 0x9e;
    return type >= FirstSummon && type <= LastSummon;
}

Core::Expected<void, std::string> ApplySummonRecord(const Protocol::Viewport::SummonRecord& record,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const bool summon = IsSummonType(record.type);
    const auto created = summon ? CreateSummon(record, resources)
                                : CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    ActorState& actor = world.Actors()[index];
    // 0x4ca16d..0x4ca1a4.
    if (auto flagged =
            ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId, world.Map().effects);
        !flagged.HasValue())
        return flagged;
    actor.angles.z = AngleOf(record.directionAndPk);
    WriteTheCells(actor, record.x, record.y, random);
    // 0x4ca4de..0x4ca4fa.
    constexpr std::uint8_t PkMask = 0xf;
    constexpr std::uint8_t MurdererPk = 6;
    actor.byte88 = 1;
    actor.pk = record.directionAndPk & PkMask;
    if (actor.pk >= MurdererPk)
        actor.level = 1;
    if (!summon)
    {
        if (auto named = NameAfterTheOwner(actor, record, resources); !named.HasValue())
            return named;
    }
    // 0x4ca5c4..0x4ca748.
    if (record.isNew)
    {
        if (auto arrived = ArriveAsNew(actor, resources); !arrived.HasValue())
            return arrived;
        return Core::Unexpected(std::string("0x4ca5f6: el efecto 0xec (0x518cf0) y el joint 0x5d5/2 (0x53fe70) del "
                                            "invocado que llega son de la CL11"));
    }
    return WalkToTheTarget(actor, record.targetX, record.targetY, resources);
}

// 0x4e0b7f..0x4e0b9b y 0x4e0dea..0x4e0e13: +0x428 en 0x96 y el ángulo rand() % 360.
void TurnAtRandom(ActorState& actor, Render::Models::CrtRandom& random)
{
    constexpr std::int32_t FullTurn = 360;
    constexpr std::int32_t ObjectMark = 0x96;
    const std::int32_t drawn = random.Next();
    actor.dword428 = ObjectMark;
    actor.angles.z = static_cast<float>(drawn % FullTurn);
}

// 0x4e0c1d..0x4e0e52, el registro de un jugador.
Core::Expected<void, std::string> ApplyObjectPlayer(const Protocol::Viewport::ObjectRecord& record,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const ActorConstructionContext construction{world.Map().terrain, runtime.map, World::CullingScene::Game,
                                                resources.camera.fixedHeight};
    const auto created = CreateActor(world, record.key, Data::Models::PlayerSkeletonModel, {record.x, record.y, 0.0f},
                                     construction, resources.cloth);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    ActorState& actor = world.Actors()[index];
    // ((b & 0x10) | (b >> 4)) >> 1, como el 12.
    constexpr std::uint8_t EvolutionBit = 0x10;
    constexpr int ClassShift = 4;
    constexpr std::uint8_t ObjectClass = 0x80;
    const std::uint8_t classByte = record.classOrTypeHigh;
    actor.characterClass = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>((classByte & EvolutionBit) | (classByte >> ClassShift)) >> 1);
    actor.skin = 0;
    actor.pk = 0;
    actor.byte88 = ObjectClass;
    if (auto flagged =
            ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId, world.Map().effects);
        !flagged.HasValue())
        return flagged;
    WriteTheCells(actor, record.x, record.y, random);
    TurnAtRandom(actor, random);
    // 0x4e0e16..0x4e0e1f: 0x5139f0 con el lugar de 0x5110d0 y el equipo de +7.
    if (auto set = ApplyCharacterSet(ActorWithKey(world, record.key), record.equipment, resources); !set.HasValue())
        return set;
    // 0x4e0e24..0x4e0e52: "   " (0x6c8648) y +0x1e3 en 0.
    constexpr std::array<std::byte, 4> Blank{std::byte{' '}, std::byte{' '}, std::byte{' '}, std::byte{0}};
    if (auto put = PutActorText(actor, Blank, 0x4e0e24); !put.HasValue())
        return put;
    actor.byte1e3 = 0;
    return {};
}

// 0x4e09ca..0x4e0c18, el registro de un monstruo (un 0 de 0x5141e0 pasa al siguiente, 0x4e0a06).
Core::Expected<void, std::string> ApplyObjectMonster(const Protocol::Viewport::ObjectRecord& record,
                                                     const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    constexpr int TypeHighShift = 8;
    const auto type = static_cast<std::int32_t>(record.classOrTypeHigh << TypeHighShift | record.typeLow);
    const auto created = CreateMonster(type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    ActorState& actor = world.Actors()[index];
    if (auto flagged =
            ApplyEffectFlags(actor, record.effects, index == runtime.hero, runtime.lastSoundId, world.Map().effects);
        !flagged.HasValue())
        return flagged;
    WriteTheCells(actor, record.x, record.y, random);
    TurnAtRandom(actor, random);
    return CheckTheSiegeGate(actor, runtime, 0x4e0bd1);
}
} // namespace

Core::Expected<void, std::string> ArriveAsNew(ActorState& actor, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    switch (actor.word376)
    {
    case 0x15:
        return RiseWithActionOne(actor, 0x4c97ff, 0x13, resources);
    case 0x2c:
        return RiseWithActionOne(actor, 0x4c97d0, 0x14e, resources);
    case 0x35:
    case 0x36:
        return Core::Unexpected(
            std::string("0x4c982b: +0x3e5 en 0x3c frena al recién llegado 60 pasos con los efectos y "
                        "los rand() de 0x505de0 (0x5069ae), sin portar"));
    case 0x55:
    case 0x5b:
    case 0x61:
    case 0x72:
    case 0x78:
    case 0x7e:
        runtime.lastSoundId = 0x173; // (CL10) 0x4c9876
        return {};
    case 0x154:
    case 0x155:
    case 0x158:
    case 0x159:
        return Core::Unexpected(std::string("0x4c9883: la llegada lee [0x6e5904] + 0x154 (P-C60 g) y suelta los joints "
                                            "0x615, 0x614 y 0x5f6 con rand() (CL11)"));
    default:
        break;
    }
    // 0x4c9acb..0x4c9af7.
    constexpr std::uint8_t ArrivalAction = 0xc1;
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        actor.alpha = 0.0f;
        return {};
    }
    const auto* skeleton = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4c9ad9: falta Player.bmd para la acción 0xc1 del recién llegado"));
    Render::Models::SetAction(actor.animation, *skeleton, ArrivalAction);
    actor.animation.previousAction = ArrivalAction;
    actor.alpha = 1.0f;
    return {};
}

Core::Expected<void, std::string> ApplyViewportMonsters(const Protocol::Viewport::Monsters& monsters,
                                                        const WorldEntryResources& resources)
{
    for (const Protocol::Viewport::MonsterRecord& record : monsters.records)
    {
        if (auto applied = ApplyMonsterRecord(record, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

Core::Expected<void, std::string> ApplySummons(const Protocol::Viewport::Summons& summons,
                                               const WorldEntryResources& resources)
{
    for (const Protocol::Viewport::SummonRecord& record : summons.records)
    {
        if (auto applied = ApplySummonRecord(record, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

Core::Expected<void, std::string> ApplyViewportObjects(const Protocol::Viewport::Objects& objects,
                                                       const WorldEntryResources& resources)
{
    constexpr std::uint8_t PlayerKind = 1;
    constexpr std::uint8_t FirstMonsterKind = 2;
    constexpr std::uint8_t LastMonsterKind = 3;
    for (const Protocol::Viewport::ObjectRecord& record : objects.records)
    {
        Core::Expected<void, std::string> applied{};
        if (record.kind == PlayerKind)
            applied = ApplyObjectPlayer(record, resources);
        else if (record.kind >= FirstMonsterKind && record.kind <= LastMonsterKind)
            applied = ApplyObjectMonster(record, resources);
        if (!applied.HasValue())
            return applied;
    }
    return {};
}
} // namespace Client::Scenes
