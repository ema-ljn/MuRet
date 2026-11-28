#include "Chat/GameMessages.h"

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/WorldRuntime.h"
#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"

#include <gtest/gtest.h>

#include <string_view>

// Los mensajes del juego sobre WorldRuntime (CL8 hito 3; 06 § El chat): 0x555190 y los relojes de 0x637700. Los
// textos son inventados.
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

// Un mensaje entra en el registro y, con el nombre vacío, en el área vieja; el reloj del área vuelve a 150.
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

// 0x637cf3 y 0x637cf8: con los relojes vencidos, un aviso vacío (300) y un mensaje vacío (150), que envejece los
// renglones del registro fuera de su ventana y sin correr.
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

    // Sin vencer, solo bajan.
    ASSERT_TRUE(Client::Chat::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    EXPECT_EQ(runtime.chat.area.clock, Client::Ui::MessageClockSteps - 1);
    EXPECT_EQ(runtime.chat.log.entries[0].age, 1u);
}

// 0x637d2c y 0x637d44: el freno del chat y el de las macros bajan de a uno por paso, hasta 0.
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
