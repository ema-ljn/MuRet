#include "Skills/SkillDelays.h"

#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The skill delays (06 § La espera de las skills), with an invented Skill.bmd.
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::SkillDelayContext;

constexpr int OwnRecordRandoms = 3848;
constexpr int SkillEntryRandoms = 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

struct InventedSkill
{
    std::uint8_t skill = 0;
    std::int32_t delay = 0;
    std::uint16_t requirement = 0;
};

Client::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Tables::SkillRecordSize;
        const auto delay = static_cast<std::uint32_t>(invented.delay);
        for (std::size_t index = 0; index < 4; ++index)
            plain[base + 0x28 + index] = static_cast<std::byte>((delay >> (8 * index)) & 0xffu);
        plain[base + 0x2e] = static_cast<std::byte>(invented.requirement & 0xffu);
        plain[base + 0x2f] = static_cast<std::byte>(invented.requirement >> 8);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

struct Delays
{
    Delays()
    {
        auto& own = world.OwnCharacter();
        own.SetSkillAt(0, 0x11);
        own.SetSkillAt(1, 0x12);
        own.SetSkillAt(2, 0x4c);
        own.SetSkillAt(3, 0x13);
        own.word36 = 10;
        own.leadership = 5;
    }
    SkillDelayContext Context()
    {
        return SkillDelayContext{.world = world, .random = random, .skills = &skills};
    }
    Client::Kernel::OwnCharacterRecord& Own()
    {
        return world.OwnCharacter();
    }

    Client::Kernel::WorldState world;
    CrtRandom random;
    Client::Tables::SkillTable skills = SkillsWith({{0x11, 3000, 15}, {0x12, 0, 0}, {0x4c, 2000, 0}, {0x13, 1500, 16}});
};

// 0x5521f0: the WORD +0x2e, with the entry opened and closed.
TEST(SkillDelaysTest, TheRequirementIsTheWordAt2e)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom expected = Advanced(tested->random, SkillEntryRandoms);
    const auto requirement = Client::Skills::SkillDelayRequirement(0x13, tested->Context());
    ASSERT_TRUE(requirement.HasValue()) << requirement.Error();
    EXPECT_EQ(requirement.Value(), 16);
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x552990: the slot's skill, its delay and, with the record's at 0 and the requirement met (15 <= 10 + 5), it sets
// it. It opens the record twice and the entry twice.
TEST(SkillDelaysTest, StartingPutsTheDelayWhenTheRequirementIsMet)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom expected =
        Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms + SkillEntryRandoms + OwnRecordRandoms);
    ASSERT_TRUE(Client::Skills::StartSkillDelay(0, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[0], 3000);
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// With the requirement unmet (16 > 15), it does not set it.
TEST(SkillDelaysTest, StartingWithoutTheRequirementLeavesTheDelay)
{
    auto tested = std::make_unique<Delays>();
    ASSERT_TRUE(Client::Skills::StartSkillDelay(3, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[3], 0);
    tested->Own().leadership = 6;
    ASSERT_TRUE(Client::Skills::StartSkillDelay(3, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[3], 1500);
}

// Without a delay in Skill.bmd, it returns without opening the record again; with the delay running, without the
// requirement.
TEST(SkillDelaysTest, StartingStopsWithoutDelayOrWithItRunning)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom none = Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms);
    ASSERT_TRUE(Client::Skills::StartSkillDelay(1, tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), none.State());
    EXPECT_EQ(tested->Own().skillDelays[1], 0);

    tested->Own().skillDelays[0] = 7;
    const CrtRandom running = Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms + OwnRecordRandoms);
    ASSERT_TRUE(Client::Skills::StartSkillDelay(0, tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), running.State());
    EXPECT_EQ(tested->Own().skillDelays[0], 7);
}

