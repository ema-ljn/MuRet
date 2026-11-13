#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/MessageLog.h"

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// El registro del chat (0x576350c), el área vieja (0x77d9868) y su entrada 0x555190 (CL8 hito 3; 06 § El chat).
// Los textos son inventados; las cuentas, las del ASM: 0x45bb20, 0x459c70, 0x45bfe0, 0x47a050, 0x45d8c0, 0x45dc40,
// 0x45ddc0, 0x45de20 y 0x45db70.
namespace
{
using Client::App::PixelSize;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::UI::ChatEntry;
using Client::UI::ChatLogState;
using Client::UI::ChatMeasure;
using Client::UI::ChatModes;

// Como el rasterizador falso: 6 columnas por letra y 13 filas.
class LetterMeasure final : public ITextMeasure
{
public:
    explicit LetterMeasure(int letter = 6) : m_letter(letter) {}

    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * m_letter, 13};
    }

private:
    int m_letter;
};

// Mide siempre lo mismo, para la división por cero de 0x459d21.
class FixedMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view /*text*/) const override
    {
        return {200, 13};
    }
};

constexpr PixelSize Screen640{640, 480};

ChatEntry Entry(std::string name, std::string text, std::int32_t type, std::uint32_t age = 0)
{
    return ChatEntry{.name = std::move(name), .text = std::move(text), .type = type, .age = age};
}

class ChatLogTest : public ::testing::Test
{
protected:
    LetterMeasure m_letters;
    ChatLogState m_log;
    ChatModes m_modes;

    [[nodiscard]] ChatMeasure Measure() const
    {
        return ChatMeasure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = Screen640};
    }

    void Add(std::string_view name, std::string_view text, std::int32_t type, bool noticeSound = false)
    {
        const auto added = Client::UI::AddToTheLog(m_log, m_modes, Measure(), name, text, type, 0, noticeSound);
        ASSERT_TRUE(added.HasValue()) << added.Error();
    }
};

