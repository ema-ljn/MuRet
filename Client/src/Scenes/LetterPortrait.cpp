#include "Scenes/LetterPortrait.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Pass3D.h"
#include "Render/ScreenScale.h"
#include "Scenes/ActionReply.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorCharacterSet.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/HelperTable.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"
#include "World/TerrainCulling.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t HeightOffsetPet = 0x1c15;
// 0x4963fd: el lugar del personaje después de 0x511290, (−300, −300, −300) (0xc3960000).
constexpr float HiddenCoordinate = -300.0f;
// 0x4978a0..0x4978b6: los ángulos de 0x497890.
constexpr float PortraitPitch = -20.0f;
constexpr float PortraitRoll = 5.0f;
constexpr float PortraitYaw = 60.0f;
// 0x6b8a5c: lo que 0x497810 y la cola de 0x496530 multiplican por la escala del ayudante.
constexpr float HelperScaleFactor = 0.7f;
// 0x497862: de 0x83 en adelante, con alas, la zona segura.
constexpr std::int32_t LastGroundAction = 0x82;
// 0x5130cc..0x513124: 0x513100 no llama a 0x4f2250 con la acción entre 0x22 y 0x97 o entre 0xc5 y 0xcc.
constexpr std::uint8_t FirstBusyAction = 0x22;
constexpr std::uint8_t LastBusyAction = 0x97;
constexpr std::uint8_t FirstEmoteAction = 0xc5;
constexpr std::uint8_t LastEmoteAction = 0xcc;
// El registro propio: las manos en 0 y 1, el cuerpo de 2 a 6, las alas en 7 y la mascota en 8 (+0xb6c + 0x48 · i).
constexpr std::size_t FirstBodySlot = 2;
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
// 0x495a20: las acciones que recorre. 0x6c7b08, con el ayudante montado (0xc6, 0x110, 0xbf y 0x14a..0x14c), y
// 0x6c7b14, los gestos. Con el índice negativo suma 300 (0x12c) o 2400 (0x960) antes del resto.
constexpr std::array<std::int32_t, 3> RidingActions{0x7a, 0x7c, 0x78};
constexpr std::int32_t RidingWrap = 300;
constexpr std::array<std::int32_t, 24> GestureActions{0x7a, 0x83, 0x85, 0x86, 0x87, 0x8b, 0x89, 0x8c,
                                                      0x8a, 0x88, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92,
                                                      0x93, 0x84, 0x7c, 0x94, 0x80, 0x81, 0x82, 0x78};
constexpr std::int32_t GestureWrap = 2400;
constexpr std::int32_t StrikeAction = 0x78;
constexpr std::int32_t StopAction = 0x7a;
constexpr std::int32_t WalkAction = 0x7c;
constexpr std::int32_t FirstGesture = 0x80;
constexpr std::int32_t LastGesture = 0x94;
// Los ayudantes montados de 0x495a2b..0x495a57.
constexpr std::uint16_t GuardianPet = 0x1c12;
constexpr std::uint16_t UniliaHelper = 0xc6;
constexpr std::uint16_t DinorantHelper = 0x110;
constexpr std::uint16_t DarkHorseHelper = 0xbf;
constexpr std::uint16_t FirstFenrirHelper = 0x14a;
constexpr std::uint16_t LastFenrirHelper = 0x14c;

ActorState& HeroOf(const WorldEntryResources& resources)
{
    return resources.world.Actors()[resources.world.Runtime().hero];
}

const Render::Models::Skeleton* PlayerSkeleton(const WorldEntryResources& resources)
{
    return resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
}

ActorStopContext StopContextOf(const WorldEntryResources& resources, const Render::Models::Skeleton& player)
{
    const auto& terrain = resources.world.Map().terrain;
    return ActorStopContext{.runtime = resources.world.Runtime(),
                            .scene = SceneId::Game,
                            .playerSkeleton = player,
                            .items = resources.items,
                            .random = resources.cloth.random,
                            .terrainWalls = terrain.attributes ? &terrain.attributes->cells : nullptr};
}

