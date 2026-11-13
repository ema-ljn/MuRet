#include "Scenes/MonsterCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterKinds.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t NameBytes = 10;
constexpr std::size_t SpillStart = NameBytes + 1;

void SetTextByte(ActorState& actor, std::size_t index, std::byte value)
{
    if (index < NameBytes)
        actor.name[index] = value;
    else if (index == NameBytes)
        actor.byte1e3 = std::to_integer<std::uint8_t>(value);
    else
        actor.nameSpill[index - SpillStart] = value;
}

std::byte TextByte(const ActorState& actor, std::size_t index)
{
    if (index < NameBytes)
        return actor.name[index];
    if (index == NameBytes)
        return std::byte{actor.byte1e3};
    return actor.nameSpill[index - SpillStart];
}

// Una parte de +0x218..+0x2f7: las cinco del cuerpo y las dos manos, de a 0x20 bytes.
constexpr std::uint16_t FirstPartOffset = 0x218;
constexpr std::uint16_t HandsOffset = 0x2b8;
constexpr std::uint16_t PartsEnd = 0x2f8;
constexpr std::uint16_t PartSize = 0x20;

Core::Expected<void, std::string> WrongWrite(const MonsterStep& step)
{
    return Core::Unexpected(std::format("{:#x}: MuRet no sabe dónde va la escritura de {} bytes en +{:#x}",
                                        step.address, step.size, step.offset));
}

Core::Expected<void, std::string> WriteThePart(ActorItemPart& part, const MonsterStep& step, std::uint16_t field)
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

// Los campos de 4 bytes que los casos escriben.
bool WriteDword(ActorState& actor, std::uint16_t offset, std::uint32_t value)
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
        // El original guarda el frame anterior en un float; MuRet guarda lo que la pose lee, truncado.
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

bool WriteWord(ActorState& actor, std::uint16_t offset, std::uint16_t value)
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

bool WriteByte(ActorState& actor, std::uint16_t offset, std::uint8_t value)
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

Core::Expected<void, std::string> WriteTheField(ActorState& actor, const MonsterStep& step)
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

// 0x5133b0: el techo de la caja más 70 (0x6ba1b0) en la pila, las alas 0x1a15, el cuerpo 0x102a..0x182a de nivel 13,
// la escala en 1 y +0x111 (la acción) en 9, sin 0x4ea030.
void PutOnTheBoxArmour(ActorState& actor)
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

// 0x612d90(n) y 0x612270(modelo) sobre los modelos del juego que no se sueltan (WorldState::FaunaResources), con el
// mapa en curso y la tabla de texturas que liga el juego.
Core::Expected<void, std::string> LoadTheModel(const MonsterStep& step, const WorldEntryResources& resources)
{
    FaunaModels& models = resources.world.FaunaResources();
    auto context = models.LoadContextOf(resources.dataRoot, resources.log);
    if (!context)
        return Core::Unexpected(std::format("{:#x}: {} sin el backend de las texturas que liga el juego "
                                            "(GameScene::Enter)",
                                            step.address,
                                            step.kind == MonsterStepKind::LoadMonster ? "0x612d90" : "0x612270"));
    if (step.kind == MonsterStepKind::LoadMonster)
    {
        // 0x613127..0x615232: la tabla de sonidos del modelo (0x612c80, CL9 hito 3, fase 2c) va a
        // WorldRuntime::modelSounds, que leen los pasos del ataque.
        context->sounds = &resources.world.Runtime().modelSounds;
        if (auto loaded =
                models.LoadMonster(static_cast<std::int32_t>(step.value), resources.world.Runtime().map, *context);
            !loaded.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", step.address, loaded.Error()));
        return {};
    }
    if (auto loaded = models.LoadNpc(static_cast<std::uint16_t>(step.value), *context); !loaded.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", step.address, loaded.Error()));
    return {};
}

// 0x5f93c0 en la escena del juego: la altura del terreno en la X y la Y del personaje.
Core::Expected<double, std::string> GroundAt(const ActorState& actor, const WorldEntryResources& resources,
                                             std::uint32_t address)
{
    const World::WorldTerrain& terrain = resources.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::format("{:#x}: 0x5f93c0 sin las alturas del mundo cargado", address));
    return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, actor.position.x, actor.position.y,
                                  resources.camera.fixedHeight);
}

class MonsterBuilder
{
public:
    MonsterBuilder(std::int32_t type, std::uint8_t x, std::uint8_t y, std::int32_t key,
                   const WorldEntryResources& resources)
        : m_type(type), m_x(x), m_y(y), m_key(key), m_resources(resources)
    {
    }