// 0x552c50
TEST(SkillDelaysTest, TryingSaysWhetherTheSkillCanBeUsed)
{
    auto tested = std::make_unique<Delays>();
    const auto first = Client::Skills::TryStartSkillDelay(0, tested->Context());
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_TRUE(first.Value());
    EXPECT_EQ(tested->Own().skillDelays[0], 3000);
    const auto again = Client::Skills::TryStartSkillDelay(0, tested->Context());
    ASSERT_TRUE(again.HasValue());
    EXPECT_FALSE(again.Value());
    const auto unmet = Client::Skills::TryStartSkillDelay(3, tested->Context());
    ASSERT_TRUE(unmet.HasValue());
    EXPECT_FALSE(unmet.Value());
    const auto free = Client::Skills::TryStartSkillDelay(1, tested->Context());
    ASSERT_TRUE(free.HasValue());
    EXPECT_TRUE(free.Value());
}

// 0x552ee3
TEST(SkillDelaysTest, TryingTheSkill4cDoesNotPutItsDelay)
{
    auto tested = std::make_unique<Delays>();
    const auto tried = Client::Skills::TryStartSkillDelay(2, tested->Context());
    ASSERT_TRUE(tried.HasValue());
    EXPECT_TRUE(tried.Value());
    EXPECT_EQ(tested->Own().skillDelays[2], 0);
}

TEST(SkillDelaysTest, WithoutSkillBmdItIsACut)
{
    auto tested = std::make_unique<Delays>();
    SkillDelayContext context = tested->Context();
    context.skills = nullptr;
    const auto started = Client::Skills::StartSkillDelay(0, context);
    ASSERT_FALSE(started.HasValue());
    EXPECT_NE(started.Error().find("0x552990"), std::string::npos) << started.Error();
}

// 0x552f10: lowers the delays of the first +0x6a + 2 slots, not below 0, with the record open.
TEST(SkillDelaysTest, TheCountDownLowersTheFirstSlots)
{
    auto tested = std::make_unique<Delays>();
    auto& own = tested->Own();
    own.byte6a = 2;
    own.skillDelays[0] = 100;
    own.skillDelays[1] = 30;
    own.skillDelays[3] = 100;
    own.skillDelays[4] = 100;
    own.skillDelays[2] = -5;
    const CrtRandom expected = Advanced(tested->random, OwnRecordRandoms);
    ASSERT_TRUE(Client::Skills::CountDownSkillDelays(40, tested->Context()).HasValue());
    EXPECT_EQ(own.skillDelays[0], 60);
    EXPECT_EQ(own.skillDelays[1], 0);
    EXPECT_EQ(own.skillDelays[2], -5);
    EXPECT_EQ(own.skillDelays[3], 60);
    EXPECT_EQ(own.skillDelays[4], 100);
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x5530c2: a SUB32 followed by a signed CMP: the subtraction wraps as a DWORD before negatives are clamped.
TEST(SkillDelaysTest, TheCountDownWrapsBeforeTheSignedClamp)
{
    for (const auto elapsed : {std::numeric_limits<std::int32_t>::min(), std::int32_t{-1}})
    {
        auto tested = std::make_unique<Delays>();
        auto& own = tested->Own();
        own.byte6a = 1;
        own.skillDelays[0] = elapsed == -1 ? std::numeric_limits<std::int32_t>::max() : 1;
        own.skillDelays[1] = -7;
        own.skillDelays[2] = 0;
        const auto counted = Client::Skills::CountDownSkillDelays(elapsed, tested->Context());
        ASSERT_TRUE(counted.HasValue()) << counted.Error();
        EXPECT_EQ(own.skillDelays[0], 0);
        EXPECT_EQ(own.skillDelays[1], -7);
        EXPECT_EQ(own.skillDelays[2], 0);
    }
}

// With +0x6a at 0xfe or more, the loop would reach slot 255, which is the equipment (+0xb68): it cuts.
TEST(SkillDelaysTest, TheCountDownPastTheDelaysIsACut)
{
    auto tested = std::make_unique<Delays>();
    tested->Own().byte6a = 0xfe;
    const auto counted = Client::Skills::CountDownSkillDelays(1, tested->Context());
    ASSERT_FALSE(counted.HasValue());
    EXPECT_NE(counted.Error().find("0x5530bb"), std::string::npos) << counted.Error();
    tested->Own().byte6a = 0xfd;
    EXPECT_TRUE(Client::Skills::CountDownSkillDelays(1, tested->Context()).HasValue());
}
} // namespace
