#include "Monsters/MonsterViewport.h"
#include "Pets/HelperTable.h"

#include "Actors/ActionReply.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/TextTable.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Animation.h"
#include "Actors/ActorCharacterSet.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActorEffectFlags.h"
#include "Kernel/ActorSearch.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrivalSparks.h"
#include "Combat/BlastEffectCalls.h"
#include "CastleSiege/CastleBattleActors.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Joints/FaunaJointCreation.h"
#include "Monsters/MonsterCreation.h"
#include "Actors/ViewportRecord.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Kernel/JointRequest.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainCulling.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Monsters
{
namespace
{
using Maps::CastleSiegeMap;
constexpr std::uint16_t SiegeGateKind = 0x115;
constexpr std::uint32_t GateOpenFlag = 0x20000;
// 0x51745c: the summon name's six dwords.
constexpr std::size_t SummonNameBytes = 24;

// 0x4c9790
Core::Expected<void, std::string> RiseWithActionOne(Kernel::ActorState& actor, std::uint32_t address,
                                                    std::optional<std::int32_t> sound,
                                                    const Scenes::WorldEntryResources& resources)
{
    if (auto set = Actors::SetMonsterAction(actor, 1, address, resources); !set.HasValue())
        return set;
    actor.animation.previousAction = 1;
    actor.alpha = 1.0f;
    if (sound)
        resources.world.Runtime().RequestSound(Audio::SoundId{*sound}); // (CL10)
    return {};
}

// 0x4c9ed2..0x4c9f44 and 0x4e0b96..0x4e0c10: the Castle Siege gate.
Core::Expected<void, std::string> CheckTheSiegeGate(Kernel::ActorState& actor,
                                                    const Scenes::WorldEntryResources& resources)
{
    if (resources.world.Runtime().map != CastleSiegeMap || actor.word376 != SiegeGateKind)
        return {};
    if ((actor.flags78 & GateOpenFlag) == GateOpenFlag)
        return CastleSiege::SetTheCastleGateState(actor, true, resources);
    if (actor.flags78 == 0)
        return CastleSiege::SetTheCastleGateState(actor, false, resources);
    return {};
}

// 0x4c9cf8..0x4c9d3c. The DWORDs at +0x420 and +0x41c are read without opening their entry: what the last close of
// 0x511290 left (Q-C849).
float AngleStepOf(const Kernel::ActorState& actor)
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

Core::Expected<void, std::string> ApplyMonsterRecord(const Protocol::Actors::MonsterRecord& record,
                                                     const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const auto created = CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    // 0x4c9cee..0x4c9cf3.
    if (auto flagged = Actors::ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources);
        !flagged.HasValue())
        return flagged;
    constexpr int DirectionShift = 4;
    const float step = AngleStepOf(actor);
    actor.angles.z = (static_cast<float>(record.direction >> DirectionShift) - 1.0f) * step;
    // 0x4c9d67..0x4c9ee9.
    Actors::WriteTheCells(actor, record.x, record.y, random);
    constexpr std::uint8_t HighBit = 0x80;
    constexpr std::uint8_t StanceMask = 7;
    actor.byte42d = record.typeHigh & HighBit;
    actor.byte93 = (record.typeHigh >> DirectionShift) & StanceMask;
    if (auto gate = CheckTheSiegeGate(actor, resources); !gate.HasValue())
        return gate;
    // 0x4c9f4c..0x4ca07a.
    if (record.isNew)
        return ArriveAsNew(actor, resources);
    return Actors::WalkToTheTarget(actor, record.targetX, record.targetY, resources);
}

// 0x51743f: wsprintfA(0x64-byte local, the +0x1d9 name as format, the owner from pkt + 0xd), with no more arguments.
// The format comes out as is, "%%" gives '%' and the first "%s" the owner up to its 0. Without a 0 in its ten bytes,
// wsprintfA reads on into the record (+0x17 and the next one), which only changes what is copied if the text before the
// "%s" is shorter than 14 bytes: it cuts (P-C356). Another specifier, or a second "%s", reads stack arguments 0x517400
// does not pass: it cuts (P-C356).
Core::Expected<std::string, std::string> FormatTheSummonName(std::string_view format, std::span<const std::byte> owner)
{
    std::string text;
    bool ownerUsed = false;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] != '%')
        {
            text.push_back(format[i]);
            continue;
        }
        const char kind = i + 1 < format.size() ? format[i + 1] : '\0';
        ++i;
        if (kind == '%')
        {
            text.push_back('%');
            continue;
        }
        if (kind == 's' && !ownerUsed)
        {
            ownerUsed = true;
            const auto end = std::ranges::find(owner, std::byte{0});
            if (end == owner.end() && text.size() + owner.size() < SummonNameBytes)
                return Core::Unexpected(std::format("0x51743f: el dueño de diez letras sin 0 sigue en el registro del "
                                                    "1F dentro de los {} bytes del nombre (P-C356)",
                                                    SummonNameBytes));
            for (auto byte = owner.begin(); byte != end; ++byte)
                text.push_back(static_cast<char>(std::to_integer<unsigned char>(*byte)));
            continue;
        }
        return Core::Unexpected(std::format("0x51743f: wsprintfA con el formato \"{}\" en el nombre lee argumentos que "
                                            "0x517400 no pasa (P-C356)",
                                            format));
    }
    return text;
}

