#include "Ui/OldEditLines.h"

#include "Core/PixelSize.h"
#include "Core/Windows1252BestFit.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowDrawParts.h"

#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::UI
{
namespace
{
using OldDraw::RatioX;
using OldDraw::RatioY;
using OldDraw::Truncate;

// 0x6c7af0: lo que mide 0x467fe0 para +0xd0.
constexpr std::string_view LineSample = "1";
// 0x6b95b4: −5, lo que 0x467fe0 le saca al ancho de la ventana (ftol(−5 · rx)).
constexpr double WindowSlack = -5.0;
// SM_CXVSCROLL: el ancho de la barra de WS_VSCROLL, que se come el área del texto (*inferido*: el de Windows XP con
// el tema clásico).
constexpr std::int32_t WindowsBarWidth = 16;
constexpr std::string_view LineBreak = "\r\n";
constexpr char Space = ' ';

bool Fits(const Render::Text::ITextMeasure& measure, std::string_view text, std::int32_t width)
{
    return measure.Measure(EditFont, text).width <= width;
}

// Cuántas letras del principio de `text` entran en `width`, por bisección: el ancho crece con las letras.
std::size_t FittingPrefix(const Render::Text::ITextMeasure& measure, std::string_view text, std::int32_t width)
{
    std::size_t low = 0;
    std::size_t high = text.size();
    while (low < high)
    {
        const std::size_t middle = low + (high - low + 1) / 2;
        if (Fits(measure, text.substr(0, middle), width))
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}

// Un párrafo (sin "\r\n") en renglones del ancho del área del texto.
void WrapTheParagraph(std::string_view paragraph, const Render::Text::ITextMeasure& measure, std::int32_t width,
                      std::vector<std::string_view>& lines)
{
    std::string_view rest = paragraph;
    while (true)
    {
        if (rest.empty() || Fits(measure, rest, width))
        {
            lines.push_back(rest);
            return;
        }
        const std::size_t fit = FittingPrefix(measure, rest, width);
        std::size_t cut = 0;
        if (rest[fit] == Space)
        {
            // Los espacios que no entran quedan en el renglón.
            cut = rest.find_first_not_of(Space, fit);
            if (cut == std::string_view::npos)
            {
                lines.push_back(rest);
                return;
            }
        }
        else
        {
            const std::size_t space = fit == 0 ? std::string_view::npos : rest.rfind(Space, fit - 1);
            cut = space != std::string_view::npos ? space + 1 : std::max<std::size_t>(fit, 1);
        }
        lines.push_back(rest.substr(0, cut));
        rest.remove_prefix(cut);
    }
}

std::int32_t MostFirstLine(const OldEdit& edit, std::int32_t lineCount)
{
    return std::max(lineCount - edit.visibleLines, 0);
}
} // namespace

void MakeTheEditMultiline(OldEdit& edit, const Render::Text::ITextMeasure& measure, Render::Text::FontKind dcFont,
                          Core::PixelSize screen)
{
    const double rx = RatioX(screen);
    const double ry = RatioY(screen);
    edit.multiline = 1;
    // FILD h, FMUL ry, FIDIV cy: con cy en 0 sería la división por 0 del original; la medida da siempre un alto.
    const std::int32_t height = measure.Measure(dcFont, LineSample).height;
    edit.visibleLines = height != 0 ? Truncate(static_cast<double>(edit.base.height) * ry / height) : 0;
    const std::int32_t slack = Truncate(WindowSlack * rx);
    edit.textWidth = Truncate(static_cast<double>(edit.base.width + slack) * rx) - WindowsBarWidth - 2 * EditTextMargin;
}

std::vector<std::string_view> EditLines(const OldEdit& edit, const Render::Text::ITextMeasure& measure)
{
    const std::string_view text = edit.text.Value();
    std::vector<std::string_view> lines;
    if (edit.multiline != 1)
    {
        lines.push_back(text);
        return lines;
    }
    std::size_t from = 0;
    while (true)
    {
        const std::size_t end = text.find(LineBreak, from);
        const std::string_view paragraph =
            text.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from);
        WrapTheParagraph(paragraph, measure, edit.textWidth, lines);
        if (end == std::string_view::npos)
            return lines;
        from = end + LineBreak.size();
    }
}

std::int32_t EditLineCount(const OldEdit& edit, const Render::Text::ITextMeasure& measure)
{
    return static_cast<std::int32_t>(EditLines(edit, measure).size());
}

void ScrollTheEditLines(OldEdit& edit, std::int32_t lineCount, std::int32_t lines)
{
    if (edit.multiline != 1)
        return;
    edit.firstLine = std::clamp(edit.firstLine + lines, 0, MostFirstLine(edit, lineCount));
}

void ScrollTheEdit(OldEdit& edit, std::int32_t lineCount, std::int32_t code)
{
    switch (code)
    {
    case EditLineUp:
        ScrollTheEditLines(edit, lineCount, -1);
        return;
    case EditLineDown:
        ScrollTheEditLines(edit, lineCount, 1);
        return;
    case EditPageUp:
        ScrollTheEditLines(edit, lineCount, -std::max(edit.visibleLines, 1));
        return;
    case EditPageDown:
        ScrollTheEditLines(edit, lineCount, std::max(edit.visibleLines, 1));
        return;
    default:
        return;
    }
}

std::int32_t EditCaretLine(const OldEdit& edit, std::span<const std::string_view> lines)
{
    const std::string_view text = edit.text.Value();
    const std::size_t cursor = edit.text.Cursor();
    std::int32_t caretLine = 0;
    for (std::size_t at = 0; at < lines.size(); ++at)
    {
        const auto start = static_cast<std::size_t>(lines[at].data() - text.data());
        if (start > cursor)
            break;
        caretLine = static_cast<std::int32_t>(at);
    }
    return caretLine;
}

void FollowTheEditCaret(OldEdit& edit, std::int32_t lineCount, std::int32_t caretLine)
{
    if (!edit.followCaret)
        return;
    edit.followCaret = false;
    if (edit.multiline != 1)
        return;
    if (caretLine < 0)
        caretLine = lineCount - 1;
    if (caretLine < edit.firstLine)
        edit.firstLine = caretLine;
    else if (caretLine >= edit.firstLine + edit.visibleLines)
        edit.firstLine = caretLine - edit.visibleLines + 1;
    edit.firstLine = std::clamp(edit.firstLine, 0, MostFirstLine(edit, lineCount));
}

void BackspaceInTheEdit(OldEdit& edit)
{
    edit.draws = 0;
    edit.followCaret = true;
    // El EDIT de Windows borra el "\r\n" entero que está antes del cursor.
    const std::string_view text = edit.text.Value();
    const std::size_t cursor = edit.text.Cursor();
    if (edit.multiline == 1 && !edit.text.HasSelection() && cursor >= LineBreak.size() &&
        text.substr(cursor - LineBreak.size(), LineBreak.size()) == LineBreak)
        edit.text.Backspace();
    edit.text.Backspace();
}

void DeleteInTheEdit(OldEdit& edit)
{
    edit.followCaret = true;
    const std::string_view text = edit.text.Value();
    const std::size_t cursor = edit.text.Cursor();
    if (edit.multiline == 1 && !edit.text.HasSelection() && text.substr(cursor, LineBreak.size()) == LineBreak)
        edit.text.Delete();
    edit.text.Delete();
}

void MoveTheEditCaret(OldEdit& edit, const Render::Text::ITextMeasure& measure, EditCaretKey key, bool shift)
{
    const std::string_view text = edit.text.Value();
    // Entre el '\r' y el '\n' de un fin de renglón.
    const auto insideABreak = [&edit, text]
    {
        const std::size_t at = edit.text.Cursor();
        return at > 0 && at < text.size() && text[at - 1] == '\r' && text[at] == '\n';
    };
    edit.followCaret = true;
    if (key == EditCaretKey::Left)
    {
        edit.text.MoveLeft(shift);
        if (insideABreak())
            edit.text.MoveLeft(shift);
        return;
    }
    if (key == EditCaretKey::Right)
    {
        edit.text.MoveRight(shift);
        if (insideABreak())
            edit.text.MoveRight(shift);
        return;
    }
    const std::vector<std::string_view> lines = EditLines(edit, measure);
    const auto lineCount = static_cast<std::int32_t>(lines.size());
    const std::int32_t caretLine = EditCaretLine(edit, lines);
    const auto startOf = [&](std::int32_t line)
    { return static_cast<std::size_t>(lines[static_cast<std::size_t>(line)].data() - text.data()); };
    const std::string_view current = lines[static_cast<std::size_t>(caretLine)];
    if (key == EditCaretKey::Home)
    {
        edit.text.SetCursor(startOf(caretLine), shift);
        return;
    }
    if (key == EditCaretKey::End)
    {
        edit.text.SetCursor(startOf(caretLine) + current.size(), shift);
        return;
    }
    std::int32_t step = 1;
    if (key == EditCaretKey::PageUp || key == EditCaretKey::PageDown)
        step = std::max(edit.visibleLines, 1);
    if (key == EditCaretKey::Up || key == EditCaretKey::PageUp)
        step = -step;
    const std::int32_t target = std::clamp(caretLine + step, 0, lineCount - 1);
    if (key == EditCaretKey::PageUp || key == EditCaretKey::PageDown)
        ScrollTheEditLines(edit, lineCount, step);
    if (target == caretLine)
        return;
    // La letra del renglón de destino más cerca de la x del cursor.
    const std::size_t column = edit.text.Cursor() - startOf(caretLine);
    const std::int32_t x = measure.Measure(EditFont, current.substr(0, std::min(column, current.size()))).width;
    const std::string_view line = lines[static_cast<std::size_t>(target)];
    std::size_t best = 0;
    std::int32_t bestDistance = x;
    for (std::size_t at = 1; at <= line.size(); ++at)
    {
        const std::int32_t distance = std::abs(measure.Measure(EditFont, line.substr(0, at)).width - x);
        if (distance < bestDistance)
        {
            best = at;
            bestDistance = distance;
        }
    }
    edit.text.SetCursor(startOf(target) + best, shift);
}

void PasteInTheEdit(OldEdit& edit, std::string_view utf8)
{
    edit.draws = 0;
    edit.followCaret = true;
    if (edit.multiline != 1)
    {
        edit.text.PasteUTF8(utf8);
        return;
    }
    std::string lines;
    for (std::size_t at = 0; at < utf8.size(); ++at)
    {
        if (utf8[at] == '\0')
            break;
        if (utf8[at] == '\r' || utf8[at] == '\n')
        {
            lines.append(LineBreak);
            if (utf8[at] == '\r' && at + 1 < utf8.size() && utf8[at + 1] == '\n')
                ++at;
            continue;
        }
        lines.push_back(utf8[at]);
    }
    const auto encoded = Core::EncodeWindows1252Xp(lines);
    if (!encoded.has_value())
        return;
    edit.text.InsertBytes(*encoded);
    // Un tope que corta el "\r\n" por la mitad no deja el '\r' solo.
    const std::string_view text = edit.text.Value();
    const std::size_t cursor = edit.text.Cursor();
    if (cursor > 0 && text[cursor - 1] == '\r' && (cursor == text.size() || text[cursor] != '\n'))
        edit.text.Backspace();
}

void BreakTheEditLine(OldEdit& edit)
{
    edit.draws = 0;
    if (edit.multiline != 1)
        return;
    edit.text.Insert(LineBreak);
    edit.followCaret = true;
}
} // namespace Client::UI
