#pragma once

#include "Tables/TextTable.h"
#include "Tooltips/TooltipLines.h"
#include "Crt/SprintfFormat.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// What the tooltip bodies (06 § Los tooltips, CL8 milestone 5) do with TooltipLines: sprintf on line
// TooltipLines::next (0x78abb30), its color and its bold, advancing the line and the "\n" that also advance the halves
// (0x78abb34). The first error (a text Text.bmd does not have, or a line outside the 30, where the original writes
// over the cursor's item, 0x7879f80) is kept and what follows writes nothing: the caller checks it before anything
// with effects outside the lines (a protected access, the box).
namespace Client::Tooltips
{
class TooltipWriter
{
public:
    // The next line is TooltipLines::next (0x78abb30), as in the item and repair bodies.
    TooltipWriter(TooltipLines& lines, const Tables::TextTable* texts);
    // The next line is `next`: the skill body (0x5b90b0) and the commands' help (0x421c10) count in a register (EBX)
    // and do not touch 0x78abb30 or 0x78abb34.
    TooltipWriter(TooltipLines& lines, const Tables::TextTable* texts, std::int32_t& next);

    // The pets also count the half lines in a local (0x422990).
    TooltipWriter(TooltipLines& lines, const Tables::TextTable* texts, std::int32_t& next, std::int32_t& halves);

    // The address of what is written, for the error.
    void At(std::string_view where);
    [[nodiscard]] bool Failed() const;
    [[nodiscard]] const std::string& Error() const;
    // Keeps the error if there is none yet.
    void Fail(std::string error);

    // Text.bmd's text `index`; without it, empty and the error.
    [[nodiscard]] std::string_view Text(std::size_t index);

    // sprintf(line, format, ...): the next line, with the format of Crt::Sprintf (Q-C590).
    template <typename... Arguments> void Print(std::string_view format, Arguments... arguments)
    {
        if (std::string* line = Current(); line != nullptr)
            *line = Crt::Sprintf(format, arguments...);
    }
    // The same with Text.bmd's text `index` as the format.
    template <typename... Arguments> void PrintText(std::size_t index, Arguments... arguments)
    {
        const std::string_view format = Text(index);
        Print(format, arguments...);
    }
    // The next line's color and bold.
    void Color(std::int32_t color);
    void Bold(std::int32_t bold);
    // The next line moves up or down by one.
    void Next();
    void Back();
    // One more half line (0x78abb34).
    void AddHalf();
    // "\n" on the next line, which advances, and one more half.
    void NewLine();
    // The most common: text `index` as the format, the color, the bold at 0 and the line that advances.
    template <typename... Arguments> void Line(std::int32_t color, std::size_t index, Arguments... arguments)
    {
        PrintText(index, arguments...);
        Color(color);
        Bold(0);
        Next();
    }

    // The next line, or nullptr with the error if it is outside the 30: for whoever writes on it with their own sprintf
    // (0x40d530).
    [[nodiscard]] std::string* Current();

    [[nodiscard]] std::int32_t Count() const;
    void SetCount(std::int32_t count);
    [[nodiscard]] std::int32_t Halves() const;
    [[nodiscard]] TooltipLines& Lines();
    [[nodiscard]] const Tables::TextTable* Texts() const;

private:
    [[nodiscard]] bool Inside();

    TooltipLines& m_lines;
    std::int32_t& m_next;
    std::int32_t& m_halves;
    const Tables::TextTable* m_texts = nullptr;
    std::string m_where;
    std::string m_error;
};
} // namespace Client::Tooltips
