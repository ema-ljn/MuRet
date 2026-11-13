#include "UI/FriendModel.h"

#include "Core/Msvc6Sort.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// Los char de MSVC son con signo: los de Latin-1 desde 0x80 van antes que los ASCII en 0x49c190.
std::int32_t SignedChar(char value)
{
    return static_cast<std::int32_t>(static_cast<signed char>(value));
}

auto FindTheFriend(FriendModel& model, std::string_view name)
{
    return std::ranges::find_if(model.entries,
                                [name](const FriendEntry& entry) { return SameFriendName(entry.name, name); });
}
} // namespace

std::string FriendName(std::string_view name)
{
    const std::size_t end = name.find('\0');
    return std::string(name.substr(0, std::min(end, FriendNameLength)));
}

bool SameFriendName(std::string_view a, std::string_view b)
{
    return FriendName(a) == FriendName(b);
}

bool NameComesFirst(const FriendEntry& a, const FriendEntry& b)
{
    const std::size_t shorter = std::min(a.name.size(), b.name.size());
    for (std::size_t at = 0; at < shorter; ++at)
    {
        if (a.name[at] != b.name[at])
            return SignedChar(b.name[at]) < SignedChar(a.name[at]);
    }
    return false;
}

bool StateComesFirst(const FriendEntry& a, const FriendEntry& b)
{
    return b.state < a.state;
}

void ClearTheFriends(FriendModel& model)
{
    model.entries.clear();
}

void AddAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state)
{
    model.entries.push_back(FriendEntry{.selected = 0, .name = FriendName(name), .flag = flag, .state = state});
}

void RemoveAFriend(FriendModel& model, std::string_view name)
{
    if (const auto found = FindTheFriend(model, name); found != model.entries.end())
        model.entries.erase(found);
}

void SetAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state)
{
    const auto found = FindTheFriend(model, name);
    if (found == model.entries.end())
        return;
    found->flag = flag;
    found->state = state;
}

void SetAllTheFriends(FriendModel& model, std::uint8_t flag, std::uint8_t state)
{
    for (FriendEntry& entry : model.entries)
    {
        entry.flag = flag;
        entry.state = state;
    }
}

void SortTheFriends(FriendModel& model, std::int32_t mode)
{
    if (mode != SameSort)
        model.sortMode = mode;
    if (model.sortMode == ByNameSort)
        Core::Msvc6Sort(model.entries, NameComesFirst);
    else if (model.sortMode == ByStateSort)
        Core::Msvc6Sort(model.entries, StateComesFirst);
}
} // namespace Client::UI
