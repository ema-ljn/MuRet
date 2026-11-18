#include "UI/LetterModel.h"

#include "Core/Msvc6Sort.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::UI
{
namespace
{
signed char SignedChar(char value)
{
    return static_cast<signed char>(value);
}

// strncpy de `size` hasta el primer '\0'.
std::string Cut(std::string_view text, std::size_t size)
{
    const std::string_view cut = text.substr(0, text.find('\0'));
    return std::string(cut.substr(0, size));
}

// 0x49f7a0: una leída (1) antes que una sin leer (0).
bool ReadComesFirst(const LetterEntry& a, const LetterEntry& b)
{
    return a.type == 1 && b.type == 0;
}

// 0x49f7d0.
bool SenderComesFirst(const LetterEntry& a, const LetterEntry& b)
{
    return TextComesAfter(a.name, b.name);
}

// 0x49f7f0: la fecha de b contra la de a y, con el principio igual, la hora.
bool DateComesFirst(const LetterEntry& a, const LetterEntry& b)
{
    bool same = false;
    const bool before = TextComesAfter(b.date, a.date, &same);
    if (same)
        return TextComesAfter(b.time, a.time);
    return before;
}

// 0x49f850.
bool SubjectComesFirst(const LetterEntry& a, const LetterEntry& b)
{
    return TextComesAfter(a.subject, b.subject);
}
} // namespace

bool TextComesAfter(std::string_view a, std::string_view b, bool* same)
{
    if (same != nullptr)
        *same = false;
    a = a.substr(0, a.find('\0'));
    b = b.substr(0, b.find('\0'));
    const std::size_t shorter = std::min(a.size(), b.size());
    for (std::size_t at = 0; at < shorter; ++at)
    {
        if (a[at] != b[at])
            return SignedChar(b[at]) < SignedChar(a[at]);
    }
    if (same != nullptr)
        *same = true;
    return false;
}

void AddALetter(LetterModel& model, std::uint16_t index, std::string_view name, std::string_view subject,
                std::string_view date, std::string_view time, std::int32_t type)
{
    if (FindALetter(model, index) != nullptr)
        return;
    model.letters.push_back(LetterEntry{.selected = 0,
                                        .index = index,
                                        .name = Cut(name, LetterNameLength),
                                        .subject = Cut(subject, LetterSubjectLength),
                                        .date = Cut(date, LetterStampLength),
                                        .time = Cut(time, LetterStampLength),
                                        .type = type});
}

void RemoveALetter(LetterModel& model, std::uint16_t index)
{
    const auto found = std::ranges::find(model.letters, index, &LetterEntry::index);
    if (found != model.letters.end())
        model.letters.erase(found);
}

LetterEntry* FindALetter(LetterModel& model, std::uint16_t index)
{
    const auto found = std::ranges::find(model.letters, index, &LetterEntry::index);
    return found != model.letters.end() ? &*found : nullptr;
}

void SortTheLetters(LetterModel& model, std::int32_t mode)
{
    if (mode != SameLetters)
        model.sortMode = mode;
    // 0x49f888: con un modo de más de 3 (sin signo), nada.
    switch (model.sortMode)
    {
    case ByTypeLetters:
        Core::Msvc6Sort(model.letters, ReadComesFirst);
        return;
    case BySenderLetters:
        Core::Msvc6Sort(model.letters, SenderComesFirst);
        return;
    case ByDateLetters:
        Core::Msvc6Sort(model.letters, DateComesFirst);
        return;
    case BySubjectLetters:
        Core::Msvc6Sort(model.letters, SubjectComesFirst);
        return;
    default:
        return;
    }
}

void KeepTheLetter(LetterModel& model, std::uint16_t index, std::vector<std::byte> copy)
{
    model.copies.emplace(index, std::move(copy));
}

const std::vector<std::byte>* KeptLetter(const LetterModel& model, std::uint16_t index)
{
    const auto found = model.copies.find(index);
    return found != model.copies.end() ? &found->second : nullptr;
}

void ForgetTheLetter(LetterModel& model, std::uint16_t index)
{
    model.copies.erase(index);
}

void ClearTheLetters(LetterModel& model)
{
    model.letters.clear();
    model.copies.clear();
}

std::optional<std::uint8_t> TakeAChatSlot(ChatRooms& rooms, std::uint32_t room)
{
    std::int32_t tries = 0;
    while (rooms.used[rooms.cursor] == 1)
    {
        ++tries;
        rooms.cursor = rooms.cursor == 0xff ? std::uint8_t{0} : static_cast<std::uint8_t>(rooms.cursor + 1);
        if (tries > static_cast<std::int32_t>(ChatSlots))
            return std::nullopt;
    }
    rooms.used[rooms.cursor] = 1;
    rooms.slots.emplace(rooms.cursor, room);
    return rooms.cursor;
}

const ChatRoom* FindTheChatRoom(const ChatRooms& rooms, std::uint32_t room)
{
    const auto found = rooms.rooms.find(room);
    return found != rooms.rooms.end() ? &found->second : nullptr;
}

void DropTheChatRoom(ChatRooms& rooms, std::uint32_t room)
{
    const auto found = rooms.rooms.find(room);
    if (found == rooms.rooms.end())
        return;
    rooms.rooms.erase(found);
    for (auto slot = rooms.slots.begin(); slot != rooms.slots.end(); ++slot)
    {
        if (slot->second != room)
            continue;
        rooms.used[slot->first] = 0;
        rooms.slots.erase(slot);
        return;
    }
}

void ForgetTheChatRoom(ChatRooms& rooms, std::uint32_t room)
{
    rooms.rooms.erase(room);
}
} // namespace Client::UI