    Core::Expected<std::size_t, std::string> Run()
    {
        WorldRuntime& runtime = m_resources.world.Runtime();
        const MonsterCreation creation = MonsterCreationOf(m_type, runtime.map);
        MonsterStepResolver resolver({.world = runtime.map, .x = m_x, .siegeActive = runtime.siegeActive});
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
        ApplyTheTail();
        return *m_index;
    }

private:
    ActorState& Actor()
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
            // (CL10) 0x41ed10(id, 0, 0): el sonido queda pedido.
            m_resources.world.Runtime().lastSoundId = static_cast<std::int32_t>(step.value);
            return {};
        case MonsterStepKind::Joint:
            return Core::Unexpected(std::format("{:#x}: el joint {:#x} (0x530860) del tipo {} es de la CL11",
                                                step.address, step.value, m_type));
        case MonsterStepKind::Arrival:
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
        WorldState& world = m_resources.world;
        const ActorConstructionContext construction{world.Map().terrain, world.Runtime().map, World::CullingScene::Game,
                                                    m_resources.camera.fixedHeight};
        const ActorPlacement placement{m_x, m_y, std::bit_cast<float>(step.extra)};
        auto created = CreateActor(world, m_key, static_cast<std::uint16_t>(step.value), placement, construction,
                                   m_resources.cloth);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        m_index = created.Value();
        return {};
    }

    Core::Expected<void, std::string> ApplyOnTheActor(const MonsterStep& step)
    {
        ActorState& actor = Actor();
        WorldRuntime& runtime = m_resources.world.Runtime();
        switch (step.kind)
        {
        case MonsterStepKind::Write:
            return WriteTheField(actor, step);
        case MonsterStepKind::Name:
        {
            // El strcpy a +0x1d9, con su 0.
            std::vector<std::byte> bytes(std::as_bytes(std::span(step.text)).begin(),
                                         std::as_bytes(std::span(step.text)).end());
            bytes.push_back(std::byte{0});
            return PutActorText(actor, bytes, step.address);
        }
        case MonsterStepKind::Dress:
            ApplyHeadAndScale(actor);
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
            NameABone(runtime, *m_index, step.text, step.value);
            return {};
        default:
            return Core::Unexpected(std::format("{:#x}: paso sin portar", step.address));
        }
    }

    // 0x4ea030(personaje, acción) con la cuenta de acciones del modelo del tipo.
    Core::Expected<void, std::string> SetTheAction(ActorState& actor, const MonsterStep& step)
    {
        return SetMonsterAction(actor, static_cast<std::uint8_t>(step.value), step.address, m_resources);
    }

    // 0x5140c0, sin el nombre de 0x7681eb8 (P-C174).
    void ApplyTheTail()
    {
        ActorState& actor = Actor();
        const WorldRuntime& runtime = m_resources.world.Runtime();
        const MonsterTail tail = MonsterTailOf(m_type, runtime.map);
        if (tail.clearsLevel)
        {
            actor.level = 0;
            actor.scale = AddTailScale(actor.scale, tail.scaleSteps);
        }
        actor.word376 = static_cast<std::uint16_t>(m_type);
        actor.byte8 = 0;
        actor.word3a4 = static_cast<std::uint16_t>(runtime.heroSlot);
        actor.byte88 = tail.objectClass;
    }

    std::int32_t m_type;
    std::uint8_t m_x;
    std::uint8_t m_y;
    std::int32_t m_key;
    const WorldEntryResources& m_resources;
    std::optional<std::size_t> m_index;
};
} // namespace

const Render::Models::Skeleton* MonsterSkeletonOf(const WorldEntryResources& resources, std::uint16_t model)
{
    if (const auto* skeleton = resources.world.FaunaResources().SkeletonOf(model))
        return skeleton;
    return resources.models.SkeletonOf(model);
}

Core::Expected<void, std::string> SetMonsterAction(ActorState& actor, std::uint8_t action, std::uint32_t address,
                                                   const WorldEntryResources& resources)
{
    const auto* skeleton = MonsterSkeletonOf(resources, actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(
            std::format("{:#x}: 0x4ea030 lee las acciones del modelo {:#x}, que no está cargado", address, actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

Core::Expected<void, std::string> PutActorText(ActorState& actor, std::span<const std::byte> bytes,
                                               std::uint32_t address, std::size_t offset)
{
    if (offset + bytes.size() > ActorTextSize)
        return Core::Unexpected(std::format("{:#x}: el nombre de {} bytes pisa +0x1f2 y la clave de +0x1f4 (Q-C847)",
                                            address, offset + bytes.size()));
    for (std::size_t index = 0; index < bytes.size(); ++index)
        SetTextByte(actor, offset + index, bytes[index]);
    return {};
}

std::string ActorText(const ActorState& actor)
{
    std::string text;
    for (std::size_t index = 0; index < ActorTextSize; ++index)
    {
        const std::byte value = TextByte(actor, index);
        if (value == std::byte{0})
            break;
        text.push_back(static_cast<char>(std::to_integer<std::uint8_t>(value)));
    }
    return text;
}

Core::Expected<std::size_t, std::string> CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y,
                                                       std::int32_t key, const WorldEntryResources& resources)
{
    return MonsterBuilder(type, x, y, key, resources).Run();
}
} // namespace Client::Scenes
