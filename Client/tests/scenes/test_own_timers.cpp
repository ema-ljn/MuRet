#include "Player/OwnTimers.h"

#include "Crt/CrtRandom.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/LogicalAccess.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

// Client::Player::StepTheOwnTimers: 0x4fc450, the three WORDs of the own record (+0x3e, +0x40 and +0x42) that the
// hero's body step (0x4fd6fe) counts down, with the bit of +0x3c each one turns off at 0.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::OwnCharacterRecord;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x4fc450;
constexpr std::uint8_t AllTimerBits = 0x01 | 0x02 | 0x08;

// The own record with its equipment empty, as the F3 03 leaves it (0x4c47c8): the stats it recomputes need no
// Item.bmd. On the heap: the WorldState does not fit in Windows' 1 MB stack beside the test's frames.
struct OwnTimers
{
    OwnTimers()
    {
        world->OwnCharacter().ClearEquipmentForEntry();
        Own().speedAdjustBits = AllTimerBits;
    }

    OwnCharacterRecord& Own()
    {
        return world->OwnCharacter();
    }

    void Step()
    {
        const auto stepped = Client::Player::StepTheOwnTimers(*world, nullptr, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{Seed};
};

TEST(OwnTimersTest, EachWordAboveOneDropsByOneAndKeepsItsBit)
{
    OwnTimers timers;
    timers.Own().word3e = 5;
    timers.Own().word40 = 6;
    timers.Own().word42 = 7;

    timers.Step();

    EXPECT_EQ(timers.Own().word3e, 4);
    EXPECT_EQ(timers.Own().word40, 5);
    EXPECT_EQ(timers.Own().word42, 6);
    EXPECT_EQ(timers.Own().speedAdjustBits, AllTimerBits);
}

// 0x4fc46a..0x4fc556 and 0x4fc617..0x4fc767: the record opens and closes once, with the 3,848 rand() of 0x40b4d0.
TEST(OwnTimersTest, TheOwnRecordClosesOnceWithItsRandoms)
{
    OwnTimers timers;
    timers.Own().word3e = 2;
    timers.Own().word40 = 2;
    timers.Own().word42 = 2;

    timers.Step();

    EXPECT_EQ(timers.world->Runtime().ownRecordDepth, 0);
    EXPECT_EQ(timers.random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());
}

// 0x4fc57e, 0x4fc5b0 and 0x4fc606: the WORD that reaches 0 turns off its own bit and leaves the others on.
TEST(OwnTimersTest, AWordThatReachesZeroTurnsOffOnlyItsBit)
{
    struct Case
    {
        std::uint16_t OwnCharacterRecord::*word;
        std::uint8_t bit;
    };
    constexpr std::array Cases{Case{&OwnCharacterRecord::word3e, 0x01}, Case{&OwnCharacterRecord::word40, 0x02},
                               Case{&OwnCharacterRecord::word42, 0x08}};
    for (const Case& tested : Cases)
    {
        OwnTimers timers;
        timers.Own().word3e = 3;
        timers.Own().word40 = 3;
        timers.Own().word42 = 3;
        timers.Own().*tested.word = 1;

        timers.Step();

        EXPECT_EQ(timers.Own().*tested.word, 0);
        EXPECT_EQ(timers.Own().speedAdjustBits, static_cast<std::uint8_t>(AllTimerBits & ~tested.bit))
            << static_cast<int>(tested.bit);
    }
}

// 0x4fc594..0x4fc5e0: +0x40 at 0 clears the marks of the set options' list before 0x54efe0 and 0x54f9b0.
TEST(OwnTimersTest, Word40AtZeroClearsTheSetOptionMarks)
{
    OwnTimers timers;
    timers.Own().word3e = 3;
    timers.Own().word42 = 3;
    timers.world->ItemSets().list.used.fill(1);

    timers.Step();

    for (const std::uint8_t mark : timers.world->ItemSets().list.used)
        EXPECT_EQ(mark, 0);
}
} // namespace
