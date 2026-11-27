#include "Scenes/QuestReplies.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Quests/Quest.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Actors/ClassParts.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GamePanels.h"
#include "Scenes/CastleWindows.h"
#include "Scenes/MonsterCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Scenes/QuestState.h"
#include "Scenes/Quests.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x4ddbb3: 0x41ed10(0x66, 0, 0) antes del switch, y otra vez al final de la 0xc9, la 0xca y la 0xcb (0x4de4db).
constexpr std::int32_t PrizeSound = 0x66;
// 0x4dde55: el de la 0xc8, que vuelve sin la acción ni el segundo 0x66.
constexpr std::int32_t PointsPrizeSound = 0x65;
// 0x4de145 (0x4de416, 0x4de4ca): 0x4ea030(personaje, 0xb8) después de 0x513100.
constexpr std::uint8_t ClassPrizeAction = 0xb8;

// 0x4ddb00 y 0x4ddb40 desde 0x418ee0: 0x657a90 y 0x4198b0(−1).
Core::Expected<void, std::string> ShowTheNewState(const WorldEntryResources& resources, std::uint8_t index,
                                                  std::uint8_t state)
{
    WorldState& world = resources.world;
    SetTheQuestState(world, index, state);
    GamePanels panels(world, resources.cloth.random, GamePanelTables{.texts = resources.texts});
    LendTheCastlePanelServices(panels, resources);
    if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
        return closed;
    auto opened = OpenTheQuestDialog(
        QuestContext{.world = world, .random = resources.cloth.random, .texts = resources.texts}, -1);
    // Q-C842: con la misión en curso fuera de los 200 registros (la 255 de la vuelta, o una del A1 desde la 200), el
    // original lee el registro después del objeto, memoria que MuRet no modela. Desde el paquete se descarta lo que
    // falta del diálogo con un aviso, sin cortar la sesión: queda lo que se escribió hasta ahí.
    if (!opened.HasValue() && ReadsAQuestOutsideTheRecords(opened.Error()))
    {
        resources.log.Warning("se descarta el diálogo de la misión {} del A1/A2: {}", index, opened.Error());
        return {};
    }
    return opened;
}

// Las quince vueltas de 0x530860 sobre el personaje, como 0x4dbfad con el F3 05 de subir de nivel: el 60f/0 en su
// posición, con su posición de destino, sus ángulos, él de dueño, el ancho 40 (0x42200000) y la key 2; con `falling`,
// cada vuelta suma el 60f/0xa desde la copia de la posición con la z más 200 (0x6b8928), hecha una vez antes de las
// vueltas (0x4de0ae..0x4de0d0). Un pool lleno no crea ni saca rand(), y las vueltas siguen.
Core::Expected<void, std::string> CreateThePrizeSparkles(ActorState& actor, const WorldEntryResources& resources,
                                                         bool falling, std::uint32_t address)
{
    constexpr int Rounds = 15;
    constexpr std::int32_t SparkleJoint = 0x60f, SparkleSubtype = 0, FallingSubtype = 0xa;
    constexpr float SparkleWidth = 40.0f;
    constexpr std::int32_t SparkleKey = 2, NoTarget = -1;
    constexpr double Raise = 200.0; // 0x6b8928
    glm::vec3 raised = actor.position;
    raised.z = static_cast<float>(static_cast<double>(actor.position.z) + Raise);
    std::span<JointState> joints = resources.world.DrawTail().joints;
    Crt::CrtRandom& random = resources.cloth.random;
    for (int round = 0; round < Rounds; ++round)
    {
        const World::FishEffectRequest sparkle{.type = SparkleJoint,
                                               .firstPosition = actor.position,
                                               .secondPosition = actor.position,
                                               .angles = actor.angles,
                                               .subtype = SparkleSubtype,
                                               .ownerContext = &actor,
                                               .scale = SparkleWidth,
                                               .key = SparkleKey,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = NoTarget};
        if (auto created = CreateFaunaJoint(joints, sparkle, random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
        if (!falling)
            continue;
        const World::FishEffectRequest descending{.type = SparkleJoint,
                                                  .firstPosition = raised,
                                                  .secondPosition = raised,
                                                  .angles = actor.angles,
                                                  .subtype = FallingSubtype,
                                                  .ownerContext = &actor,
                                                  .scale = SparkleWidth,
                                                  .key = SparkleKey,
                                                  .skill = 0,
                                                  .skillSerial = 0,
                                                  .target = NoTarget};
        if (auto created = CreateFaunaJoint(joints, descending, random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address + 0x23, created.Error()));
    }
    // 0x518cf0(0x61e, posición, ángulos, luz propia, 0, personaje, −1, 0, 0, 0, 0): el calco /0, como el de subir de
    // nivel. Con la tabla llena, ninguno.
    static_cast<void>(resources.world.Map().effects.CreatePetSummon(actor.position, actor.angles, actor.light, &actor,
                                                                    World::LevelUpDecalSubtype));
    return {};
}

// 0x513100 (llamado en `address`: 0x4de140, 0x4de411 o 0x4de4c5), 0x4ea030(personaje, 0xb8) en 0x4de4d0 y el 0x66 de
// 0x4de4db. Cada sonido de 0x4f2250 se despacha; el último queda en lastSoundId.
Core::Expected<void, std::string> DressTheNewClass(ActorState& actor, const WorldEntryResources& resources,
                                                   std::uint32_t address)
{
    const auto stop = TransitionStopContext(actor, resources);
    if (!stop.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, stop.Error()));
    const auto dressed = ApplyTheClassParts(actor, stop.Value());
    if (!dressed.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, dressed.Error()));
    if (auto acted = SetMonsterAction(actor, ClassPrizeAction, 0x4de4d0, resources); !acted.HasValue())
        return acted;
    resources.world.Runtime().RequestSound(PrizeSound);
    return {};
}
} // namespace

