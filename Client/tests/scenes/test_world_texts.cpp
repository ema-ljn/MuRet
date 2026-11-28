#include "Hud/WorldTexts.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GameProjection.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldTextQueue.h"
#include "support/RecordingTextDrawer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

// The game's 3D texts (CL8 milestone 6; 06 § HUD y ventanas, "La pasada 2D del juego"): 0x4383f0. The tests'
// projection leaves the screen x at the center minus x and the y at the center plus z minus 300, with the eye always
// at 1: with the 370 above, at 240 + z + 70. The text measures like the fake rasterizer: 6 columns per letter and 13
// rows. The texts are made up.
namespace
{
using Client::Core::PixelSize;
using Client::Hud::WorldTextDrawContext;
using Client::Kernel::GameProjection;
using Client::Kernel::HudState;
using Client::Kernel::WorldText;
using Client::Kernel::WorldTextQueue;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordingTextDrawer;

constexpr PixelSize Screen640{640, 480};

GameProjection FlatProjection(PixelSize screen)
{
    GameProjection projection;
    projection.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, -300.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    projection.horizontalScale = 1.0f;
    projection.verticalScale = 1.0f;
    projection.centerX = screen.width / 2;
    projection.centerY = screen.height / 2;
    projection.screenWidth = screen.width;
    projection.screenHeight = screen.height;
    return projection;
}

WorldText Entry(std::string_view text, glm::vec3 position)
{
    WorldText entry;
    std::copy_n(text.begin(), std::min(text.size(), entry.text.size()), entry.text.begin());
    entry.position = position;
    return entry;
}

class WorldTextsTest : public ::testing::Test
{
protected:
    WorldTextsTest() : m_text(m_sprites), m_projection(FlatProjection(Screen640))
    {
        m_sprites.Begin(Screen640);
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, bool withProjection = true)
    {
        WorldTextDrawContext context{.queue = m_queue,
                                     .projection = withProjection ? &m_projection : nullptr,
                                     .sprites = m_sprites,
                                     .text = m_text,
                                     .hud = m_hud,
                                     .projecting = projecting};
        return Client::Hud::DrawTheWorldTexts(context);
    }

    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    GameProjection m_projection;
    WorldTextQueue m_queue;
    HudState m_hud{};
};

// 0x43840b..0x438584: each entry, 370 higher, centered on x in bold, with background 0xff000064 and text 0xffc8e6ff;
// the queue stays empty.
TEST_F(WorldTextsTest, EachEntryIsDrawnAboveItsPositionAndTheQueueEmpties)
{
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    m_queue.push_back(Entry("Paso", glm::vec3(-20.0f, 0.0f, -200.0f)));
    ASSERT_TRUE(Draw().HasValue());

    EXPECT_TRUE(m_queue.empty());
    ASSERT_EQ(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[0].text, "Puerta");
    EXPECT_EQ(m_text.texts[0].x, 220 - 18);
    EXPECT_EQ(m_text.texts[0].y, 210);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffc8e6ffu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0xff000064u);
    EXPECT_EQ(m_text.texts[1].text, "Paso");
    EXPECT_EQ(m_text.texts[1].x, 340 - 12);
    EXPECT_EQ(m_text.texts[1].y, 110);
    // 0x554180 keeps the last one's size; the font and the colors stay.
    EXPECT_EQ(m_hud.arrowsTextWidth, 24);
    EXPECT_EQ(m_hud.arrowsTextHeight, 13);
    EXPECT_EQ(m_hud.dcFont, FontKind::Bold);
    EXPECT_EQ(m_hud.textColors.text, 0xffc8e6ffu);
}

// 0x438460..0x438469: with the screen x or y negative it is not drawn, but it leaves the queue.
TEST_F(WorldTextsTest, AnEntryOffTheScreenIsDroppedWithoutDrawing)
{
    m_queue.push_back(Entry("Lejos", glm::vec3(400.0f, 0.0f, 0.0f)));
    m_queue.push_back(Entry("Abajo", glm::vec3(0.0f, 0.0f, -400.0f)));
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(m_queue.empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_hud.dcFont, FontKind::Normal);
}

// strlen over the 64 bytes: without a 0, the 64.
TEST_F(WorldTextsTest, AFullEntryReadsItsSixtyFourBytes)
{
    const std::string full(Client::Kernel::WorldTextSize, 'a');
    m_queue.push_back(Entry(full, glm::vec3(0.0f, 0.0f, 0.0f)));
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, full);
}

TEST_F(WorldTextsTest, AProjectedDrawKeepsTheQueue)
{
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(m_queue.size(), 1U);
    EXPECT_EQ(m_text.texts.size(), 1U);
}

// Without the world pass's projection, the empty queue does not stop and an entry does (0x438455).
TEST_F(WorldTextsTest, AnEntryWithoutTheProjectionCuts)
{
    ASSERT_TRUE(Draw(false, false).HasValue());
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    const auto drawn = Draw(false, false);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x438455"), std::string::npos);
}
} // namespace
