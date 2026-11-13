#include "UI/GameDialogs.h"

#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::UI
{
namespace
{
// 0x632e30: el botón del aviso.
constexpr DialogButton MessageBoxButton{.state = 1, .x = 0x47, .y = 0x8c, .width = 0x46, .height = 0x15};
// 0x63374f..0x633779: los dos de 0x633300; el segundo arranca en 3.
constexpr DialogButton FirstTextButton{.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
constexpr DialogButton SecondTextButton{.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
// 0x6337ea: la y de los dos en el 0x9a.
constexpr std::int32_t ChaosCastleButtonsY = 0x78;
// 0x633794..0x6337e3: los tres del 0xa8.
constexpr DialogButton LuckFirstButton{.state = 1, .x = 0x16, .y = 0x5c, .width = 0x31, .height = 0x10};
constexpr DialogButton LuckSecondButton{.state = 3, .x = 0x52, .y = 0x5c, .width = 0x31, .height = 0x10};
constexpr DialogButton LuckThirdButton{.state = 5, .x = 0x8e, .y = 0x5c, .width = 0x31, .height = 0x10};
// 0x63364e..0x6336b8: la fruta (0x1a0f) y, por su nivel de 0 a 4 (la tabla de 0x633888), los textos de su nombre.
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::array<std::int64_t, 5> FruitNameTexts{168, 169, 167, 166, 1900};
// 0x6336d4: el texto que el 0xa8 pone detrás del nombre.
constexpr std::int64_t LuckText = 1901;
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
// 0x63344c..0x633564: los textos de la mezcla de joyas del 0xa3: la joya (bendición o alma), la pregunta al juntar y
// al deshacer, y el precio de cada una.
constexpr std::int64_t BlessJewelText = 1806;
constexpr std::int64_t SoulJewelText = 1807;
constexpr std::int64_t MixQuestionText = 1809;
constexpr std::int64_t MixPriceText = 1810;
constexpr std::int64_t UnmixQuestionText = 1813;
constexpr std::int64_t UnmixPriceText = 1814;
// 0x633419..0x63342e y 0x6334a7: la pregunta se parte en la pila en 2 renglones de 0x1e bytes ([ebp − 0x64]); el
// sprintf va antes a los 300 bytes de [ebp − 0x190].
constexpr std::size_t JewelRows = 2;
constexpr std::size_t JewelRowBytes = 0x1e;
constexpr std::size_t JewelQuestionBytes = 300;
// 0x62d779..0x62d7a4: el corrimiento del rectángulo de los botones.
constexpr std::int32_t ButtonOffsetX = 0xd5;
constexpr std::int32_t ButtonOffsetY = 0x3c;
// 0x62d80c..0x62d888: las flechas del 0x8c, con los bordes.
constexpr std::int32_t PreviousLeft = 0xf9;
constexpr std::int32_t PreviousRight = 0x108;
constexpr std::int32_t NextLeft = 0x179;
constexpr std::int32_t NextRight = 0x188;
constexpr std::int32_t ArrowsTop = 0xca;
constexpr std::int32_t ArrowsBottom = 0xde;
// 0x419713 y 0x632f73: el lugar del wsprintf de cada respuesta. "10) " y 63 bytes con su '\0' entran.
constexpr std::size_t AnswerBufferBytes = 0x48;

// 0x632c90 sobre el renglón `firstRow`: cada renglón con su '\0'. Devuelve cuántos escribió.
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

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::int64_t index,
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

// 0x69fd9b(renglón, formato, ...) desde el renglón `row`: lo que pase de los 0x26 bytes sigue en el siguiente, y lo
// que pase de los 7 corta.
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

// 0x63332f..0x6333a3: el número con sus puntos en el texto `firstText`, y los `lineCount − 1` siguientes debajo.
Core::Expected<void, std::string> WriteTheMoneyText(DialogBoxState& box, const Data::Text::TextTable* texts,
                                                    std::int32_t lineCount, std::int32_t firstText,
                                                    std::string_view money)
{
    auto format = TextOf(texts, firstText, "0x633367");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    if (auto printed = PrintIntoRow(box, 0, Sprintf(format.Value(), money), "0x633367"); !printed.HasValue())
        return printed;
    for (std::int32_t row = 1; row < lineCount; ++row)
    {
        auto text = TextOf(texts, static_cast<std::int64_t>(firstText) + row, "0x633387");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto printed = PrintIntoRow(box, static_cast<std::size_t>(row), Sprintf(text.Value()), "0x633387");
            !printed.HasValue())
            return printed;
    }
    box.lineCount = lineCount;
    return {};
}

// 0x633419..0x633525: la pregunta de la mezcla de joyas y su precio.
Core::Expected<void, std::string> WriteTheJewelMixText(DialogBoxState& box, const Data::Text::TextTable* texts,
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
    // 0x63345d: las contadas (un byte); 0x63352f: el nivel + 1.
    const std::string asked =
        mixing ? Sprintf(question.Value(), name.Value(), static_cast<std::int32_t>(arguments.jewelCount))
               : Sprintf(question.Value(), name.Value(), arguments.jewelLevel + 1);
    if (asked.size() + 1 > JewelQuestionBytes)
        return Core::Unexpected(
            std::format("0x63348a: el sprintf de {} bytes pasa los 300 de la pila de 0x633300", asked.size() + 1));
    // 0x6334ad: a lo sumo 2 renglones en la pila; los que no escribe quedan vacíos (sus primeros 20 bytes en 0).
    const std::vector<std::string> lines = SplitIntoLines(asked, JewelRows, JewelRowBytes);
    for (std::size_t row = 0; row < JewelRows; ++row)
    {
        const std::string_view line = row < lines.size() ? std::string_view(lines[row]) : std::string_view{};
        if (auto copied = PrintIntoRow(box, row, line, "0x6334f4"); !copied.HasValue())
            return copied;
    }
    const auto rows = static_cast<std::int32_t>(lines.size());
    if (auto printed =
            PrintIntoRow(box, static_cast<std::size_t>(rows), Sprintf(price.Value(), arguments.jewelPrice), "0x633517");
        !printed.HasValue())
        return printed;
    box.lineCount = rows + 1;
    return {};
}

// 0x633644..0x633739: el nombre del ítem de los diálogos en el primer renglón, sprintf de "( %s )" (o de "( %s%s )"
// con el 1901 en el 0xa8) desde el principio de 0x7d36a98, y `lineCount` veces el texto `firstText` debajo (EBX no
// avanza en el lazo de 0x633729, Q-C922; los dos que llaman pasan 1). lineCount + 1 renglones.
Core::Expected<void, std::string> WriteTheItemName(DialogBoxState& box, const Data::Text::TextTable* texts,
                                                   std::int32_t lineCount, std::int32_t firstText, std::int32_t id,
                                                   const World::ItemState& item)
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
// 0x419710 y 0x632f70 hasta antes de su final: el texto, las respuestas y su cuenta.
Core::Expected<void, std::string> WriteTheDialog(DialogBoxState& box, const Data::Text::DialogTable* table,
                                                 const Data::Text::TextTable* texts, std::int32_t index,
                                                 std::string_view where)
{
    if (table == nullptr || table->size() != Data::Text::DialogCount)
        return Core::Unexpected(std::format("{}: sin Dialog.bmd", where));
    if (index < 0 || static_cast<std::size_t>(index) >= Data::Text::DialogCount)
        return Core::Unexpected(std::format("{}: el diálogo {} está fuera de los 200 de 0x7688ac0", where, index));
    const Data::Text::DialogRecord& dialog = (*table)[static_cast<std::size_t>(index)];
    box.dialogIndex = index;
    box.lineCount = SplitIntoRows(box, 0, Data::Text::DialogTextOf(dialog), where).Value();
    box.answers.fill('\0');
    box.answerCount = 0;
    std::int32_t answer = 0;
    // 0x41975f..0x4197d5: la cuenta se vuelve a leer en cada vuelta (LoadDialogTable no deja más de 10).
    for (; answer < dialog.answerCount; ++answer)
    {
        const std::string line =
            std::format("{}) {}", answer + 1, Data::Text::DialogAnswerOf(dialog, static_cast<std::size_t>(answer)));
        const std::vector<std::string> split = SplitIntoLines(line, 1, DialogRowBytes);
        const auto start = static_cast<std::ptrdiff_t>(static_cast<std::size_t>(answer) * DialogRowBytes);
        auto out = std::copy(split.front().begin(), split.front().end(), box.answers.begin() + start);
        *out = '\0';
        ++box.answerCount;
    }
    // 0x4197db..0x419829: con la cuenta en 0 (no con una negativa), "%d) %s" con el 609 y strcpy al primer renglón.
    if (dialog.answerCount == 0)
    {
        auto read = TextOf(texts, static_cast<std::int64_t>(NoAnswersText), where);
        if (!read.HasValue())
            return Core::Unexpected(std::move(read).Error());
        const std::string line = std::format("{}) {}", answer + 1, read.Value());
        // El wsprintf va a los 0x48 bytes de la pila (EBP − 0x48); el strcpy, desde el primer renglón, entra siempre.
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
                                                     const Data::Text::DialogTable* table,
                                                     const Data::Text::TextTable* texts, std::int32_t index)
{
    if (auto written = WriteTheDialog(box, table, texts, index, "0x419710"); !written.HasValue())
        return written;
    // 0x41982d: 0x6236f0(0).
    PushDialog(dialogs, 0);
    return {};
}

Core::Expected<void, std::string> OpenTheAnswersDialog(DialogBoxState& box, DialogState& dialogs,
                                                       const Data::Text::DialogTable* table,
                                                       const Data::Text::TextTable* texts, std::int32_t index)
{
    // 0x632f90..0x632fa3: los botones en 0 entre el texto y las respuestas (nada de lo que sigue los lee).
    ResetTheButtons(box);
    if (auto written = WriteTheDialog(box, table, texts, index, "0x632f70"); !written.HasValue())
        return written;
    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = AnswersDialog;
    return {};
}

void OpenMessageBox(DialogBoxState& box, DialogState& dialogs, std::string_view text)
{
    // 0x632c90 sobre el primer renglón nunca pasa los 7.
    box.lineCount = SplitIntoRows(box, 0, text, "0x632e3e").Value();
    ResetTheButtons(box);
    box.buttons[0] = MessageBoxButton;
    (dialogs.current != 0 ? dialogs.pending : dialogs.current) = MessageBoxDialog;
}

Core::Expected<void, std::string> OpenTextDialog(DialogBoxState& box, DialogState& dialogs,
                                                 const Data::Text::TextTable* texts, std::int32_t lineCount,
                                                 std::int32_t firstText, std::int32_t id,
                                                 const World::ItemState* dialogItem,
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
    {
        if (arguments == nullptr)
            return Core::Unexpected(std::string("0x63333d: el texto del diálogo 0x99 sin el número de 0x77f3b6c"));
        if (auto written = WriteTheMoneyText(box, texts, lineCount, firstText, arguments->money); !written.HasValue())
            return written;
    }
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
        // 0x633600..0x633638: un strcpy por texto, de a 0x26 bytes; uno largo sigue en el renglón siguiente.
        for (std::int32_t row = 0; row < lineCount; ++row)
        {
            auto read = TextOf(texts, static_cast<std::int64_t>(firstText) + row, "0x633612");
            if (!read.HasValue())
                return Core::Unexpected(std::move(read).Error());
            const std::string_view text = read.Value();
            const std::size_t start = static_cast<std::size_t>(row) * DialogRowBytes;
            if (start + text.size() + 1 > box.rows.size())
                return Core::Unexpected(std::format("0x633612: el strcpy del texto {} en el renglón {} pasa los 7 de "
                                                    "0x7d36a98",
                                                    static_cast<std::int64_t>(firstText) + row, row));
            auto out = std::copy(text.begin(), text.end(), box.rows.begin() + static_cast<std::ptrdiff_t>(start));
            *out = '\0';
        }
        box.lineCount = lineCount;
    }
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

Core::Expected<void, std::string> StepTheDialogPages(DialogBoxState& box, const Data::Text::TextTable* texts,
                                                     Point mouse, bool click)
{
    const bool onTheArrows = mouse.y >= ArrowsTop && mouse.y <= ArrowsBottom;
    if (click && onTheArrows && mouse.x >= PreviousLeft && mouse.x <= PreviousRight)
    {
        // 0x62d834..0x62d846: DEC de un char con signo; 0 o menos queda en 0.
        const auto previous = static_cast<std::int8_t>(box.page - 1);
        box.page = previous > 0 ? previous : std::int8_t{0};
    }
    else if (click && onTheArrows && mouse.x >= NextLeft && mouse.x <= NextRight)
    {
        // 0x62d89d..0x62d8b5: INC de un char con signo; desde la última, la última.
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
} // namespace Client::UI
