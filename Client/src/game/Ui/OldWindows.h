#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendModel.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The old windows (CL8 milestone 7; 06 § HUD y ventanas, "Las ventanas viejas"): the manager at 0x5763514
// (0x48e470, 0xf4 bytes), the framed windows of 0x491ca0, the message box of vtable 0x6b9cb4, its buttons (0x45ad80)
// and the window list at [0x5763524]. The original builds them on the base window 0x45a210 (vtable 0x6b8d6c) and
// dispatches by vtable; MuRet keeps them as data (OldWindow, with its class in `kind`) and each vtable function is a
// function here with the address of each class's method. The original's MSVC6 STL std::map, std::list and std::deque
// are MuRet's STL ones, with the same order. Milestone 9's: the std::deque<WindowRow> of the windows tab (erase and
// its iterators: 0x474070, 0x474460, 0x478640, 0x4786d0, 0x478750, 0x478d00, 0x489d80 and 0x489e20) and the
// manager's two std::maps (0x4a95e0, 0x4aa7a0 and 0x4aaf40). 0x4a81f0 and 0x4aa0b0 use another map, milestone 10's
// letters/chats one (sentinel 0x576308c, against 0x5763094 for the windows one).
// The destructors of the friends window, the add box, the tabs and their base, the frame, the friend list, the window
// list and the EDIT (0x4621e0, 0x467710, 0x488090, 0x488660, 0x488690, 0x48fb30, 0x48fb80, 0x48fbb0, 0x48fc50,
// 0x48fcf0, 0x48fd60, 0x48fe30, 0x48fe60, 0x48ff00, 0x48ff30, 0x4900b0, 0x4900e0 and 0x491d50) free resources by
// value when the window is released (0x490200). The button's global cleanup (0x45adf0) is kept too; the native EDIT's
// destructor 0x467710 neither destroys its HWND nor changes the focus (H09-U1).
//
// CL9 (milestone 9) adds the friends window (vtable 0x6b9d6c) with its three tabs (0x6b9e24, the friends one;
// 0x6b9edc, the windows one, and 0x6b9f38, the letters one, which is milestone 10's) and the add-a-friend box
// (0x6b9d10), on the controls of Ui/OldControls. The letter windows and the friend chat window are milestone 10's.
namespace Client::Ui
{
struct OldWindowTexts;
struct OldWindowMeasure;

// The frame's +0x4c (0x492500 and 0x492ac0): the title bar with the cross, the border, the resize corner, dragging by
// the bar and the bar's two buttons (minimize, which sends message 2, and stretch, 3). 0x491ca0 sets all of them
// (0x3f); the message box, 0xb (0x4a38f0); the window list, none (0x4a48b0).
inline constexpr std::int32_t TitleFlag = 0x1;
inline constexpr std::int32_t BorderFlag = 0x2;
inline constexpr std::int32_t ResizeFlag = 0x4;
inline constexpr std::int32_t DragFlag = 0x8;
inline constexpr std::int32_t MinimizeFlag = 0x10;
inline constexpr std::int32_t StretchFlag = 0x20;

// 0x48ee60: the factory's cases. 0 is a bare frame; 3, the friends window; 4, the add-a-friend box (modal); 5 and 8
// are modal boxes (0x5762ebc), 9 and 10 are not (they go to the +0xb8 list); 5 and 9 have two buttons, 8 and 10 one.
// 1 and 2 (the friend chat) and 6 and 7 (the letters) are milestone 10's.
inline constexpr std::int32_t FrameCase = 0;
inline constexpr std::int32_t FriendsCase = 3;
inline constexpr std::int32_t AddFriendCase = 4;
inline constexpr std::int32_t ModalQuestionCase = 5;
inline constexpr std::int32_t ModalNoticeCase = 8;
inline constexpr std::int32_t QuestionCase = 9;
inline constexpr std::int32_t NoticeCase = 10;
// The friend chat: 1 shown (the one I asked for) and 2 minimized (the one I am called to); 6, reading a letter; 7,
// writing one (CL9 milestone 10).
inline constexpr std::int32_t ChatCase = 1;
inline constexpr std::int32_t CalledChatCase = 2;
inline constexpr std::int32_t LetterReadCase = 6;
inline constexpr std::int32_t LetterWriteCase = 7;
// Bit 0 of 0x48ee60's last argument: do not run the cascade over the ones at the same place.
inline constexpr std::int32_t KeepPlaceFlag = 0x1;

// [0x5763524]'s +0x40. *Inferred*, like the chat log's (9): the one after it in WinMain (0x4bb786). Only the order
// and that they differ matter (P-C97).
inline constexpr std::int32_t WindowListId = 10;

// What the 0x491ca0 frame adds (vtable 0x6b9f9c).
struct OldFrame
{
    // +0x7c and +0x80: the mouse when the drag or the resize started.
    std::int32_t grabX = 0;
    std::int32_t grabY = 0;
    // +0x84: the resize corner: 0x87 bottom right, 0x13b top left.
    std::int32_t resizeCorner = 0;
    // +0x88..+0x94: the minimum and maximum size (0, no maximum).
    std::int32_t minWidth = 100;
    std::int32_t minHeight = 100;
    std::int32_t maxWidth = 0;
    std::int32_t maxHeight = 0;
    // +0x98 (strdup of 0x4920e0).
    std::optional<std::string> title;
    // +0x9c: 1 in the ones with their own EDIT (the add-a-friend box, 0x4a3700; the friend chat, milestone 10); 0 in
    // the rest.
    std::int32_t editField = 0;
    // +0xa0: the bar button under the mouse (3, the cross; 2, stretch; 1, minimize), from the last step.
    std::int32_t hotButton = 0;
    // +0xa4, +0xa8 and +0xac: stretched from the top to the bar (0x4930f0: y at 0 and 0x1b0 tall), with the previous y
    // and height.
    std::int32_t stretched = 0;
    std::int32_t savedY = 0;
    std::int32_t savedHeight = 0;
};

// What the message box adds (vtable 0x6b9cb4, 0x3d4 bytes).
struct OldMessageBox
{
    // +0xb0: the window the answer is sent to (message 12), 0 none; −1, the answer to a friend request, which goes over
    // the network (C1 C2, 0x4a3b80).
    std::int32_t owner = 0;
    // +0x2b8: the one who asked to be a friend (0x4a4440 from the C2, 0x4dc5b0).
    std::string friendName;
    // +0xb4: with 1, a single button (cases 8 and 10).
    std::int32_t oneButton = 0;
    // +0xb8 and +0x1b8: the text cut into 2 lines of 0x7d (0x459c70).
    std::array<std::string, 2> lines;
    // +0x2c4 and +0x34c. With a single button, the second one still exists: without a parent, at (0, 0).
    std::array<OldButton, 2> buttons;
};

// What the window list at [0x5763524] adds (vtable 0x6ba074: + 0x1c 0x4a4e20, + 0x50 0x4a5490 and + 0x58 0x4a4eb0).
// Its id queues (+0xb4, the friend chats 0x4a4990 adds, and +0x118, the waiting ones) are only filled by the friend
// chats (milestone 10).
struct OldWindowListPart
{
    // +0x100: a row's height in 640 × 480, which the drawing measures the first time (GetTextExtentPointA of "0").
    float rowHeight = 0.0f;
    // +0x104 and +0x108: the bottom edge and a row's height with the margin (0x4a48b0).
    std::int32_t bottom = 0x1b3;
    std::int32_t rowStep = 0x12;
    // +0x10c and +0x110: the chosen one's blink and what it adds per draw (0.25 with F5, −0.25 when hiding it).
    float blink = 0.0f;
    float blinkStep = 0.0f;
    // +0x150: F5 showed it with 1 (Enter chooses; 0x4a4e20).
    std::int32_t armed = 0;
    // +0x144: a new letter arrived (0x4a6420). Opening the friends window opens it on the letters tab. The bar icons
    // (0x4a5120) clear it after six blinks or, with the friends window open, with no unread letters.
    std::int32_t newLetter = 0;
    // +0x148 and +0x14c: the bar icons' draws (0x4a5120, one more per draw: lit the first 12 of every 24) and the new
    // letter's blinks (one more at the 11th of every 24). 0x4a48b0 sets them to 0.
    std::int32_t barBlink = 0;
    std::int32_t letterBlinks = 0;
    // +0xb4: the friend chats, in the order they were built (0x4a4990); 0x4a6430 looks them up by friend. The MSVC6
    // std::deque<int> of +0xb4 and +0x118 (0x4a6f40 0x4a7b90 0x4a9630 0x4a96c0 0x4a9810 0x4aaa40 0x4aab90 0x4aac10
    // 0x4aac80 0x4aafa0 0x4ab030 0x4ab220 0x4ab290 0x4ab380; P-C215, P-C216) are MuRet's.
    std::deque<std::int32_t> chats;
    // +0xf0: the chosen one, an iterator into +0xb4 (0x4a5be0, 0x4a4eb0): here its position, with the size of `chats`
    // for the end. 0x4a48b0 puts it at the end, and so does 0x4a4b30 when removing a chat. When one is added (0x4a4990)
    // an end comes to point at it, like the MSVC6 deque iterator, which stays at the slot being filled.
    std::size_t cursor = 0;
    // +0x118: the chats with an unseen message, for F5 (0x4a5e50, which does not repeat).
    std::deque<std::int32_t> waiting;
    // +0x158: the friends a room was asked for (C1 CA) whose CA has not come back (0x4a6910, 0x4a6af0, 0x4a6bc0). Its
    // MSVC6 container (0x475c00, 0x469540: 0x475f40 0x477a80 0x477b20 0x478a70 0x4a7f10 0x4a8060 0x4a9c00 0x4ab1b0) is
    // MuRet's (P-C216).
    std::deque<std::string> pending;
};

// A row of the windows tab's list (0x49e6e0, 0x4c bytes): chosen, the id and the title (strncpy of 0x40).
struct WindowRow
{
    std::int32_t selected = 0;
    std::int32_t id = 0;
    std::string title;
    std::int32_t aux = 0;
    friend bool operator==(const WindowRow&, const WindowRow&) = default;
};

// What a friends window tab adds (0x49c8b0, 0x49e5c0 and 0x4a06b0): its list at +0xb0 (the friends one 0x460970 with
// 0x14 elements, the windows one 0x462010 with 0x4c ones, the letters one 0x463190) and its buttons.
struct OldTabPart
{
    OldList list;
    // Four in the friends and letters tabs; one in the windows tab.
    std::vector<OldButton> buttons;
    // The friend list's deque: the model's copy (0x49bd60).
    std::deque<Messenger::FriendEntry> friends;
    std::deque<WindowRow> windows;
    // The letter list's: the model's copy (+0x12c, 0x49f500 from 0x4a0960), and +0x414, all checked (0x4a09d0).
    std::deque<Messenger::LetterEntry> letters;
    std::int32_t allChecked = 0;
};

// A friend chat line (the list 0x45f240 at +0x2a0, its vtable + 0x94 from 0x493f80): the speaker's name (0x461530 by
// number; empty, 0x6e57a4), the text, the type (1, system; 3, a member's) and the fourth argument.
struct ChatLine
{
    std::string name;
    std::string text;
    std::int32_t kind = 0;
    std::int32_t extra = 0;
    friend bool operator==(const ChatLine&, const ChatLine&) = default;
};

// What the friend chat adds (vtable 0x6b9dc8, 0x778 bytes). The members and the invitable ones are 0x14 rows like the
// friends: the name at +4, the member number at +0xf and the state at +0x10.
struct OldChatPart
{
    // +0xb0: 1 without the invite list; 2 with it (0x4944c0 with message 10 and 1).
    std::int32_t mode = 1;
    // +0xb4: the last text sent (0x100 bytes).
    std::string lastText;
    // +0x1b4: the EDIT, capped at 100.
    OldEdit edit;
    // +0x2a0: the lines (0x45f240). Its MSVC6 std::deque (0x473ac0 0x473e80 0x473ec0 0x474040 0x476e10 0x476e30
    // 0x476ea0 0x478500 0x478590 0x478610 0x487610 0x487b70 0x48ba10 0x48c9b0 0x48cd90, and 0x4723c0 and 0x489cd0 from
    // the unwind) is MuRet's; the destructors of the chat (0x48f910, 0x493160, with its member list 0x487d90), the
    // reading one (0x48f940), the writing one (0x48f970, 0x48f9a0), the portrait (0x495ef0, 0x495f20) and the two lists
    // (0x4633b0 0x4633e0 0x488960 0x488990; 0x4647e0 0x464810 0x488360 0x488390) are C++'s on releasing the window
    // (P-C216).
    //   - `lineRows` (the list's +0x7c deque): the lines as they arrive (0x45f6f0). The original adds them at the
    //     front; MuRet, at the back: the last of `lineRows` is the original's first.
    //   - `wrappedRows` (the +0xcc one): the same ones split at the list's width minus 0x1e (0x45fea0 with
    //     0x460130), the newest first, which is what the list shows (+0xc8 at 1). Its size is +0xf8
    //     (OldList::fixedCountValue).
    OldList lines;
    std::deque<ChatLine> lineRows;
    std::deque<ChatLine> wrappedRows;
    // +0x3d0: the room's members.
    OldList users;
    std::deque<Messenger::FriendEntry> members;
    // +0x518 and +0x6e8: the invite button (1: shows or hides the list) and the send-invitation one (2).
    OldButton inviteButton;
    OldButton sendButton;
    // +0x5a0: the friends that can be invited (0x4942d0).
    OldList invite;
    std::deque<Messenger::FriendEntry> invitable;
    // +0x770 (0x493530).
    std::uint32_t room = 0;
    // +0x774.
    std::int32_t aux = 0;
};

// The letter windows' portrait (vtable 0x6ba008, 0x495e50): +0xb0 in the reading one and +0xb4 in the writing one.
// Its character (+0x7c) and what builds, dresses and moves it (0x4961d0, 0x4977d0, 0x497810, 0x4978f0, 0x496530) and
// draws it (0x497c10) are in Messenger/LetterPortrait, via OldWindowStepView::portrait; Ui keeps what they ask it to.
struct OldPortraitPart
{
    OldWindowBase base;
    // +0x68c: the character is built (0x4961d0).
    std::int32_t built = 0;
    // The character's +0x250 (0x4977d0), the equipment (0x497810) and +0x6a4, the action (0x4978f0).
    std::uint8_t portraitClass = 0;
    std::array<std::byte, 17> equipment{};
    std::int32_t action = 0;
    // +0x6ac and +0x6b0, the starting and the current angle (0x497890; 0x497970's drag and right click), and +0x6b8 and
    // +0x6bc, the starting and the current zoom (0x4978d0; the wheel and the right click). 0x495e50 sets the zooms
    // to 1.
    float baseAngle = 0.0f;
    float angle = 0.0f;
    float baseZoom = 1.0f;
    float zoom = 1.0f;
    // +0x6b4: the mouse X of the drag's last step (0x497970).
    float dragX = 0.0f;
    // +0x690: the help shown (the bottom-left icon, 0x497970 and 0x497c10).
    std::int32_t help = 0;
    // +0x694 (vtable + 0x48, 0x495ee0): the writing one sets it to 1.
    std::int32_t follow = 0;
    // +0x6c0: the blank portrait (0x497970 and 0x497c10). Only 0x495e50 writes it (to 0), but in the reading one it
    // falls on its +0x770, the "webzen" letter the C7 sets (0x4dccda): OldLetterPart::fromWebzen carries it too.
    std::int32_t blank = 0;
    // +0x7c, built by Messenger/LetterPortrait; Ui does not look at it.
    std::shared_ptr<void> actor;
};

// What the letter windows add: the reading one (vtable 0x6b9c58, 0xc88 bytes) and the writing one (0x6b9bfc, 0xc64
// bytes).
struct OldLetterPart
{
    OldPortraitPart portrait;
    // The reading one: +0x778, the letter (0x138 bytes, 0x499cb0); +0x770 at 1, the "webzen" letter (no portrait, the
    // C7); +0x8b0, the list of the body lines (0x464620), split at '\n': `lineRows` as they arrive (0x464ae0, reversed
    // from the original, as in OldChatPart) and `wrappedRows` split at the list's width minus 0x1e (0x465270 with
    // 0x465450), the newest first; its size is +0xf8.
    Messenger::LetterEntry entry;
    std::int32_t fromWebzen = 0;
    OldList lines;
    std::deque<std::string> lineRows;
    std::deque<std::string> wrappedRows;
    // The reading one's +0x774: 2 in the constructor (0x48f460), and nobody else writes it (*static*). With 2 or more,
    // the drawing leaves room for the portrait at the bottom right (0x499e44) and draws it (0x49a1d0), and the step
    // copies it to the portrait (its +0x69c, 0x49b1ea).
    std::int32_t portraitMode = 0;
    // The writing one: +0x778, sent without an answer (the C5); +0x77c, 1 with the name set (0x4983e0) and 2 with the
    // subject (0x498420); +0x780, +0x86c and +0x958, the EDITs of the name, the subject and the body.
    std::int32_t sending = 0;
    std::int32_t prepared = 0;
    OldEdit to;
    OldEdit subject;
    OldEdit text;
    // Five in the reading one (+0x9e0..+0xc00), four in the writing one (+0xa44..+0xbdc).
    std::vector<OldButton> buttons;
    // The writing one's +0xb0: 0 in the constructor and 1 at the end of 0x497f50; with 1, 0x499780 goes through buttons
    // 4 and 5 and the portrait.
    std::int32_t withPortrait = 0;
};

// What the friends window adds (vtable 0x6b9d6c, 0xb50 bytes).
struct OldFriendsPart
{
    // +0xb0: the tab shown (0, friends; 1, letters; 2, windows).
    std::int32_t currentTab = 0;
    // +0xb4: the header under the mouse.
    std::int32_t hoverTab = 0;
};

// What the add-a-friend box adds (vtable 0x6b9d10, 0x2b0 bytes).
struct OldAddBoxPart
{
    // +0xb0: the tab it sends the name to.
    std::int32_t owner = 0;
    // +0xb4.
    OldEdit edit;
    // +0x1a0 and +0x228.
    std::array<OldButton, 2> buttons;
};

enum class OldWindowKind : std::uint8_t
{
    // 0x491ca0, vtable 0x6b9f9c.
    Frame,
    // vtable 0x6b9cb4.
    MessageBox,
    // vtable 0x6ba074.
    WindowList,
    // vtable 0x6b9d6c.
    FriendsWindow,
    // vtable 0x6b9e24, 0x6b9edc and 0x6b9f38: the friends window's tabs.
    FriendTab,
    WindowsTab,
    LettersTab,
    // vtable 0x6b9d10.
    AddFriendBox,
    // vtable 0x6b9dc8: the friend chat (cases 1 and 2).
    ChatWindow,
    // vtable 0x6b9c58 and 0x6b9bfc: reading and writing a letter (cases 6 and 7).
    LetterReadWindow,
    LetterWriteWindow,
};

struct OldWindow
{
    // The allocation's identity, unlike the reusable native id. It stands in for the second map's pointers without
    // tying a stale entry to another window when the manager is copied or after the counter wraps.
    std::uint64_t allocationId = 0;
    OldWindowKind kind = OldWindowKind::Frame;
    OldWindowBase base;
    OldFrame frame;
    OldMessageBox box;
    OldWindowListPart list;
    OldFriendsPart friends;
    // The friends window's +0xb8, +0x738 and +0x4d0, by tab number: 0 the friends, 1 the letters, 2 the windows.
    std::vector<OldWindow> tabs;
    OldTabPart tab;
    OldAddBoxPart add;
    OldChatPart chat;
    OldLetterPart letter;
};

// 0x48f6f2..0x48f6f5 and 0x49090c..0x49090e: both tables compare the id's 32 bits unsigned.
struct OldWindowIdLess
{
    [[nodiscard]] bool operator()(std::int32_t left, std::int32_t right) const
    {
        return static_cast<std::uint32_t>(left) < static_cast<std::uint32_t>(right);
    }
};

struct OldTabReference
{
    std::int32_t ownerId = 0;
    std::uint64_t allocationId = 0;
};

// *(0x5763514), and the old windows' globals that only it uses.
struct OldWindowManager
{
    // +0x4c
    std::map<std::int32_t, OldWindow, OldWindowIdLess> windows;
    // +0x5c: the second map keeps pointers to the tabs (0x4908d0). The allocation reference keeps that identity by
    // value: the first entry wins and neither 0x490200 nor Reset (0x48ea50) erases it.
    std::map<std::int32_t, OldTabReference, OldWindowIdLess> tabs;
    // 0x48f7b2 rejects a duplicate id without destroying the new object; its tabs are already in the second map. It is
    // kept until the manager is destroyed, also when the drawing's projection is copied and on Reset.
    std::list<OldWindow> unregisteredWindows;
    // MuRet's ownership metadata; neither the native counter nor Reset resets it.
    std::uint64_t lastAllocationId = 0;
    // +0x80: the ids bottom up; the last one is the top one.
    std::list<std::int32_t> order;
    // +0x08..+0x3c: the manager's message queue and the one it is handling.
    std::deque<OldMessage> queue;
    OldMessage current;
    // +0x40: 1 after the drawing (0x490390), 0 after the step (0x490490). Nobody reads it in CL8.
    std::int32_t drawn = 0;
    // +0x44
    std::int32_t focus = 0;
    // +0x48: the friends window's id (0x48eff1..0x48eff6), not its pointer.
    std::int32_t friends = 0;
    // +0xa8
    std::int32_t hiddenByF6 = 0;
    // +0xac
    std::list<std::int32_t> hidden;
    // +0xb8: the non-modal message boxes (0x4917e0, 0x491840).
    std::list<std::int32_t> boxes;
    // +0xc4..+0xdc: the friends window's place, size, pre-stretch y and height and whether it was stretched, when
    // released (0x490200); 0x491080 reopens it so if the width is not 0.
    std::int32_t friendsX = 0;
    std::int32_t friendsY = 0;
    std::int32_t friendsWidth = 0;
    std::int32_t friendsHeight = 0;
    std::int32_t friendsSavedY = 0;
    std::int32_t friendsSavedHeight = 0;
    std::int32_t friendsStretched = 0;
    // +0xe0: C4 00 (offline) was sent.
    std::int32_t offlineSent = 0;
    // +0xe4: the tab shown when it was released.
    std::int32_t lastTab = 0;
    // +0xe8: the list arrived (C0, 0x4918d0(1)); with 0, opening the window asks for it. 0x48ea50 sets it to 1.
    std::int32_t listReceived = 1;
    // +0xec: the title text, 990 or 1066 (0x4918d0, 0x48ea50).
    std::int32_t titleText = 990;
    // +0xf0: the friends tab's add-a-friend box (0x49d0b0); 0x490200 sets it back to 0.
    std::int32_t letter = 0;
    // [0x5763518]
    Messenger::FriendModel friendModel;
    // 0x5767970: the letter cap the C0 sends (0x4dc010); the C6 reads it (0x4dcb4c).
    std::uint8_t letterLimit = 0;
    // [0x576351c] and [0x5763520]: the letters and the friend chat rooms (CL9 milestone 10).
    Messenger::LetterModel letterModel;
    Messenger::ChatRooms chatRooms;
    // +0x94: each letter's reading window (0x491720; 0x491570 and 0x491780 look it up).
    std::map<std::uint16_t, std::int32_t> letterWindows;
    // 0x5762f0c and 0x5762f10: where the C7 opens the next reading window (−1, at (100, 100); 0x49a1f0 sets them when
    // moving to another letter).
    std::int32_t nextReadX = -1;
    std::int32_t nextReadY = -1;
    // The id of the old windows' EDIT with the keyboard focus (GetFocus, with ChatFocus::OldWindow).
    std::int32_t focusedEdit = 0;
    // MuRet's snapshot of the focused EDIT whose wrapper is already destroyed; it keeps only the state of the control
    // that still gets input, not the original's leaked HWND.
    bool focusedEditIsOrphan = false;
    std::optional<OldEdit> orphanedEdit;
    // 0x5762ebc: the modal box. With one, the factory builds no other.
    std::int32_t modal = 0;
    // 0x5762eb0: the last id 0x459ef0 gave.
    std::int32_t lastId = WindowListId;
    // [0x5763524]: the window list, which WinMain builds apart (it is not in the map).
    OldWindow windowList = MakeTheWindowList();

