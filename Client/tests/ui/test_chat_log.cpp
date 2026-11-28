#include "Chat/ChatLog.h"
#include "Ui/MessageArea.h"
#include "Ui/MessageLog.h"

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// The chat log (0x576350c), the old area (0x77d9868) and its entry point 0x555190 (CL8 milestone 3; 06 § El chat).
// The texts are invented; the arithmetic is the ASM's: 0x45bb20, 0x459c70, 0x45bfe0, 0x47a050, 0x45d8c0, 0x45dc40,
// 0x45ddc0, 0x45de20 and 0x45db70.
namespace
{
using Client::Chat::ChatEntry;
using Client::Chat::ChatLogState;
using Client::Chat::ChatMeasure;
using Client::Chat::ChatModes;
using Client::Core::PixelSize;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

// Like the fake rasterizer: 6 columns per letter and 13 rows.
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

// Always measures the same, for 0x459d21's division by zero.
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
        const auto added = Client::Chat::AddToTheLog(m_log, m_modes, Measure(), name, text, type, 0, noticeSound);
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

// 0x459c70: 40 letters measure 240 > 180; 180 / 6 = 30 letters, which measure 180 (within ±4). The second half,
// without the name, goes before the first.
TEST_F(ChatLogTest, ALongMessageIsCutInTwoAt180AndTheSecondHalfLosesTheName)
{
    const std::string text(40, 'a');
    Add("Ana", text, 0);

    ASSERT_EQ(m_log.entries.size(), 2u);
    EXPECT_EQ(m_log.entries[0], Entry("", std::string(10, 'a'), 0));
    EXPECT_EQ(m_log.entries[1], Entry("Ana", std::string(30, 'a'), 0));
}

// ~len(name) + ~len(text) − 2 < 0x14: 19 bytes go in whole even if they measure more than 180.
TEST_F(ChatLogTest, NineteenBytesBetweenNameAndTextAreNeverCut)
{
    const LetterMeasure wide(20);
    const ChatMeasure measure{.measure = wide, .dcFont = FontKind::Normal, .screen = Screen640};
    const auto added = Client::Chat::AddToTheLog(m_log, m_modes, measure, "Ana", std::string(16, 'b'), 0, 0, false);

    ASSERT_TRUE(added.HasValue()) << added.Error();
    ASSERT_EQ(m_log.entries.size(), 1u);
    EXPECT_EQ(m_log.entries[0].text, std::string(16, 'b'));
}

// 0x459d21: a long text that measures less than a point per letter divides by zero in the original.
TEST_F(ChatLogTest, ATextOfLettersNarrowerThanAPointIsTheOriginalsDivisionByZero)
{
    const FixedMeasure fixed;
    const ChatMeasure measure{.measure = fixed, .dcFont = FontKind::Normal, .screen = Screen640};
    const auto added = Client::Chat::AddToTheLog(m_log, m_modes, measure, "Ana", std::string(300, 'c'), 0, 0, false);

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

// 0x45dc40, 0x45ddc0 and 0x45bb20's 0x26 with the filter set and 0x77f3b78.
TEST_F(ChatLogTest, TheFilterLetsInOnlyNormalChatWithOneOfItsWordsAndRings)
{
    EXPECT_EQ(Client::Chat::SetTheChatFilter(m_log, "/filter dragon alas"), Client::Chat::FilterOnText);
    EXPECT_EQ(Client::Chat::JoinedChatFilter(m_log), "dragon, alas");

    Add("Ana", "vendo escudo", 3);
    EXPECT_TRUE(m_log.entries.empty());

    const auto rung = Client::Chat::AddToTheLog(m_log, m_modes, Measure(), "Ana", "compro alas", 3, 0, true);
    ASSERT_TRUE(rung.HasValue()) << rung.Error();
    EXPECT_EQ(rung.Value(), std::optional{Client::Chat::ChatFilterSound});
    // The other types do not go through the filter.
    Add("Ana", "vendo escudo", 0);
    EXPECT_EQ(m_log.entries.size(), 2u);

    EXPECT_EQ(Client::Chat::SetTheChatFilter(m_log, "/filter"), Client::Chat::FilterOffText);
    EXPECT_EQ(Client::Chat::SetTheChatFilter(m_log, "/filter"), std::nullopt);
}

// The lengths of all the words must add up to less than 31; the first one that does not fit cuts them all.
TEST_F(ChatLogTest, TheFilterWordsMustAddUpToLessThan31Bytes)
{
    EXPECT_EQ(Client::Chat::SetTheChatFilter(m_log, "/filter " + std::string(31, 'x')), std::nullopt);
    EXPECT_TRUE(m_log.filter[0].empty());

    EXPECT_EQ(Client::Chat::SetTheChatFilter(m_log, "/filter " + std::string(20, 'x') + " " + std::string(11, 'y')),
              Client::Chat::FilterOnText);
    EXPECT_EQ(m_log.filter[0], std::string(20, 'x'));
    EXPECT_TRUE(m_log.filter[1].empty());
}

// Both empty, with the log outside its window and not scrolled: every entry ages.
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

// 0x47a050: from 150 on it drops the oldest.
TEST_F(ChatLogTest, TheLogKeeps150)
{
    for (int i = 0; i < 151; ++i)
        Add("Ana", "uno", 0);
    EXPECT_EQ(m_log.entries.size(), 150u);
}

// 0x45bfe0 with −6, not scrolled and outside its window: counts rows with their ages up to 6.
TEST_F(ChatLogTest, TheAgedScrollCountsRowsWithTheirAges)
{
    for (int i = 0; i < 20; ++i)
        m_log.entries.push_back(Entry("Ana", "uno", 0));
    m_log.entries[2].age = 3;

    Client::Chat::ScrollTheLog(m_log, m_modes, -6);

    // 1, 2, 2 + 1 + 3 = 6: the third.
    EXPECT_EQ(m_log.scroll, 3);
}

TEST_F(ChatLogTest, ScrollingStopsAtTheOldestRows)
{
    for (int i = 0; i < 10; ++i)
        m_log.entries.push_back(Entry("Ana", "uno", 0));
    m_modes.placement = 1;

    Client::Chat::ScrollTheLog(m_log, m_modes, -100);
    EXPECT_EQ(m_log.scroll, 4);
    Client::Chat::ScrollTheLog(m_log, m_modes, 100);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x45d8c0, F4 and the second button: (0, 6) → (1, 3) → (1, 6) → (1, 30) → (0, 6).
TEST_F(ChatLogTest, F4CyclesTheSizes)
{
    Client::Chat::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_modes.placement, 1);
    EXPECT_EQ(m_log.rows, 3);
    Client::Chat::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_log.rows, 6);
    Client::Chat::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_log.rows, 30);
    Client::Chat::CycleTheLogSize(m_log, m_modes);
    EXPECT_EQ(m_modes.placement, 0);
    EXPECT_EQ(m_log.rows, 6);
}