// 0x517400(owner, key, type, X, Y, just arrived).
Core::Expected<std::size_t, std::string> CreateSummon(const Protocol::Actors::SummonRecord& record,
                                                      const Scenes::WorldEntryResources& resources)
{
    const auto created = CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return created;
    Kernel::ActorState& actor = resources.world.Actors()[created.Value()];
    constexpr std::uint16_t FirstSummonLevel = 0x97;
    actor.level = static_cast<std::uint16_t>(record.type - FirstSummonLevel);
    // 0x51743f: the format is the name the tail (0x5140c0) left with NpcName(%s).txt over the empty string of case
    // 0x514390: "Magic stone of %s " for 152 to 157 and "Shadow Knight" for 158 in the lab's file (P-C177).
    const auto format = ActorCString(actor, 0x51743f);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const auto text = FormatTheSummonName(format.Value(), record.owner);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    constexpr std::uint16_t LastSummonLevel = 7;
    if (actor.level == LastSummonLevel)
        actor.subtype = 1;
    // 0x51745c..0x517466: REP MOVSD of 6 dwords, the local's first 24 bytes to +0x1d9..+0x1f0. With fewer than 24
    // letters the 0 goes and then the uninitialized stack (Q-C848): MuRet writes up to the 0 and leaves the rest as it
    // was (P-C177). With 24 or more, the first 24, no 0.
    std::vector<std::byte> bytes(std::as_bytes(std::span(text.Value())).begin(),
                                 std::as_bytes(std::span(text.Value())).end());
    bytes.push_back(std::byte{0});
    bytes.resize(std::min(bytes.size(), SummonNameBytes));
    if (auto put = PutActorText(actor, bytes, 0x51745c); !put.HasValue())
        return Core::Unexpected(put.Error());
    if (record.isNew)
    {
        // 0x51746e..0x517555: four 0x530860, the bolt that falls on the summon (FaunaJointCreation.cpp); width rand() %
        // 10 plus 60, 50, 50 and 60 (0x6b86cc and 0x6b86b0; fild and fadd, stored as float). With the pool full, the
        // rand() still happen.
        constexpr std::int32_t ArrivalBolt = 0x615, ArrivalSubtype = 1;
        constexpr int WidthSpan = 10;
        constexpr std::array<double, 4> WidthBases{60.0, 50.0, 50.0, 60.0};
        constexpr std::array<std::uint32_t, 4> Calls{0x5174a5, 0x5174de, 0x517517, 0x517550};
        for (std::size_t bolt = 0; bolt < WidthBases.size(); ++bolt)
        {
            const int jitter = resources.cloth.random.Next() % WidthSpan;
            const Kernel::JointRequest request{.type = ArrivalBolt,
                                               .firstPosition = actor.position,
                                               .secondPosition = actor.position,
                                               .angles = actor.angles,
                                               .subtype = ArrivalSubtype,
                                               .ownerContext = nullptr,
                                               .scale =
                                                   static_cast<float>(static_cast<double>(jitter) + WidthBases[bolt]),
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
            if (auto made =
                    Joints::CreateFaunaJoint(resources.world.DrawTail().joints, request, resources.cloth.random);
                !made.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", Calls[bolt], made.Error()));
        }
    }
    return created;
}

// 0x4ca516..0x4ca5bf: the owner (ten bytes, with +0x1e3 at 0 first), Text 485 and the name it had.
Core::Expected<void, std::string> NameAfterTheOwner(Kernel::ActorState& actor,
                                                    const Protocol::Actors::SummonRecord& record,
                                                    const Scenes::WorldEntryResources& resources)
{
    // 0x76e0a38
    constexpr std::size_t OwnerText = 485;
    const auto monster = ActorCString(actor, 0x4ca516);
    if (!monster.HasValue())
        return Core::Unexpected(monster.Error());
    actor.byte1e3 = 0;
    actor.name = record.owner;
    if (resources.texts == nullptr)
        return Core::Unexpected(std::string("0x4ca570: falta Text.bmd para el nombre del invocado (el 485)"));
    const auto text = resources.texts->Text(OwnerText);
    if (!text)
        return Core::Unexpected(std::string("0x4ca570: Text.bmd no tiene el 485"));
    // The two strcat, each with its 0: Text 485 (0x4ca57a..0x4ca592) and the previous name (0x4ca5a7..0x4ca5bf).
    const auto concatenate = [&actor](std::string_view added,
                                      std::uint32_t address) -> Core::Expected<void, std::string>
    {
        const auto before = ActorCString(actor, address);
        if (!before.HasValue())
            return Core::Unexpected(before.Error());
        const std::string_view cut = added.substr(0, added.find('\0'));
        std::vector<std::byte> bytes(std::as_bytes(std::span(cut)).begin(), std::as_bytes(std::span(cut)).end());
        bytes.push_back(std::byte{0});
        return PutActorText(actor, bytes, address, before.Value().size());
    };
    if (auto added = concatenate(*text, 0x4ca58b); !added.HasValue())
        return added;
    return concatenate(monster.Value(), 0x4ca5b8);
}

bool IsSummonType(std::uint16_t type)
{
    constexpr std::uint16_t FirstSummon = 0x98;
    constexpr std::uint16_t LastSummon = 0x9e;
    return type >= FirstSummon && type <= LastSummon;
}

Core::Expected<void, std::string> ApplySummonRecord(const Protocol::Actors::SummonRecord& record,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const bool summon = IsSummonType(record.type);
    const auto created = summon ? CreateSummon(record, resources)
                                : CreateMonster(record.type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    // 0x4ca16d..0x4ca1a4.
    if (auto flagged = Actors::ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources);
        !flagged.HasValue())
        return flagged;
    actor.angles.z = Actors::AngleOf(record.directionAndPk);
    Actors::WriteTheCells(actor, record.x, record.y, random);
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
        // 0x4ca5d5..0x4ca611: the magic circle 0xec (0x518cf0, Effects/BlastModelEffects) and the 0x5d5 bolt (0x53fe70)
        // that follows the character in its step, like the transformed one of 0x4c965c; neither draws rand(). With the
        // table full, nothing.
        constexpr std::int16_t MagicCircle = 0xec;
        if (auto circle = Combat::CreateBlastEffectOutsideTheStep({.type = MagicCircle,
                                                                   .position = actor.position,
                                                                   .angles = actor.angles,
                                                                   .light = actor.light,
                                                                   .subtype = 0,
                                                                   .key = 0xffff},
                                                                  &actor, world, random);
            !circle.HasValue())
            return Core::Unexpected(std::format("0x4ca5f6: {}", circle.Error()));
        const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::Actor, index};
        if (auto bolt = world.Particles().CreateOwnedChecked(Particles::LightningParticle, actor.position, actor.angles,
                                                             actor.light, Particles::LightningFollowSubtype, 1.0f, 0.0f,
                                                             random, 0, owner);
            !bolt.HasValue())
            return Core::Unexpected(
                std::format("0x4ca611: la partícula 0x5d5/2, error {}", static_cast<unsigned>(bolt.Error())));
        // 0x4ca616..0x4ca642
        if (summon)
        {
            constexpr Audio::SoundId SummonSound = Sound::WallFallSound;
            actor.animation.frame = 0.0f;
            actor.animation.previousKey = 0;
            runtime.RequestSound(SummonSound);
        }
        return {};
    }
    return Actors::WalkToTheTarget(actor, record.targetX, record.targetY, resources);
}