// [0x6c9dd0] en 0 mientras vive: 0x495a20 lo baja alrededor de 0x4f2250, 0x4f4a10 y 0x4f37c0 y lo vuelve a poner.
class MapAtZero
{
public:
    explicit MapAtZero(WorldRuntime& runtime) : m_runtime(runtime), m_map(runtime.map)
    {
        m_runtime.map = 0;
    }
    MapAtZero(const MapAtZero&) = delete;
    MapAtZero& operator=(const MapAtZero&) = delete;
    ~MapAtZero()
    {
        m_runtime.map = m_map;
    }

private:
    WorldRuntime& m_runtime;
    std::int32_t m_map;
};

Core::Expected<void, std::string> StopThePortrait(ActorState& actor, const WorldEntryResources& resources,
                                                  std::string_view where)
{
    const auto* player = PlayerSkeleton(resources);
    if (player == nullptr)
        return Core::Unexpected(std::format("{}: falta Player.bmd para 0x4f2250", where));
    auto stopped = SetActorStop(actor, StopContextOf(resources, *player));
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    // 0x41ed10 desde 0x4f2250: sin audio hasta la CL10.
    if (stopped.Value().sound)
        resources.world.Runtime().lastSoundId = stopped.Value().sound;
    return {};
}

// 0x513100 sobre el personaje del retrato (0x4977d0 y 0x496575).
Core::Expected<void, std::string> ApplyTheClassParts(ActorState& actor, const WorldEntryResources& resources)
{
    if (actor.type != Data::Models::PlayerSkeletonModel)
        return {};
    const std::uint8_t action = actor.animation.action;
    const bool emote = action >= FirstEmoteAction && action <= LastEmoteAction;
    const bool busy = action >= FirstBusyAction && action <= LastBusyAction;
    if (!busy && !emote)
    {
        if (auto stopped = StopThePortrait(actor, resources, "0x513135"); !stopped.HasValue())
            return stopped;
    }
    // 0x513146..0x51324d: cada parte con el modelo base de la clase (la clase & 7, sin las vueltas) pasa al de la clase
    // entera con el nivel y los bytes +3 y +4 en 0. La comparación de la parte 0 lee la suya; las de las partes 1 a 4
    // leen el word de la parte siguiente (+0x258, +0x278, +0x298 y la mano derecha, +0x2b8), Q-C1024.
    const std::uint8_t characterClass = actor.characterClass;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const auto bodyPart = static_cast<GameLogic::BodyPart>(part);
        const std::uint16_t compared = part == 0                      ? actor.body[0].model
                                       : part + 1 < actor.body.size() ? actor.body[part + 1].model
                                                                      : actor.hands[0].model;
        const auto base = static_cast<std::int32_t>(GameLogic::ClassPartModel(bodyPart, characterClass & 7));
        if (static_cast<std::int16_t>(compared) != base)
            continue;
        ActorItemPart& worn = actor.body[part];
        worn.level = 0;
        worn.flags1b = 0;
        worn.flags1c = 0;
        worn.model = GameLogic::ClassPartModel(bodyPart, characterClass);
    }
    // 0x513255.
    ApplyHeadAndScale(actor);
    return {};
}

// 0x496430: el word del modelo, el nivel (+2) y el byte +3.
bool SamePart(const ActorItemPart& part, const ActorItemPart& other)
{
    return part.model == other.model && part.level == other.level && part.flags1b == other.flags1b;
}

