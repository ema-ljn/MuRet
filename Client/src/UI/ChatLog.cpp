#include "UI/ChatLog.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/ScreenScale.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/NameChecks.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::UI
{
namespace
{
constexpr std::int32_t SystemType = 1;
constexpr std::int32_t ErrorType = 2;
constexpr std::int32_t ChatType = 3;
// 0x45bb20: con menos de 0x14 bytes entre el nombre y el texto, sin cortar.
constexpr std::size_t ShortMessage = 0x14;
// 0x45bfe0: n = −6, el corrimiento por las edades, que busca 6 renglones.
constexpr std::int32_t AgedScroll = -6;
constexpr std::int32_t AgedRows = 6;
// 0x459c70: un renglón entra si mide entre el ancho − 4 y el ancho + 4.
constexpr double WrapSlack = 4.0;
// 0x45db70: strncmp de 0x10.
constexpr std::size_t RecentBytes = 0x10;
// 0x45d8c0: los tamaños.
constexpr std::int32_t SmallRows = 3;
constexpr std::int32_t NormalRows = 6;
constexpr std::int32_t LargeRows = 0x1e;

std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

std::string Bounded(std::string_view text, std::size_t bytes)
{
    return std::string(CString(text).substr(0, bytes));
}

// _ftol (0x69fd3c): trunca hacia el 0.
int Truncate(double value)
{
    return static_cast<int>(value);
}

// rx (0x5762ea8): W / 640 en float. Sin pantalla, 1.
double RatioX(App::PixelSize screen)
{
    return screen.width > 0 ? static_cast<double>(Render::ScaleX(1.0f, screen)) : 1.0;
}

// strtok(…, " "): las palabras separadas por espacios, sin las vacías.
std::vector<std::string_view> Words(std::string_view text)
{
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < text.size())
    {
        const std::size_t start = text.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
            break;
        const std::size_t end = text.find(' ', start);
        words.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            break;
        at = end + 1;
    }
    return words;
}

// La entrada que arma 0x45bb20 en 0x575d270 y copia a la lista.
ChatEntry MakeEntry(std::string_view name, std::string_view text, std::int32_t type, std::int32_t param)
{
    return ChatEntry{.blink = 0,
                     .name = Bounded(name, ChatNameBytes),
                     .text = Bounded(text, ChatTextBytes),
                     .type = type,
                     .param = param,
                     .age = 0};
}
} // namespace

std::string NameOf(const ChatEntry& entry)
{
    if (entry.name.size() < ChatNameBytes)
        return entry.name;
    return entry.name + entry.text;
}

bool IsHiddenInTheLog(const ChatEntry& entry, ChatModes modes)
{
    if (!modes.showChat && entry.type == ChatType)
        return true;
    return modes.placement == 1 && (entry.type == SystemType || entry.type == ErrorType);
}

std::int32_t VisibleInTheLog(const ChatLogState& log, ChatModes modes)
{
    std::int32_t hidden = 0;
    if (!modes.showChat || modes.placement == 1)
    {
        for (const ChatEntry& entry : log.entries)
        {
            if (IsHiddenInTheLog(entry, modes))
                ++hidden;
        }
    }
    return static_cast<std::int32_t>(log.entries.size()) - hidden;
}

std::size_t FirstShownInTheLog(const ChatLogState& log, ChatModes modes)
{
    std::size_t at = 0;
    if (log.scroll <= 0)
        return at;
    std::int32_t skipped = 0;
    do
    {
        if (at == log.entries.size())
            return at;
        if (!IsHiddenInTheLog(log.entries[at], modes))
            ++skipped;
        ++at;
    } while (skipped < log.scroll);
    return at;
}

std::size_t SelectedInTheLog(const ChatLogState& log, ChatModes modes)
{
    // 0x45c190 también vuelve la elección a 0 con la lista vacía; nadie lo llama así (0x45d760 lo pide con una
    // elección, que no pasa de los que se ven).
    if (log.entries.empty() || log.selection == 0)
        return log.entries.size();
    std::int32_t row = 1;
    for (std::size_t at = 0; at < log.entries.size(); ++at)
    {
        if (!IsHiddenInTheLog(log.entries[at], modes))
        {
            if (row == log.selection)
                return at;
        }
        else
            --row;
        ++row;
    }
    return log.entries.size();
}

