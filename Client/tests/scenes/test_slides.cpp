#include "Scenes/Slides.h"

#include "Data/Text/SlideTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/SlideBanner.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>

// El cartel de Slide.bmd del lado del juego (CL8 hito 3): 0x46b010 en el primer paso, 0x46b0e0 con el registro propio y
// el WM_TIMER 0x3eb entre dos vueltas de WinMain, con sus rand(). Los textos son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 99;

class SlidesTest : public ::testing::Test
{
protected:
    SlidesTest() : m_world(std::make_unique<WorldState>()), m_random(Seed)
    {
        Client::Data::Text::SlideTable table;
        table.periodSeconds = 60;
        table.speed = 2.5f;
        table.groups[0] = {.maxLevel = 15, .count = 1, .texts = {"un consejo inventado"}};
        Client::UI::LoadTheSlides(m_world->Runtime().hud.slides, table);
        m_world->OwnCharacter().level = 3;
    }

    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }

    [[nodiscard]] Client::UI::SlideBanner& Banner()
    {
        return m_world->Runtime().hud.slides;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random;
};

// 0x46b010: las colas, el timer de 60 s y 0x46b0e0: el cierre del registro propio y el rand() del sorteo.
TEST_F(SlidesTest, TheFirstGameStepArmsTheTimerAndQueuesOne)
{
    Client::Scenes::StartTheSlides(*m_world, m_random, 5000);

    EXPECT_EQ(Banner().timerDue, std::optional<std::uint32_t>(65000));
    EXPECT_EQ(m_random.State(), After(OwnRecordCloseRandoms + 1));
    ASSERT_EQ(Banner().normal.entries.size(), 1U);
    const auto& entry = Banner().normal.entries.begin()->second;
    EXPECT_EQ(entry.text, "un consejo inventado");
    EXPECT_EQ(entry.kind, 0);
    EXPECT_EQ(entry.color, 0xffc8dce6u);
    EXPECT_EQ(entry.speed, 2.5f);
}

// Sin el nivel en ningún tramo, el registro se abre y se cierra igual, sin el rand() del sorteo.
TEST_F(SlidesTest, ALevelOutOfTheGroupsDrawsNoPick)
{
    m_world->OwnCharacter().level = 99;

    Client::Scenes::StartTheSlides(*m_world, m_random, 0);

    EXPECT_EQ(m_random.State(), After(OwnRecordCloseRandoms));
    EXPECT_TRUE(Banner().normal.entries.empty());
}

// El WM_TIMER 0x3eb: antes de vencer, nada; después, el foco, 0x46b0e0 y el foco otra vez, una vez, y el período vuelve
// a contar desde ahí.
TEST_F(SlidesTest, TheTimerRunsOnceWhenDue)
{
    Client::Scenes::StartTheSlides(*m_world, m_random, 0);
    Banner().normal.entries.clear();
    const std::uint32_t afterStart = m_random.State();

    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 59999, true);
    EXPECT_EQ(m_random.State(), afterStart);

    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 61000, true);
    EXPECT_EQ(m_random.State(), After(2 * (OwnRecordCloseRandoms + 1) + 2));
    EXPECT_EQ(Banner().timerDue, std::optional<std::uint32_t>(121000));
    EXPECT_EQ(Banner().normal.entries.size(), 1U);

    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 61001, true);
    EXPECT_EQ(m_random.State(), After(2 * (OwnRecordCloseRandoms + 1) + 2));
}

// Fuera del juego, o con la cola normal todavía prendida, el timer saca solo los dos del foco.
TEST_F(SlidesTest, OutsideTheGameOnlyTheFocusIsRead)
{
    Client::Scenes::StartTheSlides(*m_world, m_random, 0);
    const std::uint32_t start = OwnRecordCloseRandoms + 1;

    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 60000, false);
    EXPECT_EQ(m_random.State(), After(start + 2));

    Banner().normal.alpha = 30;
    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 120000, true);
    EXPECT_EQ(m_random.State(), After(start + 4));

    Banner().normal.alpha = 0;
    m_world->Runtime().chat.slidesOn = false;
    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 180000, true);
    EXPECT_EQ(m_random.State(), After(start + 6));
}

// Sin el primer paso del juego no hay timer.
TEST_F(SlidesTest, WithoutTheFirstGameStepThereIsNoTimer)
{
    Client::Scenes::RunTheSlideTimer(*m_world, m_random, 600000, true);

    EXPECT_EQ(m_random.State(), After(0));
}
} // namespace
