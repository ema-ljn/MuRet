#include "Scenes/GameMessages.h"

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/WorldRuntime.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

// Los mensajes del juego sobre WorldRuntime (CL8 hito 3; 06 § El chat): 0x555190 y los relojes de 0x637700. Los
// textos son inventados.
namespace
{
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Scenes::WorldRuntime;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr Client::App::PixelSize Screen640{640, 480};

// Un mensaje entra en el registro y, con el nombre vacío, en el área vieja; el reloj del área vuelve a 150.
TEST(GameMessagesTest, AMessageGoesToTheLogAndTheOldArea)
{
    WorldRuntime runtime;
    const LetterMeasure letters;
    ASSERT_TRUE(Client::Scenes::AddGameMessage(runtime, letters, Screen640, "", "hola", 1).HasValue());
    ASSERT_EQ(runtime.chat.log.entries.size(), 1u);
    EXPECT_EQ(runtime.chat.log.entries[0].text, "hola");
    EXPECT_EQ(runtime.chat.area.count, 1);
    EXPECT_EQ(runtime.chat.area.clock, Client::UI::MessageClockSteps);
}

// 0x637cf3 y 0x637cf8: con los relojes vencidos, un aviso vacío (300) y un mensaje vacío (150), que envejece los
// renglones del registro fuera de su ventana y sin correr.
TEST(GameMessagesTest, TheClocksAddEmptyNoticesAndMessagesWhenTheyRunOut)
{
    WorldRuntime runtime;
    const LetterMeasure letters;
    runtime.chat.log.entries.push_back(Client::UI::ChatEntry{.name = "Ana", .text = "hola", .type = 0});
    runtime.chat.area.clock = 0;
    runtime.chat.area.noticeClock = 0;

    ASSERT_TRUE(Client::Scenes::StepTheMessageClocks(runtime, letters, Screen640).HasValue());

    EXPECT_EQ(runtime.chat.area.clock, Client::UI::MessageClockSteps);
    EXPECT_EQ(runtime.chat.area.noticeClock, Client::UI::NoticeClockSteps);
    EXPECT_EQ(runtime.chat.log.entries[0].age, 1u);
    EXPECT_EQ(runtime.chat.dcFont, FontKind::Bold);

    // Sin vencer, solo bajan.
    ASSERT_TRUE(Client::Scenes::StepTheMessageClocks(runtime, letters, Screen640).HasValue());
    EXPECT_EQ(runtime.chat.area.clock, Client::UI::MessageClockSteps - 1);
    EXPECT_EQ(runtime.chat.log.entries[0].age, 1u);
}
} // namespace