std::uint8_t LevelOf(const World::ItemState& item)
{
    return static_cast<std::uint8_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

// 0x496470(parte, ítem, el de la clase): sin ítem (0xffff), el modelo con signo igual a `empty`; si no, al índice con
// signo + 0x212. Después el nivel ((D+4 >> 3) & 0xf) y el byte +3 con el D+0x1b.
bool SameAsItem(const ActorItemPart& part, const World::ItemState& item, std::int32_t empty)
{
    const std::int32_t model = static_cast<std::int16_t>(part.model);
    const std::int32_t expected =
        item.id == NoItemModel ? empty : static_cast<std::int16_t>(item.id) + Data::Models::FirstItemModel;
    return model == expected && part.level == LevelOf(item) && part.flags1b == item.flags1b;
}

// 0x4964f0(parte, ítem, el de la clase): el modelo (el word de `empty` sin ítem), el nivel y el byte +3; el +4 queda.
void FillFromItem(ActorItemPart& part, const World::ItemState& item, std::uint16_t empty)
{
    part.model = item.id == NoItemModel ? empty : static_cast<std::uint16_t>(item.id + Data::Models::FirstItemModel);
    part.level = LevelOf(item);
    part.flags1b = item.flags1b;
}

std::uint16_t ClassPart(std::size_t part, std::uint8_t characterClass)
{
    return GameLogic::ClassPartModel(static_cast<GameLogic::BodyPart>(part), characterClass);
}

struct Differences
{
    bool body = false;
    bool hands = false;
    bool wings = false;
    bool pet = false;
};

// 0x496595..0x496658: con +0x3e3 del héroe en 0, contra el héroe.
Differences CompareWithTheHero(const ActorState& actor, const ActorState& hero)
{
    Differences differ;
    // 0x49659b..0x4965c5: la cabeza (+0x1f8) y las cinco partes, cortando en la primera distinta. De la cabeza MuRet
    // guarda solo el word: el nivel y el +3 los deja en 0 0x511290 en los dos (*inferido*).
    differ.body = actor.head != hero.head;
    for (std::size_t part = 0; !differ.body && part < actor.body.size(); ++part)
        differ.body = !SamePart(actor.body[part], hero.body[part]);
    for (std::size_t side = 0; !differ.hands && side < actor.hands.size(); ++side)
        differ.hands = !SamePart(actor.hands[side], hero.hands[side]);
    differ.wings = !SamePart(actor.wings, hero.wings);
    differ.pet = !SamePart(actor.pet, hero.pet);
    return differ;
}

// 0x49665d..0x496975: con +0x3e3 del héroe distinto de 0, contra el registro propio, con su acceso abierto.
Differences CompareWithTheRecord(const ActorState& actor, std::uint8_t heroClass, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, resources.cloth.random,
                                            Render::Models::OwnRecordCloseRandoms);
    const auto& equipment = world.OwnCharacter().equipment;
    Differences differ;
    for (std::size_t part = 0; !differ.body && part < actor.body.size(); ++part)
        differ.body = !SameAsItem(actor.body[part], equipment[FirstBodySlot + part], ClassPart(part, heroClass));
    constexpr std::int32_t Empty = -1;
    for (std::size_t side = 0; !differ.hands && side < actor.hands.size(); ++side)
        differ.hands = !SameAsItem(actor.hands[side], equipment[side], Empty);
    differ.wings = !SameAsItem(actor.wings, equipment[WingsSlot], Empty);
    differ.pet = !SameAsItem(actor.pet, equipment[PetSlot], Empty);
    return differ;
}

// 0x4969b2..0x496a5e: del héroe tal cual, de a partes de 0x20 bytes.
void CopyFromTheHero(LetterPortrait& portrait, const ActorState& hero, const Differences& differ)
{
    ActorState& actor = portrait.actor;
    if (differ.body)
    {
        // 0x4969bd: 0x4fb1a0(personaje, 0, 0), las telas.
        portrait.cloth = CharacterCloth{};
        actor.head = hero.head;
        actor.body = hero.body;
        // 0x4969e3..0x4969f7: el byte +0x14 y los dwords +0x18 y +0x1c de las seis partes en 0; MuRet no los tiene.
    }
    if (differ.hands)
        actor.hands = hero.hands;
    if (differ.wings)
        actor.wings = hero.wings;
    if (differ.pet)
    {
        actor.pet = hero.pet;
        actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
    }
}

