#include "Tables/AddedEmotes.h"

#include "Core/Expected.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Tables
{
namespace
{
constexpr char ColumnEnd = '\t';
constexpr char LineEnd = '\n';
constexpr char CarriageReturn = '\r';
constexpr std::string_view Loop = "loop";
constexpr std::string_view Once = "once";
constexpr std::size_t FixedColumns = 4;
constexpr unsigned LastAction = 0xff;

std::vector<std::string_view> Split(std::string_view text, char end)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t found = text.find(end, start);
        if (found == std::string_view::npos)
        {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, found - start));
        start = found + 1;
    }
}

std::string_view WithoutCarriageReturn(std::string_view line)
{
    return line.ends_with(CarriageReturn) ? line.substr(0, line.size() - 1) : line;
}

template <typename Number> std::optional<Number> Parsed(std::string_view text)
{
    Number value{};
    const char* const end = text.data() + text.size();
    const auto [stopped, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || stopped != end)
        return std::nullopt;
    return value;
}

std::optional<std::uint8_t> ActionIn(std::string_view text, unsigned first)
{
    const std::optional<unsigned> action = Parsed<unsigned>(text);
    if (!action || *action < first || *action > LastAction)
        return std::nullopt;
    return static_cast<std::uint8_t>(*action);
}

Core::Expected<AddedEmote, std::string> ReadRow(std::string_view line)
{
    const std::vector<std::string_view> columns = Split(line, ColumnEnd);
    if (columns.size() <= FixedColumns)
        return Core::Unexpected(std::format("it has {} columns, and a row needs the action, the elf's action, the keys "
                                            "per step, loop or once and at least one trigger",
                                            columns.size()));
    const std::optional<std::uint8_t> action = ActionIn(columns[0], FirstAddedEmoteAction);
    if (!action)
        return Core::Unexpected(std::format("the action '{}' is not a number from {} to {}", columns[0],
                                            FirstAddedEmoteAction, LastAction));
    const std::optional<std::uint8_t> elfAction = ActionIn(columns[1], 0);
    if (!elfAction)
        return Core::Unexpected(
            std::format("the elf's action '{}' is not a number from 0 to {}", columns[1], LastAction));
    const std::optional<float> keysPerStep = Parsed<float>(columns[2]);
    if (!keysPerStep || !(*keysPerStep > 0.0f && *keysPerStep <= MostKeysPerStep))
        return Core::Unexpected(
            std::format("the keys per step '{}' are not a number above 0 and at most {}", columns[2], MostKeysPerStep));
    if (columns[3] != Loop && columns[3] != Once)
        return Core::Unexpected(std::format("'{}' is neither {} nor {}", columns[3], Loop, Once));
    AddedEmote emote{.action = *action,
                     .elfAction = *elfAction,
                     .keysPerStep = *keysPerStep,
                     .repeats = columns[3] == Loop,
                     .triggers = {}};
    for (std::size_t column = FixedColumns; column < columns.size(); ++column)
    {
        if (columns[column].empty())
            return Core::Unexpected(
                std::format("trigger {} is empty, and an empty one is in every message", column - FixedColumns + 1));
        emote.triggers.emplace_back(columns[column]);
    }
    return emote;
}
} // namespace

Core::Expected<std::vector<AddedEmote>, std::string> ReadAddedEmotes(std::span<const std::byte> file)
{
    std::string text(file.size(), '\0');
    std::ranges::transform(file, text.begin(), [](std::byte value) { return static_cast<char>(value); });
    const std::vector<std::string_view> lines = Split(text, LineEnd);
    if (WithoutCarriageReturn(lines.front()) != AddedEmotesHeader)
        return Core::Unexpected(std::format("its first line is not '{}'", AddedEmotesHeader));
    std::vector<AddedEmote> emotes;
    for (std::size_t index = 1; index < lines.size(); ++index)
    {
        const std::string_view line = WithoutCarriageReturn(lines[index]);
        if (line.empty())
            continue;
        auto row = ReadRow(line);
        if (!row.HasValue())
            return Core::Unexpected(std::format("line {}: {}", index + 1, row.Error()));
        if (FindAddedEmote(emotes, row.Value().action) != nullptr)
            return Core::Unexpected(std::format("line {}: action {} already has a row", index + 1, row.Value().action));
        emotes.push_back(std::move(row).Value());
    }
    return emotes;
}

const AddedEmote* FindAddedEmote(std::span<const AddedEmote> emotes, std::uint8_t action)
{
    const auto found = std::ranges::find(emotes, action, &AddedEmote::action);
    return found == emotes.end() ? nullptr : &*found;
}
} // namespace Client::Tables
