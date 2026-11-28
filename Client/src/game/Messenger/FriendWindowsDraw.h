#pragma once

#include "Core/Expected.h"
#include "Messenger/FriendModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

// The drawing of the friends window and the add-a-friend box (CL9 milestone 9; 06 § HUD y ventanas, "Los amigos"):
// their vtable + 0x50, which the frame (0x492500) calls with the normal font in the DC after the shadow and before the
// title bar. The pieces they share with the other old windows are in Ui/OldWindowDrawParts.
namespace Client::Messenger
{
// 0x4a2620: dragged or resizing, it places the tabs (SettleTheFriendsForTheDraw); the tab on view (0x49cc20, 0x4a0a30
// or 0x49e7f0; the letters one is from milestone 10); with the normal blend, the headers' black band (palette 7, 20
// high), the headers (0x4a2460), the palette 2 line from 0x9f, the tabs' titles centered in their place (in white the
// one on view or under the mouse), the state button's text against the right edge and its checkbox (0x463090), checked
// with the manager's +0xe0 at 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheFriendsWindow(Ui::OldWindowManager& manager,
                                                                     Ui::OldWindow& window,
                                                                     const Ui::OldWindowDrawContext& context,
                                                                     const Ui::OldWindowDrawView& view);

// 0x4a3750: dragged or resizing, it places its children; the two buttons (0x45b2e0) and, with the normal blend, the
// EDIT (0x468320). It ends without blend (0x620980).
[[nodiscard]] Core::Expected<void, std::string> DrawTheAddFriendBox(Ui::OldWindowManager& manager, Ui::OldWindow& box,
                                                                    const Ui::OldWindowDrawContext& context,
                                                                    const Ui::OldWindowDrawView& view);

// 0x461cd0, the friend list's vtable + 0x80 (0x460970): the row (DrawTheChosenRow), the name (sprintf "%s", 0x6c7084)
// and, with +0x130 at 1, the place. Returns 1.
std::int32_t DrawTheFriendRow(const Ui::OldWindowDrawContext& context, const Ui::OldList& list,
                              const std::deque<FriendEntry>& friends, std::int32_t row, std::size_t at,
                              const Ui::OldWindowDrawView& view);

// 0x47a810, the vtable + 0x14 of a 0x460970 list (the friends tab's, and the members and invite lists of the chat with
// a friend): the bar (0x461890) and the rows of 0x461cd0.
void DrawTheFriendList(const Ui::OldWindowDrawContext& context, Ui::OldList& list,
                       const std::deque<FriendEntry>& friends, const Ui::OldWindowDrawView& view);
} // namespace Client::Messenger