// 0x496a65..0x497636: del registro propio, cada grupo con su acceso (y los 3.848 rand() de su cierre).
void CopyFromTheRecord(LetterPortrait& portrait, std::uint8_t heroClass, const Differences& differ,
                       const WorldEntryResources& resources)
{
    ActorState& actor = portrait.actor;
    WorldState& world = resources.world;
    std::uint8_t& depth = world.Runtime().ownRecordDepth;
    Render::Models::CrtRandom& random = resources.cloth.random;
    const auto& equipment = world.OwnCharacter().equipment;
    if (differ.body)
    {
        // 0x496a79: 0x4fb1a0 antes de abrir el registro.
        portrait.cloth = CharacterCloth{};
        {
            const Render::Models::LogicalAccess own(depth, random, Render::Models::OwnRecordCloseRandoms);
            actor.head = GameLogic::ClassPartModel(GameLogic::BodyPart::Helm, heroClass);
            for (std::size_t part = 0; part < actor.body.size(); ++part)
                FillFromItem(actor.body[part], equipment[FirstBodySlot + part], ClassPart(part, heroClass));
        }
        // 0x496e30..0x496e52: el byte +0x14 y los dwords +0x18 y +0x1c de las seis partes en 0; MuRet no los tiene.
    }
    if (differ.hands)
    {
        const Render::Models::LogicalAccess own(depth, random, Render::Models::OwnRecordCloseRandoms);
        for (std::size_t side = 0; side < actor.hands.size(); ++side)
            FillFromItem(actor.hands[side], equipment[side], NoItemModel);
    }
    if (differ.wings)
    {
        const Render::Models::LogicalAccess own(depth, random, Render::Models::OwnRecordCloseRandoms);
        FillFromItem(actor.wings, equipment[WingsSlot], NoItemModel);
    }
    if (differ.pet)
    {
        const Render::Models::LogicalAccess own(depth, random, Render::Models::OwnRecordCloseRandoms);
        FillFromItem(actor.pet, equipment[PetSlot], NoItemModel);
        actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
    }
}

// 0x497676..0x49777c: el ayudante de nuevo por la mascota (las tablas 0x4977a4 y 0x49778c), su alpha (+0x174) en 0 y
// +0x688 con la escala del personaje del retrato.
Core::Expected<void, std::string> RebuildTheHelper(LetterPortrait& portrait, const WorldEntryResources& resources)
{
    ActorState& actor = portrait.actor;
    HelperState& helper = portrait.helper;
    helper.alive = false;
    std::optional<std::uint16_t> type;
    switch (actor.pet.model)
    {
    case GuardianPet:
        type = GuardianPet;
        break;
    case 0x1c14:
        type = UniliaHelper;
        break;
    case HeightOffsetPet:
        type = DinorantHelper;
        break;
    case 0x1c16:
        type = DarkHorseHelper;
        break;
    case 0x1c37:
        // 0x49770a..0x497753: por +0x31b, el byte +3 de la mascota.
        type = actor.pet.flags1b == 1   ? std::uint16_t{0x14a}
               : actor.pet.flags1b == 2 ? std::uint16_t{0x14c}
                                        : std::uint16_t{0x14b};
        break;
    default:
        break;
    }
    if (type)
    {
        auto built = ConstructHelper(helper, *type, actor.position, actor, ActorPoolSize, resources.world.Runtime().map,
                                     &resources.cloth.random);
        if (!built.HasValue())
            return Core::Unexpected(built.Error());
    }
    helper.horse.alpha = 0.0f;
    portrait.helperScale = static_cast<float>(
        static_cast<double>(helper.scale) * static_cast<double>(HelperScaleFactor) / static_cast<double>(actor.scale));
    return {};
}

CharacterStepContext StepContextOf(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    const float worldClock =
        resources.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(resources.clock->Now()).count())
            : 0.0f;
    return CharacterStepContext{.world = world,
                                .random = resources.cloth.random,
                                .scene = SceneId::Game,
                                .playerSkeleton = PlayerSkeleton(resources),
                                .items = resources.items,
                                .darkSpiritSkeleton = resources.models.SkeletonOf(DarkSpiritModel),
                                .particles = &world.Particles(),
                                .worldClock = worldClock,
                                .counter = resources.cloth.logicSteps,
                                .actionSpeeds = &world.ActionSpeeds(),
                                .fixedHeight = resources.camera.fixedHeight,
                                .soundRequest = [&runtime](std::int32_t sound) { runtime.lastSoundId = sound; }};
}

bool RidesTheHelper(const HelperState& helper)
{
    if (!helper.alive)
        return false;
    const std::uint16_t type = helper.type;
    return type == UniliaHelper || type == DinorantHelper || type == DarkHorseHelper ||
           (type >= FirstFenrirHelper && type <= LastFenrirHelper);
}