void ScrollTheLog(ChatLogState& log, ChatModes modes, std::int32_t lines)
{
    if (static_cast<std::uint32_t>(log.rows) > static_cast<std::uint32_t>(log.entries.size()))
        return;
    if (modes.placement == 0 && log.scroll == 0 && lines == AgedScroll)
    {
        // 0x45c04f..0x45c0dc: la suma va sin signo, como el int + uint del original.
        std::int32_t shown = 1;
        std::uint32_t oldest = 0;
        std::uint32_t taken = 0;
        for (const ChatEntry& entry : log.entries)
        {
            taken = taken + 1u + (entry.age - oldest);
            oldest = std::max(oldest, entry.age);
            if (static_cast<std::int32_t>(taken) == AgedRows)
                break;
            if (static_cast<std::int32_t>(taken) > AgedRows)
            {
                --shown;
                break;
            }
            ++shown;
        }
        log.scroll = shown;
    }
    else
        log.scroll -= lines;
    if (log.scroll < 0)
    {
        log.scroll = 0;
        return;
    }
    // Puede quedar negativo: con escondidos, los que se ven pueden ser menos que los renglones.
    const std::int32_t visible = VisibleInTheLog(log, modes);
    if (visible - log.rows < log.scroll)
        log.scroll = visible - log.rows;
}

void SelectInTheLog(ChatLogState& log, ChatModes modes, std::int32_t row)
{
    log.selection = row;
    const std::int32_t visible = VisibleInTheLog(log, modes);
    if (visible < log.selection)
        log.selection = visible;
}

void TrimTheLog(ChatLogState& log, ChatModes modes)
{
    const auto size = static_cast<std::int32_t>(log.entries.size());
    if (log.maxEntries > size)
        return;
    for (std::int32_t extra = size - log.maxEntries; extra > 0; --extra)
        log.entries.pop_back();
    SelectInTheLog(log, modes, 0);
}

void ClearTheLog(ChatLogState& log, ChatModes modes)
{
    log.entries.clear();
    // 0x4793b0(0): el parpadeo de las que quedan, ninguna.
    SelectInTheLog(log, modes, 0);
    log.scroll = 0;
}

Core::Expected<WrappedText, std::string> WrapByWidth(std::string_view text, const Render::Text::ITextMeasure& measure,
                                                     Render::Text::FontKind font, App::PixelSize screen, int width,
                                                     int lines, int indent)
{
    WrappedText wrapped;
    wrapped.lines.resize(static_cast<std::size_t>(std::max(lines, 0)));
    const double ratio = RatioX(screen);
    std::string_view rest = CString(text);
    for (int line = 0;; ++line)
    {
        if (lines < line + 1)
        {
            wrapped.result = line + 1;
            return wrapped;
        }
        const Render::Text::TextExtent extent = measure.Measure(font, rest);
        const int measured = Truncate(static_cast<double>(extent.width) / ratio);
        if (extent.width == 0)
        {
            wrapped.result = line;
            return wrapped;
        }
        const int limit = width - (line == 0 ? indent : 0);
        std::string& out = wrapped.lines[static_cast<std::size_t>(line)];
        if (measured <= limit)
        {
            out = std::string(rest.substr(0, ChatTextBytes));
            wrapped.result = line + 1;
            return wrapped;
        }
        const auto length = static_cast<int>(rest.size());
        const int perLetter = measured / length;
        if (perLetter == 0)
            return Core::Unexpected(
                std::format("0x459d21: {} bytes que miden {} dan menos de un punto por letra", length, measured));
        int count = limit / perLetter;
        int step = Truncate((static_cast<double>(count) + 1.0) * 0.5);
        while (step != 0)
        {
            step = Truncate((static_cast<double>(step) + 1.0) * 0.5);
            // GetTextExtentPointA de los primeros `count` bytes. Más allá del '\0' el original mide lo que sigue en
            // el búfer del que llama; MuRet mide ceros (*inferido*: los búferes de los textos empiezan en 0).
            std::string head(rest.substr(0, static_cast<std::size_t>(std::clamp(count, 0, length))));
            if (count > length)
                head.append(static_cast<std::size_t>(count - length), '\0');
            const double inDesign = static_cast<double>(measure.Measure(font, head).width) / ratio;
            if (inDesign > static_cast<double>(limit) + WrapSlack)
            {
                count -= step;
                if (step == 1)
                    break;
                continue;
            }
            if (inDesign < static_cast<double>(limit) - WrapSlack)
            {
                count += step;
                continue;
            }
            break;
        }
        // 0x459dbb..0x459dd7: _mbclen de a un byte, hasta `count` si es positivo.
        const std::size_t bytes = count > 0 ? static_cast<std::size_t>(count) : 0;
        out = std::string(rest.substr(0, std::min(bytes, rest.size())));
        rest = rest.substr(std::min(bytes, rest.size()));
    }
}

