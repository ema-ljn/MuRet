#include "UI/OldEditLines.h"

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/OldControls.h"
#include "UI/OldWindowDrawParts.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
                          App::PixelSize screen)
{
    const double rx = RatioX(screen);
    const double ry = RatioY(screen);
    edit.multiline = 1;
    // FILD h, FMUL ry, FIDIV cy: con cy en 0 sería la división por 0 del original; la medida da siempre un alto.
    const std::int32_t height = measure.Measure(dcFont, LineSample).height;
    edit.visibleLines = height != 0 ? Truncate(static_cast<double>(edit.base.height) * ry / height) : 0;
    const std::int32_t slack = Truncate(WindowSlack * rx);
    edit.textWidth = Truncate(static_cast<double>(edit.base.width + slack) * rx) - WindowsBarWidth;
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

void FollowTheEditCaret(OldEdit& edit, std::int32_t lineCount)
{
    if (!edit.followCaret)
        return;
    edit.followCaret = false;
    if (edit.multiline != 1)
        return;
    const std::int32_t caretLine = lineCount - 1;
    if (caretLine < edit.firstLine)
        edit.firstLine = caretLine;
    else if (caretLine >= edit.firstLine + edit.visibleLines)
        edit.firstLine = caretLine - edit.visibleLines + 1;
    edit.firstLine = std::clamp(edit.firstLine, 0, MostFirstLine(edit, lineCount));
}

void TypeInTheEdit(OldEdit& edit, std::string_view typed)
{
    edit.draws = 0;
    edit.text.Type(typed);
    edit.followCaret = true;
}

void BackspaceInTheEdit(OldEdit& edit)
{
    edit.draws = 0;
    edit.followCaret = true;
    // El EDIT de Windows borra el "\r\n" entero.
    const std::string_view text = edit.text.Value();
    if (edit.multiline == 1 && !edit.text.AllSelected() && text.ends_with(LineBreak))
    {
        edit.text.SetText(std::string(text.substr(0, text.size() - LineBreak.size())));
        return;
    }
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
