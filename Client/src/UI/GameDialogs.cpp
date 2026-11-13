#include "UI/GameDialogs.h"

#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Data/Text/TextTable.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/Widget.h"

#include <algorithm>
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
                                                 std::int32_t firstText, std::int32_t id)
{
    if (id == ItemNameDialog || id == ItemNameWithLuckDialog)
        return Core::Unexpected(std::format("0x63330f: el texto del diálogo {:#x} (el nombre del ítem, 0x78a59c8) es "
                                            "de la CL9.3",
                                            id));
    if (id == MoneyTextDialog)
        return Core::Unexpected(std::string("0x63333d: el texto del diálogo 0x99 (el dinero, 0x5b1130) es de la "
                                            "CL9.3"));
    if (id == ServerTimeDialog)
        return Core::Unexpected(std::string("0x63340d: el texto del diálogo 0xa3 (0x6e5794 y 0x402ae0) es de la "
                                            "CL9.3"));
    if (id == ChaosCastleDialog)
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
    else
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
    if (id == ChaosCastleDialog)
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
} // namespace Client::UI
