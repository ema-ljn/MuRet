#include "UI/GameText.h"

#include "Data/Text/TextTable.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
constexpr char Mark = '%';

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
} // namespace Client::UI