// 0x45db70: among the first n, one with the same first 16 bytes or with an age.
TEST_F(ChatLogTest, SaidRecentlyLooksAtTheFirst16BytesOrAnAge)
{
    Add("Ana", "abcdefghijklmnopXYZ", 0);
    EXPECT_TRUE(Client::Chat::SaidRecently(m_log, "abcdefghijklmnop", 1));
    EXPECT_FALSE(Client::Chat::SaidRecently(m_log, "otra cosa", 1));
    m_log.entries[0].age = 1;
    EXPECT_TRUE(Client::Chat::SaidRecently(m_log, "otra cosa", 1));
}

// An 11-byte name has no '\0' and reads on into the text.
TEST_F(ChatLogTest, AnElevenByteNameRunsIntoTheText)
{
    Add("Abcdefghijkl", "hola", 0);
    EXPECT_EQ(m_log.entries[0].name, "Abcdefghijk");
    EXPECT_EQ(Client::Chat::NameOf(m_log.entries[0]), "Abcdefghijkhola");
}

// 0x555190: with an empty name or a system type, also to the old area; other people's chat, not.
TEST(MessageLogTest, TheOldAreaTakesOnlyNamelessOrSystemMessages)
{
    const LetterMeasure letters;
    ChatLogState log;
    Client::Ui::MessageAreaState area;
    const Client::Ui::MessageSink sink{
        .log = log,
        .area = area,
        .modes = ChatModes{},
        .measure = ChatMeasure{.measure = letters, .dcFont = FontKind::Normal, .screen = Screen640}};

    ASSERT_TRUE(Client::Ui::AddMessage(sink, "Ana", "hola", 0).HasValue());
    EXPECT_EQ(area.count, 0);
    ASSERT_TRUE(Client::Ui::AddMessage(sink, "Ana", "aviso", 1).HasValue());
    ASSERT_TRUE(Client::Ui::AddMessage(sink, "", "suelto", 0).HasValue());
    EXPECT_EQ(area.count, 2);
    EXPECT_EQ(area.rows[0].text, "aviso");
    EXPECT_EQ(area.rows[1].text, "suelto");
    EXPECT_EQ(log.entries.size(), 3u);
    EXPECT_EQ(area.clock, Client::Ui::MessageClockSteps);
}

// 0x5554b0: 151 steps from 150 to the empty message.
TEST(MessageLogTest, TheClockAddsAnEmptyMessageEvery151Steps)
{
    const LetterMeasure letters;
    ChatLogState log;
    log.entries.push_back(Entry("Ana", "hola", 0));
    Client::Ui::MessageAreaState area;
    area.clock = Client::Ui::MessageClockSteps;
    const Client::Ui::MessageSink sink{
        .log = log,
        .area = area,
        .modes = ChatModes{},
        .measure = ChatMeasure{.measure = letters, .dcFont = FontKind::Normal, .screen = Screen640}};

    for (int step = 0; step < 150; ++step)
        ASSERT_TRUE(Client::Ui::StepTheMessageClock(sink).HasValue());
    EXPECT_EQ(log.entries[0].age, 0u);
    EXPECT_EQ(area.count, 0);
    ASSERT_TRUE(Client::Ui::StepTheMessageClock(sink).HasValue());
    EXPECT_EQ(log.entries[0].age, 1u);
    EXPECT_EQ(area.count, 1);
    EXPECT_EQ(area.clock, Client::Ui::MessageClockSteps);
}
} // namespace