// 0x495a59..0x495aba: el índice de `action` en la lista (el tamaño si no está), más `delta`, con la vuelta si quedó
// negativo y el resto con signo.
template <std::size_t N>
Core::Expected<std::int32_t, std::string> NextIn(const std::array<std::int32_t, N>& actions, std::int32_t wrap,
                                                 std::int32_t action, std::int32_t delta)
{
    std::int32_t index = 0;
    while (static_cast<std::size_t>(index) < N && actions[static_cast<std::size_t>(index)] != action)
        ++index;
    index += delta;
    if (index < 0)
        index += wrap;
    const std::int32_t at = index % static_cast<std::int32_t>(N);
    if (at < 0)
        return Core::Unexpected(std::format("0x495a85: el índice {} queda fuera de la lista de 0x495a20", at));
    return actions[static_cast<std::size_t>(at)];
}
} // namespace

Core::Expected<void, std::string> BuildThePortrait(LetterPortrait& portrait, const WorldEntryResources& resources)
{
    Render::Models::CrtRandom& random = resources.cloth.random;
    // 0x4961d9..0x4961ec: el ayudante en cero (0x75 dwords).
    portrait.helper = HelperState{};
    ActorState& actor = portrait.actor;
    {
        // 0x4961ee..0x4962b7: abre +0x41c y +0x420; 0x4962bc..0x4962c7 borra los 0x438 bytes (las entradas de la tabla
        // quedan); 0x4962c9..0x496354 cierra la X y después la Y.
        std::optional<Render::Models::LogicalAccess> cellX;
        cellX.emplace(actor.cellXAccesses, random, Render::Models::SmallLogicalCloseRandoms);
        std::optional<Render::Models::LogicalAccess> cellY;
        cellY.emplace(actor.cellYAccesses, random, Render::Models::SmallLogicalCloseRandoms);
        const std::uint8_t xDepth = actor.cellXAccesses;
        const std::uint8_t yDepth = actor.cellYAccesses;
        const std::uint8_t byte38eDepth = actor.byte38eAccesses;
        actor = ActorState{};
        actor.cellXAccesses = xDepth;
        actor.cellYAccesses = yDepth;
        actor.byte38eAccesses = byte38eDepth;
        // Las telas (+0x18c y +0x190) también quedan en cero, sin soltarlas.
        portrait.cloth = CharacterCloth{};
        cellX.reset();
        cellY.reset();
    }
    // 0x496354..0x4963e0: la celda lógica del héroe publicado, la Y y después la X.
    const LogicalCell cell = ReadLogicalCell(HeroOf(resources), random);
    WorldState& world = resources.world;
    const ActorConstructionContext construction{world.Map().terrain, world.Runtime().map, World::CullingScene::Game,
                                                resources.camera.fixedHeight};
    if (auto built = ConstructActor(actor, Data::Models::PlayerSkeletonModel, {cell.x, cell.y, 0.0f}, construction,
                                    resources.cloth);
        !built.HasValue())
        return built;
    actor.position = {HiddenCoordinate, HiddenCoordinate, HiddenCoordinate};
    return {};
}

Core::Expected<void, std::string> SetThePortraitClass(LetterPortrait& portrait, std::uint8_t characterClass,
                                                      const WorldEntryResources& resources)
{
    // 0x4977de..0x497804: [0x743a16c] (el pool) apunta al personaje del retrato mientras dura; 0x513100 no lo lee.
    portrait.actor.characterClass = characterClass;
    return ApplyTheClassParts(portrait.actor, resources);
}

Core::Expected<void, std::string> DressThePortrait(LetterPortrait& portrait, GameLogic::Equipment equipment,
                                                   std::int32_t action, const WorldEntryResources& resources)
{
    ActorState& actor = portrait.actor;
    if (auto dressed = ApplyCharacterSetTo(actor, portrait.helper, equipment, resources); !dressed.HasValue())
        return dressed;
    // 0x497836..0x497855: la x87 con la escala del héroe publicado, guardada en float.
    portrait.helperScale =
        static_cast<float>(static_cast<double>(portrait.helper.scale) * static_cast<double>(HelperScaleFactor) /
                           static_cast<double>(HeroOf(resources).scale));
    // 0x49784a..0x497875.
    actor.safeZone = actor.wings.model != NoItemModel && action > LastGroundAction ? 1 : 0;
    return {};
}

