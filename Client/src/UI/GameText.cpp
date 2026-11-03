#include "UI/GameText.h"

#include "Data/Text/TextTable.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::UI
{
namespace
{
constexpr char Mark = '%';
// 0x632c90 vuelve hasta un espacio solo si está a menos del menor de la mitad del renglón y este.
constexpr std::size_t MaxBackToSpace = 10;

bool IsArgumentMark(char kind)
{
    return kind == 's' || kind == 'd';
}
} // namespace

std::string PlaceArguments(std::string_view format, std::span<const std::string_view> arguments)
{
    std::string placed;
    placed.reserve(format.size());
    std::size_t next = 0;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        const bool mark = format[i] == Mark && i + 1 < format.size() && IsArgumentMark(format[i + 1]);
        if (mark && next < arguments.size())
        {
            placed.append(arguments[next]);
            ++next;
            ++i;
            continue;
        }
        placed.push_back(format[i]);
    }
    return placed;
}

std::optional<std::string_view> TextAt(const Data::Text::TextTable* texts, std::size_t index)
{
    if (texts == nullptr)
    {
        return std::nullopt;
    }
    return texts->Text(index);
}

std::vector<std::string> SplitIntoLines(std::string_view text, std::size_t maxLines, std::size_t lineBytes)
{
    std::vector<std::string> lines(1);
    const std::size_t backLimit = std::min(lineBytes / 2, MaxBackToSpace);
    std::size_t lineStart = 0;
    std::optional<std::size_t> lastSpace;
    std::size_t next = 0;
    while (next < text.size() && text[next] != '\0')
    {
        // 0x632cd0..0x632cd7: lo copiado del renglón más este carácter llega a lineBytes.
        if (next - lineStart + 1 >= lineBytes)
        {
            if (lastSpace && next - *lastSpace < backLimit)
            {
                lines.back().resize(*lastSpace + 1 - lineStart);
                next = *lastSpace + 1;
            }
            lineStart = next;
            if (lines.size() >= maxLines)
            {
                return lines;
            }
            lines.emplace_back();
            lastSpace.reset();
        }
        lines.back().push_back(text[next]);
        if (text[next] == ' ')
        {
            lastSpace = next;
        }
        ++next;
    }
    return lines;
}
} // namespace Client::UI