// 0x4e0b7f..0x4e0b9b, 0x4e0dea..0x4e0e13
void TurnAtRandom(Kernel::ActorState& actor, Crt::CrtRandom& random)
{
    using Math::FullTurn;
    constexpr std::int32_t ObjectMark = 0x96;
    const std::int32_t drawn = random.Next();
    actor.dword428 = ObjectMark;
    actor.angles.z = static_cast<float>(drawn % FullTurn);
}

// 0x4e0c1d..0x4e0e52
Core::Expected<void, std::string> ApplyObjectPlayer(const Protocol::Actors::ObjectRecord& record,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    const Actors::ActorConstructionContext construction{Scenes::TerrainOf(resources), runtime.map,
                                                        Terrain::CullingScene::Game, resources.camera.fixedHeight};
    const auto created = Actors::CreateActor(world, record.key, Tables::PlayerSkeletonModel, {record.x, record.y, 0.0f},
                                             construction, resources.cloth);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    // ((b & 0x10) | (b >> 4)) >> 1, as in the 12.
    constexpr std::uint8_t EvolutionBit = 0x10;
    constexpr int ClassShift = 4;
    constexpr std::uint8_t ObjectClass = 0x80;
    const std::uint8_t classByte = record.classOrTypeHigh;
    actor.characterClass = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>((classByte & EvolutionBit) | (classByte >> ClassShift)) >> 1);
    actor.skin = 0;
    actor.pk = 0;
    actor.byte88 = ObjectClass;
    if (auto flagged = Actors::ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources);
        !flagged.HasValue())
        return flagged;
    Actors::WriteTheCells(actor, record.x, record.y, random);
    TurnAtRandom(actor, random);
    // 0x4e0e16..0x4e0e1f: 0x5139f0 with the slot from 0x5110d0 and the equipment at +7.
    if (auto set = Actors::ApplyCharacterSet(Kernel::ActorWithKey(world, record.key), record.equipment, resources,
                                             Pets::TableHelpers(world, &resources.cloth.random));
        !set.HasValue())
        return set;
    // 0x4e0e24..0x4e0e52: "   " (0x6c8648).
    constexpr std::array<std::byte, 4> Blank{std::byte{' '}, std::byte{' '}, std::byte{' '}, std::byte{0}};
    if (auto put = PutActorText(actor, Blank, 0x4e0e24); !put.HasValue())
        return put;
    actor.byte1e3 = 0;
    return {};
}

