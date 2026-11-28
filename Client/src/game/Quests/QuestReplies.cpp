#include "Quests/QuestReplies.h"
#include "Actors/ActionReply.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Quests/Quest.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Actors/ClassParts.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Hud/GamePanels.h"
#include "CastleSiege/CastleWindows.h"
#include "Monsters/MonsterCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Quests/QuestState.h"
#include "Quests/Quests.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Quests
{
namespace
{
// 0x4ddbb3: 0x41ed10(0x66, 0, 0) before the switch, and again at the end of 0xc9, 0xca and 0xcb (0x4de4db).
constexpr Audio::SoundId PrizeSound{0x66};
// 0x4dde55: the 0xc8's, which returns without the action or the second 0x66.
constexpr Audio::SoundId PointsPrizeSound{0x65};
// 0x4de145 (0x4de416, 0x4de4ca): 0x4ea030(character, 0xb8) after 0x513100.
constexpr std::uint8_t ClassPrizeAction = 0xb8;

// 0x4ddb00 and 0x4ddb40 from 0x418ee0: 0x657a90 and 0x4198b0(−1).
Core::Expected<void, std::string> ShowTheNewState(const Scenes::WorldEntryResources& resources, std::uint8_t index,
                                                  std::uint8_t state)
{
    Kernel::WorldState& world = resources.world;
    SetTheQuestState(world, index, state);
    Hud::GamePanels panels(world, resources.cloth.random, Hud::GamePanelTables{.texts = resources.texts});
    CastleSiege::LendTheCastlePanelServices(panels, resources);
    if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
        return closed;
    auto opened = OpenTheQuestDialog(
        QuestContext{.world = world, .random = resources.cloth.random, .texts = resources.texts}, -1);
    // Q-C842: with the current quest outside the 200 records (the 255 of the wraparound, or one from the A1 from 200
    // on), the original reads the record past the object, memory MuRet does not model. From the packet, what is missing
    // from the dialog is discarded with a warning, without cutting the session: what was written so far stays.
    if (!opened.HasValue() && ReadsAQuestOutsideTheRecords(opened.Error()))
    {
        resources.log.Warning("se descarta el diálogo de la misión {} del A1/A2: {}", index, opened.Error());
        return {};
    }
    return opened;
}

// The fifteen turns of 0x530860 over the character, like 0x4dbfad with the level-up F3 05: the 0x60f/0 at its
// position, with its target position, its angles, it as owner, width 40 (0x42200000) and key 2; with `falling`, each
// turn adds the 0x60f/0xa from the position copy with z plus 200 (0x6b8928), made once before the turns
// (0x4de0ae..0x4de0d0). A full pool neither creates nor draws rand(), and the turns go on.
Core::Expected<void, std::string> CreateThePrizeSparkles(Kernel::ActorState& actor,
                                                         const Scenes::WorldEntryResources& resources, bool falling,
                                                         std::uint32_t address)
{
    constexpr int Rounds = 15;
    constexpr std::int32_t SparkleJoint = 0x60f, SparkleSubtype = 0, FallingSubtype = 0xa;
    constexpr float SparkleWidth = 40.0f;
    constexpr std::int32_t SparkleKey = 2, NoTarget = -1;
    constexpr double Raise = 200.0; // 0x6b8928
    glm::vec3 raised = actor.position;
    raised.z = static_cast<float>(static_cast<double>(actor.position.z) + Raise);
    std::span<Kernel::JointState> joints = resources.world.DrawTail().joints;
    Crt::CrtRandom& random = resources.cloth.random;
    for (int round = 0; round < Rounds; ++round)
    {
        const Kernel::JointRequest sparkle{.type = SparkleJoint,
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
        if (auto created = Joints::CreateFaunaJoint(joints, sparkle, random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
        if (!falling)
            continue;
        const Kernel::JointRequest descending{.type = SparkleJoint,
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
        if (auto created = Joints::CreateFaunaJoint(joints, descending, random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address + 0x23, created.Error()));
    }
    // 0x518cf0(0x61e, position, angles, own light, 0, character, −1, 0, 0, 0, 0): the decal /0, like the level-up one.
    // With the table full, none.
    static_cast<void>(resources.world.Map().effects.CreatePetSummon(actor.position, actor.angles, actor.light, &actor,
                                                                    Effects::LevelUpDecalSubtype));
    return {};
}

// 0x513100 (called at `address`: 0x4de140, 0x4de411 or 0x4de4c5), 0x4ea030(character, 0xb8) at 0x4de4d0 and the 0x66
// of 0x4de4db. Each sound of 0x4f2250 is dispatched; the last one stays in lastSoundId.
Core::Expected<void, std::string> DressTheNewClass(Kernel::ActorState& actor,
                                                   const Scenes::WorldEntryResources& resources, std::uint32_t address)
{
    const auto stop = Scenes::TransitionStopContext(actor, resources);
    if (!stop.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, stop.Error()));
    const auto dressed = Actors::ApplyTheClassParts(actor, stop.Value());
    if (!dressed.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, dressed.Error()));
    if (auto acted = Actors::SetMonsterAction(actor, ClassPrizeAction, 0x4de4d0, resources); !acted.HasValue())
        return acted;
    resources.world.Runtime().RequestSound(PrizeSound);
    return {};
}
} // namespace

void ApplyQuestFlag(const Protocol::Quests::QuestFlag& flag, Kernel::WorldState& world)
{
    QuestState& quest = world.Runtime().quest;
    if (flag.index < quest.flags.size())
        quest.flags[flag.index] = flag.value;
}

void ApplyQuestList(const Protocol::Quests::QuestList& list, const Scenes::WorldEntryResources& resources)
{
    // 0x418e20: what the copy would write past the object (+0x21398) is discarded with a warning (Q-C1126).
    if (const std::size_t outside = ApplyTheQuestList(resources.world, list.count, std::span(list.states));
        outside != 0)
        resources.log.Warning("el A0 copia {} bytes: se descartan los {} que pasan el objeto de las misiones (Q-C1126)",
                              list.count, outside);
}

Core::Expected<void, std::string> ApplyQuestState(const Protocol::Quests::QuestStateChange& change,
                                                  const Scenes::WorldEntryResources& resources)
{
    return ShowTheNewState(resources, change.index, change.state);
}

Core::Expected<void, std::string> ApplySetQuestResult(const Protocol::Quests::SetQuestResult& result,
                                                      const Scenes::WorldEntryResources& resources)
{
    if (result.result != 0)
        return {};
    return ShowTheNewState(resources, result.index, result.state);
}

Core::Expected<void, std::string> ApplyQuestResult(const Protocol::Quests::QuestResult& result,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    // With a key that is not there, 0x5110d0 gives the 400, as in the 1A (Q-C1125): the class, the joints, the decal,
    // 0x513100 and the action go to it, as in the original.
    const std::size_t slot = Kernel::ActorWithKey(world, result.key);
    runtime.RequestSound(PrizeSound);
    Kernel::ActorState& actor = world.Actors()[slot];
    const bool isHero = slot == runtime.hero;
    switch (result.kind)
    {
    case Protocol::Quests::PointsPrize:
        // 0x4ddbd5..0x4dddf2: the own record open around the sum.
        if (isHero)
        {
            const Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Models::OwnRecordCloseRandoms);
            Kernel::OwnCharacterRecord& record = world.OwnCharacter();
            record.levelUpPoints = static_cast<std::uint16_t>(record.levelUpPoints + result.value);
        }
        if (auto created = CreateThePrizeSparkles(actor, resources, false, 0x4dde20); !created.HasValue())
            return created;
        runtime.RequestSound(PointsPrizeSound);
        return {};
    case Protocol::Quests::ClassChangePrize:
        // 0x4dde7b..0x4dde91: the class before checking whether it is the hero.
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
            Kernel::OwnCharacterRecord& record = world.OwnCharacter();
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
        // 0x4de4e3: with no case, only the sound.
        return {};
    }
}
} // namespace Client::Quests
