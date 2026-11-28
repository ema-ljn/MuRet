#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The letters and the rooms of the chat with a friend (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas y el chat
// con un amigo"): the object at [0x576351c], which the C6 (0x49eb60) and the C8 (0x49ee20) fill and the letters tab
// copies to its list (0x4a0960); and the one at [0x5763520], the rooms, which 0x49de50 builds when a CA arrives. The
// MSVC6 STL std::deque and std::map are MuRet's, with the same order (P-C216, by who calls them, *static*): the
// letters deque and its copies in the list (0x4746d0 0x474760 0x474b50 0x474b90 0x474ba0 0x477120 0x477270 0x4772f0
// 0x477310 0x477380 0x478780 0x478810 0x47ee30 0x487740 0x4877d0 0x487860 0x489e50 0x48bcb0 0x48ca90 0x48cdf0
// 0x4a7dc0); the std::sort of 0x49f870, which is Crt::Msvc6Sort (0x4ab4d0 0x4abe80 0x4ac370 0x4ac3f0 0x4ac860 0x4aca90
// 0x4acb50 0x4acca0 0x4acd10); the copies' map (0x4a03c0, 0x4a0500, 0x4a0560: 0x4a8bf0 0x4aa390 0x4aa3d0 0x4aae80
// 0x4aaeb0 0x4ab160 0x4ab340); the read windows' map and the room slots' (0x491570..0x491780 and 0x49e280: 0x4a98a0
// 0x4a9e30 0x4aad60 0x4aad90 0x4ab300); the rooms' (0x49de50, 0x49dfd0, 0x49e120, 0x49e420: 0x4a8720 0x4a8b30
// 0x4a8ba0 0x4aa0d0 0x4aa110 0x4aadf0 0x4aae20 0x4ab320); the tab's checked ones (0x4a11b0: 0x4a98c0 0x4a9bc0 0x4aace0
// 0x4ab0d0 0x4ab3c0); and the two objects' destructors on exit (0x4aeda0 from WinMain, 0x4bec50 0x4bed30 0x4bfce0
// 0x4bfdd0 0x4bfe00 0x4bfef0), which are C++'s.
namespace Client::Messenger
{
// 0x49f870: by type (0), sender (1), date and time (2) or subject (3); −1, the one it had.
inline constexpr std::int32_t ByTypeLetters = 0;
inline constexpr std::int32_t BySenderLetters = 1;
inline constexpr std::int32_t ByDateLetters = 2;
inline constexpr std::int32_t BySubjectLetters = 3;
inline constexpr std::int32_t SameLetters = -1;
// 0x49eb60: strncpy of 10, 0x20, 0x10 and 0x10.
inline constexpr std::size_t LetterNameLength = 10;
inline constexpr std::size_t LetterSubjectLength = 0x20;
inline constexpr std::size_t LetterStampLength = 0x10;

// 0x138 bytes.
struct LetterEntry
{
    // +0: the click on its row flips it; in the model it stays at 0 (the template at 0x5762f18, *static*).
    std::int32_t selected = 0;
    // +4: the server's number.
    std::uint16_t index = 0;
    // +8, +0x13, +0x113 and +0x123.
    std::string name;
    std::string subject;
    std::string date;
    std::string time;
    // +0x134: 0 unread... as the server marks it (the C6), and 1 after opening it (0x4dcb70).
    std::int32_t type = 0;