void ApplyQuestFlag(const Protocol::Quests::QuestFlag& flag, WorldState& world)
{
    QuestState& quest = world.Runtime().quest;
    if (flag.index < quest.flags.size())
        quest.flags[flag.index] = flag.value;
}

void ApplyQuestList(const Protocol::Quests::QuestList& list, const WorldEntryResources& resources)
{
    // 0x418e20: lo que la copia escribiría después del objeto (+0x21398) se descarta con un aviso (Q-C1126).
    if (const std::size_t outside = ApplyTheQuestList(resources.world, list.count, std::span(list.states));
        outside != 0)
        resources.log.Warning("el A0 copia {} bytes: se descartan los {} que pasan el objeto de las misiones (Q-C1126)",
                              list.count, outside);
}

Core::Expected<void, std::string> ApplyQuestState(const Protocol::Quests::QuestStateChange& change,
                                                  const WorldEntryResources& resources)
{
    return ShowTheNewState(resources, change.index, change.state);
}

Core::Expected<void, std::string> ApplySetQuestResult(const Protocol::Quests::SetQuestResult& result,
                                                      const WorldEntryResources& resources)
{
    if (result.result != 0)
        return {};
    return ShowTheNewState(resources, result.index, result.state);
}

Core::Expected<void, std::string> ApplyQuestResult(const Protocol::Quests::QuestResult& result,
                                                   const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    // Con una clave que no está, 0x5110d0 da el 400, como en el 1A (Q-C1125): la clase, los joints, el calco, 0x513100
    // y la acción van a él, como en el original.
    const std::size_t slot = ActorWithKey(world, result.key);
    runtime.RequestSound(PrizeSound);
    ActorState& actor = world.Actors()[slot];
    const bool isHero = slot == runtime.hero;
    switch (result.kind)
    {
    case Protocol::Quests::PointsPrize:
        // 0x4ddbd5..0x4dddf2: el registro propio abierto alrededor de la suma.
        if (isHero)
        {
            const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Models::OwnRecordCloseRandoms);
            OwnCharacterRecord& record = world.OwnCharacter();
            record.levelUpPoints = static_cast<std::uint16_t>(record.levelUpPoints + result.value);
        }
        if (auto created = CreateThePrizeSparkles(actor, resources, false, 0x4dde20); !created.HasValue())
            return created;
        runtime.RequestSound(PointsPrizeSound);
        return {};
    case Protocol::Quests::ClassChangePrize:
        // 0x4dde7b..0x4dde91: la clase antes de mirar si es el héroe.
        actor.characterClass =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>((result.value & 0x10u) | (result.value >> 4u)) >> 1u);
        if (isHero)
        {
            const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Models::OwnRecordCloseRandoms);
            world.OwnCharacter().characterClass = actor.characterClass;
        }
        if (auto created = CreateThePrizeSparkles(actor, resources, true, 0x4de0eb); !created.HasValue())
            return created;
        return DressTheNewClass(actor, resources, 0x4de140);
    case Protocol::Quests::ClassPointsPrize:
        if (isHero)
        {
            const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Models::OwnRecordCloseRandoms);
            OwnCharacterRecord& record = world.OwnCharacter();
            record.levelUpPoints = static_cast<std::uint16_t>(record.levelUpPoints + result.value);
        }
        if (auto created = CreateThePrizeSparkles(actor, resources, true, 0x4de3bc); !created.HasValue())
            return created;
        return DressTheNewClass(actor, resources, 0x4de411);
    case Protocol::Quests::EffectPrize:
        if (auto created = CreateThePrizeSparkles(actor, resources, true, 0x4de470); !created.HasValue())
            return created;
        return DressTheNewClass(actor, resources, 0x4de4c5);
    default:
        // 0x4de4e3: sin caso, solo el sonido.
        return {};
    }
}
} // namespace Client::Scenes
