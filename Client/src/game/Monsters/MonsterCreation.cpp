#include "Monsters/MonsterCreation.h"

#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Models/Cloth.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActionReply.h"
#include "Actors/ActorEquipment.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/FaunaModels.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Monsters/MonsterKinds.h"
#include "Monsters/NpcNames.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Monsters
{
namespace
{
// Skill\Aurora (0x42d513, 0x42d5af).
constexpr std::int16_t ArrivalAuraEffect = 0x134;
// From +0x1d9: the name (ten bytes), +0x1e3, nameSpill up to +0x1f1, the WORD at +0x1f2, the key at +0x1f4, +0x1f6 and
// +0x1f7 (MuRet does not keep them), the head at +0x1f8 and the rest of its part, +0x1fa..+0x217 (not kept either).
constexpr std::size_t NameBytes = 10;
constexpr std::size_t SpillStart = NameBytes + 1;
constexpr std::size_t Word1f2Start = ActorTextSize;
constexpr std::size_t KeyStart = Word1f2Start + 2;
constexpr std::size_t HeadStart = 0x1f8 - 0x1d9;

std::uint16_t WithByte(std::uint16_t value, std::size_t index, std::byte byte)
{
    const auto shift = static_cast<unsigned>(8 * index);
    const auto kept = static_cast<unsigned>(value) & ~(0xffu << shift);
    return static_cast<std::uint16_t>(kept | (std::to_integer<unsigned>(byte) << shift));
}

std::byte ByteOf(std::uint16_t value, std::size_t index)
{
    return static_cast<std::byte>((static_cast<unsigned>(value) >> (8 * index)) & 0xffu);
}

void SetTextByte(Kernel::ActorState& actor, std::size_t index, std::byte value)
{
    if (index < NameBytes)
        actor.name[index] = value;
    else if (index == NameBytes)
        actor.byte1e3 = std::to_integer<std::uint8_t>(value);
    else if (index < Word1f2Start)
        actor.nameSpill[index - SpillStart] = value;
    else if (index < KeyStart)
        actor.word1f2 = WithByte(actor.word1f2, index - Word1f2Start, value);
    else if (index < ActorTextReadable)
        actor.key = WithByte(actor.key, index - KeyStart, value);
    else if (index == HeadStart || index == HeadStart + 1)
        actor.head = WithByte(actor.head, index - HeadStart, value);
    // +0x1f6, +0x1f7 and +0x1fa..+0x217 are not in ActorState: the byte is lost (P-C352).
}

std::byte TextByte(const Kernel::ActorState& actor, std::size_t index)
{
    if (index < NameBytes)
        return actor.name[index];
    if (index == NameBytes)
        return std::byte{actor.byte1e3};
    if (index < Word1f2Start)
        return actor.nameSpill[index - SpillStart];
    if (index < KeyStart)
        return ByteOf(actor.word1f2, index - Word1f2Start);
    return ByteOf(actor.key, index - KeyStart);
}

// A part of +0x218..+0x2f7: the five body parts and the two hands, 0x20 bytes each.
constexpr std::uint16_t FirstPartOffset = 0x218;
constexpr std::uint16_t HandsOffset = 0x2b8;
constexpr std::uint16_t PartsEnd = 0x2f8;
constexpr std::uint16_t PartSize = 0x20;

Core::Expected<void, std::string> WrongWrite(const MonsterStep& step)
{
    return Core::Unexpected(std::format("{:#x}: MuRet no sabe dónde va la escritura de {} bytes en +{:#x}",
                                        step.address, step.size, step.offset));
}

Core::Expected<void, std::string> WriteThePart(Kernel::ActorItemPart& part, const MonsterStep& step,
                                               std::uint16_t field)
{
    constexpr std::uint16_t ModelField = 0;
    constexpr std::uint16_t LevelField = 2;
    constexpr std::uint16_t Flags1bField = 3;
    if (field == ModelField && step.size == 2)
        part.model = static_cast<std::uint16_t>(step.value);
    else if (field == LevelField && step.size == 1)
        part.level = static_cast<std::uint8_t>(step.value);
    else if (field == Flags1bField && step.size == 1)
        part.flags1b = static_cast<std::uint8_t>(step.value);
    else
        return WrongWrite(step);
    return {};
}

bool WriteDword(Kernel::ActorState& actor, std::uint16_t offset, std::uint32_t value)
{
    const float real = std::bit_cast<float>(value);
    const auto integer = static_cast<std::int32_t>(value);
    switch (offset)
    {
    case 0x4:
        actor.subtype = integer;
        return true;
    case 0xc:
        actor.scale = real;
        return true;
    case 0x24:
        actor.angles.z = real;
        return true;
    case 0x58:
        actor.dword58 = integer;
        return true;
    case 0x60:
        actor.dword60 = integer;
        return true;
    case 0x64:
        actor.dword64 = integer;
        return true;
    case 0x68:
        actor.blendMeshLight = real;
        return true;
    case 0x78:
        actor.flags78 = value;
        return true;
    case 0x94:
        actor.float94 = real;
        return true;
    case 0x114:
        actor.animation.frame = real;
        return true;
    case 0x118:
        // The original keeps the previous frame in a float; MuRet keeps what the pose reads, truncated.
        actor.animation.previousKey = static_cast<std::int16_t>(real);
        return true;
    case 0x130:
        actor.boundsMaxX = real;
        return true;
    case 0x134:
        actor.boundsMaxY = real;
        return true;
    case 0x138:
        actor.boundsTop = real;
        return true;
    case 0x170:
        actor.alphaTarget = real;
        return true;
    case 0x394:
        actor.dword394 = integer;
        return true;
    default:
        return false;
    }
}

bool WriteWord(Kernel::ActorState& actor, std::uint16_t offset, std::uint16_t value)
{
    switch (offset)
    {
    case 0x1d6:
        actor.level = value;
        return true;
    case 0x386:
        actor.baseMovementSpeed = value;
        return true;
    default:
        return false;
    }
}

bool WriteByte(Kernel::ActorState& actor, std::uint16_t offset, std::uint8_t value)
{
    switch (offset)
    {
    case 0x0:
        actor.alive = value != 0;
        return true;
    case 0x88:
        actor.byte88 = value;
        return true;
    case 0x92:
        actor.byte92 = value;
        return true;
    case 0x98:
        actor.byte98 = value;
        return true;
    case 0xe8:
        actor.byteE8 = value;
        return true;
    case 0x16c:
        actor.seen = value != 0;
        return true;
    case 0x1d5:
        actor.skin = value;
        return true;
    case 0x375:
        actor.pk = value;
        return true;
    case 0x38a:
        actor.byte38a = value;
        return true;
    case 0x3e4:
        actor.byte3e4 = value;
        return true;
    case 0x42c:
        actor.byte42c = value;
        return true;
    default:
        return false;
    }
}

Core::Expected<void, std::string> WriteTheField(Kernel::ActorState& actor, const MonsterStep& step)
{
    if (step.offset >= FirstPartOffset && step.offset < PartsEnd)
    {
        const std::uint16_t relative = step.offset - FirstPartOffset;
        const std::size_t index = relative / PartSize;
        const auto field = static_cast<std::uint16_t>(relative % PartSize);
        if (step.offset < HandsOffset)
            return WriteThePart(actor.body[index], step, field);
        return WriteThePart(actor.hands[index - actor.body.size()], step, field);
    }
    bool written = false;
    if (step.size == 4)
        written = WriteDword(actor, step.offset, step.value);
    else if (step.size == 2)
        written = WriteWord(actor, step.offset, static_cast<std::uint16_t>(step.value));
    else if (step.size == 1)
        written = WriteByte(actor, step.offset, static_cast<std::uint8_t>(step.value));
    return written ? Core::Expected<void, std::string>{} : WrongWrite(step);
}

// 0x5133b0: the box top plus 70 (0x6ba1b0) on the x87 stack; +0x111 (the action) is set without 0x4ea030.
void PutOnTheBoxArmour(Kernel::ActorState& actor)
{
    constexpr double BoxRise = 70.0;
    constexpr std::uint16_t BoxWings = 0x1a15;
    constexpr std::uint16_t FirstBoxPart = 0x102a;
    constexpr std::uint16_t BoxPartStep = 0x200;
    constexpr std::uint8_t BoxLevel = 13;
    constexpr std::uint8_t BoxAction = 9;
    actor.boundsTop = static_cast<float>(static_cast<double>(actor.boundsTop) + BoxRise);
    actor.wings.model = BoxWings;
    for (std::size_t index = 0; index < actor.body.size(); ++index)
    {
        actor.body[index].model = static_cast<std::uint16_t>(FirstBoxPart + index * BoxPartStep);
        actor.body[index].level = BoxLevel;
    }
    actor.scale = 1.0f;
    actor.animation.action = BoxAction;
}

// 0x612d90(n) and 0x612270(model) on the game models that are never released (Assets::AssetsState::fauna), with the
// current map and the texture table the game binds.
Core::Expected<void, std::string> LoadTheModel(const MonsterStep& step, const Scenes::WorldEntryResources& resources)
{
    Assets::FaunaModels* models = Scenes::FaunaModelsOf(resources);
    if (models == nullptr)
        return Core::Unexpected(std::format("{:#x}: no fauna models (0x5769ad8)", step.address));
    auto context = models->LoadContextOf(resources.dataRoot, resources.log);
    if (!context)
        return Core::Unexpected(std::format("{:#x}: {} sin el backend de las texturas que liga el juego "
                                            "(GameScene::Enter)",
                                            step.address,
                                            step.kind == MonsterStepKind::LoadMonster ? "0x612d90" : "0x612270"));
    context->audio = resources.world.Runtime().audio;
    if (step.kind == MonsterStepKind::LoadMonster)
    {
        // 0x613127..0x615232: the model's sound table (0x612c80) goes to WorldRuntime::modelSounds, which the attack
        // steps read.
        context->sounds = &resources.world.Runtime().modelSounds;
        if (auto loaded =
                models->LoadMonster(static_cast<std::int32_t>(step.value), resources.world.Runtime().map, *context);
            !loaded.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", step.address, loaded.Error()));
        return {};
    }
    if (auto loaded = models->LoadNpc(static_cast<std::uint16_t>(step.value), *context); !loaded.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", step.address, loaded.Error()));
    return {};
}

// 0x5f93c0 in the game scene.
Core::Expected<double, std::string> GroundAt(const Kernel::ActorState& actor,
                                             const Scenes::WorldEntryResources& resources, std::uint32_t address)
{
    const Terrain::WorldTerrain& terrain = Scenes::TerrainOf(resources);
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::format("{:#x}: 0x5f93c0 sin las alturas del mundo cargado", address));
    return Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, actor.position.x, actor.position.y,
                                    resources.camera.fixedHeight);
}

class MonsterBuilder
{
public:
    MonsterBuilder(std::int32_t type, std::uint8_t x, std::uint8_t y, std::int32_t key,
                   const Scenes::WorldEntryResources& resources)
        : m_type(type), m_x(x), m_y(y), m_key(key), m_resources(resources)
    {
    }

    Core::Expected<std::size_t, std::string> Run()
    {
        Kernel::WorldRuntime& runtime = m_resources.world.Runtime();
        const MonsterCreation creation = MonsterCreationOf(m_type, runtime.map);
        MonsterStepResolver resolver({.world = runtime.map, .x = m_x, .siegeActive = runtime.castleSiege.siegeActive});
        for (const MonsterStep& listed : creation.kind.steps)
        {
            auto step = resolver.Next(listed, m_type, m_resources.cloth.random);
            if (!step.HasValue())
                return Core::Unexpected(step.Error());
            if (!step.Value())
                continue;
            if (auto applied = Apply(*step.Value()); !applied.HasValue())
                return Core::Unexpected(applied.Error());
        }
        if (!m_index)
            return Core::Unexpected(
                std::format("{:#x}: el caso del tipo {} no llamó a 0x512870", creation.kind.address, m_type));
        if (auto tail = ApplyTheTail(); !tail.HasValue())
            return Core::Unexpected(tail.Error());
        return *m_index;
    }

private:
    Kernel::ActorState& Actor()
    {
        return m_resources.world.Actors()[*m_index];
    }

    Core::Expected<void, std::string> Apply(const MonsterStep& step)
    {
        switch (step.kind)
        {
        case MonsterStepKind::LoadMonster:
        case MonsterStepKind::LoadNpc:
            return LoadTheModel(step, m_resources);
        case MonsterStepKind::Random:
        case MonsterStepKind::Siege:
            return {};
        case MonsterStepKind::Create:
            return Create(step);
        case MonsterStepKind::Sound:
            // 0x4410eb → 0x41ed10
            m_resources.world.Runtime().RequestSound(Audio::SoundId{static_cast<std::int32_t>(step.value)});
            return {};
        case MonsterStepKind::Joint:
            // 0x515f9c and the other normal cases use the same contract as 349: Actor owner and 618/2,3. 0x5165d1, NPC
            // 257: the 0x60f/0x2a that circles it, width 15, owned by it.
            if (m_index && ((step.value == 0x618 && (step.offset == 2 || step.offset == 3)) ||
                            (step.value == 0x60f && step.offset == 0x2a)))
            {
                Kernel::ActorState& actor = m_resources.world.Actors()[*m_index];
                auto created =
                    Joints::CreateFaunaJoint(m_resources.world.DrawTail().joints,
                                             Kernel::JointRequest{static_cast<std::int32_t>(step.value), actor.position,
                                                                  actor.position, actor.angles, step.offset, &actor,
                                                                  std::bit_cast<float>(step.extra), -1, 0, 0, -1},
                                             m_resources.cloth.random, Kernel::JointOwnerKind::Actor);
                if (!created.HasValue())
                    return Core::Unexpected(std::move(created).Error());
                return {};
            }
            return Core::Unexpected(std::format("{:#x}: el joint {:#x} (0x530860) del tipo {} es de la CL11",
                                                step.address, step.value, m_type));
        case MonsterStepKind::Arrival:
            if (m_index && step.value == ArrivalAuraEffect)
            {
                auto& actor = Actor();
                // 0x42d4c0..0x42d513, 0x42d5af: 0x518cf0 with the key holding the scale.
                constexpr std::uint16_t ArrivalScaleKey = 120;
                const Kernel::ModelEffectArguments arguments{.type = ArrivalAuraEffect,
                                                             .position = actor.position,
                                                             .angles = actor.angles,
                                                             .light = actor.light,
                                                             .owner = &actor,
                                                             .key = ArrivalScaleKey};
                const auto created = Effects::CreateHighModelEffect(
                    arguments, {.world = m_resources.world, .random = m_resources.cloth.random});
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return {};
            }
            // 0x518d0f returns to the caller when the pool has no free slot.
            if (std::ranges::all_of(m_resources.world.Map().effects.Effects(), &Effects::ModelEffect::alive))
                return {};
            return Core::Unexpected(std::format("{:#x}: el efecto {:#x} (0x518cf0) del tipo {} es de la CL11",
                                                step.address, step.value, m_type));
        default:
            break;
        }
        if (!m_index)
            return Core::Unexpected(std::format("{:#x}: el paso escribe el personaje antes de 0x512870", step.address));
        return ApplyOnTheActor(step);
    }

    Core::Expected<void, std::string> Create(const MonsterStep& step)
    {
        Kernel::WorldState& world = m_resources.world;
        const Actors::ActorConstructionContext construction{Scenes::TerrainOf(m_resources), world.Runtime().map,
                                                            Terrain::CullingScene::Game,
                                                            m_resources.camera.fixedHeight};
        const Actors::ActorPlacement placement{m_x, m_y, std::bit_cast<float>(step.extra)};
        auto created = Actors::CreateActor(world, m_key, static_cast<std::uint16_t>(step.value), placement,
                                           construction, m_resources.cloth);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        m_index = created.Value();
        return {};
    }

    Core::Expected<void, std::string> ApplyOnTheActor(const MonsterStep& step)
    {
        Kernel::ActorState& actor = Actor();
        Kernel::WorldRuntime& runtime = m_resources.world.Runtime();
        switch (step.kind)
        {
        case MonsterStepKind::Write:
            return WriteTheField(actor, step);
        case MonsterStepKind::Name:
        {
            // The strcpy to +0x1d9 copies its 0.
            std::vector<std::byte> bytes(std::as_bytes(std::span(step.text)).begin(),
                                         std::as_bytes(std::span(step.text)).end());
            bytes.push_back(std::byte{0});
            return PutActorText(actor, bytes, step.address);
        }
        case MonsterStepKind::Dress:
            Actors::ApplyHeadAndScale(actor);
            return {};
        case MonsterStepKind::Box:
            PutOnTheBoxArmour(actor);
            return {};
        case MonsterStepKind::Action:
            return SetTheAction(actor, step);
        case MonsterStepKind::Ground:
        {
            const auto ground = GroundAt(actor, m_resources, step.address);
            if (!ground.HasValue())
                return Core::Unexpected(ground.Error());
            actor.position.z =
                static_cast<float>(ground.Value() + static_cast<double>(std::bit_cast<float>(step.value)));
            return {};
        }
        case MonsterStepKind::Lower:
            actor.position.z = static_cast<float>(static_cast<double>(actor.position.z) -
                                                  static_cast<double>(std::bit_cast<float>(step.value)));
            return {};
        case MonsterStepKind::CopyHeight:
        {
            constexpr std::uint16_t HeightCopy = 0xd8;
            if (step.offset != HeightCopy)
                return WrongWrite(step);
            actor.floatD8 = actor.position.z;
            return {};
        }
        case MonsterStepKind::MeshFlag:
            runtime.modelPatches.push_back({.model = actor.type,
                                            .mesh = step.offset,
                                            .offset = 0,
                                            .value = static_cast<std::uint8_t>(step.value)});
            return {};
        case MonsterStepKind::ModelByte:
            runtime.modelPatches.push_back({.model = static_cast<std::uint16_t>(step.extra),
                                            .mesh = std::nullopt,
                                            .offset = step.offset,
                                            .value = static_cast<std::uint8_t>(step.value)});
            return {};
        case MonsterStepKind::BoneName:
            Actors::NameABone(runtime, *m_index, step.text, step.value);
            return {};
        default:
            return Core::Unexpected(std::format("{:#x}: paso sin portar", step.address));
        }
    }

    // 0x4ea030(character, action) with the action count of the type's model.
    Core::Expected<void, std::string> SetTheAction(Kernel::ActorState& actor, const MonsterStep& step)
    {
        return Actors::SetMonsterAction(actor, static_cast<std::uint8_t>(step.value), step.address, m_resources);
    }

    // 0x5140c0.
    Core::Expected<void, std::string> ApplyTheTail()
    {
        Kernel::ActorState& actor = Actor();
        const Kernel::WorldRuntime& runtime = m_resources.world.Runtime();
        const MonsterTail tail = MonsterTailOf(m_type, runtime.map);
        if (tail.clearsLevel)
        {
            actor.level = 0;
            actor.scale = AddTailScale(actor.scale, tail.scaleSteps);
        }
        // 0x51411f..0x51416b: strcpy to +0x1d9 of the name of the first record in 0x7681eb8 with the type (the word
        // against the dword), if there is one. One longer than 24 letters (three in the lab's NpcName(%s).txt) runs
        // into the WORD at +0x1f2, the key, +0x1f6, the head and what follows, as in the original (Q-C847, P-C352).
        if (const std::optional<std::string> name = NpcNameOf(runtime.npcNames, m_type))
        {
            std::vector<std::byte> bytes(std::as_bytes(std::span(*name)).begin(),
                                         std::as_bytes(std::span(*name)).end());
            bytes.push_back(std::byte{0});
            constexpr std::uint32_t TheCopy = 0x514164;
            if (auto put = PutActorText(actor, bytes, TheCopy); !put.HasValue())
                return put;
        }
        actor.word376 = static_cast<std::uint16_t>(m_type);
        actor.byte8 = 0;
        actor.attackTarget = static_cast<std::uint16_t>(runtime.heroSlot);
        actor.byte88 = tail.objectClass;
        return {};
    }

    std::int32_t m_type;
    std::uint8_t m_x;
    std::uint8_t m_y;
    std::int32_t m_key;
    const Scenes::WorldEntryResources& m_resources;
    std::optional<std::size_t> m_index;
};
} // namespace

Core::Expected<void, std::string> PutActorText(Kernel::ActorState& actor, std::span<const std::byte> bytes,
                                               std::uint32_t address, std::size_t offset)
{
    if (offset + bytes.size() > ActorTextReach)
        return Core::Unexpected(std::format("{:#x}: el nombre de {} bytes desde +0x1d9 pisa las partes de +0x218 "
                                            "(Q-C847, P-C352)",
                                            address, offset + bytes.size()));
    for (std::size_t index = 0; index < bytes.size(); ++index)
        SetTextByte(actor, offset + index, bytes[index]);
    return {};
}

std::string ActorText(const Kernel::ActorState& actor)
{
    std::string text;
    for (std::size_t index = 0; index < ActorTextReadable; ++index)
    {
        const std::byte value = TextByte(actor, index);
        if (value == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
    }
    return text;
}

Core::Expected<std::string, std::string> ActorCString(const Kernel::ActorState& actor, std::uint32_t address)
{
    std::string text = ActorText(actor);
    if (text.size() >= ActorTextReadable)
        return Core::Unexpected(std::format("{:#x}: el nombre de +0x1d9 sigue sin 0 en +0x1f6, que MuRet no guarda "
                                            "(Q-C847, P-C352)",
                                            address));
    return text;
}

Core::Expected<std::size_t, std::string> CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                       std::int32_t key, const Scenes::WorldEntryResources& resources)
{
    return MonsterBuilder(type, x, y, key, resources).Run();
}
} // namespace Client::Monsters