Core::Expected<std::optional<std::int32_t>, std::string> AddToTheLog(ChatLogState& log, ChatModes modes,
                                                                     const ChatMeasure& measure, std::string_view name,
                                                                     std::string_view text, std::int32_t type,
                                                                     std::int32_t param, bool noticeSound)
{
    name = CString(name);
    text = CString(text);
    std::optional<std::int32_t> sound;
    if (name.empty() && text.empty())
    {
        if (modes.placement != 0 || log.scroll != 0)
            return sound;
        for (ChatEntry& entry : log.entries)
            ++entry.age;
        return sound;
    }
    if (type == ChatType)
    {
        if (!modes.showChat)
            return sound;
        if (!PassesTheChatFilter(log, name) && !PassesTheChatFilter(log, text))
            return sound;
        if (!log.filter[0].empty() && noticeSound)
            sound = ChatFilterSound;
    }
    ChatEntry entry = MakeEntry(name, text, type, param);
    if (name.size() + text.size() < ShortMessage)
        log.entries.push_front(entry);
    else
    {
        auto wrapped = WrapByWidth(text, measure.measure, measure.dcFont, measure.screen, ChatWrapWidth, 2, 0);
        if (!wrapped.HasValue())
            return Core::Unexpected(wrapped.Error());
        const std::vector<std::string>& lines = wrapped.Value().lines;
        if (!lines[0].empty())
        {
            entry.text = Bounded(lines[0], ChatTextBytes);
            log.entries.push_front(entry);
        }
        if (!lines[1].empty())
        {
            // 0x45bd3f: el segundo sin nombre, adelante del primero.
            entry.name.clear();
            entry.text = Bounded(lines[1], ChatTextBytes);
            log.entries.push_front(entry);
        }
    }
    TrimTheLog(log, modes);
    SelectInTheLog(log, modes, 0);
    if (log.scroll != 0 && !IsHiddenInTheLog(entry, modes))
        ++log.scroll;
    if (log.rows <= VisibleInTheLog(log, modes) && VisibleInTheLog(log, modes) - log.scroll < log.rows)
        log.scroll = VisibleInTheLog(log, modes) - log.rows;
    return sound;
}

bool PassesTheChatFilter(const ChatLogState& log, std::string_view text)
{
    if (log.filter[0].empty())
        return true;
    for (const std::string& word : log.filter)
    {
        if (word.empty())
            return false;
        if (ContainsLikeTheOriginal(CString(text), word))
            return true;
    }
    return false;
}

std::string JoinedChatFilter(const ChatLogState& log)
{
    std::string joined;
    if (log.filter[0].empty())
        return joined;
    for (std::size_t word = 0; word < ChatFilterWords; ++word)
    {
        joined += log.filter[word];
        if (word + 1 == ChatFilterWords || log.filter[word + 1].empty())
            return joined;
        joined += ", ";
    }
    return joined;
}

void ClearTheChatFilter(ChatLogState& log)
{
    log.filter.fill(std::string{});
}

std::optional<std::size_t> SetTheChatFilter(ChatLogState& log, std::string_view command)
{
    command = CString(command);
    if (command.size() >= ChatTextBytes)
        return std::nullopt;
    const bool hadOne = !log.filter[0].empty();
    const std::vector<std::string_view> words = Words(command);
    for (std::size_t slot = 0; slot < ChatFilterWords; ++slot)
    {
        // La primera palabra es el comando.
        if (slot + 1 >= words.size())
        {
            log.filter[slot].clear();
            continue;
        }
        const std::string_view word = words[slot + 1];
        std::size_t total = word.size();
        for (const std::string& kept : log.filter)
            total += kept.size();
        if (total < ChatFilterTotalBytes)
            log.filter[slot] = std::string(word);
        else
        {
            if (slot == 0)
                return std::nullopt;
            log.filter[slot].clear();
        }
    }
    if (!log.filter[0].empty())
        return FilterOnText;
    if (hadOne)
        return FilterOffText;
    return std::nullopt;
}

bool SaidRecently(const ChatLogState& log, std::string_view text, std::int32_t count)
{
    if (log.entries.empty())
        return false;
    count = std::min(count, static_cast<std::int32_t>(log.entries.size()));
    const std::string wanted(CString(text).substr(0, RecentBytes));
    for (std::int32_t at = 0; at < count; ++at)
    {
        const ChatEntry& entry = log.entries[static_cast<std::size_t>(at)];
        if (entry.text.substr(0, RecentBytes) == wanted || entry.age != 0)
            return true;
    }
    return false;
}

void CycleTheLogSize(ChatLogState& log, ChatModes& modes)
{
    switch (log.rows)
    {
    case SmallRows:
        log.rows = NormalRows;
        break;
    case 6:
    case 9:
    case 0xc:
    case 0xf:
    case 0x12:
    case 0x15:
    case 0x18:
    case 0x1b:
        if (modes.placement == 1)
            log.rows = LargeRows;
        else
        {
            modes.placement = 1;
            log.rows = SmallRows;
        }
        break;
    case LargeRows:
        log.rows = NormalRows;
        modes.placement = 0;
        break;
    default:
        if (log.rows > LargeRows)
        {
            modes.placement = 1;
            log.rows = NormalRows;
        }
        break;
    }
    ScrollTheLog(log, modes, 0);
}
} // namespace Client::UI
