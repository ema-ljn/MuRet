#include "Ui/GameDialogs.h"

#include "Core/Expected.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Ui/Dialogs.h"
#include "Ui/GameText.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Ui
{
namespace
{
// 0x632e30
constexpr DialogButton MessageBoxButton{.state = 1, .x = 0x47, .y = 0x8c, .width = 0x46, .height = 0x15};
// 0x63374f..0x633779: 0x633300's two; the second starts at 3.
constexpr DialogButton FirstTextButton{.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
constexpr DialogButton SecondTextButton{.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
// 0x6337ea
constexpr std::int32_t ChaosCastleButtonsY = 0x78;
// 0x633794..0x6337e3: the 0xa8's three.
constexpr DialogButton LuckFirstButton{.state = 1, .x = 0x16, .y = 0x5c, .width = 0x31, .height = 0x10};
constexpr DialogButton LuckSecondButton{.state = 3, .x = 0x52, .y = 0x5c, .width = 0x31, .height = 0x10};
constexpr DialogButton LuckThirdButton{.state = 5, .x = 0x8e, .y = 0x5c, .width = 0x31, .height = 0x10};
// 0x63364e..0x6336b8: the fruit and, by its level 0 to 4 (the table at 0x633888), its name texts.
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::array<std::int64_t, 5> FruitNameTexts{168, 169, 167, 166, 1900};
// 0x6336d4: the text the 0xa8 puts after the name.
constexpr std::int64_t LuckText = 1901;
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
// 0x63344c..0x633564: the 0xa3's jewel mix texts: the jewel (bless or soul), the question when combining and when
// splitting, and the price of each.
constexpr std::int64_t BlessJewelText = 1806;
constexpr std::int64_t SoulJewelText = 1807;
constexpr std::int64_t MixQuestionText = 1809;
constexpr std::int64_t MixPriceText = 1810;
constexpr std::int64_t UnmixQuestionText = 1813;
constexpr std::int64_t UnmixPriceText = 1814;
// 0x633419..0x63342e and 0x6334a7: the question is split on the stack into 2 rows of 0x1e bytes ([ebp − 0x64]); the
// sprintf goes first to the 300 bytes at [ebp − 0x190].
constexpr std::size_t JewelRows = 2;
constexpr std::size_t JewelRowBytes = 0x1e;
constexpr std::size_t JewelQuestionBytes = 300;
// 0x62d779..0x62d7a4
constexpr std::int32_t ButtonOffsetX = 0xd5;
constexpr std::int32_t ButtonOffsetY = 0x3c;
// 0x62d80c..0x62d888: the 0x8c's arrows, edges included.
constexpr std::int32_t PreviousLeft = 0xf9;
constexpr std::int32_t PreviousRight = 0x108;
constexpr std::int32_t NextLeft = 0x179;
constexpr std::int32_t NextRight = 0x188;
constexpr std::int32_t ArrowsTop = 0xca;
constexpr std::int32_t ArrowsBottom = 0xde;
// 0x419713 and 0x632f73: each answer's wsprintf buffer. "10) " and 63 bytes with their '\0' fit.
constexpr std::size_t AnswerBufferBytes = 0x48;

// 0x632c90 from row `firstRow`: each row with its '\0'. Returns how many it wrote.
Core::Expected<std::int32_t, std::string> SplitIntoRows(DialogBoxState& box, std::size_t firstRow,
                                                        std::string_view text, std::string_view where)
{
    const std::vector<std::string> lines = SplitIntoLines(text, DialogRowCount, DialogRowBytes);
    if (firstRow + lines.size() > DialogRowCount)
        return Core::Unexpected(std::format("{}: 0x632c90 escribe {} renglones desde el {} y pasa los 7 de "
                                            "0x7d36a98",
                                            where, lines.size(), firstRow));
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        const auto start = static_cast<std::ptrdiff_t>((firstRow + line) * DialogRowBytes);
        auto out = std::copy(lines[line].begin(), lines[line].end(), box.rows.begin() + start);
        *out = '\0';
    }
    return static_cast<std::int32_t>(lines.size());
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::int64_t index,
                                                     std::string_view where)
{
    if (texts == nullptr)
        return Core::Unexpected(std::format("{}: sin Text.bmd", where));
    if (index < 0)
        return Core::Unexpected(std::format("{}: el texto {} lee antes de Text.bmd", where, index));
    const std::optional<std::string_view> text = TextAt(texts, static_cast<std::size_t>(index));
    if (!text)
        return Core::Unexpected(std::format("{}: el texto {} lee después de Text.bmd", where, index));
    return *text;
}

// 0x69fd9b(row, format, ...) from row `row`: what passes the 0x26 bytes goes on in the next, and what passes the 7
// fails.
Core::Expected<void, std::string> PrintIntoRow(DialogBoxState& box, std::size_t row, std::string_view text,
                                               std::string_view where)
{
    const std::size_t start = row * DialogRowBytes;
    if (start + text.size() + 1 > box.rows.size())
        return Core::Unexpected(std::format("{}: el sprintf de {} bytes en el renglón {} pasa los 7 de 0x7d36a98",
                                            where, text.size() + 1, row));
    auto out = std::copy(text.begin(), text.end(), box.rows.begin() + static_cast<std::ptrdiff_t>(start));
    *out = '\0';
    return {};
}

// 0x633419..0x633525: the question and its price.
Core::Expected<void, std::string> WriteTheJewelMixText(DialogBoxState& box, const Tables::TextTable* texts,
                                                       const DialogTextArguments& arguments)
{
    const bool mixing = arguments.jewelMode == 0;
    auto name = TextOf(texts, arguments.jewelKind == 0 ? BlessJewelText : SoulJewelText, "0x63344c");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    auto question = TextOf(texts, mixing ? MixQuestionText : UnmixQuestionText, "0x63348a");
    if (!question.HasValue())
        return Core::Unexpected(std::move(question).Error());
    auto price = TextOf(texts, mixing ? MixPriceText : UnmixPriceText, "0x633517");
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    // 0x63345d: the count (one byte); 0x63352f: the level + 1.
    const std::string asked =
        mixing ? Crt::Sprintf(question.Value(), name.Value(), static_cast<std::int32_t>(arguments.jewelCount))
               : Crt::Sprintf(question.Value(), name.Value(), arguments.jewelLevel + 1);
    if (asked.size() + 1 > JewelQuestionBytes)
        return Core::Unexpected(
            std::format("0x63348a: el sprintf de {} bytes pasa los 300 de la pila de 0x633300", asked.size() + 1));
    // 0x6334ad: at most 2 rows on the stack; the ones it does not write stay empty (their first 20 bytes at 0).
    const std::vector<std::string> lines = SplitIntoLines(asked, JewelRows, JewelRowBytes);
    for (std::size_t row = 0; row < JewelRows; ++row)
    {
        const std::string_view line = row < lines.size() ? std::string_view(lines[row]) : std::string_view{};
        if (auto copied = PrintIntoRow(box, row, line, "0x6334f4"); !copied.HasValue())
            return copied;
    }
    const auto rows = static_cast<std::int32_t>(lines.size());
    if (auto printed = PrintIntoRow(box, static_cast<std::size_t>(rows),
                                    Crt::Sprintf(price.Value(), arguments.jewelPrice), "0x633517");
        !printed.HasValue())
        return printed;
    box.lineCount = rows + 1;
    return {};
}

// 0x633644..0x633739: the dialogs' item name in the first row, sprintf of "( %s )" (or "( %s%s )" with the 1901 in
// the 0xa8) from the start of 0x7d36a98, and `lineCount` times the text `firstText` below (EBX does not advance in
// the loop at 0x633729, Q-C922; both callers pass 1). lineCount + 1 rows.
Core::Expected<void, std::string> WriteTheItemName(DialogBoxState& box, const Tables::TextTable* texts,
                                                   std::int32_t lineCount, std::int32_t firstText, std::int32_t id,
                                                   const Kernel::ItemState& item)
{
    std::string name;
    if (const std::uint32_t level = (item.scalar1Low >> LevelShift) & LevelMask;
        item.id == Fruit && level < FruitNameTexts.size())
    {
        auto read = TextOf(texts, FruitNameTexts[level], "0x633670");
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        name = std::string(read.Value());
    }
    std::string first;
    if (id == ItemNameWithLuckDialog)
    {
        auto luck = TextOf(texts, LuckText, "0x6336d4");
        if (!luck.HasValue())
            return Core::Unexpected(std::move(luck).Error());
        first = std::format("( {}{} )", name, luck.Value());
    }
    else
        first = std::format("( {} )", name);
    const auto write = [&box](std::size_t row, std::string_view text,
                              std::string_view where) -> Core::Expected<void, std::string>
    {
        const std::size_t start = row * DialogRowBytes;
        if (start + text.size() + 1 > box.rows.size())
            return Core::Unexpected(
                std::format("{}: {} bytes en el renglón {} pasan los 7 de 0x7d36a98", where, text.size() + 1, row));
        auto out = std::copy(text.begin(), text.end(), box.rows.begin() + static_cast<std::ptrdiff_t>(start));
        *out = '\0';
        return {};
    };
    if (auto written = write(0, first, "0x6336e4"); !written.HasValue())
        return written;
    if (lineCount >= 1)
    {
        auto read = TextOf(texts, static_cast<std::int64_t>(firstText), "0x63372b");
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        for (std::int32_t row = 1; row <= lineCount; ++row)
        {
            if (auto written = write(static_cast<std::size_t>(row), read.Value(), "0x63372b"); !written.HasValue())
                return written;
        }
    }
    box.lineCount = lineCount + 1;
    return {};
}

void ResetTheButtons(DialogBoxState& box)
{
    box.buttons.fill(DialogButton{});
}
} // namespace

std::string_view DialogRow(const DialogBoxState& box, std::size_t row)
{
    if (row >= DialogRowCount)
        return {};
    const std::string_view rest(box.rows.data() + row * DialogRowBytes, box.rows.size() - row * DialogRowBytes);
    return rest.substr(0, rest.find('\0'));
}

std::string_view DialogAnswer(const DialogBoxState& box, std::size_t row)
{
    if (row >= DialogAnswerRows)
        return {};
    const std::string_view rest(box.answers.data() + row * DialogRowBytes, box.answers.size() - row * DialogRowBytes);
    return rest.substr(0, rest.find('\0'));
}

namespace
{
// 0x419710 and 0x632f70 up to before their end: the text, the answers and their count.
Core::Expected<void, std::string> WriteTheDialog(DialogBoxState& box, const Tables::DialogTable* table,
                                                 const Tables::TextTable* texts, std::int32_t index,
                                                 std::string_view where)
{
    if (table == nullptr || table->size() != Tables::DialogCount)
        return Core::Unexpected(std::format("{}: sin Dialog.bmd", where));
    if (index < 0 || static_cast<std::size_t>(index) >= Tables::DialogCount)
        return Core::Unexpected(std::format("{}: el diálogo {} está fuera de los 200 de 0x7688ac0", where, index));
    const Tables::DialogRecord& dialog = (*table)[static_cast<std::size_t>(index)];
    box.dialogIndex = index;
    box.lineCount = SplitIntoRows(box, 0, Tables::DialogTextOf(dialog), where).Value();
    box.answers.fill('\0');
    box.answerCount = 0;
    std::int32_t answer = 0;
    // 0x41975f..0x4197d5: the count is read again each pass (LoadDialogTable allows no more than 10).
    for (; answer < dialog.answerCount; ++answer)
    {
        const std::string line =
            std::format("{}) {}", answer + 1, Tables::DialogAnswerOf(dialog, static_cast<std::size_t>(answer)));
        const std::vector<std::string> split = SplitIntoLines(line, 1, DialogRowBytes);
        const auto start = static_cast<std::ptrdiff_t>(static_cast<std::size_t>(answer) * DialogRowBytes);
        auto out = std::copy(split.front().begin(), split.front().end(), box.answers.begin() + start);
        *out = '\0';
        ++box.answerCount;
    }
    // 0x4197db..0x419829: with the count at 0 (not a negative one), "%d) %s" with the 609 and strcpy to the first row.
    if (dialog.answerCount == 0)
    {
        auto read = TextOf(texts, static_cast<std::int64_t>(NoAnswersText), where);
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        const std::string line = std::format("{}) {}", answer + 1, read.Value());
        // The wsprintf goes to the 0x48 bytes on the stack (EBP − 0x48); the strcpy, from the first row, always fits.
        if (line.size() + 1 > AnswerBufferBytes)
            return Core::Unexpected(
                std::format("{}: el wsprintf de {} bytes pasa los 0x48 de la pila", where, line.size() + 1));
        auto out = std::copy(line.begin(), line.end(), box.answers.begin());
        *out = '\0';
        box.answerCount = 1;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> ShowTheQuestDialog(DialogBoxState& box, DialogState& dialogs,
                                                     const Tables::DialogTable* table, const Tables::TextTable* texts,
                                                     std::int32_t index)
{
    if (auto written = WriteTheDialog(box, table, texts, index, "0x419710"); !written.HasValue())
        return written;
    // 0x41982d: 0x6236f0(0).
    PushDialog(dialogs, 0);
    return {};
}

Core::Expected<void, std::string> OpenTheAnswersDialog(DialogBoxState& box, DialogState& dialogs,
                                                       const Tables::DialogTable* table, const Tables::TextTable* texts,
                                                       std::int32_t index)
{
    // 0x632f90..0x632fa3: buttons zeroed between the text and the answers (nothing after reads them).
    ResetTheButtons(box);
    if (auto written = WriteTheDialog(box, table, texts, index, "0x632f70"); !written.HasValue())
        return written;
    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = AnswersDialog;
    return {};
}

void OpenMessageBox(DialogBoxState& box, DialogState& dialogs, std::string_view text)
{
    // 0x632c90 from the first row never passes the 7.
    box.lineCount = SplitIntoRows(box, 0, text, "0x632e3e").Value();
    ResetTheButtons(box);
    box.buttons[0] = MessageBoxButton;
    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = MessageBoxDialog;
}

namespace
{
// The texts `firstText + row` from `fromRow` to `lineCount` − 1, one strcpy each into its 0x26-byte row; a long one
// goes on in the next row. What would pass the 7 fails, like a missing text.
Core::Expected<void, std::string> CopyTheTexts(DialogBoxState& box, const Tables::TextTable* texts,
                                               std::int32_t firstText, std::int32_t fromRow, std::int32_t lineCount,
                                               std::string_view where)
{
    for (std::int32_t row = fromRow; row < lineCount; ++row)
    {
        auto read = TextOf(texts, static_cast<std::int64_t>(firstText) + row, where);
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        const std::string_view text = read.Value();
        const std::size_t start = static_cast<std::size_t>(row) * DialogRowBytes;
        if (start + text.size() + 1 > box.rows.size())
            return Core::Unexpected(std::format("{}: el strcpy del texto {} en el renglón {} pasa los 7 de 0x7d36a98",
                                                where, static_cast<std::int64_t>(firstText) + row, row));
        auto out = std::copy(text.begin(), text.end(), box.rows.begin() + static_cast<std::ptrdiff_t>(start));
        *out = '\0';
    }
    return {};
}

// 0x633742..0x633816: buttons zeroed, the first and the second (with y = 0x78 in the 0x9a), and from 0x633818 the id
// through 0x6236f0, inline.
void ShowTheTextDialog(DialogBoxState& box, DialogState& dialogs, std::int32_t id)
{
    ResetTheButtons(box);
    box.buttons[0] = FirstTextButton;
    box.buttons[1] = SecondTextButton;
    if (id == ItemNameWithLuckDialog)
    {
        box.buttons[0] = LuckFirstButton;
        box.buttons[1] = LuckSecondButton;
        box.thirdButton = LuckThirdButton;
    }
    else if (id == ChaosCastleDialog)
    {
        box.buttons[0].y = ChaosCastleButtonsY;
        box.buttons[1].y = ChaosCastleButtonsY;
    }
    PushDialog(dialogs, id);
}
} // namespace

Core::Expected<void, std::string> OpenTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                 const Tables::TextTable* texts, std::int32_t lineCount,
                                                 std::int32_t firstText, std::int32_t id,
                                                 const Kernel::ItemState* dialogItem,
                                                 const DialogTextArguments* arguments)
{
    if (id == ItemNameDialog || id == ItemNameWithLuckDialog)
    {
        if (dialogItem == nullptr)
            return Core::Unexpected(std::format("0x63330f: el texto del diálogo {:#x} sin el ítem de 0x78a59c8", id));
        auto written = WriteTheItemName(box, texts, lineCount, firstText, id, *dialogItem);
        if (!written.HasValue())
            return written;
    }
    else if (id == MoneyTextDialog)
        return Core::Unexpected(std::string("0x63333d: el 0x99 escribe el zen tipeado (0x77f3b6c) con 0x5b1130: "
                                            "lo abre OpenTheMoneyTextDialog"));
    if (id == ServerTimeDialog)
    {
        if (arguments == nullptr)
            return Core::Unexpected(std::string("0x63340d: el texto del diálogo 0xa3 sin la mezcla de joyas"));
        if (auto written = WriteTheJewelMixText(box, texts, *arguments); !written.HasValue())
            return written;
    }
    else if (id == ChaosCastleDialog)
    {
        box.lineCount = 0;
        for (std::int32_t text = 0; text < lineCount; ++text)
        {
            auto read = TextOf(texts, static_cast<std::int64_t>(firstText) + text, "0x6333e9");
            if (!read.HasValue())
                return Core::Unexpected(std::move(read).Error());
            auto written = SplitIntoRows(box, static_cast<std::size_t>(box.lineCount), read.Value(), "0x6333e9");
            if (!written.HasValue())
                return Core::Unexpected(std::move(written).Error());
            box.lineCount += written.Value();
        }
    }
    else if (id != ItemNameDialog && id != ItemNameWithLuckDialog && id != MoneyTextDialog)
    {
        // 0x633600..0x633638: one strcpy per text, 0x26 bytes apart; a long one goes on in the next row.
        if (auto copied = CopyTheTexts(box, texts, firstText, 0, lineCount, "0x633612"); !copied.HasValue())
            return copied;
        box.lineCount = lineCount;
    }
    ShowTheTextDialog(box, dialogs, id);
    return {};
}

Core::Expected<void, std::string> OpenTheMoneyTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                         const Tables::TextTable* texts, std::int32_t lineCount,
                                                         std::int32_t firstText, std::string_view zen)
{
    // 0x633342..0x633367: sprintf(0x7d36a98, the text, the written zen), unbounded: a long one goes on in the rows
    // below, which the strcpy of the following ones overwrites.
    auto format = TextOf(texts, static_cast<std::int64_t>(firstText), "0x633367");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    const std::string first = Crt::Sprintf(format.Value(), zen);
    if (first.size() + 1 > box.rows.size())
        return Core::Unexpected(
            std::format("0x633367: el sprintf del texto {} pasa los 7 renglones de 0x7d36a98", firstText));
    auto out = std::copy(first.begin(), first.end(), box.rows.begin());
    *out = '\0';
    // 0x63336c..0x63339e: with more than one row, the following texts from the second, each through the same sprintf
    // without arguments (0x633387), and the count.
    for (std::int32_t row = 1; row < lineCount; ++row)
    {
        auto read = TextOf(texts, static_cast<std::int64_t>(firstText) + row, "0x633387");
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        const std::string line = Crt::Sprintf(read.Value());
        const std::size_t start = static_cast<std::size_t>(row) * DialogRowBytes;
        if (start + line.size() + 1 > box.rows.size())
            return Core::Unexpected(std::format("0x633387: el sprintf del texto {} en el renglón {} pasa los 7 de "
                                                "0x7d36a98",
                                                static_cast<std::int64_t>(firstText) + row, row));
        auto written = std::copy(line.begin(), line.end(), box.rows.begin() + static_cast<std::ptrdiff_t>(start));
        *written = '\0';
    }
    box.lineCount = lineCount;
    ShowTheTextDialog(box, dialogs, MoneyTextDialog);
    return {};
}

Core::Expected<void, std::string> OpenTheChaosCastleDialog(DialogBoxState& box, DialogState& dialogs,
                                                           std::span<const std::string_view> texts)
{
    box.lineCount = 0;
    for (const std::string_view text : texts)
    {
        auto written = SplitIntoRows(box, static_cast<std::size_t>(box.lineCount), text, "0x6333e9");
        if (!written.HasValue())
            return Core::Unexpected(std::move(written).Error());
        box.lineCount += written.Value();
    }
    ResetTheButtons(box);
    box.buttons[0] = FirstTextButton;
    box.buttons[1] = SecondTextButton;
    box.buttons[0].y = ChaosCastleButtonsY;
    box.buttons[1].y = ChaosCastleButtonsY;
    PushDialog(dialogs, ChaosCastleDialog);
    return {};
}

std::int32_t StepTheDialogButtons(DialogBoxState& box, Point mouse, bool& click, bool& leftHeld)
{
    std::int32_t selected = -1;
    for (std::size_t index = 0; index < box.buttons.size(); ++index)
    {
        DialogButton& button = box.buttons[index];
        if (button.state < 1)
            continue;
        const bool inside = mouse.x >= button.x + ButtonOffsetX && mouse.y >= button.y + ButtonOffsetY &&
                            mouse.x <= button.x + button.width + ButtonOffsetX &&
                            mouse.y <= button.y + button.height + ButtonOffsetY;
        if (!inside)
        {
            button.state = 1;
            continue;
        }
        button.state = 2;
        if (click)
        {
            click = false;
            selected = static_cast<std::int32_t>(index);
            leftHeld = false;
        }
    }
    return selected;
}

Core::Expected<void, std::string> StepTheDialogPages(DialogBoxState& box, const Tables::TextTable* texts, Point mouse,
                                                     bool click)
{
    const bool onTheArrows = mouse.y >= ArrowsTop && mouse.y <= ArrowsBottom;
    if (click && onTheArrows && mouse.x >= PreviousLeft && mouse.x <= PreviousRight)
    {
        // 0x62d834..0x62d846: DEC of a signed char; 0 or less stays 0.
        const auto previous = static_cast<std::int8_t>(box.page - 1);
        box.page = previous > 0 ? previous : std::int8_t{0};
    }
    else if (click && onTheArrows && mouse.x >= NextLeft && mouse.x <= NextRight)
    {
        // 0x62d89d..0x62d8b5: INC of a signed char; from the last, the last.
        const auto next = static_cast<std::int8_t>(box.page + 1);
        box.page = next < box.lastPage ? next : box.lastPage;
    }
    else
        return {};
    auto read = TextOf(texts, static_cast<std::int64_t>(box.pageText) + box.page, "0x62d8df");
    if (!read.HasValue())
        return Core::Unexpected(std::move(read).Error());
    auto written = SplitIntoRows(box, 0, read.Value(), "0x62d8df");
    if (!written.HasValue())
        return Core::Unexpected(std::move(written).Error());
    box.lineCount = written.Value();
    return {};
}

std::int32_t DialogTextTop(const DialogBoxState& box, std::int32_t id)
{
    constexpr std::int32_t BoxTextRows = static_cast<std::int32_t>(DialogRowCount);
    constexpr std::int32_t BoxTextTop = 0x4c;
    const std::int32_t top = ((BoxTextRows - box.answerCount - box.lineCount) * DialogLineStep) / 2 + BoxTextTop;
    if (id == AnswersDialog)
        return std::max(top, AnswersDialogTopMinimum);
    return top;
}
} // namespace Client::Ui
