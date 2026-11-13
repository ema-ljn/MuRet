#include "Scenes/SkillDelays.h"

#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La espera de las skills (CL9 hito 4, 06 § La espera de las skills), con un Skill.bmd inventado.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::SkillDelayContext;

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

// Skill.bmd con la espera (+0x28, int) y el requisito (+0x2e, WORD) de cada skill de `skills`.
Client::Data::Tables::SkillTable SkillsWith(std::initializer_list<InventedSkill> skills)
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    for (const InventedSkill& invented : skills)
    {
        const std::size_t base = std::size_t{invented.skill} * Client::Data::Tables::SkillRecordSize;
        const auto delay = static_cast<std::uint32_t>(invented.delay);
        for (std::size_t index = 0; index < 4; ++index)
            plain[base + 0x28 + index] = static_cast<std::byte>((delay >> (8 * index)) & 0xffu);
        plain[base + 0x2e] = static_cast<std::byte>(invented.requirement & 0xffu);
        plain[base + 0x2f] = static_cast<std::byte>(invented.requirement >> 8);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
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
    Client::Scenes::OwnCharacterRecord& Own()
    {
        return world.OwnCharacter();
    }

    Client::Scenes::WorldState world;
    CrtRandom random;
    Client::Data::Tables::SkillTable skills =
        SkillsWith({{0x11, 3000, 15}, {0x12, 0, 0}, {0x4c, 2000, 0}, {0x13, 1500, 16}});
};

// 0x5521f0: el WORD +0x2e, con la entrada abierta y cerrada.
TEST(SkillDelaysTest, TheRequirementIsTheWordAt2e)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom expected = Advanced(tested->random, SkillEntryRandoms);
    const auto requirement = Client::Scenes::SkillDelayRequirement(0x13, tested->Context());
    ASSERT_TRUE(requirement.HasValue()) << requirement.Error();
    EXPECT_EQ(requirement.Value(), 16);
    EXPECT_EQ(tested->random.State(), expected.State());
}

// 0x552990: la skill del lugar, su espera y, con la del registro en 0 y el requisito cumplido (15 <= 10 + 5), la
// pone. Abre el registro dos veces y la entrada dos.
TEST(SkillDelaysTest, StartingPutsTheDelayWhenTheRequirementIsMet)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom expected =
        Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms + SkillEntryRandoms + OwnRecordRandoms);
    ASSERT_TRUE(Client::Scenes::StartSkillDelay(0, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[0], 3000);
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
}

// Con el requisito sin cumplir (16 > 15), no la pone.
TEST(SkillDelaysTest, StartingWithoutTheRequirementLeavesTheDelay)
{
    auto tested = std::make_unique<Delays>();
    ASSERT_TRUE(Client::Scenes::StartSkillDelay(3, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[3], 0);
    tested->Own().leadership = 6;
    ASSERT_TRUE(Client::Scenes::StartSkillDelay(3, tested->Context()).HasValue());
    EXPECT_EQ(tested->Own().skillDelays[3], 1500);
}

// Sin espera en Skill.bmd, vuelve sin abrir el registro otra vez; con la espera corriendo, sin el requisito.
TEST(SkillDelaysTest, StartingStopsWithoutDelayOrWithItRunning)
{
    auto tested = std::make_unique<Delays>();
    const CrtRandom none = Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms);
    ASSERT_TRUE(Client::Scenes::StartSkillDelay(1, tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), none.State());
    EXPECT_EQ(tested->Own().skillDelays[1], 0);

    tested->Own().skillDelays[0] = 7;
    const CrtRandom running = Advanced(tested->random, OwnRecordRandoms + SkillEntryRandoms + OwnRecordRandoms);
    ASSERT_TRUE(Client::Scenes::StartSkillDelay(0, tested->Context()).HasValue());
    EXPECT_EQ(tested->random.State(), running.State());
    EXPECT_EQ(tested->Own().skillDelays[0], 7);
}

// 0x552c50: dice si se puede usar.
TEST(SkillDelaysTest, TryingSaysWhetherTheSkillCanBeUsed)
{
    auto tested = std::make_unique<Delays>();
    const auto first = Client::Scenes::TryStartSkillDelay(0, tested->Context());
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_TRUE(first.Value());
    EXPECT_EQ(tested->Own().skillDelays[0], 3000);
    const auto again = Client::Scenes::TryStartSkillDelay(0, tested->Context());
    ASSERT_TRUE(again.HasValue());
    EXPECT_FALSE(again.Value());
    const auto unmet = Client::Scenes::TryStartSkillDelay(3, tested->Context());
    ASSERT_TRUE(unmet.HasValue());
    EXPECT_FALSE(unmet.Value());
    const auto free = Client::Scenes::TryStartSkillDelay(1, tested->Context());
    ASSERT_TRUE(free.HasValue());
    EXPECT_TRUE(free.Value());
}

// 0x552ee3: la 0x4c se puede usar pero no le pone la espera.
TEST(SkillDelaysTest, TryingTheSkill4cDoesNotPutItsDelay)
{
    auto tested = std::make_unique<Delays>();
    const auto tried = Client::Scenes::TryStartSkillDelay(2, tested->Context());
    ASSERT_TRUE(tried.HasValue());
    EXPECT_TRUE(tried.Value());
    EXPECT_EQ(tested->Own().skillDelays[2], 0);
}

TEST(SkillDelaysTest, WithoutSkillBmdItIsACut)
{
    auto tested = std::make_unique<Delays>();
    SkillDelayContext context = tested->Context();
    context.skills = nullptr;
    const auto started = Client::Scenes::StartSkillDelay(0, context);
    ASSERT_FALSE(started.HasValue());
    EXPECT_NE(started.Error().find("0x552990"), std::string::npos) << started.Error();
}

// 0x552f10: baja las esperas de los +0x6a + 2 primeros lugares, sin pasar de 0, con el registro abierto.
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
    ASSERT_TRUE(Client::Scenes::CountDownSkillDelays(40, tested->Context()).HasValue());
    EXPECT_EQ(own.skillDelays[0], 60);
    EXPECT_EQ(own.skillDelays[1], 0);
    EXPECT_EQ(own.skillDelays[2], -5);
    EXPECT_EQ(own.skillDelays[3], 60);
    EXPECT_EQ(own.skillDelays[4], 100);
    EXPECT_EQ(tested->random.State(), expected.State());
}

// Con +0x6a en 0xfe o más, el lazo llegaría al lugar 255, que es el equipo (+0xb68): corta.
TEST(SkillDelaysTest, TheCountDownPastTheDelaysIsACut)
{
    auto tested = std::make_unique<Delays>();
    tested->Own().byte6a = 0xfe;
    const auto counted = Client::Scenes::CountDownSkillDelays(1, tested->Context());
    ASSERT_FALSE(counted.HasValue());
    EXPECT_NE(counted.Error().find("0x5530bb"), std::string::npos) << counted.Error();
    tested->Own().byte6a = 0xfd;
    EXPECT_TRUE(Client::Scenes::CountDownSkillDelays(1, tested->Context()).HasValue());
}
} // namespace
