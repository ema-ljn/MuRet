#include "Chat/GameMessages.h"

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/WorldRuntime.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"

#include <gtest/gtest.h>

#include <string_view>

// The game messages on WorldRuntime (CL8 milestone 3; 06 § El chat): 0x555190 and the clocks of 0x637700. The texts
// are made up.
namespace
{
using Client::Kernel::WorldRuntime;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Client::Core::PixelSize Screen1280{1280, 960};

// A message goes to the log and, with the name empty, to the old area; the area's clock goes back to 150.
TEST(GameMessagesTest, AMessageGoesToTheLogAndTheOldArea)
{
    WorldRuntime runtime;
    const LetterMeasure letters;
    ASSERT_TRUE(Client::Chat::AddGameMessage(runtime, letters, Screen640, "", "hola", 1).HasValue());
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "hola");
    EXPECT_EQ(runtime.chat.area.count, 1);
    EXPECT_EQ(runtime.chat.area.clock, Client::Ui::MessageClockSteps);
}

// What the modules write through Kernel::IOnScreenMessages is AddGameMessage measured on the screen of that moment: a
// line that wraps at 640 fits in one at 1280.
TEST(GameMessagesTest, TheModulesMessagesAreMeasuredOnTheScreenOfTheMoment)
{
    constexpr std::string_view Long = "uno dos tres cuatro cinco seis siete ocho nueve";
    const LetterMeasure letters;
    WorldRuntime asked;
    int screens = 0;
    Client::Chat::ChatMessages messages{asked, letters, [&screens]
                                        {
                                            ++screens;
                                            return Screen1280;
                                        }};

    ASSERT_TRUE(messages.Add("Ana", Long, 1).HasValue());

    EXPECT_EQ(screens, 1);
    WorldRuntime direct;
    ASSERT_TRUE(Client::Chat::AddGameMessage(direct, letters, Screen1280, "Ana", Long, 1).HasValue());
    EXPECT_EQ(asked.chat.log.entries, direct.chat.log.entries);
    EXPECT_EQ(asked.chat.log.entries.size(), 1u);
}

// Without the screen, the messages are measured at 640 × 480, where the same line takes two.
TEST(GameMessagesTest, TheModulesMessagesWithoutAScreenAreMeasuredAt640)
{
    constexpr std::string_view Long = "uno dos tres cuatro cinco seis siete ocho nueve";
    const LetterMeasure letters;
    WorldRuntime asked;
    Client::Chat::ChatMessages messages{asked, letters, {}};

    ASSERT_TRUE(messages.Add("Ana", Long, 1).HasValue());

    WorldRuntime direct;
    ASSERT_TRUE(Client::Chat::AddGameMessage(direct, letters, Screen640, "Ana", Long, 1).HasValue());
    EXPECT_EQ(asked.chat.log.entries, direct.chat.log.entries);
    EXPECT_EQ(asked.chat.log.entries.size(), 2u);
}

// 0x637cf3 and 0x637cf8: with the clocks run out, an empty notice (300) and an empty message (150), which ages the
// log's lines outside its window and not scrolled.
TEST(GameMessagesTest, TheClocksAddEmptyNoticesAndMessagesWhenTheyRunOut)
{
    WorldRuntime runtime;
    const LetterMeasure letters;
    runtime.chat.log.entries.push_back(Client::Chat::ChatEntry{.name = "Ana", .text = "hola", .type = 0});
    runtime.chat.area.clock = 0;
    runtime.chat.area.noticeClock = 0;

    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());

    EXPECT_EQ(runtime.chat.area.clock, Client::Ui::MessageClockSteps);
    EXPECT_EQ(runtime.chat.area.noticeClock, Client::Ui::NoticeClockSteps);
    EXPECT_EQ(runtime.chat.log.entries[0].age, 1u);
    EXPECT_EQ(runtime.hud.dcFont, FontKind::Bold);

    // Not run out yet, they only go down.
    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    EXPECT_EQ(runtime.chat.area.clock, Client::Ui::MessageClockSteps - 1);
    EXPECT_EQ(runtime.chat.log.entries[0].age, 1u);
}

// 0x637d2c and 0x637d44: the chat brake and the macros' one go down by one per step, down to 0.
TEST(GameMessagesTest, TheBrakesGoDownOnePerStep)
{
    WorldRuntime runtime;
    const LetterMeasure letters;
    runtime.chat.input.brake = 2;
    runtime.hud.macroTime = 1;

    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    EXPECT_EQ(runtime.chat.input.brake, 1);
    EXPECT_EQ(runtime.hud.macroTime, 0);
    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    EXPECT_EQ(runtime.chat.input.brake, 0);
    EXPECT_EQ(runtime.hud.macroTime, 0);
}
} // namespace