    // 0x491ca0 at 0x4bb786, with vtable 0x6ba074 and 0x4a48b0: above the bar, at (0x246, 0x1b3), 0x34 wide and 0 tall,
    // without flags and hidden.
    [[nodiscard]] static OldWindow MakeTheWindowList();
};

// The step's globals: 0x7d36874 (the left button down), 0x7d366d4 (the click edge), 0x7d366ec (the release edge, which
// WM_LBUTTONUP sets), 0x775857c, 0x5762eb8 (the one under the mouse), 0x5762eb4 (the one that captured the mouse),
// 0x5762ec4 (the pressed button), the keyboard focus (GetFocus and SetFocus with [0x5763544]) and the last sound
// (0x41ed10).
struct OldWindowGlobals
{
    bool& leftHeld;
    bool& leftClick;
    bool& releasedClick;
    std::uint8_t& mouseOnUi;
    std::int32_t& hovered;
    std::int32_t& capture;
    std::int32_t& pressedButton;
    Chat::ChatFocus& keyboardFocus;
    std::optional<Audio::SoundId>& sound;
    // 0x5762ec0 (the list that takes the arrows), 0x7d366d8 (the wheel), 0x7d36880 (the right click) and 0x7d36849 (the
    // double click, fed by SDL like WM_LBUTTONDBLCLK: P-C182/P-C187).
    std::int32_t& focusedList;
    std::int32_t& wheel;
    bool& rightClick;
    bool& doubleClick;
    // 0x77f3b10 and 0x6c8dcc (Movement::GameMovementState): a click on an EDIT sets them to 0 and 6 (0x4690e0). Without
    // them (a step without the game), they are not touched.
    std::int32_t* mouseUpdateTime = nullptr;
    std::int32_t* mouseUpdateTimeMax = nullptr;
    std::function<void(Audio::SoundId)> soundRequest{};