Core::Expected<bool, std::string> CopyTheHeroToThePortrait(LetterPortrait& portrait, std::int32_t& action,
                                                           const WorldEntryResources& resources)
{
    ActorState& actor = portrait.actor;
    const ActorState& hero = HeroOf(resources);
    // 0x49654e..0x496572.
    if (actor.characterClass != hero.characterClass)
    {
        actor.characterClass = hero.characterClass;
        if (auto parts = ApplyTheClassParts(actor, resources); !parts.HasValue())
            return Core::Unexpected(parts.Error());
    }
    // 0x496584: +0x3e3 del héroe, leído otra vez antes de copiar (0x49699b).
    const bool fromTheRecord = hero.byte3e3 != 0;
    const Differences differ =
        fromTheRecord ? CompareWithTheRecord(actor, hero.characterClass, resources) : CompareWithTheHero(actor, hero);
    // 0x496975..0x49698f.
    if (!differ.body && !differ.hands && !differ.wings && !differ.pet)
        return false;
    if (fromTheRecord)
        CopyFromTheRecord(portrait, hero.characterClass, differ, resources);
    else
        CopyFromTheHero(portrait, hero, differ);
    if (differ.pet)
    {
        // 0x49763b..0x497649.
        auto shown = ShowThePortraitAction(portrait, StopAction, 0, resources);
        if (!shown.HasValue())
            return Core::Unexpected(shown.Error());
        action = StopAction;
    }
    else
    {
        // 0x497655..0x497677: el `[EBP − 0xc] == 1` de la mascota no se cumple acá; queda el de las manos.
        auto shown = ShowThePortraitAction(portrait, portrait.shownAction, 0, resources);
        if (!shown.HasValue())
            return Core::Unexpected(shown.Error());
        if (!differ.hands)
            return true;
    }
    if (auto rebuilt = RebuildTheHelper(portrait, resources); !rebuilt.HasValue())
        return Core::Unexpected(rebuilt.Error());
    return true;
}

Core::Expected<std::int32_t, std::string> ShowThePortraitAction(LetterPortrait& portrait, std::int32_t action,
                                                                std::int32_t delta,
                                                                const WorldEntryResources& resources)
{
    auto next = RidesTheHelper(portrait.helper) ? NextIn(RidingActions, RidingWrap, action, delta)
                                                : NextIn(GestureActions, GestureWrap, action, delta);
    if (!next.HasValue())
        return next;
    const std::int32_t chosen = next.Value();
    ActorState& actor = portrait.actor;
    WorldRuntime& runtime = resources.world.Runtime();
    // 0x495adb: la tabla 0x495dd4, de 0x78 a 0x94.
    if (chosen == StopAction)
    {
        const MapAtZero map(runtime);
        if (auto stopped = StopThePortrait(actor, resources, "0x495aed"); !stopped.HasValue())
            return Core::Unexpected(stopped.Error());
    }
    else if (chosen == StrikeAction)
    {
        return Core::Unexpected(std::string(
            "0x495b0b: la acción 0x78 del retrato de las cartas va a 0x4f4a10 con el mundo en 0 (sin leer, con sus "
            "rand()), que es del hito 3 de la CL9; después pone +0x381 en 1, el frame (+0x114) en 0 y +0x3a4 en "
            "0xffff"));
    }
    else if (chosen == WalkAction)
    {
        const auto* player = PlayerSkeleton(resources);
        if (player == nullptr)
            return Core::Unexpected(std::string("0x495b43: falta Player.bmd para 0x4f37c0"));
        const MapAtZero map(runtime);
        auto walked = SetActorWalk(actor, ActorWalkContext{.stop = StopContextOf(resources, *player),
                                                           .actionSpeeds = resources.world.ActionSpeeds(),
                                                           .hero = false,
                                                           .actorSkeleton = player});
        if (!walked.HasValue())
            return Core::Unexpected(walked.Error());
        // 0x41ed10 desde 0x4f37c0: sin audio hasta la CL10.
        if (walked.Value().sound)
            runtime.lastSoundId = walked.Value().sound;
    }
    else if (chosen >= FirstGesture && chosen <= LastGesture)
    {
        // 0x495b56..0x495da7: 0x4ea030 con la animación de la tabla de los gestos.
        const auto* skeleton = resources.models.SkeletonOf(actor.type);
        if (skeleton == nullptr)
            return Core::Unexpected(
                std::format("0x495da7: la acción {:#x} sin las acciones del modelo {:#x}", chosen, actor.type));
        Render::Models::SetAction(actor.animation, *skeleton,
                                  TableAnimationOf(actor.characterClass, static_cast<std::uint8_t>(chosen)));
    }
    // 0x495daf..0x495db8: 0x4fd5f0 y 0x505de0 con el mundo en curso.
    if (auto stepped = StepTheBodyAndEffects(actor, StepContextOf(resources)); !stepped.HasValue())
        return Core::Unexpected(stepped.Error());
    portrait.shownAction = chosen;
    return chosen;
}