    friend bool operator==(const LetterEntry&, const LetterEntry&) = default;
};

// [0x576351c].
struct LetterModel
{
    // +0: the order of the last 0x49f870.
    std::int32_t sortMode = ByTypeLetters;
    // +8..+0x30.
    std::deque<LetterEntry> letters;
    // +0x44: the C7 of each letter already read (0x4a03c0), 0x404 bytes from pkt[0].
    std::map<std::uint16_t, std::vector<std::byte>> copies;
};

// 0x49c190: up to the shorter one's length, at the first different char (signed) it returns whether `a` is the
// larger. With the same start it sets `same` to 1 (if given) and returns false.
[[nodiscard]] bool TextComesAfter(std::string_view a, std::string_view b, bool* same = nullptr);

// 0x49eb60: if there is already one with the number, nothing; otherwise, push_back with the texts cut.
void AddALetter(LetterModel& model, std::uint16_t index, std::string_view name, std::string_view subject,
                std::string_view date, std::string_view time, std::int32_t type);

// 0x49ee20: the first with the number, if any.
void RemoveALetter(LetterModel& model, std::uint16_t index);

// 0x4a01a0: the first with the number, or none.
[[nodiscard]] LetterEntry* FindALetter(LetterModel& model, std::uint16_t index);

// 0x49f870: with a mode other than −1 it stores it, and sorts with the MSVC6 sort by the stored one.
void SortTheLetters(LetterModel& model, std::int32_t mode);

// 0x4a03c0: stores the copy if there was none for the number (std::map::insert).
void KeepTheLetter(LetterModel& model, std::uint16_t index, std::vector<std::byte> copy);
// 0x4a04a0: the stored copy, or none.
[[nodiscard]] const std::vector<std::byte>* KeptLetter(const LetterModel& model, std::uint16_t index);
// 0x4a0500.
void ForgetTheLetter(LetterModel& model, std::uint16_t index);
// 0x49f260 (the game's first step): no letters (0x49f2c0..) and no copies (0x4a0560).
void ClearTheLetters(LetterModel& model);

// [0x5763520], the rooms of the chat with a friend.
inline constexpr std::size_t ChatSlots = 0x100;
// 0x49de50: the room connection's message is 0x500 plus its slot.
inline constexpr std::uint32_t ChatSocketMessage = 0x500;
struct ChatRoom
{
    // +0 and +0x4028 of the 0x402c-byte object: the room and the chat window.
    std::uint32_t room = 0;
    std::int32_t window = 0;
};
struct ChatRoomOrder
{
    enum class Kind : std::uint8_t
    {
        // C1 04 with `text` (0x4944c0 with the 7).
        Message,
        // C1 03 01 (0x493a70) and closing the connection (0x49dfd0).
        Leave,
        // C1 03 05 (0x4a6550).
        Ping,
    };
    Kind kind = Kind::Message;
    std::uint32_t room = 0;
    std::string text;
    friend bool operator==(const ChatRoomOrder&, const ChatRoomOrder&) = default;
};
struct ChatRooms
{
    // +0: where 0x49e280 starts searching; +1..+0x100, the slots in use.
    std::uint8_t cursor = 0;
    std::array<std::uint8_t, ChatSlots> used{};
    // +0x108: each slot with its room.
    std::map<std::uint32_t, std::uint32_t> slots;
    // +0x118: each room with its connection (the session's, Chat::IChatRoomLinks), since 0x49de50 opens it.
    std::map<std::uint32_t, ChatRoom> rooms;
    // What the windows send to their room's connection (0x4944c0's message with the 7, 0x493a70's leave with 0x49dfd0's
    // close and 0x4a6550's), in order: the windows do not see the session, and Messenger::FlushTheChatRoomOrders takes
    // them out.
    std::deque<ChatRoomOrder> orders;
};

// 0x49e280: from the cursor, the first free slot (moves the cursor, wrapping at 0xff); marks it and ties it to the
// room. After 0x101 taken in a row, none: the caller writes "max chat count over".
[[nodiscard]] std::optional<std::uint8_t> TakeAChatSlot(ChatRooms& rooms, std::uint32_t room);

// 0x49e420: the room's connection, or none.
[[nodiscard]] const ChatRoom* FindTheChatRoom(const ChatRooms& rooms, std::uint32_t room);

// 0x49dfd0: without the room, nothing; with it, removes it and frees the slot that has it.
void DropTheChatRoom(ChatRooms& rooms, std::uint32_t room);
// 0x49de50 with the connect failing: the room is not noted and the slot that 0x49e280 took stays taken (Q-C1005).
void ForgetTheChatRoom(ChatRooms& rooms, std::uint32_t room);
} // namespace Client::Messenger
