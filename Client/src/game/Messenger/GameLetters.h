#pragma once

#include "App/IChatRoomLinks.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Letters.h"

#include <cstdint>
#include <string>
#include <string_view>

// The game's letters and chat with a friend (CL9 milestone 10; 04 § Las cartas y el chat con un amigo, and 06 § HUD y
// ventanas): packets C5..C8 (0x4dc840..0x4dcd90), the CA and the CB (0x4dce10 and 0x4dd020) and what arrives through a
// room's connection (0x4e2a60), over the old windows' manager and the models at [0x576351c] and [0x5763520].
namespace Client::Messenger
{
struct ChatRooms;
}

namespace Client::Chat
{
struct ChatReceiveContext;
} // namespace Client::Chat

namespace Client::Messenger
{

// 0x4dc840: 0, 2, 3, 4, 6 and 7, the writing window (if given) without the pending send (+0x778 = 0, Q-C1001) and a
// box with text 1053, 1061, 1064, 1065, 1068 or 423; 1, the window closes (the 4) and text 1046 with the cost (1000)
// to the chat.
[[nodiscard]] Core::Expected<void, std::string>
ApplyLetterSendResult(const Protocol::Messenger::LetterSendResult& result, const Chat::ChatReceiveContext& context);

// 0x4dc9e0: with type 0 or 1, the letter to the model and the order by date (2); with 2, sound 0x29, the new-letter
// mark (0x4a6420), text 1062 to the chat, the letter as unread and the order it had (−1); then, the letters tab
// (0x4a0960) and, with 0x5767970's limit reached, text 1073.
[[nodiscard]] Core::Expected<void, std::string> ApplyLetterRow(const Protocol::Messenger::LetterRow& row,
                                                               const Chat::ChatReceiveContext& context);

// 0x4dcb70: the packet's copy (0x4a03c0), the letter as read (+0x134 = 1), the tab, the read window (the 6 with text
// 1054 and the subject, at (100, 100) or where the previous one left it), the body (0x499cb0), the window at +0x94
// (0x491720) and the portrait (class, gear, action, angle and zoom) except for the "webzen" letter; the 0xe to the
// window.
[[nodiscard]] Core::Expected<void, std::string> ApplyLetterRead(const Protocol::Messenger::LetterRead& letter,
                                                                const Chat::ChatReceiveContext& context);

// 0x4dccbd..0x4dccd2: _strnicmp(name, "webzen", 10) == 0. 0x6a3450 lowercases 'A'..'Z' and stops at the first 0 of
// either: the name has to be "webzen", in capitals or not.
[[nodiscard]] bool LetterFromWebzen(std::string_view name);

// 0x4dcd90: 0, the box with text 1055; 1, the letter out of the model and its copy (0x49ee20 and 0x4a0500). Then, the
// tab.
[[nodiscard]] Core::Expected<void, std::string>
ApplyLetterDeleteResult(const Protocol::Messenger::LetterDeleteResult& result, const Chat::ChatReceiveContext& context);

// 0x4dce10: 0 and 2, the friend out of the requests (0x4a6bc0) and box 1069 or 1070; 1, by type, the chat I asked for
// (the 1), the one already open with the friend (it leaves its room) or a minimized one at the back (the 2), and the
// room (0x493530).
[[nodiscard]] Core::Expected<void, std::string> ApplyRoomAnswer(const Protocol::Messenger::RoomAnswer& answer,
                                                                const Chat::ChatReceiveContext& context);

// 0x4dd020, in the window's chat: 0, text 1056; 1, the invited one and the 1057; 3, the 1074.
[[nodiscard]] Core::Expected<void, std::string> ApplyInviteResult(const Protocol::Messenger::InviteResult& result,
                                                                  const Chat::ChatReceiveContext& context);

// 0x4e2a60: what arrives through the room connection of chat `window` (00, 01, 02, 04 and 0D; App::ChatRoomPacket).
// One shorter than what its handler reads does nothing (Q-C1011).
[[nodiscard]] Core::Expected<void, std::string> ApplyRoomPacket(std::int32_t window, const Network::Packet& packet,
                                                                const Chat::ChatReceiveContext& context);

// The close of a room's connection (App::ChatRoomClosed): open, 0x49e480's FD_CLOSE, the 402 to the chat; not opened,
// the room is forgotten and its slot stays taken (Q-C1005).
[[nodiscard]] Core::Expected<void, std::string> ApplyChatRoomClosed(const App::ChatRoomClosed& closed,
                                                                    const Chat::ChatReceiveContext& context);

// What the windows left for the rooms (Messenger::ChatRooms::orders), in order: the C1 04, the C1 03 01 and the close,
// or the C1 03 05. Without the connections, it is discarded.
void FlushTheChatRoomOrders(Messenger::ChatRooms& rooms, App::IChatRoomLinks* links);
} // namespace Client::Messenger
