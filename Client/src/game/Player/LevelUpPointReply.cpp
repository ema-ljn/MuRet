#include "Player/LevelUpPointReply.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Models/LogicalAccess.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Player/OwnStatHelpers.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/Cloth.h"

#include <cstdint>
#include <string>

namespace Client::Player
{
namespace
{
namespace Player = Protocol::Player;

// 0x4d320c: TEST byte [pkt + 4], 0xf0.
constexpr std::uint8_t AddedBits = 0xf0;
// 0x4d32dc: AND ECX, 0xf.
constexpr std::uint8_t StatBits = 0x0f;
} // namespace

Core::Expected<void, std::string> ApplyLevelUpPointResult(const Player::LevelUpPointResult& reply,
                                                          const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    if ((reply.result & AddedBits) != 0)
    {
        // 0x4d3216..0x4d32cd: the inline opening; 0x4d335a..0x4d3415, the closing.
        const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        // 0x4d32d8: DEC word [+0x68], before the switch.
        --own.levelUpPoints;
        switch (reply.result & StatBits)
        {
        case Player::StrengthPoint: // 0x4d32eb
            ++own.strength;
            break;
        case Player::AgilityPoint: // 0x4d3327
            ++own.agility;
            break;
        case Player::VitalityPoint: // 0x4d332d
            ++own.vitality;
            own.maxLife = reply.maxLifeOrMana;
            break;
        case Player::EnergyPoint: // 0x4d333b
            ++own.energy;
            own.maxMana = reply.maxLifeOrMana;
            break;
        case Player::CommandPoint: // 0x4d3349
            ++own.leadership;
            break;
        default: // 0x4d32e2: JA to 0x4d334d.
            break;
        }
        // 0x4d334d..0x4d3356.
        own.maxBp = reply.maxBp;
    }
    // 0x4d341a..0x4d34ed: 0x550d10 between an opening (0x40b240, or 0x40ad90 with a new entry) and a closing
    // (0x40b4d0).
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return RecalculateOwnStats(world.OwnCharacter(), OwnStatsContext{.itemSetTables = resources.itemSetTables,
                                                                     .itemSets = &world.ItemSets(),
                                                                     .items = resources.items,
                                                                     .publishedHero = world.Hero(),
                                                                     .ownRecordAccesses = runtime.ownRecordDepth,
                                                                     .random = random,
                                                                     .held = HeldItemOf(runtime),
                                                                     .scan505820 = runtime.fullSetScan});
}
} // namespace Client::Player
