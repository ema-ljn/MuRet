#include "Scenes/QuestReplies.h"

#include "Core/Expected.h"
#include "Protocol/Quest.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/QuestState.h"
#include "Scenes/Quests.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x4ddbb3: 0x41ed10(0x66, 0, 0) antes del switch.
constexpr std::int32_t PrizeSound = 0x66;

// 0x4ddb00 y 0x4ddb40 desde 0x418ee0: 0x657a90 y 0x4198b0(−1).
Core::Expected<void, std::string> ShowTheNewState(const WorldEntryResources& resources, std::uint8_t index,
                                                  std::uint8_t state)
{
    WorldState& world = resources.world;
    if (auto set = SetTheQuestState(world, index, state); !set.HasValue())
        return set;
    GamePanels panels(world, resources.cloth.random, GamePanelTables{.texts = resources.texts});
    panels.LendTheWindowClose(resources.sendWindowClose);
    if (auto closed = panels.Registry().CloseAll(); !closed.HasValue())
        return closed;
    return OpenTheQuestDialog(QuestContext{.world = world, .random = resources.cloth.random, .texts = resources.texts},
                              -1);
}

std::string JointCut(std::uint8_t kind, std::uint32_t address)
{
    return std::format("{:#x}: los joints 0x60f del premio de una misión (A3 {:#x}, 0x530860) son de la CL11", address,
                       kind);
}
} // namespace

void ApplyQuestFlag(const Protocol::Quest::QuestFlag& flag, WorldState& world)
{
    QuestState& quest = world.Runtime().quest;
    if (flag.index < quest.flags.size())
        quest.flags[flag.index] = flag.value;
}

Core::Expected<void, std::string> ApplyQuestList(const Protocol::Quest::QuestList& list, WorldState& world)
{
    return ApplyTheQuestList(world, list.count, std::span(list.states));
}

Core::Expected<void, std::string> ApplyQuestState(const Protocol::Quest::QuestStateChange& change,
                                                  const WorldEntryResources& resources)
{
    return ShowTheNewState(resources, change.index, change.state);
}

Core::Expected<void, std::string> ApplySetQuestResult(const Protocol::Quest::SetQuestResult& result,
                                                      const WorldEntryResources& resources)
{
    if (result.result != 0)
        return {};
    return ShowTheNewState(resources, result.index, result.state);
}

Core::Expected<void, std::string> ApplyQuestResult(const Protocol::Quest::QuestResult& result,
                                                   const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    const std::size_t slot = ActorWithKey(world, result.key);
    runtime.lastSoundId = PrizeSound;
    ActorState& actor = world.Actors()[slot];
    const bool isHero = slot == runtime.hero;
    switch (result.kind)
    {
    case Protocol::Quest::PointsPrize:
        // 0x4ddbd5..0x4dddf2: el registro propio abierto alrededor de la suma.
        if (isHero)
        {
            const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                    Render::Models::OwnRecordCloseRandoms);
            OwnCharacterRecord& record = world.OwnCharacter();
            record.levelUpPoints = static_cast<std::uint16_t>(record.levelUpPoints + result.value);
        }
        return Core::Unexpected(JointCut(result.kind, 0x4dde20));
    case Protocol::Quest::ClassChangePrize:
        // 0x4dde7b..0x4dde91: la clase antes de mirar si es el héroe.
        actor.characterClass =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>((result.value & 0x10u) | (result.value >> 4u)) >> 1u);
        if (isHero)
        {
            const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                    Render::Models::OwnRecordCloseRandoms);
            world.OwnCharacter().characterClass = actor.characterClass;
        }
        return Core::Unexpected(JointCut(result.kind, 0x4de0eb));
    case Protocol::Quest::ClassPointsPrize:
        if (isHero)
        {
            const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                    Render::Models::OwnRecordCloseRandoms);
            OwnCharacterRecord& record = world.OwnCharacter();
            record.levelUpPoints = static_cast<std::uint16_t>(record.levelUpPoints + result.value);
        }
        return Core::Unexpected(JointCut(result.kind, 0x4de3bc));
    case Protocol::Quest::EffectPrize:
        return Core::Unexpected(JointCut(result.kind, 0x4de470));
    default:
        // 0x4de4e3: sin caso, solo el sonido.
        return {};
    }
}
} // namespace Client::Scenes
