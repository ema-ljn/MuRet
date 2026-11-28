#pragma once

#include "Core/Expected.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"

#include <string>

// The drawing of the chat with a friend and of a letter's windows (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas
// y el chat con un amigo"): their vtable + 0x50, which the frame (0x492500) calls with the normal font in the DC after
// the shadow and before the title bar, and the chat rows in the F5 window list. Parts shared with the other old windows
// are in Ui/OldWindowDrawParts; the friend lists and the letters tab, in Messenger/FriendWindowsDraw.
namespace Client::Messenger
{
// 0x493fc0: with the normal blend, dragged or resizing it places its children (SettleTheLetterKindsForTheDraw); the
// rows (0x45f9c0, with the bar 0x45fac0 and the rows 0x460550); with more than two members or with the invite list
// (+0xb0 at 2), the members and their line (0x491ba0) and, with the invite list, it too and its line; the line of the 2
// and the black (the 7) below, the invite button, the EDIT (0x468320) and, with the invite list, the send-invitation
// button. Ends without blend (0x620980). The error is the one of re-splitting the rows (the 9 of
// SettleTheLetterKindsForTheDraw).
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatWindow(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                                  const Ui::OldWindowDrawContext& context,
                                                                  const Ui::OldWindowDrawView& view);

// 0x499d60: with the normal blend, dragged or resizing it places its children; the body (0x464d90, with the bar
// 0x464e90 and the rows 0x465770); below, the line of the 2 and the black (the 7) of the buttons, with +0x774 at 2 or
// more only up to the portrait (0x78 before the right edge) and with a line on the side; above, the line and the black
// of the row of the 1014 with the sender, the date and the time; and the five buttons. Ends without blend. The error,
// as the chat's.
[[nodiscard]] Core::Expected<void, std::string> DrawTheReadWindow(Ui::OldWindowManager& manager, Ui::OldWindow& read,
                                                                  const Ui::OldWindowDrawContext& context,
                                                                  const Ui::OldWindowDrawView& view);

// 0x498480: with the normal blend, dragged or resizing it places its children (without the 9); above, the black (the
// 7) of the two rows and the lines of the 2 under each; below, the black and the line of the buttons; the 1000 and the
// 1005 right-aligned in the 1000's width (0x467060's mode 3); the three EDITs (0x468320); without blend, the buttons
// +0xa44 and +0xacc and, with +0xb0 at 1, +0xb54 and +0xbdc.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWriteWindow(Ui::OldWindowManager& manager, Ui::OldWindow& write,
                                                                   const Ui::OldWindowDrawContext& context,
                                                                   const Ui::OldWindowDrawView& view);

// 0x4a5537..0x4a5739, from 0x4a5490 with the normal blend: with chats in +0xb4 and the list in view, the ones not
// minimized; with any, the background (the 3) and the border (the 15) with the blink, (row + 4) · count high above
// +0x104, and their rows (0x4a5750): the chosen one and the waiting ones with their stripe (the 0x10 and the 0x11), the
// title's friend (strtok and 0x459c70) and, with more than two members, the win_mark.tga mark (0x64c) in red. The
// error is 0x459c70's.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatsInTheList(Ui::OldWindowManager& manager,
                                                                      const Ui::OldWindow& window,
                                                                      const Ui::OldWindowDrawContext& context,
                                                                      const Ui::OldWindowDrawView& view);

// 0x4a5120, from the bottom bar's draw (0x5a9ddb) with the window list: the friends button's icons (at +0x50 and +0x104
// of the list, with +0x58 × +0x108). Lit the first 12 frames of every 24 (+0x148). With the friends window open, the
// pressed button (win_push.jpg, 0x64f) and the icons at (x + 7, y + 3) and (x + 0x24, y + 7); without it, at (x + 4,
// y + 2) and (x + 0x21, y + 5). With waiting chats (+0x140, 0x4a6410), win_man.jpg (0x64e) lit (with the friends
// window, its bottom half). The letter (win_letter.jpg, 0x64d): without a new letter (+0x144), steady with some unread
// (0x4a0310: +0x134 at 0); with a new letter, lit, and six blinks turn it off (+0x14c, one at the 11 of every 24) or,
// with the friends window, having none unread. No blend or color of its own: the bar's.
void DrawTheFriendBarIcons(Ui::OldWindowManager& manager, Render::SpriteBatch& sprites);

// The letter windows' vtable + 0x54, at the end of the frame: 0x49a1d0 (the read one, with +0x774 at 2 or more) and
// 0x498800 (the write one, with +0xb0 at 1) draw the portrait (its vtable + 0x14, 0x497c10) with
// OldWindowDrawView::portrait, which the game gives (Messenger::DrawTheLetterPortrait); without it, it stops. Without
// the portrait, nothing.
[[nodiscard]] Core::Expected<void, std::string> DrawTheLetterPortrait(Ui::OldWindow& window,
                                                                      const Ui::OldWindowDrawView& view);
} // namespace Client::Messenger
