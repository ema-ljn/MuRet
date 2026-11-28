#include "Hud/Slides.h"

#include "Crt/CrtRandom.h"
#include "Tables/SlideTable.h"
#include "Models/LogicalAccess.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/SlideBanner.h"
#include "Chat/ChatRuntime.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

// The Slide.bmd banner on the game side (CL8 milestone 3): 0x46b010 in the first step, 0x46b0e0 with the own record and
// the WM_TIMER 0x3eb between two WinMain loops, with their rand(). The texts are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;

constexpr std::uint32_t Seed = 99;

class SlidesTest : public ::testing::Test
{
protected:
    SlidesTest() : m_world(std::make_unique<WorldState>()), m_random(Seed)
    {
        Client::Tables::SlideTable table;
        table.periodSeconds = 60;
        table.speed = 2.5f;
        table.groups[0] = {.maxLevel = 15, .count = 1, .texts = {"un consejo inventado"}};
        Client::Ui::LoadTheSlides(m_world->Runtime().hud.slides, table);
        m_world->OwnCharacter().level = 3;
    }

    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }

    [[nodiscard]] Client::Ui::SlideBanner& Banner()
    {
        return m_world->Runtime().hud.slides;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
};

// 0x46b010: the queues, the 60 s timer and 0x46b0e0: the own record's close and the pick's rand().
TEST_F(SlidesTest, TheFirstGameStepArmsTheTimerAndQueuesOne)
{
    Client::Hud::StartTheSlides(*m_world, m_random, 5000);

    EXPECT_EQ(Banner().timerDue, std::optional<std::uint32_t>(65000));
    EXPECT_EQ(m_random.State(), After(OwnRecordCloseRandoms + 1));
    ASSERT_EQ(Banner().normal.entries.size(), 1U);
    const auto& entry = Banner().normal.entries.begin()->second;
    EXPECT_EQ(entry.text, "un consejo inventado");
    EXPECT_EQ(entry.kind, 0);
    EXPECT_EQ(entry.color, 0xffc8dce6u);
    EXPECT_EQ(entry.speed, 2.5f);
}

// With the level in no range, the record opens and closes anyway, without the pick's rand().
TEST_F(SlidesTest, ALevelOutOfTheGroupsDrawsNoPick)
{
    m_world->OwnCharacter().level = 99;

    Client::Hud::StartTheSlides(*m_world, m_random, 0);

    EXPECT_EQ(m_random.State(), After(OwnRecordCloseRandoms));
    EXPECT_TRUE(Banner().normal.entries.empty());
}

// The WM_TIMER 0x3eb: before it is due, nothing; after, the focus, 0x46b0e0 and the focus again, once, and the period
// counts again from there.
TEST_F(SlidesTest, TheTimerRunsOnceWhenDue)
{
    Client::Hud::StartTheSlides(*m_world, m_random, 0);
    Banner().normal.entries.clear();
    const std::uint32_t afterStart = m_random.State();

    Client::Hud::RunTheSlideTimer(*m_world, m_random, 59999, true);
    EXPECT_EQ(m_random.State(), afterStart);

    Client::Hud::RunTheSlideTimer(*m_world, m_random, 61000, true);
    EXPECT_EQ(m_random.State(), After(2 * (OwnRecordCloseRandoms + 1) + 2));
    EXPECT_EQ(Banner().timerDue, std::optional<std::uint32_t>(121000));
    EXPECT_EQ(Banner().normal.entries.size(), 1U);

    Client::Hud::RunTheSlideTimer(*m_world, m_random, 61001, true);
    EXPECT_EQ(m_random.State(), After(2 * (OwnRecordCloseRandoms + 1) + 2));
}

// Outside the game, or with the normal queue still on, the timer draws only the focus's two.
TEST_F(SlidesTest, OutsideTheGameOnlyTheFocusIsRead)
{
    Client::Hud::StartTheSlides(*m_world, m_random, 0);
    const std::uint32_t start = OwnRecordCloseRandoms + 1;

    Client::Hud::RunTheSlideTimer(*m_world, m_random, 60000, false);
    EXPECT_EQ(m_random.State(), After(start + 2));

    Banner().normal.alpha = 30;
    Client::Hud::RunTheSlideTimer(*m_world, m_random, 120000, true);
    EXPECT_EQ(m_random.State(), After(start + 4));

    Banner().normal.alpha = 0;
    m_world->Runtime().chat.slidesOn = false;
    Client::Hud::RunTheSlideTimer(*m_world, m_random, 180000, true);
    EXPECT_EQ(m_random.State(), After(start + 6));
}

TEST_F(SlidesTest, WithoutTheFirstGameStepThereIsNoTimer)
{
    Client::Hud::RunTheSlideTimer(*m_world, m_random, 600000, true);

    EXPECT_EQ(m_random.State(), After(0));
}
} // namespace
