#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Ui/OldWindows.h"

#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Chat
{
struct ChatReceiveContext;
} // namespace Client::Chat

namespace Client::Ui
{
class IGameUiRequests;
} // namespace Client::Ui

// The game's friends (CL9 milestone 9; 04 § Los amigos and 06 § HUD y ventanas, "Los amigos"): opening the window
// from panel 1 (0x491080) and the packets C0..C4 (0x4dc010..0x4dc720) over the old windows' manager and the model at
// [0x5763518].
namespace Client::Messenger
{

// The old windows' Text.bmd texts: the box's (228, 991, 1037 and 1038) and the others by their index.
[[nodiscard]] Ui::OldWindowTexts OldWindowTextsOf(const Tables::TextTable* texts);

// The packet for what a friends window asks for (Protocol/Messenger/Friends, and Protocol/Messenger/FriendChat and
// Protocol/Messenger/Letters from milestone 10).
[[nodiscard]] Protocol::Outgoing FriendPacketOf(const Ui::FriendSend& send);

// The game's OldWindowStepView::send: the packet through the session. The letter (C2 C5) goes out encrypted as C4
// (0x4992fe..0x499416, Q-C1002): before, a rand() for the padding (0x4992fe) and the serial through the protection
// table (0x5767913), like the C3 F1 02 (0x62449a..0x6244f2). The others go out in clear, without rand().
void SendAFriendPacket(Ui::IGameUiRequests& requests, Crt::CrtRandom& random, const Ui::FriendSend& send);

// What 0x491080 reads from the game.
struct FriendsOpenContext
{
    Kernel::WorldState& world;
    // The CRT's rand(): the own record's close draws it (3848).
    Crt::CrtRandom& random;
    // What the level message is measured with; without a measurer, the message cuts.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    const Tables::TextTable* texts = nullptr;
};

// 0x491080(0x186, 0x106): 0x490830, the level (+0xe) with the own record open (3848 rand() on closing it); below 6,
// text 1067 (0x555190 with type 1, if it is not among the last ones: 0x45db70 with 1); otherwise,
// Messenger::OpenTheFriendsWindow and, if it built it and both banner queues are idle (0x46b7d0), text 1244 to the
// banner.
[[nodiscard]] Core::Expected<void, std::string> OpenTheFriends(const FriendsOpenContext& context,
                                                               const Ui::OldWindowGlobals& globals,
                                                               const Ui::OldWindowStepView& view);

// 0x4dc010: the whole list (emptied and filled, by name and by state), the tab, 0x4918d0(1), the letter limit
// (0x5767970) and, with letters, text 1072 with how many and the limit.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendList(const Protocol::Messenger::FriendList& list,
                                                                const Chat::ChatReceiveContext& context);

// 0x4dc3d0: 1, text 1075 and the friend to the list; 0, 3, 4, 5 and 6, a box (0x48ee60(10)) with text 1047 (with the
// name), 1048, 1049 (with the name), 1050 or 1068.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendAddResult(const Protocol::Messenger::FriendAddResult& result,
                                                                     const Chat::ChatReceiveContext& context);

// 0x4dc5b0: "<name><1051>" in a two-button box (0x48ee60(9), owner −1) that stores the name (0x4a4440), and sound
// 0x2a.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendRequest(const Protocol::Messenger::FriendRequest& request,
                                                                   const Chat::ChatReceiveContext& context);

// 0x4dc690: 0, the box with text 1052; 1, the friend out of the list.
[[nodiscard]] Core::Expected<void, std::string>
ApplyFriendDeleteResult(const Protocol::Messenger::FriendDeleteResult& result, const Chat::ChatReceiveContext& context);

// 0x4dc720: with 0xfc, all offline and 0x4918d0(0); otherwise, the friend's state.
[[nodiscard]] Core::Expected<void, std::string> ApplyFriendState(const Protocol::Messenger::FriendState& state,
                                                                 const Chat::ChatReceiveContext& context);
} // namespace Client::Messenger
