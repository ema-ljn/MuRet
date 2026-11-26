#include "Scenes/LevelUpPointReply.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
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
                                                          const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    Crt::CrtRandom& random = resources.cloth.random;
    if ((reply.result & AddedBits) != 0)
    {
        // 0x4d3216..0x4d32cd: la apertura en línea; 0x4d335a..0x4d3415, el cierre.
        const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& own = world.OwnCharacter();
        // 0x4d32d8: DEC word [+0x68], antes del switch.
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
        default: // 0x4d32e2: JA a 0x4d334d.
            break;
        }
        // 0x4d334d..0x4d3356.
        own.maxBp = reply.maxBp;
    }
    // 0x4d341a..0x4d34ed: 0x550d10 entre una apertura (0x40b240, o 0x40ad90 con una entrada nueva) y un cierre
    // (0x40b4d0).
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    return RecalculateOwnStats(world.OwnCharacter(), OwnStatsContext{.itemSetTables = resources.itemSetTables,
                                                                     .itemSets = &world.ItemSets(),
                                                                     .items = resources.items,
                                                                     .publishedHero = world.Actors()[runtime.hero],
                                                                     .ownRecordAccesses = runtime.ownRecordDepth,
                                                                     .random = random,
                                                                     .held = HeldItemOf(runtime),
                                                                     .scan505820 = runtime.fullSetScan});
}
} // namespace Client::Scenes