void TurnThePortrait(LetterPortrait& portrait)
{
    portrait.actor.angles = {PortraitPitch, PortraitRoll, PortraitYaw};
}

Render::Pass3D PortraitPass(App::PixelSize screen, const UI::OldWindowBase& base, const ActorState& actor)
{
    // 0x6ba004: el alto del retrato.
    constexpr float PortraitHeight = 141.0f;
    constexpr float FieldOfView = 1.0f;
    constexpr float NearPlane = 2000.0f;
    constexpr float FarPlane = 20000.0f;
    constexpr float RightAngle = -90.0f;
    constexpr glm::vec3 Distance{-10000.0f, 0.0f, -75.0f};
    // 0x6b86b0: lo que baja el caballo oscuro.
    constexpr double HorseDrop = 50.0;
    constexpr std::uint16_t DarkHorsePet = 0x1c16;
    const double rx = static_cast<double>(Render::ScaleX(1.0f, screen));
    const double ry = static_cast<double>(Render::ScaleY(1.0f, screen));
    // 0x4957bd..0x4957fc: _ftol trunca hacia el 0.
    const auto x = static_cast<int>(static_cast<double>(base.x) * rx);
    const auto y = static_cast<int>(static_cast<double>(base.y) * ry);
    const auto width = static_cast<int>(static_cast<double>(base.width) * rx);
    const auto height = static_cast<int>(ry * static_cast<double>(PortraitHeight));
    Render::Pass3D pass;
    pass.screen = screen;
    // 0x620d10: la y de GL desde abajo.
    pass.viewport = {x, screen.height - y - height, width, height};
    // 0x495801..0x495826: la x87, guardada en float.
    const auto aspect =
        static_cast<float>((static_cast<double>(base.width) * rx) / (ry * static_cast<double>(PortraitHeight)));
    pass.projection = glm::perspective(glm::radians(FieldOfView), aspect, NearPlane, FarPlane);
    glm::mat4 view = glm::rotate(glm::mat4(1.0f), glm::radians(RightAngle), glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::rotate(view, glm::radians(RightAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    view = glm::translate(view, Distance);
    double drop = -static_cast<double>(actor.position.z);
    if (actor.pet.model == DarkHorsePet)
        drop -= HorseDrop;
    pass.view = glm::translate(view, glm::vec3(-actor.position.x, -actor.position.y, static_cast<float>(drop)));
    pass.farPlane = FarPlane;
    return pass;
}

Core::Expected<Render::Pass3D, std::string> PrepareThePortraitDraw(LetterPortrait& portrait, UI::OldPortraitPart& part,
                                                                   App::PixelSize screen,
                                                                   const WorldEntryResources& resources)
{
    ActorState& actor = portrait.actor;
    // 0x497cac..0x497d3d: el frame contra +0x6a8 (la x87: el int a la pila y el float del frame).
    if (static_cast<double>(actor.animation.frame) < static_cast<double>(portrait.frameMark))
    {
        constexpr int IdleOdds = 4;
        if (portrait.idle == 0 && resources.cloth.random.Next() % IdleOdds == 0)
        {
            portrait.idle = 1;
            if (auto shown = ShowThePortraitAction(portrait, part.action, 0, resources); !shown.HasValue())
                return Core::Unexpected(shown.Error());
        }
        else if (part.action < StopAction || part.action > LastGroundAction)
        {
            portrait.idle = 0;
            if (auto shown = ShowThePortraitAction(portrait, StopAction, 0, resources); !shown.HasValue())
                return Core::Unexpected(shown.Error());
        }
        portrait.frameMark = 0;
    }
    else
    {
        portrait.frameMark = static_cast<std::int32_t>(actor.animation.frame);
    }
    // 0x497d3d..0x497d4f: +0x360 por debajo de 4, 0x414340.
    constexpr std::uint8_t FirstKeptLayer = 4;
    if (actor.byte360 < FirstKeptLayer)
        ReleaseTheLayer(actor);
    {
        // 0x495766..0x49579e: con el mundo en 0.
        const MapAtZero map(resources.world.Runtime());
        if (auto stepped = StepTheBodyAndEffects(actor, StepContextOf(resources)); !stepped.HasValue())
            return Core::Unexpected(stepped.Error());
        if (portrait.helper.alive)
            return Core::Unexpected(std::format("0x495796: el paso del ayudante {:#x} del retrato (0x444010) con su "
                                                "jinete fuera del pool no está portado (P-C310)",
                                                portrait.helper.type));
    }
    Render::Pass3D pass = PortraitPass(screen, part.base, actor);
    // 0x4958cc..0x4958df.
    actor.angles = {0.0f, 0.0f, part.angle};
    // 0x49594f..0x4959bf: +0x3e4 en 1, que MuRet no tiene.
    actor.scale = static_cast<float>(static_cast<double>(part.zoom) * static_cast<double>(HelperScaleFactor));
    portrait.helper.scale =
        static_cast<float>(static_cast<double>(portrait.helperScale) * static_cast<double>(part.zoom));
    actor.light = glm::vec3(1.0f);
    portrait.helper.light = glm::vec3(1.0f);
    actor.safeZone = actor.wings.model != NoItemModel && part.action > LastGroundAction ? 1 : 0;
    return pass;
}

Core::Expected<void, std::string> DrawThePortraitModel(const LetterPortrait& portrait)
{
    return Core::Unexpected(std::format("0x4959cc: el dibujo del ayudante (0x445260) y del personaje (0x50ba30, clase "
                                        "{:#x}) del retrato de las cartas, fuera del pool, no está portado (P-C310)",
                                        portrait.actor.characterClass));
}

UI::OldPortraitHook LetterPortraitHook(const WorldEntryResources& resources)
{
    return [&resources](UI::OldPortraitPart& part, UI::PortraitCallKind call,
                        std::int32_t arg) -> Core::Expected<void, std::string>
    {
        if (!part.actor)
            part.actor = std::make_shared<LetterPortrait>();
        LetterPortrait& portrait = *std::static_pointer_cast<LetterPortrait>(part.actor);
        const bool built = part.built != 0;
        switch (call)
        {
        case UI::PortraitCallKind::Build:
            if (auto made = BuildThePortrait(portrait, resources); !made.HasValue())
                return made;
            part.built = 1;
            return {};
        case UI::PortraitCallKind::SetClass:
            return built ? SetThePortraitClass(portrait, part.portraitClass, resources)
                         : Core::Expected<void, std::string>{};
        case UI::PortraitCallKind::SetEquipment:
            return built ? DressThePortrait(portrait, part.equipment, part.action, resources)
                         : Core::Expected<void, std::string>{};
        case UI::PortraitCallKind::CopyTheHero:
        {
            if (!built)
                return {};
            auto copied = CopyTheHeroToThePortrait(portrait, part.action, resources);
            return copied.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(copied.Error());
        }
        case UI::PortraitCallKind::SetAction:
        {
            auto shown = ShowThePortraitAction(portrait, part.action, 0, resources);
            return shown.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(shown.Error());
        }
        case UI::PortraitCallKind::TurnAction:
        {
            auto shown = ShowThePortraitAction(portrait, part.action, arg, resources);
            if (!shown.HasValue())
                return Core::Unexpected(shown.Error());
            part.action = shown.Value();
            portrait.frameMark = 0;
            portrait.idle = 1;
            return {};
        }
        case UI::PortraitCallKind::SetAngle:
            if (built)
                TurnThePortrait(portrait);
            return {};
        case UI::PortraitCallKind::Step:
            portrait.actor.byte360 = HeroOf(resources).byte360;
            return {};
        }
        return {};
    };
}
} // namespace Client::Scenes
