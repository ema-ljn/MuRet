#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

// The game's friend list (CL9 milestone 9; 06 § HUD y ventanas, "Los amigos"): the object at [0x5763518], which
// packets C0..C4 fill (0x4dc010..0x4dc720) and the friends tab copies to its list (0x49bd60). It is an MSVC6 STL
// std::deque with 0x14-byte elements and a work iterator (+0x34) that MuRet does not need. MuRet's std::deque
// replaces the original's instantiation over FriendEntry, the model's and the tab's (push_back, pop_front, their
// iterators and the deque's map): 0x4733c0, 0x473830, 0x473860, 0x473a20, 0x476b20, 0x476c70, 0x476cf0, 0x476d10,
// 0x476d80, 0x4784d0, 0x47a770, 0x4874e0, 0x487560, 0x4875f0, 0x4a76b0, 0x4a7bd0 and 0x4a7d20.
namespace Client::Messenger
{
// strncpy of 10 with the '\0' at +0xe (0x49b220).
inline constexpr std::size_t FriendNameLength = 10;
// 0x49c260: by name (0x49c190), by state (0x49c240) or the one it had (−1).
inline constexpr std::int32_t ByNameSort = 0;
inline constexpr std::int32_t ByStateSort = 1;
inline constexpr std::int32_t SameSort = -1;
// C4's state with everyone offline (0x4dc72b): the friends server went down.
inline constexpr std::uint8_t AllOfflineState = 0xfc;

// 0x14 bytes.
struct FriendEntry
{
    // +0: the click on its row flips it (0x461f00). In the model it stays at 0: 0x49b220 copies the template at
    // 0x5762ef8, which nobody writes at +0 (*static*).
    std::int32_t selected = 0;
    // +4..+0xe.
    std::string name;
    // +0xf: C0, C1 and C4 write it as 0; nobody reads it in milestone 9.
    std::uint8_t flag = 0;
    // +0x10: the server's number; 0xfc..0xff offline.
    std::uint8_t state = 0;

    friend bool operator==(const FriendEntry&, const FriendEntry&) = default;
};

// [0x5763518].
struct FriendModel
{
    // +0: the order of the last 0x49c260.
    std::int32_t sortMode = ByNameSort;
    // +8..+0x30.
    std::deque<FriendEntry> entries;
};

// strncpy of 10 up to the first '\0'.
[[nodiscard]] std::string FriendName(std::string_view name);

// strncmp(a, b, 10) == 0.
[[nodiscard]] bool SameFriendName(std::string_view a, std::string_view b);

// 0x49c190 (with 0x49c220): up to the shorter one's length, at the first different char (signed) the larger goes
// first; one that starts like the other does not go first. It leaves the names from Z to A.
[[nodiscard]] bool NameComesFirst(const FriendEntry& a, const FriendEntry& b);

// 0x49c240: the highest state (unsigned) first.
[[nodiscard]] bool StateComesFirst(const FriendEntry& a, const FriendEntry& b);

// 0x49bac0.
void ClearTheFriends(FriendModel& model);

// 0x49b220: insert(end()) with the name, the flag and the state (in an empty one, push_front; otherwise, push_back).
void AddAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state);

// 0x49b690: the first with that name (strncmp of 10), if any.
void RemoveAFriend(FriendModel& model, std::string_view name);

// 0x49c010: the first with that name, if any.
void SetAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state);

// 0x49c0e0.
void SetAllTheFriends(FriendModel& model, std::uint8_t flag, std::uint8_t state);

// 0x49c260: with a mode other than −1 it stores it, and sorts with the MSVC6 sort by name (0) or by state (1); with
// another stored mode, nothing.
void SortTheFriends(FriendModel& model, std::int32_t mode);
} // namespace Client::Messenger