// 0x4e09ca..0x4e0c18 (a 0 from 0x5141e0 goes on to the next record, 0x4e0a06).
Core::Expected<void, std::string> ApplyObjectMonster(const Protocol::Actors::ObjectRecord& record,
                                                     const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = resources.cloth.random;
    constexpr int TypeHighShift = 8;
    const auto type = static_cast<std::int32_t>(record.classOrTypeHigh << TypeHighShift | record.typeLow);
    const auto created = CreateMonster(type, record.x, record.y, record.key, resources);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    if (auto flagged = Actors::ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources);
        !flagged.HasValue())
        return flagged;
    Actors::WriteTheCells(actor, record.x, record.y, random);
    TurnAtRandom(actor, random);
    return CheckTheSiegeGate(actor, resources);
}
} // namespace

Core::Expected<void, std::string> ArriveAsNew(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldRuntime& runtime = resources.world.Runtime();
    switch (actor.word376)
    {
    case 0x15:
        return RiseWithActionOne(actor, 0x4c97ff, 0x13, resources);
    case 0x2c:
        return RiseWithActionOne(actor, 0x4c97d0, 0x14e, resources);
    case 0x35:
    case 0x36:
    {
        // 0x4c9828..0x4c984f, 0x4c9832. The brake is lowered by 0x505de0 (0x5069ae, Actors::StepActorCooldown, with its
        // twenty effect turns), 0x508ba0 and the D3.
        constexpr std::uint8_t ArrivalCooldown = 0x3c;
        actor.movementCooldown = ArrivalCooldown;
        return RiseWithActionOne(actor, 0x4c9832, std::nullopt, resources);
    }
    case 0x55:
    case 0x5b:
    case 0x61:
    case 0x72:
    case 0x78:
    case 0x7e:
        runtime.RequestSound(Audio::SoundId{0x173}); // (CL10) 0x4c9876
        return {};
    case 0x154:
    case 0x155:
    case 0x158:
    case 0x159:
        // 0x4c9883: stage 3 reaches no constructor and does not touch the RNG.
        if (runtime.events.cameraTour.stage == Protocol::Events::CrywolfReadyStage)
        {
            actor.alpha = 1.0f;
            return {};
        }
        actor.alpha = 0.0f;
        return Effects::CreateArrivalSparks(actor, resources.world, resources.cloth.random);
    default:
        break;
    }
    // 0x4c9acb..0x4c9af7.
    constexpr std::uint8_t ArrivalAction = 0xc1;
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        actor.alpha = 0.0f;
        return {};
    }
    const auto* skeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4c9ad9: falta Player.bmd para la acción 0xc1 del recién llegado"));
    Models::SetAction(actor.animation, *skeleton, ArrivalAction);
    actor.animation.previousAction = ArrivalAction;
    actor.alpha = 1.0f;
    return {};
}

Core::Expected<void, std::string> ApplyViewportMonsters(const Protocol::Actors::Monsters& monsters,
                                                        const Scenes::WorldEntryResources& resources)
{
    for (const Protocol::Actors::MonsterRecord& record : monsters.records)
    {
        if (auto applied = ApplyMonsterRecord(record, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

Core::Expected<void, std::string> ApplySummons(const Protocol::Actors::Summons& summons,
                                               const Scenes::WorldEntryResources& resources)
{
    for (const Protocol::Actors::SummonRecord& record : summons.records)
    {
        if (auto applied = ApplySummonRecord(record, resources); !applied.HasValue())
            return applied;
    }
    return {};
}

Core::Expected<void, std::string> ApplyViewportObjects(const Protocol::Actors::Objects& objects,
                                                       const Scenes::WorldEntryResources& resources)
{
    constexpr std::uint8_t PlayerKind = 1;
    constexpr std::uint8_t FirstMonsterKind = 2;
    constexpr std::uint8_t LastMonsterKind = 3;
    for (const Protocol::Actors::ObjectRecord& record : objects.records)
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
} // namespace Client::Monsters
