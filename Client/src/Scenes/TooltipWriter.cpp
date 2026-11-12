#include "Scenes/TooltipWriter.h"

#include "Data/Text/TextTable.h"
#include "Scenes/TooltipLines.h"
#include "UI/GameText.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
TooltipWriter::TooltipWriter(TooltipLines& lines, const Data::Text::TextTable* texts) : m_lines(lines), m_texts(texts)
{
}

void TooltipWriter::At(std::string_view where)
{
    m_where = where;
}

bool TooltipWriter::Failed() const
{
    return !m_error.empty();
}

const std::string& TooltipWriter::Error() const
{
    return m_error;
}

void TooltipWriter::Fail(std::string error)
{
    if (m_error.empty())
        m_error = std::move(error);
}

std::string_view TooltipWriter::Text(std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(m_texts, index);
    if (!text)
    {
        Fail(std::format("{}: sin el texto {} de Text.bmd", m_where, index));
        return {};
    }
    return *text;
}

bool TooltipWriter::Inside()
{
    if (Failed())
        return false;
    if (m_lines.next >= 0 && m_lines.next < static_cast<std::int32_t>(TooltipLineCount))
        return true;
    Fail(std::format("{}: el renglón {} del tooltip cae fuera de los 30 de 0x78793c8 (el original escribe sobre lo que "
                     "sigue, el ítem del cursor en 0x7879f80)",
                     m_where, m_lines.next));
    return false;
}

std::string* TooltipWriter::Current()
{
    if (!Inside())
        return nullptr;
    return &m_lines.text[static_cast<std::size_t>(m_lines.next)];
}

void TooltipWriter::Color(std::int32_t color)
{
    if (Inside())
        m_lines.colors[static_cast<std::size_t>(m_lines.next)] = color;
}

void TooltipWriter::Bold(std::int32_t bold)
{
    if (Inside())
        m_lines.bold[static_cast<std::size_t>(m_lines.next)] = bold;
}

void TooltipWriter::Next()
{
    if (!Failed())
        ++m_lines.next;
}

void TooltipWriter::Back()
{
    if (!Failed())
        --m_lines.next;
}

void TooltipWriter::AddHalf()
{
    if (!Failed())
        ++m_lines.halves;
}

void TooltipWriter::NewLine()
{
    Print("\n");
    Next();
    AddHalf();
}

std::int32_t TooltipWriter::Count() const
{
    return m_lines.next;
}

void TooltipWriter::SetCount(std::int32_t count)
{
    if (!Failed())
        m_lines.next = count;
}

std::int32_t TooltipWriter::Halves() const
{
    return m_lines.halves;
}

TooltipLines& TooltipWriter::Lines()
{
    return m_lines;
}

const Data::Text::TextTable* TooltipWriter::Texts() const
{
    return m_texts;
}
} // namespace Client::Scenes
