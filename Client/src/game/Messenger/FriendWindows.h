#pragma once

#include "Core/Expected.h"
#include "Ui/OldWindows.h"
#include "Ui/OldEditInput.h" // IWYU pragma: export

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The friends window (CL9 milestone 9; 06 § HUD y ventanas, "Los amigos"): what the game and packets C0..C4 ask of
// the old windows' manager. The window, its tabs and the add-a-friend box are classes of the manager (OldWindowKind)
// and run through its step (Ui/OldWindows); here is what is called from outside.
namespace Client::Ui
{
struct OldWindowDrawView;
} // namespace Client::Ui

namespace Client::Messenger
{
// The x and y of the first time (0x6599a0 with panel 1, 0x659f3d, and 0x4906a0).
inline constexpr std::int32_t FriendsWindowX = 0x186;
inline constexpr std::int32_t FriendsWindowY = 0x106;
// +0xb0 of the friends window, which is also the place in OldWindow::tabs: friends, letters and windows.
inline constexpr std::int32_t FriendTabNumber = 0;
inline constexpr std::int32_t LettersTabNumber = 1;
inline constexpr std::int32_t WindowsTabNumber = 2;
inline constexpr std::size_t FriendsTabCount = 3;
// 0x491080: the level needed (+0xe of the own record) and the text without it.
inline constexpr std::uint16_t FriendsLevel = 6;
inline constexpr std::size_t FriendsLevelText = 1067;
// 0x491080: the Text.bmd help banner (0x77183ac) in the normal queue (type 2), at 2.5 and in 0xffc8dce6.
inline constexpr std::size_t FriendsHelpText = 1244;

// 0x491080 after the level: with the friends window hidden (3), it shows it (0x490670) with sounds 0x19 and 0x1c;
// shown (0), it releases it (0x4914e0); with another state, nothing; without it, it builds it (0x48ee60(3)) at (x, y)
// the first time or where it was, raises it, notes the windows (0x4914f0) and, if it existed, gives it back its size
// and stretch, settles it and runs a whole step of the windows (0x490490). Returns whether it built it: then 0x491080
// puts up the help banner (0x46b720) if the queues are idle.
[[nodiscard]] Core::Expected<bool, std::string> OpenTheFriendsWindow(Ui::OldWindowManager& manager, std::int32_t x,
                                                                     std::int32_t y,
                                                                     const Ui::OldWindowGlobals& globals,
                                                                     const Ui::OldWindowStepView& view);

// 0x49ddd0 over the window's friends tab (+0xb8), if there is one: copies the model to the list (0x49bd60) and leaves
// chosen the last one with the name of the one that was chosen.
void RefreshTheFriendTab(Ui::OldWindowManager& manager);

// 0x4918d0: +0xe8 and the title, 990 with the list or 1066 without it; changes the window's if it exists.
void MarkTheFriendList(Ui::OldWindowManager& manager, std::int32_t received, const Ui::OldWindowTexts& texts);

// 0x4a4440: the name of the one who asked to be a friend, in the C2's box.
void RememberTheFriendRequest(Ui::OldWindowManager& manager, std::int32_t box, std::string_view name);

// What the drawing of the friends window (0x4a2620), its tabs (0x49cc20 and 0x49e7f0) and the add box (0x4a3750) does
// before drawing: dragged or resizing, they place and measure their children again (the 8 and the 9 with 0x45aa50),
// and the friends window passes them the state. The drawing itself is in Messenger/FriendWindowsDraw.
[[nodiscard]] Core::Expected<void, std::string>
SettleTheFriendsForTheDraw(Ui::OldWindowManager& manager, Ui::OldWindow& window, const Ui::OldWindowDrawView& view);
} // namespace Client::Messenger