    void RequestSound(Audio::SoundId id) const
    {
        if (soundRequest)
            soundRequest(id);
        else
            sound = id;
    }
};

// What the friends windows send (Protocol/Messenger/Friends; Messenger builds it and sends it through the session).
enum class FriendSendKind : std::uint8_t
{
    // C1 03 C0.
    ListRequest,
    // C1 0D C1 and the name.
    Add,
    // C1 0E C2, the answer and the name.
    Answer,
    // C1 0D C3 and the name.
    Delete,
    // C1 04 C4 and the state.
    State,
    // C1 0D CA and the name: asks for a room to chat with the friend.
    RoomRequest,
    // C1 14 CB: invites `name` to room `room` of window `window`.
    Invite,
    // C2 C5 (encrypted like C4): the letter of window `window` to `name`, with `subject`, `body`, the direction
    // (`value`) and the portrait's action.
    LetterSend,
    // C1 06 C7 and C1 06 C8 with letter `index`, and C1 03 C9.
    LetterRead,
    LetterDelete,
    LetterList,
};
struct FriendSend
{
    FriendSendKind kind = FriendSendKind::ListRequest;
    std::uint8_t value = 0;
    std::string name;
    std::uint16_t index = 0;
    std::uint16_t room = 0;
    std::int32_t window = 0;
    std::uint8_t action = 0;
    std::string subject{};
    std::string body{};
    friend bool operator==(const FriendSend&, const FriendSend&) = default;
};

// What Ui asks of the portrait (OldPortraitPart), with the address of the vtable 0x6ba008 method.
enum class PortraitCallKind : std::uint8_t
{
    // +0x30, 0x4961d0: builds the +0x7c character in the hero's cell.
    Build,
    // +0x34, 0x4977d0 (with 0x513100).
    SetClass,
    // +0x38, 0x497810 (with 0x5139f0).
    SetEquipment,
    // +0x3c, 0x496530: copies the hero's class and equipment.
    CopyTheHero,
    // +0x4c, 0x4978f0 (with 0x495a20).
    SetAction,
    // +0x50, 0x497910: the action after (1) or before (−1) the one at +0x6a4 (0x495a20).
    TurnAction,
    // +0x40, 0x497890 with the character built: its angles (+0x1c..+0x24). Ui sets the window's angle.
    SetAngle,
    // The character part of the portrait's step (0x4979c0..0x4979cf): the published hero's +0x360 into the character's
    // +0x360. The queue, the mouse and the hero copy (vtable + 0x3c) are Ui's (0x497970).
    Step,
};
// The third argument is TurnAction's shift or Step's argument; the rest pass 0.
using OldPortraitHook =
    std::function<Core::Expected<void, std::string>(OldPortraitPart&, PortraitCallKind, std::int32_t)>;
using OldWindowSender = std::function<void(const FriendSend&)>;

// What the step reads from the game.
struct OldWindowStepView
{
    // 0x7d3682c and 0x7d36828: in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x553520(vk): a key's edge, with the latch all readers share.
    std::function<bool(std::uint8_t)> keyEdge{};
    // GetAsyncKeyState(vk) & 0x8000 (the lists' arrows, 0x47b0f0).
    std::function<bool(std::uint8_t)> keyDown{};
    // 0x77f3b69: the chat field is open (the F key does not close the friends window, 0x4a2f70).
    bool chatOpen = false;
    // The friends windows' texts, measure and wiring. Without them, what uses them bails out.
    const OldWindowTexts* texts = nullptr;
    const OldWindowMeasure* measure = nullptr;
    OldWindowSender send{};
    // 0x4694c0(0, 0, 1, name): the name to the whisper EDIT (the right click on a friend, 0x461f00).
    std::function<void(std::string_view)> whisperTo{};
    // 0x491080(0x186, 0x106): opens the friends window with its own record, its message and its banner, which are the
    // game's (Messenger/GameFriends). 0x4906a0 calls it when showing again with the friends window already released.
    std::function<Core::Expected<void, std::string>()> openFriends{};
    // The letters' portrait character (Messenger/LetterPortrait). Without it, what uses it bails out.
    OldPortraitHook portrait{};
    // 0x78abaac (GameUiState::field78abaac): with 1, the step of the portrait that follows the hero copies its
    // equipment again (0x4979b0).
    std::uint8_t field78abaac = 0;
    // 0x4dcb70 on the C7 copy 0x4a03c0 kept (0x4a11b0 with message 10 and 2): Hud applies it again.
    std::function<Core::Expected<void, std::string>(const std::vector<std::byte>&)> replayLetter{};
    // 0x622b40(text, 0): the bad-word filter, which sets the text 0x76e6dd4 when it triggers. Without it, nothing.
    std::function<bool(std::string_view)> badWord{};
    // [0x743a170] + 0x1d9 (0x495530, 0x461600, 0x4942d0).
    std::string heroName{};
};

// The message box's Text.bmd texts: the title and the button of the one-button box (228), the title of the two-button
// one (991) and the two buttons (1037 and 1038).
inline constexpr std::size_t NoticeText = 228;
inline constexpr std::size_t QuestionText = 991;
inline constexpr std::size_t YesText = 1037;
inline constexpr std::size_t NoText = 1038;
struct OldWindowTexts
{
    std::string notice;
    std::string question;
    std::string yes;
    std::string no;
    // The other Text.bmd texts the friends windows use, by index. Without it, empty.
    std::function<std::string(std::size_t)> lookup{};