TEST_F(ChatLogTest, AShortMessageGoesInFrontWithItsTypeAndNoAge)
{
    Add("Ana", "hola", 0);
    Add("Beto", "chau", 1);

    ASSERT_EQ(m_log.entries.size(), 2u);
    EXPECT_EQ(m_log.entries[0], Entry("Beto", "chau", 1));
    EXPECT_EQ(m_log.entries[1], Entry("Ana", "hola", 0));
    EXPECT_EQ(m_log.selection, 0);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x459c70: 40 letras miden 240 > 180; 180 / 6 = 30 letras, que miden 180 (dentro de ±4). La segunda mitad, sin
// nombre, queda adelante de la primera.
TEST_F(ChatLogTest, ALongMessageIsCutInTwoAt180AndTheSecondHalfLosesTheName)
{
    const std::string text(40, 'a');
    Add("Ana", text, 0);

    ASSERT_EQ(m_log.entries.size(), 2u);
    EXPECT_EQ(m_log.entries[0], Entry("", std::string(10, 'a'), 0));
    EXPECT_EQ(m_log.entries[1], Entry("Ana", std::string(30, 'a'), 0));
}

// ~len(nombre) + ~len(texto) − 2 < 0x14: 19 bytes entran enteros aunque midan más de 180.
TEST_F(ChatLogTest, NineteenBytesBetweenNameAndTextAreNeverCut)
{
    const LetterMeasure wide(20);
    const ChatMeasure measure{.measure = wide, .dcFont = FontKind::Normal, .screen = Screen640};
    const auto added = Client::UI::AddToTheLog(m_log, m_modes, measure, "Ana", std::string(16, 'b'), 0, 0, false);

    ASSERT_TRUE(added.HasValue()) << added.Error();
    ASSERT_EQ(m_log.entries.size(), 1u);
    EXPECT_EQ(m_log.entries[0].text, std::string(16, 'b'));
}

// 0x459d21: un texto largo que mide menos de un punto por letra divide por cero en el original.
TEST_F(ChatLogTest, ATextOfLettersNarrowerThanAPointIsTheOriginalsDivisionByZero)
{
    const FixedMeasure fixed;
    const ChatMeasure measure{.measure = fixed, .dcFont = FontKind::Normal, .screen = Screen640};
    const auto added = Client::UI::AddToTheLog(m_log, m_modes, measure, "Ana", std::string(300, 'c'), 0, 0, false);

    EXPECT_FALSE(added.HasValue());
}

TEST_F(ChatLogTest, NormalChatIsDroppedWithoutF2)
{
    m_modes.showChat = false;
    Add("Ana", "hola", 3);
    Add("Ana", "aviso", 1);

    ASSERT_EQ(m_log.entries.size(), 1u);
    EXPECT_EQ(m_log.entries[0].type, 1);
}

// 0x45dc40, 0x45ddc0 y el 0x26 de 0x45bb20 con el filtro puesto y 0x77f3b78.
TEST_F(ChatLogTest, TheFilterLetsInOnlyNormalChatWithOneOfItsWordsAndRings)
{
    EXPECT_EQ(Client::UI::SetTheChatFilter(m_log, "/filter dragon alas"), Client::UI::FilterOnText);
    EXPECT_EQ(Client::UI::JoinedChatFilter(m_log), "dragon, alas");

    Add("Ana", "vendo escudo", 3);
    EXPECT_TRUE(m_log.entries.empty());

    const auto rung = Client::UI::AddToTheLog(m_log, m_modes, Measure(), "Ana", "compro alas", 3, 0, true);
    ASSERT_TRUE(rung.HasValue()) << rung.Error();
    EXPECT_EQ(rung.Value(), std::optional<std::int32_t>(Client::UI::ChatFilterSound));
    // Los otros tipos no pasan por el filtro.
    Add("Ana", "vendo escudo", 0);
    EXPECT_EQ(m_log.entries.size(), 2u);

    EXPECT_EQ(Client::UI::SetTheChatFilter(m_log, "/filter"), Client::UI::FilterOffText);
    EXPECT_EQ(Client::UI::SetTheChatFilter(m_log, "/filter"), std::nullopt);
}

// La suma de los largos de todas las palabras tiene que quedar por debajo de 31; la primera que no entra corta todo.
TEST_F(ChatLogTest, TheFilterWordsMustAddUpToLessThan31Bytes)
{
    EXPECT_EQ(Client::UI::SetTheChatFilter(m_log, "/filter " + std::string(31, 'x')), std::nullopt);
    EXPECT_TRUE(m_log.filter[0].empty());

    EXPECT_EQ(Client::UI::SetTheChatFilter(m_log, "/filter " + std::string(20, 'x') + " " + std::string(11, 'y')),
              Client::UI::FilterOnText);
    EXPECT_EQ(m_log.filter[0], std::string(20, 'x'));
    EXPECT_TRUE(m_log.filter[1].empty());
}

// Los dos vacíos, con el registro fuera de su ventana y sin correr: todas las entradas suben su edad.
TEST_F(ChatLogTest, AnEmptyMessageAgesTheLogOnlyOutsideItsWindowAndUnscrolled)
{
    Add("Ana", "hola", 0);
    Add("", "", 0);
    EXPECT_EQ(m_log.entries[0].age, 1u);

    m_modes.placement = 1;
    Add("", "", 0);
    EXPECT_EQ(m_log.entries[0].age, 1u);
    EXPECT_EQ(m_log.entries.size(), 1u);
}

// 0x47a050: de 150 en adelante saca los de atrás.
TEST_F(ChatLogTest, TheLogKeeps150)
{
    for (int i = 0; i < 151; ++i)
        Add("Ana", "uno", 0);
    EXPECT_EQ(m_log.entries.size(), 150u);
}

// 0x45bfe0 con −6, sin correr y fuera de su ventana: cuenta renglones con sus edades hasta 6.
TEST_F(ChatLogTest, TheAgedScrollCountsRowsWithTheirAges)
{
    for (int i = 0; i < 20; ++i)
        m_log.entries.push_back(Entry("Ana", "uno", 0));
    m_log.entries[2].age = 3;

    Client::UI::ScrollTheLog(m_log, m_modes, -6);

    // 1, 2, 2 + 1 + 3 = 6: el tercero.
    EXPECT_EQ(m_log.scroll, 3);
}

TEST_F(ChatLogTest, ScrollingStopsAtTheOldestRows)
{
    for (int i = 0; i < 10; ++i)
        m_log.entries.push_back(Entry("Ana", "uno", 0));
    m_modes.placement = 1;

    Client::UI::ScrollTheLog(m_log, m_modes, -100);
    EXPECT_EQ(m_log.scroll, 4);
    Client::UI::ScrollTheLog(m_log, m_modes, 100);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x45d8c0, F4 y el segundo botón: (0, 6) → (1, 3) → (1, 6) → (1, 30) → (0, 6).
TEST_F(ChatLogTest, F4CyclesTheSizes)
{
    Client::UI::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_modes.placement, 1);
    EXPECT_EQ(m_log.rows, 3);
    Client::UI::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_log.rows, 6);
    Client::UI::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_log.rows, 30);
    Client::UI::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_modes.placement, 0);
    EXPECT_EQ(m_log.rows, 6);
}

