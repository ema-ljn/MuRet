#include "UI/NameChecks.h"

#include "UI/Notices.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// El strlen de 0x63d990: con menos, el 0x79.
constexpr std::size_t ShortestName = 4;

// Las que 0x622c70 busca en el nombre tal cual (0x6d6124 a 0x6d6150).
constexpr std::array<std::string_view, 9> FixedWords{" ",      "\xa1\xa1", ".",      "\xa1\xa4", "\xa1\xad",
                                                     "Webzen", "WebZen",   "webzen", "WEBZEN"};

std::string WithoutSpaces(std::string_view name)
{
    std::string kept;
    for (const char letter : name)
    {
        if (letter != ' ')
        {
            kept.push_back(letter);
        }
    }
    return kept;
}

bool ContainsAny(std::string_view text, std::span<const std::string> words)
{
    for (const std::string& word : words)
    {
        if (ContainsLikeTheOriginal(text, word))
        {
            return true;
        }
    }
    return false;
}

// Un byte que 0x447ca0 rechaza.
bool IsBadByte(unsigned char byte)
{
    return byte < 0x21 || (byte > 0x7e && byte < 0xb0) || byte > 0xf7;
}
} // namespace

bool ContainsLikeTheOriginal(std::string_view text, std::string_view word)
{
    return text.find(word) != std::string_view::npos;
}

bool IsFilteredName(std::string_view name, const NameFilters& filters)
{
    const std::string compact = WithoutSpaces(name);
    if (ContainsAny(compact, filters.filterName))
    {
        return true;
    }
    if ((name.empty() || name.front() != '/') && ContainsAny(compact, filters.filter))
    {
        return true;
    }
    for (const std::string_view word : FixedWords)
    {
        if (ContainsLikeTheOriginal(name, word))
        {
            return true;
        }
    }
    return ContainsLikeTheOriginal(name, filters.operation) || ContainsLikeTheOriginal(name, filters.server);
}

bool HasForbiddenSymbol(std::string_view name)
{
    const auto at = [name](std::size_t index)
    { return index < name.size() ? static_cast<unsigned char>(name[index]) : static_cast<unsigned char>(0); };
    if (at(0) == 0)
    {
        return false;
    }
    for (std::size_t index = 0;; index += 2)
    {
        if (IsBadByte(at(index)))
        {
            return true;
        }
        if (at(index + 2) == 0)
        {
            return false;
        }
    }
}

std::optional<NoticeId> NoticeOfNewName(std::string_view name, const NameFilters& filters)
{
    if (name.size() < ShortestName)
    {
        return ShortNameNotice;
    }
    if (IsFilteredName(name, filters))
    {
        return ForbiddenNameNotice;
    }
    if (HasForbiddenSymbol(name))
    {
        return SymbolNameNotice;
    }
    return std::nullopt;
}
} // namespace Client::UI