    [[nodiscard]] std::string At(std::size_t index) const;
};

// What 0x459c70 measures when cutting a box's text: GetTextExtentPointA on the DC with its font.
struct OldWindowMeasure
{
    const Render::Text::ITextMeasure& measure;
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    Core::PixelSize screen{};
};

// What 0x48ee60(case, x, y, data, aux, flags) takes: x at −1 is 0 and y at −1, 0x14c. The data is the frame's title
// (0x4920a0, 0x4a2280, 0x4a35a0) or the box's text (0x4a38f0); aux, the frame's parent or the box's owner.
struct OldWindowRequest
{
    std::int32_t kind = FrameCase;
    std::int32_t x = -1;
    std::int32_t y = -1;
    std::string_view data;
    std::int32_t aux = 0;
    std::int32_t flags = 0;
};

// 0x490a00: the id's window in the first map or, failing that, in the second (a friends window tab), or none.
[[nodiscard]] OldWindow* FindTheOldWindow(OldWindowManager& manager, std::int32_t id);
// 0x490aa0.
[[nodiscard]] bool HasTheOldWindow(const OldWindowManager& manager, std::int32_t id);
// 0x490880: skips the friends window; 0 with none or with only the friends window.
[[nodiscard]] std::int32_t TopOldWindow(const OldWindowManager& manager);
// The id at the end of the list (+0x84 → previous → +8), or 0 with the list empty: what 0x490490, 0x492500 and
// 0x4a43c0 look at.
[[nodiscard]] std::int32_t LastOldWindow(const OldWindowManager& manager);

// 0x459f70 on the manager and 0x490b40 (to the id's window, if present).
void PostToTheManager(OldWindowManager& manager, std::int32_t type, std::int32_t a, std::int32_t b);
void PostToTheWindow(OldWindowManager& manager, std::int32_t id, std::int32_t type, std::int32_t a, std::int32_t b);

// 0x48ee60: builds the window, places it (with the cascade unless KeepPlaceFlag), adds it to the map and to the top of
// the list and runs its vtable + 0x30. Returns its id, or 0 if it did not build it (a modal with another modal, the
// friends window with another, or the cascade out of room). The friends and add-a-friend ones use the step's globals
// and view (the focus, the list request); without them, they bail out.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
CreateTheOldWindow(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts,
                   const OldWindowMeasure& measure, const OldWindowGlobals* globals = nullptr,
                   const OldWindowStepView* view = nullptr);

// 0x636e3c: the window list's vtable + 0x18 (0x45aa80) with 0, before 0x490490.
[[nodiscard]] Core::Expected<void, std::string>
StepTheWindowList(OldWindowManager& manager, const OldWindowGlobals& globals, const OldWindowStepView& view);

// 0x490490: the modal that is gone, Esc, F5, F6, the hidden friends window, each window's step top down and the
// manager's messages.
[[nodiscard]] Core::Expected<void, std::string>
StepTheOldWindows(OldWindowManager& manager, const OldWindowGlobals& globals, const OldWindowStepView& view);

// 0x490200: takes the window out of the map, the list and the boxes, and releases it; 0x45adf0 releases its buttons'
// press, keeping another window's button that is still alive.
[[nodiscard]] Core::Expected<void, std::string> DestroyTheOldWindow(OldWindowManager& manager, std::int32_t id,
                                                                    std::int32_t& pressedButton);

// 0x4906a0(hide, with the friends window): hides (state 3) or shows again the ones F6 hid; if one it hid is gone (the
// friends window 0x4905a2 released), opens the friends window with view->openFriends.
[[nodiscard]] Core::Expected<void, std::string> HideTheOldWindows(OldWindowManager& manager, std::int32_t hide,
                                                                  std::int32_t withFriends,
                                                                  const OldWindowGlobals& globals,
                                                                  const OldWindowStepView* view = nullptr);

// 0x490830
void ForgetTheHiddenWindows(OldWindowManager& manager);

// 0x48ea50, the game's first step (0x636290): releases all the windows and empties the lists, the modal to 0, and
// +0xe4, +0xe8 (to 1), +0xec (990), +0xf0 and +0xe0. The C4 02 it sends with 0x6c7b8c at 0 never runs (Q-C886).
void ResetTheOldWindows(OldWindowManager& manager, std::int32_t& pressedButton);

// 0x4914e0: if there is one.
[[nodiscard]] Core::Expected<void, std::string> DropTheFriendsWindow(OldWindowManager& manager,
                                                                     std::int32_t& pressedButton);

// 0x45aa50(8, 0, 0) on a box's two buttons: places them against it again (0x4a3a50 and 0x4a3a90).
void SettleTheButtons(OldWindowManager& manager, OldWindow& box);

// 0x4a5e10: if shown.
void HideTheWindowList(OldWindow& list);

// 0x4a5be0(arm), F5 (0x490538 with the focus on the game and 0x467d3b in an EDIT): with friend chats, +0x150 = arm
// and, hidden, shows it (height and y by the +0x100 row plus 4, the blink rises by 0.25) and, with 1, chooses the
// first one that is not the top one; shown, with 1, chooses the next one, wrapping.
void ShowTheWindowList(OldWindowManager& manager, std::int32_t arm);
} // namespace Client::Ui