// 0x45db70: entre los primeros n, uno con los mismos 16 primeros bytes o con edad.
TEST_F(ChatLogTest, SaidRecentlyLooksAtTheFirst16BytesOrAnAge)
{
    Add("Ana", "abcdefghijklmnopXYZ", 0);
    EXPECT_TRUE(Client::UI::SaidRecently(m_log, "abcdefghijklmnop", 1));
    EXPECT_FALSE(Client::UI::SaidRecently(m_log, "otra cosa", 1));
    m_log.entries[0].age = 1;
    EXPECT_TRUE(Client::UI::SaidRecently(m_log, "otra cosa", 1));
}

// Un nombre de 11 bytes no tiene '\0' y se lee seguido del texto.
TEST_F(ChatLogTest, AnElevenByteNameRunsIntoTheText)
{
    Add("Abcdefghijkl", "hola", 0);
    EXPECT_EQ(m_log.entries[0].name, "Abcdefghijk");
    EXPECT_EQ(Client::UI::NameOf(m_log.entries[0]), "Abcdefghijkhola");
}

// 0x555190: con el nombre vacío o un tipo de sistema, también al área vieja; el chat de otros, no.
TEST(MessageLogTest, TheOldAreaTakesOnlyNamelessOrSystemMessages)
{
    const LetterMeasure letters;
    ChatLogState log;
    Client::UI::MessageAreaState area;
    const Client::UI::MessageSink sink{
        .log = log,
        .area = area,
        .modes = ChatModes{},
        .measure = ChatMeasure{.measure = letters, .dcFont = FontKind::Normal, .screen = Screen640}};

    ASSERT_TRUE(Client::UI::AddMessage(sink, "Ana", "hola", 0).HasValue());
    EXPECT_EQ(area.count, 0);
    ASSERT_TRUE(Client::UI::AddMessage(sink, "Ana", "aviso", 1).HasValue());
    ASSERT_TRUE(Client::UI::AddMessage(sink, "", "suelto", 0).HasValue());
    EXPECT_EQ(area.count, 2);
    EXPECT_EQ(area.rows[0].text, "aviso");
    EXPECT_EQ(area.rows[1].text, "suelto");
    EXPECT_EQ(log.entries.size(), 3u);
    EXPECT_EQ(area.clock, Client::UI::MessageClockSteps);
}

// 0x5554b0: 151 pasos desde 150 hasta el mensaje vacío.
TEST(MessageLogTest, TheClockAddsAnEmptyMessageEvery151Steps)
{
    const LetterMeasure letters;
    ChatLogState log;
    log.entries.push_back(Entry("Ana", "hola", 0));
    Client::UI::MessageAreaState area;
    area.clock = Client::UI::MessageClockSteps;
    const Client::UI::MessageSink sink{
        .log = log,
        .area = area,
        .modes = ChatModes{},
        .measure = ChatMeasure{.measure = letters, .dcFont = FontKind::Normal, .screen = Screen640}};

    for (int step = 0; step < 150; ++step)
        ASSERT_TRUE(Client::UI::StepTheMessageClock(sink).HasValue());
    EXPECT_EQ(log.entries[0].age, 0u);
    EXPECT_EQ(area.count, 0);
    ASSERT_TRUE(Client::UI::StepTheMessageClock(sink).HasValue());
    EXPECT_EQ(log.entries[0].age, 1u);
    EXPECT_EQ(area.count, 1);
    EXPECT_EQ(area.clock, Client::UI::MessageClockSteps);
}
} // namespace
