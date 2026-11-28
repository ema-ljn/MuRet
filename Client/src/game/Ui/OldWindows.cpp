#include "Ui/OldWindows.h"

#include "Core/Expected.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatLogWindow.h"
#include "Messenger/LetterWindows.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditLines.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindowSteps.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <string>
#include <utility>

namespace Client::Ui
{
namespace
{
// 0x459f00
constexpr std::int32_t TopLeftRect = 1;
// 0x41ed10(0x19, 0, 0)
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;
// 0x553520
constexpr std::uint8_t EscapeKey = 0x1b;
constexpr std::uint8_t F5Key = 0x74;
constexpr std::uint8_t F6Key = 0x75;
constexpr std::uint8_t EnterKey = 0x0d;
// Bounds dragging and resizing (0x492ac0) and the cascade (0x48ee60).
constexpr std::int32_t ScreenWidth = 0x280;
constexpr std::int32_t ScreenHeight = 0x1e0;
// 0x492ac0: the title bar buttons are 9 × 9 at y + 8, the cross at x + w − 0x10, the stretch one at x + w − 0x1b and
// the minimize one at the same place or, with the stretch one, at x + w − 0x26; the resize corners, 7 at the top left
// and 10 at the bottom right, and the drag bar, 0x14 tall.
constexpr std::int32_t TitleButtonSize = 9;
constexpr std::int32_t TitleButtonY = 8;
constexpr std::int32_t CloseButtonX = 0x10;
constexpr std::int32_t StretchButtonX = 0x1b;
constexpr std::int32_t MinimizeAloneX = 0x1b;
constexpr std::int32_t MinimizeWithStretchX = 0x26;
constexpr std::int32_t TopLeftGrip = 7;
constexpr std::int32_t BottomRightGrip = 10;
constexpr std::int32_t DragBarHeight = 0x14;
// The frame's +0x84: the grabbed corner.
constexpr std::int32_t BottomRightCorner = 0x87;
constexpr std::int32_t TopLeftCorner = 0x13b;
// 0x48f890..0x48f8e0, the frame's vtable + 0x3c..+0x48: the client area starts at (x + 5, y + 0x15) and is
// w − 10 × h − 0x1a.
constexpr std::int32_t ClientLeft = 5;
constexpr std::int32_t ClientTop = 0x15;
constexpr std::int32_t ClientWidthCut = 10;
constexpr std::int32_t ClientHeightCut = 0x1a;
// 0x4930f0: stretched, the window goes from y = 0 down to the bottom bar.
constexpr std::int32_t StretchedHeight = 0x1b0;
// 0x468d50: the mouse over the EDIT, 4 above and 4 below; the 13 × 13 arrows at x + w − 0xf, y − 4 and y + h − 9;
// the bar at x + w − 0xe; the counters repeat on their own after 15 (like the list's); grabbed, state 2; the ratio's
// half point (0x6b882c); 0x6c8dcc at 6.
constexpr std::int32_t EditArrowRise = 4;
constexpr std::int32_t EditMouseSlack = 8;
constexpr std::int32_t EditArrowInset = 0xf;
constexpr std::int32_t EditArrowDrop = 9;
constexpr std::int32_t EditArrowSize = 13;
constexpr std::int32_t EditBarInset = 0xe;
constexpr std::int32_t EditRepeatSteps = 0xf;
constexpr std::int32_t DraggedEdit = 2;
constexpr double EditDragHalf = 0.5;
constexpr std::int32_t EditMouseUpdateTimeMax = 6;
// 0x48ee60: the cascade shifts 20 each time; out of room, it goes back to (r + 1, r + 1) with r = y % 10, and with
// r = 9 it does not build it.
constexpr std::int32_t CascadeStep = 0x14;
constexpr std::int32_t CascadeWrap = 10;
constexpr std::int32_t CascadeGiveUp = 9;
// 0x48ee60
constexpr std::int32_t DefaultY = 0x14c;
// 0x4920a0: the bare frame, at (0x32, 0x32), 0xd5 × 0xaa.
constexpr std::int32_t FramePlace = 0x32;
constexpr std::int32_t FrameWidth = 0xd5;
constexpr std::int32_t FrameHeight = 0xaa;
// 0x491ca0: all the flags and the 100 × 100 minimum.
constexpr std::int32_t AllFrameFlags = 0x3f;
// 0x45ad80
constexpr std::int32_t ButtonWidth = 0x46;
constexpr std::int32_t ButtonHeight = 0x14;
// 0x4a38f0: the box at (0x32, 0x32), 0x96 × 100, which does not shrink below that, with the bar, the border and the
// drag; the text in 2 lines of 0x7d; the 0x32 × 0x14 buttons at (0x12, 0x28) and (0x49, 0x28) of the client area, or
// the single one at (0x2d, 0x28).
constexpr std::int32_t BoxPlace = 0x32;
constexpr std::int32_t BoxWidth = 0x96;
constexpr std::int32_t BoxHeight = 100;
constexpr std::int32_t BoxFlags = TitleFlag | BorderFlag | DragFlag;
constexpr int BoxWrapWidth = 0x7d;
constexpr int BoxLines = 2;
constexpr std::int32_t BoxButtonWidth = 0x32;
constexpr std::int32_t BoxButtonHeight = 0x14;
constexpr std::int32_t YesButtonX = 0x12;
constexpr std::int32_t NoButtonX = 0x49;
constexpr std::int32_t OnlyButtonX = 0x2d;
constexpr std::int32_t BoxButtonsY = 0x28;
// 0x4a3b80
constexpr std::int32_t YesCommand = 1;
constexpr std::int32_t NoCommand = 2;
// 0x4a48b0: the window list, at (0x246, 0x1b3), 0x34 wide.
constexpr std::int32_t WindowListX = 0x246;
constexpr std::int32_t WindowListY = 0x1b3;
constexpr std::int32_t WindowListWidth = 0x34;
// 0x4a5e10: hidden, the blink drops by 0.25; 0x4a5be0: shown, it rises.
constexpr float WindowListBlinkDown = -0.25f;
constexpr float WindowListBlinkUp = 0.25f;
// 0x6b8a44: what 0x4a4eb0, 0x4a5be0 and 0x4a5490 add to the row height (+0x100).
constexpr double WindowListRowMargin = 4.0;

// 0x45ae34..0x45ae47: each button's destructor only releases its own global press.
// The EDIT of `window` that has the focus (and is not already the orphan), or nullptr.
OldEdit* FocusedEditOf(const OldWindowManager& manager, OldWindow& window)
{
    if (manager.focusedEditIsOrphan)
        return nullptr;
    if (window.kind == OldWindowKind::AddFriendBox && window.add.edit.base.id == manager.focusedEdit)
        return &window.add.edit;
    if (window.kind == OldWindowKind::ChatWindow && window.chat.edit.base.id == manager.focusedEdit)
        return &window.chat.edit;
    if (window.kind != OldWindowKind::LetterWriteWindow)
        return nullptr;
    for (OldEdit* edit : {&window.letter.to, &window.letter.subject, &window.letter.text})
    {
        if (edit->base.id == manager.focusedEdit)
            return edit;
    }
    return nullptr;
}

void ReleaseTheDestroyedButtons(const OldWindow& window, std::int32_t& pressedButton)
{
    const auto release = [&pressedButton](const OldButton& button)
    {
        if (button.base.id == pressedButton)
            pressedButton = 0;
    };
    if (window.kind == OldWindowKind::MessageBox)
        std::ranges::for_each(window.box.buttons, release);
    else if (window.kind == OldWindowKind::AddFriendBox)
        std::ranges::for_each(window.add.buttons, release);
    else if (window.kind == OldWindowKind::FriendTab || window.kind == OldWindowKind::WindowsTab ||
             window.kind == OldWindowKind::LettersTab)
        std::ranges::for_each(window.tab.buttons, release);
    for (const OldWindow& tab : window.tabs)
        ReleaseTheDestroyedButtons(tab, pressedButton);
}

} // namespace

namespace Steps
{
bool MouseIn(const OldWindowStepView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
             std::int32_t mode)
{
    return Chat::MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

bool IsShowing(std::int32_t state)
{
    return state != HiddenWindow && state != MinimizedWindow;
}

std::int32_t NextId(OldWindowManager& manager)
{
    // 0x459ef5: INC of a DWORD, sign-bit crossing and wrap to zero included.
    manager.lastId = static_cast<std::int32_t>(static_cast<std::uint32_t>(manager.lastId) + 1u);
    return manager.lastId;
}

// 0x45a210.
OldWindowBase MakeTheBase(OldWindowManager& manager)
{
    OldWindowBase base;
    base.id = NextId(manager);
    return base;
}

// 0x45ad80.
OldButton MakeTheButton(OldWindowManager& manager)
{
    OldButton button;
    button.base = MakeTheBase(manager);
    button.base.width = ButtonWidth;
    button.base.height = ButtonHeight;
    return button;
}

// 0x491ca0.
OldWindow MakeTheFrame(OldWindowManager& manager)
{
    OldWindow window;
    window.allocationId = ++manager.lastAllocationId;
    window.base = MakeTheBase(manager);
    window.base.flags = AllFrameFlags;
    return window;
}

// 0x48ee60, cases 5, 8, 9 and 10: the frame, +0xb0 and +0xb4, the two buttons (0x45ad80) and vtable 0x6b9cb4.
OldWindow MakeTheMessageBox(OldWindowManager& manager, std::int32_t oneButton)
{
    OldWindow window = MakeTheFrame(manager);
    window.kind = OldWindowKind::MessageBox;
    window.box.owner = 0;
    window.box.oneButton = oneButton;
    window.box.buttons[0] = MakeTheButton(manager);
    window.box.buttons[1] = MakeTheButton(manager);
    return window;
}

// vtable + 0x3c..+0x48: 0x48f890..0x48f8e0 in the frame and its derived ones; 0x48fb40..0x48fb70 in the friends
// window's tabs, without border or bar.
bool HasNoInsets(const OldWindow& window)
{
    return window.kind == OldWindowKind::FriendTab || window.kind == OldWindowKind::WindowsTab ||
           window.kind == OldWindowKind::LettersTab;
}

std::int32_t ClientX(const OldWindow& window, std::int32_t offset)
{
    return window.base.x + offset + (HasNoInsets(window) ? 0 : ClientLeft);
}

std::int32_t ClientY(const OldWindow& window, std::int32_t offset)
{
    return window.base.y + offset + (HasNoInsets(window) ? 0 : ClientTop);
}

std::int32_t ClientWidth(const OldWindow& window)
{
    return window.base.width - (HasNoInsets(window) ? 0 : ClientWidthCut);
}

std::int32_t ClientHeight(const OldWindow& window)
{
    return window.base.height - (HasNoInsets(window) ? 0 : ClientHeightCut);
}

namespace
{
// Message 9 of 0x45abc0 on the parent already found: the size by +0x64 (0, the fixed one unless it is 0x0; 1, the
// parent's inner width plus +0x70; 2, the height plus +0x74; 3, both).
void SizeAgainst(const OldWindow& p, OldWindowBase& base)
{
    switch (base.sizeMode)
    {
    case 0:
        if (base.sizeX == 0 && base.sizeY == 0)
            return;
        base.width = base.sizeX;
        base.height = base.sizeY;
        return;
    case 1:
        base.width = ClientWidth(p) + base.sizeX;
        base.height = base.sizeY;
        return;
    case 2:
        base.width = base.sizeX;
        base.height = ClientHeight(p) + base.sizeY;
        return;
    case 3:
        base.height = ClientHeight(p) + base.sizeY;
        base.width = ClientWidth(p) + base.sizeX;
        return;
    default:
        return;
    }
}
} // namespace

void SizeAgainstTheParent(OldWindowManager& manager, OldWindowBase& base)
{
    if (base.parent == 0)
        return;
    if (const OldWindow* parent = FindTheOldWindow(manager, base.parent); parent != nullptr)
        SizeAgainst(*parent, base);
}

// 0x45abc0, vtable + 0x24 of all of them: message 8 places and 9 sizes against the parent's client area.
void PlaceAgainstTheParent(OldWindowManager& manager, OldWindowBase& base)
{
    const std::int32_t type = base.current.type;
    if ((type != PlaceMessage && type != SizeMessage) || base.parent == 0)
        return;
    const OldWindow* parent = FindTheOldWindow(manager, base.parent);
    if (parent == nullptr)
        return;
    const OldWindow& p = *parent;
    if (type == PlaceMessage)
    {
        switch (base.placeMode)
        {
        case 0:
            base.x = ClientX(p, base.placeX);
            base.y = ClientY(p, base.placeY);
            return;
        case 1:
            base.y = ClientY(p, base.placeY);
            base.x = ClientX(p, -base.placeX) + ClientWidth(p);
            return;
        case 2:
            base.y = ClientY(p, -base.placeY) + ClientHeight(p);
            base.x = ClientX(p, base.placeX);
            return;
        case 3:
            base.y = ClientY(p, -base.placeY) + ClientHeight(p);
            base.x = ClientX(p, -base.placeX) + ClientWidth(p);
            return;
        default:
            return;
        }
    }
    SizeAgainst(p, base);
}

// 0x45aa80 from the queue to the rectangle: the mouse over the window makes it the hovered one if there is no other
// (and nobody captured the mouse or it did); shown, being the hovered or the capturing one, with the left button down
// it asks the manager to raise the topmost one of its family (0x490a00 up the parents to the one without one, and
// 0x490aa0). Returns false where the original exits with 0 without vtable + 0x20.
bool ClaimTheMouse(OldWindowManager& manager, const OldWindowBase& base, const OldWindowGlobals& globals,
                   const OldWindowStepView& view)
{
    if (!MouseIn(view, base.x, base.y, base.width, base.height, base.rectMode))
        return true;
    if (globals.hovered == 0)
    {
        if (globals.capture != 0 && globals.capture != base.id)
            return false;
        globals.hovered = base.id;
    }
    if (base.state != ShownWindow)
        return true;
    if (globals.hovered != base.id && globals.capture != base.id)
        return false;
    if (!globals.leftHeld)
        return true;
    const OldWindowBase* root = &base;
    if (base.parent != 0)
    {
        const OldWindow* parent = FindTheOldWindow(manager, base.parent);
        root = parent != nullptr ? &parent->base : nullptr;
    }
    if (root == nullptr)
        return true;
    while (root->parent != 0)
    {
        const OldWindow* parent = FindTheOldWindow(manager, root->parent);
        if (parent == nullptr)
            return true;
        root = &parent->base;
    }
    if (HasTheOldWindow(manager, root->id))
        PostToTheManager(manager, RaiseMessage, root->id, 0);
    return true;
}

// 0x45b200, the button's vtable + 0x20: a click that starts over it presses it (0x5762ec4) and the release over it
// sends its command to the parent (0x490b40 with message 10) with sound 0x19; the mouse off it with the button held
// releases everything.
void StepTheButtonMouse(OldWindowManager& manager, OldButton& button, const OldWindowGlobals& globals,
                        const OldWindowStepView& view)
{
    OldWindowBase& base = button.base;
    if (base.state == DisabledWindow)
        return;
    if (MouseIn(view, base.x, base.y, base.width, base.height, TopLeftRect))
    {
        globals.mouseOnUi = 1;
        if (globals.leftClick && globals.pressedButton == 0)
        {
            globals.pressedButton = base.id;
            button.pressed = 1;
        }
        if (globals.releasedClick && globals.pressedButton == base.id)
        {
            PostToTheWindow(manager, base.parent, CommandMessage, button.command, 0);
            globals.releasedClick = false;
            globals.RequestSound(ButtonSound);
            button.pressed = 0;
            globals.pressedButton = 0;
        }
        return;
    }
    if (button.pressed != 1)
        return;
    globals.leftHeld = false;
    globals.leftClick = false;
    globals.releasedClick = false;
    globals.pressedButton = 0;
    button.pressed = 0;
}

// 0x45aa80 on a button: the queue (0x45a640 returns 0 and 0x45abc0 handles it), vtable + 0x1c (0x403160, nothing),
// the mouse and 0x45b200.
void StepTheButton(OldWindowManager& manager, OldButton& button, std::int32_t arg, const OldWindowGlobals& globals,
                   const OldWindowStepView& view)
{
    OldWindowBase& base = button.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        PlaceAgainstTheParent(manager, base);
    }
    if (arg == 1)
        return;
    if (!ClaimTheMouse(manager, base, globals, view))
        return;
    StepTheButtonMouse(manager, button, globals, view);
}

// 0x468d50, the EDIT's vtable + 0x20 (all of them: the original does not look at +0xc4). With the mouse in
// (x, y − 4, w, h + 8) and the click with the EDIT idle: the bar's arrows (13 × 13 at x + w − 0xf, y − 4 and
// y + h − 9) scroll one line, with sound 0x19 the first time, and with as many lines as +0xd0 or more, the thumb is
// grabbed (state 2) and the track scrolls by pages, all with the list's counters. In a one-line EDIT, EM_SCROLL does
// nothing but the sound plays (Q-C1009); its thumb and track, without 0x468970, are 0 × 0. Then the mouse belongs to
// the UI and the click gives it the focus (vtable + 0x30 with 1: EM_SETSEL(−2, −1), the caret at the end and nothing
// selected) and sets 0x77f3b10 to 0 and 0x6c8dcc to 6. Grabbed, the mouse drags the thumb: EM_LINESCROLL to 0 and to
// the ratio's line; without the click, it lets go.
void StepTheEditMouse(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals,
                      const OldWindowStepView& view)
{
    OldWindowBase& base = edit.base;
    // EM_GETLINECOUNT. Without the measure (tests that do not give one), one line.
    const auto lineCount = [&edit, &view]
    { return view.measure != nullptr ? EditLineCount(edit, view.measure->measure) : 1; };
    // 0x468e1c and 0x468ff7: without a parent, or with the drawing done (the manager's +0x40 at 1), the counter rises.
    const auto mayCount = [&manager, &base] { return base.parent == 0 || manager.drawn == 1; };
    const auto repeat = [&](std::int32_t& counter, std::int32_t code, bool sound)
    {
        if (counter == 0)
        {
            ScrollTheEdit(edit, lineCount(), code);
            if (sound)
                globals.RequestSound(ButtonSound);
            counter = 1;
        }
        else if (counter > EditRepeatSteps)
            ScrollTheEdit(edit, lineCount(), code);
        else if (mayCount())
            ++counter;
    };
    // 0x468f9d: the track, without a parent or with the drawing done.
    const auto trackMayRun = [&manager, &base] { return base.parent == 0 || manager.drawn != 0; };
    const std::int32_t barX = base.x + base.width - EditBarInset;
    if (MouseIn(view, base.x, base.y - EditArrowRise, base.width, base.height + EditMouseSlack, TopLeftRect))
    {
        if (globals.leftClick && base.state == ShownWindow)
        {
            const std::int32_t arrowX = base.x + base.width - EditArrowInset;
            if (MouseIn(view, arrowX, base.y - EditArrowRise, EditArrowSize, EditArrowSize, TopLeftRect))
                repeat(edit.arrowRepeat, EditLineUp, true);
            if (MouseIn(view, arrowX, base.y + base.height - EditArrowDrop, EditArrowSize, EditArrowSize, TopLeftRect))
                repeat(edit.arrowRepeat, EditLineDown, true);
            if (lineCount() >= edit.visibleLines)
            {
                const double thumbY = static_cast<double>(edit.thumbY);
                const double thumbBottom = thumbY + static_cast<double>(edit.thumbHeight);
                const std::int32_t barWidth = OldDraw::Truncate(static_cast<double>(edit.barWidth));
                if (MouseIn(view, barX, OldDraw::Truncate(thumbY), barWidth,
                            OldDraw::Truncate(static_cast<double>(edit.thumbHeight)), TopLeftRect))
                {
                    if (base.state == ShownWindow && globals.capture == 0)
                    {
                        globals.capture = base.id;
                        base.state = DraggedEdit;
                        edit.dragOffset = static_cast<float>(static_cast<double>(view.mouseY) - thumbY);
                    }
                }
                else if (MouseIn(view, barX, OldDraw::Truncate(static_cast<double>(edit.barTop)), barWidth,
                                 OldDraw::Truncate(thumbY - static_cast<double>(edit.barTop)), TopLeftRect))
                {
                    if (trackMayRun())
                        repeat(edit.trackRepeat, EditPageUp, false);
                }
                else if (MouseIn(view, barX, OldDraw::Truncate(thumbBottom), barWidth,
                                 OldDraw::Truncate(static_cast<double>(edit.barBottom) - thumbY -
                                                   static_cast<double>(edit.thumbHeight)),
                                 TopLeftRect))
                {
                    if (trackMayRun())
                        repeat(edit.trackRepeat, EditPageDown, false);
                }
            }
        }
        else
        {
            edit.arrowRepeat = 0;
            edit.trackRepeat = 0;
        }
        globals.mouseOnUi = 1;
        if (globals.leftClick)
        {
            // The focus stops belonging to the EDIT of an already destroyed box (the orphan, CL9 milestone 9).
            manager.orphanedEdit.reset();
            manager.focusedEditIsOrphan = false;
            globals.keyboardFocus = Chat::ChatFocus::OldWindow;
            manager.focusedEdit = base.id;
            globals.focusedList = base.id;
            edit.text.Deselect();
            if (globals.mouseUpdateTime != nullptr)
                *globals.mouseUpdateTime = 0;
            if (globals.mouseUpdateTimeMax != nullptr)
                *globals.mouseUpdateTimeMax = EditMouseUpdateTimeMax;
        }
    }
    if (base.state != DraggedEdit)
        return;
    if (!globals.leftClick)
    {
        base.state = ShownWindow;
        if (globals.capture == base.id)
            globals.capture = 0;
        return;
    }
    globals.mouseOnUi = 1;
    edit.thumbY = static_cast<float>(static_cast<double>(view.mouseY) - static_cast<double>(edit.dragOffset));
    const std::int32_t lines = lineCount();
    const double top = static_cast<double>(edit.barTop);
    const std::int32_t line =
        OldDraw::Truncate(static_cast<double>(lines) * ((static_cast<double>(edit.thumbY) - top + EditDragHalf) /
                                                        (static_cast<double>(edit.barBottom) - top)));
    edit.firstLine = 0;
    ScrollTheEditLines(edit, lines, line);
}

// 0x45aa80 on an EDIT: the queue (0x45abc0), the mouse and 0x468d50.
void StepTheEdit(OldWindowManager& manager, OldEdit& edit, std::int32_t arg, const OldWindowGlobals& globals,
                 const OldWindowStepView& view)
{
    while (!edit.base.queue.empty())
    {
        edit.base.current = edit.base.queue.front();
        edit.base.queue.pop_front();
        PlaceAgainstTheParent(manager, edit.base);
    }
    if (arg == 1)
        return;
    if (!ClaimTheMouse(manager, edit.base, globals, view))
        return;
    StepTheEditMouse(manager, edit, globals, view);
}

// 0x45aa50 on a button: the message and its step with 1 (0x45aa80), which handles the queue and does not reach the
// mouse.
void PostAndSettle(OldWindowManager& manager, OldButton& button, std::int32_t type)
{
    button.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!button.base.queue.empty())
    {
        button.base.current = button.base.queue.front();
        button.base.queue.pop_front();
        PlaceAgainstTheParent(manager, button.base);
    }
}

// vtable + 0x30: 0x48faf0 (nothing) in the frame and the list, and 0x4a3a50 in the box: it places the two buttons
// (message 8 and its step with 1) and sets 0x77f3708 to 1, which nobody reads (*static*; MuRet does not keep it). The
// friends ones, in Messenger/FriendWindows.
Core::Expected<void, std::string> SettleTheWindow(OldWindowManager& manager, OldWindow& window,
                                                  const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return SettleTheFriendsKinds(manager, window, globals, view);
    if (IsALetterKind(window.kind))
        return SettleTheLetterKinds(manager, window, globals, view);
    if (window.kind == OldWindowKind::MessageBox)
        SettleTheButtons(manager, window);
    return {};
}

// 0x4930f0, vtable + 0x38 of the frame and the box: stretches the window from y = 0 to the bar, or restores it.
Core::Expected<void, std::string> StretchTheWindow(OldWindowManager& manager, OldWindow& window,
                                                   const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    if (frame.stretched == 0)
    {
        frame.savedHeight = base.height;
        frame.savedY = base.y;
        base.y = 0;
        base.height = StretchedHeight;
        if (auto settled = SettleTheWindow(manager, window, globals, view); !settled.HasValue())
            return settled;
        frame.stretched = 1;
        return {};
    }
    base.height = frame.savedHeight;
    base.y = frame.savedY;
    if (auto settled = SettleTheWindow(manager, window, globals, view); !settled.HasValue())
        return settled;
    frame.stretched = 0;
    return {};
}

// 0x4a3b80, the box's vtable + 0x28: a button's command. 1 (and 2 with two buttons) tells the owner (message 12 with
// the id and 1 or 0) and asks the manager to close it. Always returns 0: 0x45abc0 runs after.
Core::Expected<void, std::string> AnswerTheBox(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowStepView& view)
{
    const OldMessage& message = window.base.current;
    if (message.type != CommandMessage)
        return {};
    if (message.a != YesCommand && (message.a != NoCommand || window.box.oneButton != 0))
        return {};
    // 0x4a3bde and 0x4a3fc8: the answer to a friend request (C1 0E C2, 1 or 0, and the name at +0x2b8).
    if (window.box.owner == -1)
    {
        if (!view.send)
            return Core::Unexpected(std::string("0x4a3b80: la respuesta a un pedido de amistad (C1 C2) sin el cable"));
        view.send(FriendSend{.kind = FriendSendKind::Answer,
                             .value = static_cast<std::uint8_t>(message.a == YesCommand ? 1 : 0),
                             .name = window.box.friendName});
    }
    else if (window.box.owner != 0)
        PostToTheWindow(manager, window.box.owner, AnswerMessage, window.base.id, message.a == YesCommand ? 1 : 0);
    PostToTheManager(manager, CloseMessage, window.base.id, 0);
    return {};
}

// vtable + 0x1c: 0x48fb10 (nothing) in the frame; 0x4a43c0 in the box (Enter, when it is the last of the list, sends
// message 10 with 1; and the step of both buttons, also the unused one), and 0x4a4e20 in the window list (with +0x150
// at 1, Enter hides it: with no entries, raising none). The friends ones, in Messenger/FriendWindows.
Core::Expected<void, std::string> BeforeTheMouse(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                 const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return StepTheFriendsChildren(manager, window, arg, globals, view);
    if (IsALetterKind(window.kind))
        return StepTheLetterKindsChildren(manager, window, arg, globals, view);
    if (window.kind == OldWindowKind::MessageBox)
    {
        if (LastOldWindow(manager) == window.base.id && view.keyEdge && view.keyEdge(EnterKey))
            window.base.queue.push_back(OldMessage{.type = CommandMessage, .a = YesCommand, .b = 0, .text = {}});
        for (OldButton& button : window.box.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    }
    // 0x4a4e20, the list's vtable + 0x1c: with +0x150 at 1, Enter raises the chosen one (if present and not the end),
    // hides the list and clears +0x150.
    if (window.kind == OldWindowKind::WindowList && window.list.armed == 1 && view.keyEdge && view.keyEdge(EnterKey))
    {
        const OldWindowListPart& list = window.list;
        if (list.cursor < list.chats.size() && FindTheOldWindow(manager, list.chats[list.cursor]) != nullptr)
            PostToTheManager(manager, RaiseMessage, list.chats[list.cursor], 0);
        HideTheWindowList(window);
        window.list.armed = 0;
    }
    return {};
}

// 0x4a4eb0, the list's vtable + 0x58, shown: the mouse over the rows (bottom up, from +0x104) chooses them and belongs
// to the UI; on release, the focus to the game, sound 0x19 and the row's window: the top one is minimized (and the
// list stays shown) and another is raised. Outside, without F5, the chosen one goes to the end.
Core::Expected<void, std::string> StepTheWindowListRows(OldWindowManager& manager, OldWindow& window,
                                                        const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowListPart& list = window.list;
    const std::size_t size = list.chats.size();
    const double step = static_cast<double>(list.rowHeight) + WindowListRowMargin;
    const std::int32_t height = OldDraw::Truncate(step * static_cast<double>(size));
    if (window.base.state != ShownWindow)
        return {};
    if (!Steps::MouseIn(view, window.base.x, list.bottom - height, window.base.width, height, 1))
    {
        if (list.armed == 0)
            list.cursor = size;
        return {};
    }
    list.armed = 0;
    manager.focus = window.base.id;
    // 0x4a4f28..0x4a4f5a: trunc(size − (mouse − (+0x104 − height)) / (row + 4)), and the iterator advances that from
    // the start, without passing the end.
    const auto from = static_cast<double>(view.mouseY - list.bottom + height);
    const std::int32_t row = OldDraw::Truncate(static_cast<double>(size) - from / step);
    list.cursor = row > 0 ? std::min(static_cast<std::size_t>(row), size) : 0;
    if (globals.releasedClick && window.base.state == ShownWindow)
    {
        // SetFocus([0x5763544]), like FocusTheGame.
        globals.keyboardFocus = Chat::ChatFocus::Game;
        globals.RequestSound(ButtonSound);
        globals.releasedClick = false;
        // With the chosen one at the end (the top edge, distance 0), the original reads the slot past the last one
        // (Q-C1008): MuRet finds none.
        const std::int32_t id = list.cursor < size ? list.chats[list.cursor] : 0;
        if (OldWindow* chosen = id != 0 ? FindTheOldWindow(manager, id) : nullptr; chosen != nullptr)
        {
            if (chosen->base.state != HiddenWindow && TopOldWindow(manager) == id)
            {
                PostToTheManager(manager, MinimizeMessage, id, 0);
                HideTheWindowList(window);
                globals.mouseOnUi = 1;
                return {};
            }
            PostToTheManager(manager, RaiseMessage, id, 0);
            globals.leftHeld = false;
        }
        HideTheWindowList(window);
    }
    globals.mouseOnUi = 1;
    return {};
}

// vtable + 0x58: 0x48fb20 and 0x4a4430 (nothing) in the frame and the box, and 0x495520, 0x49b210 and 0x499830 (ret)
// in the chat and the letter ones; 0x4a4eb0 in the list, which only does something when shown (its rows, P-C215). The
// friends ones, in Messenger/FriendWindows.
Core::Expected<void, std::string> AfterTheMove(OldWindowManager& manager, OldWindow& window,
                                               const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (IsAFriendsKind(window.kind))
        return AfterTheFriendsMove(manager, window, globals, view);
    if (window.kind == OldWindowKind::WindowList)
        return StepTheWindowListRows(manager, window, globals, view);
    return {};
}

// 0x492ac0, +0x84 at 0x87: bounded by the screen, the minimum and the maximum; the grabbed point only follows the mouse
// if it did not overshoot.
void ResizeFromTheBottomRight(OldWindow& window, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    std::int32_t width = 0;
    if (base.width - frame.grabX + base.x + view.mouseX < ScreenWidth + 1)
        width = base.width - frame.grabX + view.mouseX;
    else
        width = ScreenWidth - base.x;
    base.width = width;
    if (width < frame.minWidth)
        base.width = frame.minWidth;
    else if (frame.maxWidth < 1 || width <= frame.maxWidth)
        frame.grabX = view.mouseX;
    else
        base.width = frame.maxWidth;
    if (base.height - frame.grabY + base.y + view.mouseY < ScreenHeight + 1)
        base.height = base.height - frame.grabY + view.mouseY;
    else
        base.height = ScreenHeight - base.y;
    if (base.height < frame.minHeight)
    {
        base.height = frame.minHeight;
        return;
    }
    if (frame.maxHeight > 0 && frame.maxHeight < base.height)
    {
        base.height = frame.maxHeight;
        return;
    }
    frame.grabY = view.mouseY;
}

// +0x84 at 0x13b: each axis only if it does not go below the minimum or off the screen at the top or the left; the
// grabbed point always follows the mouse.
void ResizeFromTheTopLeft(OldWindow& window, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    const std::int32_t right = base.width + frame.grabX;
    const std::int32_t x = base.x - frame.grabX + view.mouseX;
    if (frame.minWidth <= right - view.mouseX && x >= 0)
    {
        base.x = x;
        base.width = right - view.mouseX;
    }
    frame.grabX = view.mouseX;
    const std::int32_t bottom = frame.grabY + base.height;
    const std::int32_t y = base.y + (view.mouseY - frame.grabY);
    if (frame.minHeight <= bottom - view.mouseY && y >= 0)
    {
        base.y = y;
        base.height = bottom - view.mouseY;
    }
    frame.grabY = view.mouseY;
}

// A title bar button under the mouse (0x492ac0): on release with the mouse unowned in the UI, the message to the
// manager; the cross, only if the window can be closed (vtable + 0x4c: 0x48f8f0, which returns 1, or 0x499840 in the
// letter-writing one).
Core::Expected<void, std::string> PressTheTitleButton(OldWindowManager& manager, OldWindow& window, std::int32_t button,
                                                      std::int32_t message, const OldWindowGlobals& globals,
                                                      const OldWindowStepView& view)
{
    window.frame.hotButton = button;
    if (globals.mouseOnUi != 0 || !globals.releasedClick)
        return {};
    bool may = true;
    if (message == CloseMessage)
    {
        auto asked = MayCloseTheWindow(manager, window, globals, view);
        if (!asked.HasValue())
            return Core::Unexpected(std::move(asked).Error());
        may = asked.Value();
    }
    if (may)
        PostToTheManager(manager, message, window.base.id, 0);
    globals.releasedClick = false;
    return {};
}

// Captures the mouse to drag (4) or to resize (1) from a corner.
void Grab(OldWindow& window, std::int32_t state, std::int32_t corner, const OldWindowGlobals& globals,
          const OldWindowStepView& view)
{
    if (!globals.leftHeld || window.base.state == state || globals.capture != 0)
        return;
    globals.capture = window.base.id;
    window.base.state = state;
    if (state == ResizedWindow)
        window.frame.resizeCorner = corner;
    window.frame.grabX = view.mouseX;
    window.frame.grabY = view.mouseY;
    if (state == ResizedWindow)
        window.frame.stretched = 0;
}

// 0x492ac0, vtable + 0x20 of the three: the bar buttons, the corners and the drag bar; then dragging and resizing
// with the left button down, or releasing them.
Core::Expected<void, std::string> StepTheFrameMouse(OldWindowManager& manager, OldWindow& window,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    OldFrame& frame = window.frame;
    const std::int32_t flags = base.flags;
    frame.hotButton = 0;
    if (MouseIn(view, base.x, base.y, base.width, base.height, TopLeftRect))
    {
        const std::int32_t buttonY = base.y + TitleButtonY;
        const std::int32_t minimizeX = (flags & StretchFlag) != 0 ? MinimizeWithStretchX : MinimizeAloneX;
        Core::Expected<void, std::string> pressed;
        if ((flags & TitleFlag) != 0 &&
            MouseIn(view, base.x - CloseButtonX + base.width, buttonY, TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 3, CloseMessage, globals, view);
        else if ((flags & StretchFlag) != 0 && MouseIn(view, base.x - StretchButtonX + base.width, buttonY,
                                                       TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 2, StretchMessage, globals, view);
        else if ((flags & MinimizeFlag) != 0 &&
                 MouseIn(view, base.x - minimizeX + base.width, buttonY, TitleButtonSize, TitleButtonSize, TopLeftRect))
            pressed = PressTheTitleButton(manager, window, 1, MinimizeMessage, globals, view);
        else if ((flags & ResizeFlag) != 0 && MouseIn(view, base.x, base.y, TopLeftGrip, TopLeftGrip, TopLeftRect))
            Grab(window, ResizedWindow, TopLeftCorner, globals, view);
        else if ((flags & DragFlag) != 0 && (flags & TitleFlag) != 0 &&
                 MouseIn(view, base.x, base.y, base.width, DragBarHeight, TopLeftRect))
            Grab(window, DraggedWindow, 0, globals, view);
        else if ((flags & ResizeFlag) != 0 &&
                 MouseIn(view, base.x - BottomRightGrip + base.width, base.y - BottomRightGrip + base.height,
                         BottomRightGrip, BottomRightGrip, TopLeftRect))
            Grab(window, ResizedWindow, BottomRightCorner, globals, view);
        else if (globals.capture == base.id && base.state != DraggedWindow && base.state != ResizedWindow)
        {
            globals.leftHeld = false;
            globals.leftClick = false;
        }
        globals.mouseOnUi = 1;
        if (!pressed.HasValue())
            return pressed;
    }
    if (base.state == DraggedWindow)
    {
        if (globals.leftHeld)
        {
            if (globals.hovered == 0)
                globals.hovered = base.id;
            globals.mouseOnUi = 1;
            const std::int32_t x = base.x - frame.grabX + view.mouseX;
            if (x < 0)
                base.x = 0;
            else if (base.x - frame.grabX + base.width + view.mouseX < ScreenWidth + 1)
                base.x = x;
            else
                base.x = ScreenWidth - base.width;
            const std::int32_t y = base.y - frame.grabY + view.mouseY;
            if (y < 0)
                base.y = 0;
            else if (base.y - frame.grabY + base.height + view.mouseY < ScreenHeight + 1)
                base.y = y;
            else
                base.y = ScreenHeight - base.height;
            frame.grabX = view.mouseX;
            frame.grabY = view.mouseY;
            return AfterTheMove(manager, window, globals, view);
        }
        base.state = ShownWindow;
        if (globals.capture == base.id)
            globals.capture = 0;
        return AfterTheMove(manager, window, globals, view);
    }
    if (base.state != ResizedWindow)
        return AfterTheMove(manager, window, globals, view);
    if (globals.leftHeld)
    {
        if (frame.resizeCorner != BottomRightCorner && frame.resizeCorner != TopLeftCorner)
            return AfterTheMove(manager, window, globals, view);
        if (globals.hovered == 0)
            globals.hovered = base.id;
        globals.mouseOnUi = 1;
        if (frame.resizeCorner == BottomRightCorner)
            ResizeFromTheBottomRight(window, view);
        else
            ResizeFromTheTopLeft(window, view);
        return AfterTheMove(manager, window, globals, view);
    }
    base.state = ShownWindow;
    if (globals.capture == base.id)
        globals.capture = 0;
    return AfterTheMove(manager, window, globals, view);
}

// 0x45aa80, vtable + 0x18 of the three: the queue (vtable + 0x28 and, if it returns 0, 0x45abc0), vtable + 0x1c, the
// mouse and vtable + 0x20 (0x492ac0).
Core::Expected<void, std::string> StepTheWindow(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    OldWindowBase& base = window.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        if (window.kind == OldWindowKind::MessageBox)
        {
            if (auto answered = AnswerTheBox(manager, window, view); !answered.HasValue())
                return answered;
        }
        else if (IsAFriendsKind(window.kind))
        {
            if (auto handled = HandleTheFriendsMessage(manager, window, globals, view); !handled.HasValue())
                return handled;
        }
        else if (IsALetterKind(window.kind))
        {
            if (auto handled = HandleTheLetterKindsMessage(manager, window, globals, view); !handled.HasValue())
                return handled;
        }
        PlaceAgainstTheParent(manager, base);
    }
    if (auto before = BeforeTheMouse(manager, window, arg, globals, view); !before.HasValue())
        return before;
    if (arg == 1)
        return {};
    if (!ClaimTheMouse(manager, base, globals, view))
        return {};
    return StepTheFrameMouse(manager, window, globals, view);
}

// 0x490670(id, show): state 0 or 3.
void ShowTheWindow(OldWindowManager& manager, std::int32_t id, bool show)
{
    if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        window->base.state = show ? ShownWindow : HiddenWindow;
}

// SetFocus([0x5763544]). In message 1 of 0x490b70, before that, if the focus was on one of the chat's EDITs (or on the
// one of the 0x5763504 dialog, milestone 6) or the top one is a chat with a friend (+0x9c at 1), 0x467360 saves and
// turns off the IME mode, which MuRet does not have; 0x4906a0 does not call it.
void FocusTheGame(const OldWindowGlobals& globals)
{
    globals.keyboardFocus = Chat::ChatFocus::Game;
}

// The end of messages 2 and 4 of 0x490b70: the focus to the new last one of the list, or to the game.
void FocusTheLast(OldWindowManager& manager, const OldWindowGlobals& globals)
{
    if (manager.order.empty() || manager.order.back() == 0)
    {
        manager.focus = 0;
        FocusTheGame(globals);
        return;
    }
    const OldWindow* last = FindTheOldWindow(manager, manager.order.back());
    if (last == nullptr)
        return;
    if (IsShowing(last->base.state))
    {
        PostToTheWindow(manager, manager.order.back(), RaisedMessage, 0, 0);
        return;
    }
    manager.focus = 0;
    FocusTheGame(globals);
}

// 0x45aa50: the message and the step with 1.
Core::Expected<void, std::string> PostAndStep(OldWindowManager& manager, OldWindow& window, std::int32_t type,
                                              const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    window.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    return StepTheWindow(manager, window, 1, globals, view);
}

// 0x490b70
Core::Expected<void, std::string> HandleTheMessage(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                   const OldWindowStepView& view)
{
    const std::int32_t id = manager.current.a;
    switch (manager.current.type)
    {
    case RaiseMessage:
    {
        if (id == 0 || FindTheOldWindow(manager, id) == nullptr)
            return {};
        // 0x490bad..0x490c5a: without its own EDIT (+0x9c), the keyboard focus to the game window.
        if (FindTheOldWindow(manager, id)->frame.editField == 0)
            FocusTheGame(globals);
        if (FindTheOldWindow(manager, id)->base.state == HiddenWindow)
            ShowTheWindow(manager, id, true);
        // 0x490c7d..0x490cd8: raises it to the end, unless it is already there with the focus elsewhere.
        if (LastOldWindow(manager) != id || globals.keyboardFocus == Chat::ChatFocus::Game)
        {
            manager.order.remove(id);
            manager.order.push_back(id);
            PostToTheWindow(manager, id, RaisedMessage, 0, 0);
        }
        manager.focus = id;
        // 0x4a60b0(id): stops waiting in the window list's +0x118; 0x4a5e10.
        std::deque<std::int32_t>& waiting = manager.windowList.list.waiting;
        waiting.erase(std::remove(waiting.begin(), waiting.end(), id), waiting.end());
        HideTheWindowList(manager.windowList);
        return {};
    }
    case MinimizeMessage:
        if (id == 0 || (manager.modal != 0 && manager.friends != 0 && id == manager.friends))
            return {};
        globals.RequestSound(ButtonSound);
        ShowTheWindow(manager, id, false);
        manager.order.remove(id);
        manager.order.push_front(id);
        FocusTheLast(manager, globals);
        return {};
    case StretchMessage:
        if (id == 0)
            return {};
        if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        {
            if (auto stretched = StretchTheWindow(manager, *window, globals, view); !stretched.HasValue())
                return stretched;
            globals.RequestSound(ButtonSound);
        }
        return {};
    case CloseMessage:
        if (id == 0)
            return {};
        globals.RequestSound(ButtonSound);
        if (auto destroyed = DestroyTheOldWindow(manager, id, globals.pressedButton); !destroyed.HasValue())
            return destroyed;
        FocusTheLast(manager, globals);
        return {};
    case SendBackMessage:
        if (id == 0 || FindTheOldWindow(manager, id) == nullptr || manager.order.empty() || manager.order.front() == id)
            return {};
        manager.order.remove(id);
        manager.order.push_front(id);
        return {};
    default:
        return {};
    }
}
} // namespace Steps

using Steps::ClaimTheMouse;
using Steps::FocusTheGame;
using Steps::HandleTheMessage;
using Steps::IsShowing;
using Steps::MakeTheFrame;
using Steps::MakeTheMessageBox;
using Steps::PostAndSettle;
using Steps::SettleTheWindow;
using Steps::StepTheWindow;

OldWindow OldWindowManager::MakeTheWindowList()
{
    OldWindow list;
    list.kind = OldWindowKind::WindowList;
    list.base.id = WindowListId;
    list.base.flags = 0;
    list.base.x = WindowListX;
    list.base.y = WindowListY;
    list.base.width = WindowListWidth;
    list.base.height = 0;
    list.base.state = HiddenWindow;
    list.list = OldWindowListPart{};
    return list;
}

OldWindow* FindTheOldWindow(OldWindowManager& manager, std::int32_t id)
{
    if (const auto found = manager.windows.find(id); found != manager.windows.end())
        return &found->second;
    const auto tab = manager.tabs.find(id);
    if (tab == manager.tabs.end())
        return nullptr;
    const OldTabReference& reference = tab->second;
    OldWindow* owner = nullptr;
    if (const auto found = manager.windows.find(reference.ownerId);
        found != manager.windows.end() && found->second.allocationId == reference.allocationId)
        owner = &found->second;
    else
    {
        for (OldWindow& rejected : manager.unregisteredWindows)
        {
            if (rejected.base.id == reference.ownerId && rejected.allocationId == reference.allocationId)
            {
                owner = &rejected;
                break;
            }
        }
    }
    if (owner == nullptr)
        return nullptr;
    for (OldWindow& candidate : owner->tabs)
    {
        if (candidate.base.id == id)
            return &candidate;
    }
    return nullptr;
}

std::string OldWindowTexts::At(std::size_t index) const
{
    return lookup ? lookup(index) : std::string();
}

bool HasTheOldWindow(const OldWindowManager& manager, std::int32_t id)
{
    return manager.windows.contains(id);
}

std::int32_t TopOldWindow(const OldWindowManager& manager)
{
    if (manager.order.empty())
        return 0;
    const std::int32_t last = manager.order.back();
    if (last != manager.friends)
        return last;
    if (manager.order.size() == 1)
        return 0;
    return *std::next(manager.order.rbegin());
}

std::int32_t LastOldWindow(const OldWindowManager& manager)
{
    return manager.order.empty() ? 0 : manager.order.back();
}

void PostToTheManager(OldWindowManager& manager, std::int32_t type, std::int32_t a, std::int32_t b)
{
    manager.queue.push_back(OldMessage{.type = type, .a = a, .b = b, .text = {}});
}

void PostToTheWindow(OldWindowManager& manager, std::int32_t id, std::int32_t type, std::int32_t a, std::int32_t b)
{
    if (OldWindow* window = FindTheOldWindow(manager, id); window != nullptr)
        window->base.queue.push_back(OldMessage{.type = type, .a = a, .b = b, .text = {}});
}

Core::Expected<std::int32_t, std::string>
CreateTheOldWindow(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts,
                   const OldWindowMeasure& measure, const OldWindowGlobals* globals, const OldWindowStepView* view)
{
    // 0x48ee93: with 0x6c7b8c at 0 it builds nothing; it is 1 in .data and nobody writes it.
    OldWindow window;
    switch (request.kind)
    {
    case FrameCase:
        window = MakeTheFrame(manager);
        break;
    case FriendsCase:
    {
        // Only one: with +0x48 set it returns 0.
        if (manager.friends != 0)
            return 0;
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(std::string("0x48ee60(3): la ventana de amigos sin el paso de las ventanas"));
        auto made = Steps::MakeTheFriendsWindow(manager, request, texts, measure);
        if (!made.HasValue())
            return Core::Unexpected(std::move(made).Error());
        window = std::move(made).Value();
        manager.friends = window.base.id;
        // +0xb0: with a new letter (the window list's +0x144), the letters tab; otherwise the previous one.
        window.friends.currentTab = manager.windowList.list.newLetter == 1 ? 1 : manager.lastTab;
        // 0x4a6420(0).
        manager.windowList.list.newLetter = 0;
        if (manager.listReceived == 0)
        {
            if (!view->send)
                return Core::Unexpected(std::string("0x48ee60(3): el pedido de la lista de amigos (C1 03 C0) sin el "
                                                    "cable"));
            view->send(FriendSend{.kind = FriendSendKind::ListRequest, .value = 0, .name = {}});
        }
        break;
    }
    case AddFriendCase:
    {
        if (manager.modal != 0)
            return 0;
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(
                std::string("0x48ee60(4): la caja de agregar un amigo sin el paso de las ventanas"));
        window = Steps::MakeTheAddFriendBox(manager, request, texts);
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        manager.modal = window.base.id;
        break;
    }
    case ModalQuestionCase:
    case ModalNoticeCase:
    case QuestionCase:
    case NoticeCase:
    {
        const bool modal = request.kind == ModalQuestionCase || request.kind == ModalNoticeCase;
        if (modal && manager.modal != 0)
            return 0;
        const bool oneButton = request.kind == ModalNoticeCase || request.kind == NoticeCase;
        window = MakeTheMessageBox(manager, oneButton ? 1 : 0);
        // 0x49e6e0(id, text) on the friends window's +0x4d0.
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        if (modal)
            manager.modal = window.base.id;
        else if (std::ranges::find(manager.boxes, window.base.id) == manager.boxes.end())
            manager.boxes.push_back(window.base.id);
        break;
    }
    case ChatCase:
    case CalledChatCase:
    case LetterReadCase:
    case LetterWriteCase:
    {
        // 0x48eee4, 0x48f41f and 0x48f4ee (the writing one, without a modal box). All four are listed in the friends
        // window's windows tab (0x48f5cd).
        if (request.kind == LetterWriteCase && manager.modal != 0)
            return 0;
        auto made = Steps::MakeTheLetterKind(manager, request, texts, measure, view);
        if (!made.HasValue())
            return Core::Unexpected(std::move(made).Error());
        window = std::move(made).Value();
        Steps::AddTheWindowRow(manager, window.base.id, request.data);
        break;
    }
    default:
        // 0x48ee98..0x48ee9b: outside 0..10 (unsigned), returns 0 without building anything or using up ids.
        return 0;
    }
    std::int32_t x = request.x == -1 ? 0 : request.x;
    std::int32_t y = request.y == -1 ? DefaultY : request.y;
    // vtable + 0x2c: 0x4920a0 in the frame and 0x4a38f0 in the box (0x4a2280 and 0x4a35a0, in Steps::MakeThe...).
    OldWindowBase& base = window.base;
    if (window.kind == OldWindowKind::Frame)
    {
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = FramePlace;
        base.y = FramePlace;
        base.width = FrameWidth;
        base.height = FrameHeight;
    }
    else if (window.kind == OldWindowKind::MessageBox)
    {
        OldMessageBox& box = window.box;
        window.frame.title = box.oneButton == 0 ? texts.question : texts.notice;
        box.owner = request.aux;
        base.parent = 0;
        auto wrapped =
            Chat::WrapByWidth(request.data, measure.measure, measure.dcFont, measure.screen, BoxWrapWidth, BoxLines, 0);
        if (!wrapped.HasValue())
            return Core::Unexpected(std::move(wrapped).Error());
        for (std::size_t line = 0; line < box.lines.size(); ++line)
            box.lines[line] = line < wrapped.Value().lines.size() ? wrapped.Value().lines[line] : std::string();
        base.x = BoxPlace;
        base.y = BoxPlace;
        base.width = BoxWidth;
        base.height = BoxHeight;
        window.frame.minWidth = BoxWidth;
        window.frame.minHeight = BoxHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        base.flags = BoxFlags;
        const auto setUp =
            [&base](OldButton& button, std::int32_t command, const std::string& label, std::int32_t placeX)
        {
            button.command = command;
            button.label = label;
            button.base.parent = base.id;
            button.base.placeMode = 0;
            button.base.placeX = placeX;
            button.base.placeY = BoxButtonsY;
            button.base.width = BoxButtonWidth;
            button.base.height = BoxButtonHeight;
        };
        if (box.oneButton == 0)
        {
            setUp(box.buttons[0], YesCommand, texts.yes, YesButtonX);
            setUp(box.buttons[1], NoCommand, texts.no, NoButtonX);
        }
        else
            setUp(box.buttons[0], YesCommand, texts.notice, OnlyButtonX);
    }
    // 0x48f606..0x48f6b8: the cascade, through the map in id order. On a shift it goes back to the first one and the ++
    // at the end of the loop skips it (Q-C571).
    if ((request.flags & KeepPlaceFlag) == 0)
    {
        for (auto it = manager.windows.begin(); it != manager.windows.end(); ++it)
        {
            const OldWindowBase& other = it->second.base;
            if (other.x != x || other.y != y)
                continue;
            if (base.width + x + CascadeStep <= ScreenWidth)
                x += CascadeStep;
            if (base.height + y + CascadeStep <= ScreenHeight)
                y += CascadeStep;
            if (base.width + x + CascadeStep > ScreenWidth && base.height + y + CascadeStep > ScreenHeight)
            {
                const std::int32_t rest = y % CascadeWrap;
                if (rest == CascadeGiveUp)
                {
                    // 0x48f700..0x48f70a destroys the new object, each button's global included (0x45adf0).
                    if (globals != nullptr)
                        ReleaseTheDestroyedButtons(window, globals->pressedButton);
                    return 0;
                }
                y = rest + 1;
                x = y;
            }
            it = manager.windows.begin();
        }
    }
    base.x = x;
    base.y = y;
    const std::int32_t id = base.id;
    const auto [registered, inserted] = manager.windows.try_emplace(id, std::move(window));
    // 0x48f7b2 keeps the previous entry without destroying the new one; 0x48f82d always settles the one just built. It
    // must be findable before settling: the list looks up its tab in the second map (0x490a00).
    if (!inserted)
        manager.unregisteredWindows.push_back(std::move(window));
    OldWindow& placed = inserted ? registered->second : manager.unregisteredWindows.back();
    manager.order.push_back(id);
    if (Steps::IsAFriendsKind(placed.kind))
    {
        if (auto settled = SettleTheWindow(manager, placed, *globals, *view); !settled.HasValue())
            return Core::Unexpected(std::move(settled).Error());
    }
    else if (Steps::IsALetterKind(placed.kind))
    {
        // 0x48f810..0x48f847: the chats to the window list's +0xb4 (0x4a4990), vtable + 0x30 and, in case 1, the focus
        // to the EDIT.
        if (request.kind == ChatCase || request.kind == CalledChatCase)
            Messenger::AddTheChatToTheList(manager, id);
        if (globals == nullptr || view == nullptr)
            return Core::Unexpected(std::string("0x48ee60: el chat o una carta sin el paso de las ventanas"));
        if (auto settled = SettleTheWindow(manager, placed, *globals, *view); !settled.HasValue())
            return Core::Unexpected(std::move(settled).Error());
        if (request.kind == ChatCase || request.kind == LetterWriteCase)
            Steps::FocusTheChatEdit(manager, placed, *globals);
    }
    else if (placed.kind == OldWindowKind::MessageBox)
        SettleTheButtons(manager, placed);
    return id;
}

Core::Expected<void, std::string> StepTheWindowList(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                    const OldWindowStepView& view)
{
    return StepTheWindow(manager, manager.windowList, 0, globals, view);
}

Core::Expected<void, std::string> StepTheOldWindows(OldWindowManager& manager, const OldWindowGlobals& globals,
                                                    const OldWindowStepView& view)
{
    if (manager.modal != 0 && FindTheOldWindow(manager, manager.modal) == nullptr)
        manager.modal = 0;
    // 0x4904bc: the Esc edge is always read; it closes the last one of the list if shown and closable (vtable + 0x4c).
    if (view.keyEdge && view.keyEdge(EscapeKey) && !manager.order.empty() && manager.order.back() != 0)
    {
        OldWindow* last = FindTheOldWindow(manager, manager.order.back());
        if (last != nullptr && IsShowing(last->base.state))
        {
            auto may = Steps::MayCloseTheWindow(manager, *last, globals, view);
            if (!may.HasValue())
                return Core::Unexpected(std::move(may).Error());
            if (may.Value())
                PostToTheManager(manager, CloseMessage, LastOldWindow(manager), 0);
        }
    }
    // 0x490538..0x49055c: F5 with the focus on the game window, 0x4a5be0(1) on the window list. With the focus on an
    // old window's EDIT, its subclass (0x467b10) reads it too with every message it gets, except WM_CHAR and the IME
    // ones (0x467d3b), before the step (*inferred*: here, in place of the game window's). The two EDITs of the chat
    // input have another subclass, which does not read it.
    if ((globals.keyboardFocus == Chat::ChatFocus::Game || globals.keyboardFocus == Chat::ChatFocus::OldWindow) &&
        view.keyEdge && view.keyEdge(F5Key))
        ShowTheWindowList(manager, 1);
    // 0x490561..0x4905a2: F6 hides the windows, or shows them again if it hid any.
    if (view.keyEdge && view.keyEdge(F6Key) && (manager.hiddenByF6 == 0 || !manager.hidden.empty()))
    {
        manager.hiddenByF6 = (manager.hiddenByF6 + 1) % 2;
        if (auto hidden = HideTheOldWindows(manager, manager.hiddenByF6, 1, globals, &view); !hidden.HasValue())
            return hidden;
    }
    // 0x4905a2..0x4905bf: the hidden friends window is released (0x4914e0); on showing again, 0x4906a0 opens it
    // (Q-C448).
    if (manager.friends != 0)
    {
        const OldWindow* friends = FindTheOldWindow(manager, manager.friends);
        if (friends != nullptr && friends->base.state == HiddenWindow)
        {
            if (auto dropped = DropTheFriendsWindow(manager, globals.pressedButton); !dropped.HasValue())
                return dropped;
        }
    }
    // 0x4905c4: top down, each shown one runs its step with 0 (only those of the first map).
    for (auto it = manager.order.rbegin(); it != manager.order.rend(); ++it)
    {
        OldWindow* window = FindTheOldWindow(manager, *it);
        if (window == nullptr || !IsShowing(window->base.state))
            continue;
        if (auto stepped = StepTheWindow(manager, *window, 0, globals, view); !stepped.HasValue())
            return stepped;
    }
    while (!manager.queue.empty())
    {
        manager.current = manager.queue.front();
        manager.queue.pop_front();
        if (auto handled = HandleTheMessage(manager, globals, view); !handled.HasValue())
            return handled;
    }
    manager.drawn = 0;
    return {};
}

Core::Expected<void, std::string> DestroyTheOldWindow(OldWindowManager& manager, std::int32_t id,
                                                      std::int32_t& pressedButton)
{
    if (manager.friends == id)
    {
        if (manager.modal != 0)
            return {};
        // 0x49022a..0x490270: saves the friends window's place for the next one.
        if (const OldWindow* friends = FindTheOldWindow(manager, manager.friends); friends != nullptr)
            Steps::SaveTheFriendsPlace(manager, *friends);
        manager.friends = 0;
    }
    if (manager.letter == id)
        manager.letter = 0;
    const auto found = manager.windows.find(id);
    if (found == manager.windows.end())
        return {};
    // The EDIT's destructor (0x467710) never calls DestroyWindow: the native EDIT outlives the wrapper, with the focus
    // if it had it. MuRet keeps only the editable state of the one that still has the focus: the add-a-friend one, the
    // friend chat one and the three of the letter-writing one.
    if (OldEdit* focused = FocusedEditOf(manager, found->second); focused != nullptr)
    {
        manager.orphanedEdit = *focused;
        manager.focusedEditIsOrphan = true;
    }
    // 0x491840. The chat's destructor leaves the room (0x493a70) and the reading one's releases its letter (0x4916b0).
    manager.boxes.remove(id);
    ReleaseTheDestroyedButtons(found->second, pressedButton);
    if (auto forgotten = Steps::ForgetTheLetterKind(manager, found->second); !forgotten.HasValue())
        return forgotten;
    manager.windows.erase(found);
    manager.order.remove(id);
    if (manager.order.empty())
        manager.focus = 0;
    // 0x49e710(id) on the friends window's windows tab.
    Steps::ForgetTheWindowRow(manager, id);
    // 0x4a4b30(id): leaves the window list's +0xb4 chats and, if it was there, the chosen one goes to the end.
    std::deque<std::int32_t>& chats = manager.windowList.list.chats;
    if (const auto chat = std::find(chats.begin(), chats.end(), id); chat != chats.end())
    {
        chats.erase(chat);
        manager.windowList.list.cursor = chats.size();
    }
    if (manager.modal == id)
        manager.modal = 0;
    return {};
}

Core::Expected<void, std::string> HideTheOldWindows(OldWindowManager& manager, std::int32_t hide,
                                                    std::int32_t withFriends, const OldWindowGlobals& globals,
                                                    const OldWindowStepView* view)
{
    std::int32_t shown = 0;
    for (auto& [id, window] : manager.windows)
    {
        if ((withFriends != 1 && id == manager.friends) || id == manager.modal)
            continue;
        if (hide == 1)
        {
            if (window.base.state != ShownWindow)
                continue;
            if (withFriends == 1)
                manager.hidden.push_back(id);
            window.base.state = HiddenWindow;
            continue;
        }
        for (const std::int32_t hiddenId : manager.hidden)
        {
            if (hiddenId != id)
                continue;
            ++shown;
            window.base.state = ShownWindow;
        }
    }
    if (hide != 0)
    {
        manager.focus = 0;
        FocusTheGame(globals);
        return {};
    }
    // 0x490608: the list still has one that is gone (the friends window 0x4914e0 released, Q-C448): 0x491080 opens
    // another.
    const auto count = static_cast<std::int32_t>(manager.hidden.size());
    if (count != shown && count - shown >= 0)
    {
        if (view == nullptr || !view->openFriends)
            return Core::Unexpected(std::string("0x491080: abrir la ventana de amigos (0x186, 0x106) sin el juego"));
        if (auto opened = view->openFriends(); !opened.HasValue())
            return opened;
    }
    if (shown > 0 && TopOldWindow(manager) != 0)
        PostToTheManager(manager, RaiseMessage, TopOldWindow(manager), 0);
    manager.hidden.clear();
    return {};
}

Core::Expected<void, std::string> DropTheFriendsWindow(OldWindowManager& manager, std::int32_t& pressedButton)
{
    if (manager.friends == 0)
        return {};
    return DestroyTheOldWindow(manager, manager.friends, pressedButton);
}

void ForgetTheHiddenWindows(OldWindowManager& manager)
{
    manager.hiddenByF6 = 0;
    manager.hidden.clear();
}

void ResetTheOldWindows(OldWindowManager& manager, std::int32_t& pressedButton)
{
    // 0x48ea5f..0x48eb43: +0x48 to 0, the main map (releasing each window with its destructor: the reading one takes
    // its letter out of +0x94; the chat's, 0x493160, leaves its room with 0x493a70), the letter map (+0x94), the order
    // list, 0x4a4480 (the window list's +0xb4 chats and +0x118), what F6 hid and the boxes. The second map +0x5c is not
    // touched: it keeps stale references and the still-live tabs of the objects the main map rejected.
    manager.friends = 0;
    for (const auto& [id, window] : manager.windows)
    {
        ReleaseTheDestroyedButtons(window, pressedButton);
        static_cast<void>(Steps::ForgetTheLetterKind(manager, window));
    }
    manager.windows.clear();
    // 0x48eae6..0x48eb07: the whole letter map (+0x94), also the ones registered (0x491570) that wait for the C7.
    manager.letterWindows.clear();
    manager.order.clear();
    // 0x4a448e: the chosen one to the end before emptying (the iterator is left in a block that is freed; here, at 0).
    manager.windowList.list.chats.clear();
    manager.windowList.list.cursor = 0;
    manager.windowList.list.waiting.clear();
    ForgetTheHiddenWindows(manager);
    manager.boxes.clear();
    manager.modal = 0;
    manager.lastTab = 0;
    manager.listReceived = 1;
    manager.titleText = 990;
    manager.letter = 0;
    manager.offlineSent = 0;
    // The scene reset forgets the previous EDIT's logical id.
    manager.focusedEdit = 0;
    manager.focusedEditIsOrphan = false;
    manager.orphanedEdit.reset();
    // 0x48eb7d: +0x48 is already 0, so 0x4a23b0 on the friends window does not run.
}

void SettleTheButtons(OldWindowManager& manager, OldWindow& box)
{
    for (OldButton& button : box.box.buttons)
        PostAndSettle(manager, button, PlaceMessage);
}

void ShowTheWindowList(OldWindowManager& manager, std::int32_t arm)
{
    OldWindow& window = manager.windowList;
    OldWindowListPart& list = window.list;
    const std::size_t size = list.chats.size();
    if (size == 0)
        return;
    list.armed = arm;
    if (window.base.state == HiddenWindow)
    {
        // 0x4a5c0f..0x4a5c6b: vtable + 4 with 0; the height adds and the y subtracts what the rows take.
        window.base.state = ShownWindow;
        const double step = static_cast<double>(list.rowHeight) + WindowListRowMargin;
        const double rows = static_cast<double>(size) * step;
        window.base.height = OldDraw::Truncate(rows + static_cast<double>(window.base.height));
        window.base.y = OldDraw::Truncate(static_cast<double>(window.base.y) - rows);
        list.blinkStep = WindowListBlinkUp;
        if (arm != 1)
            return;
        // 0x4a5c7b..0x4a5d2d: from the start, skips the ones that are the top window (the end of +0x80).
        list.cursor = 0;
        if (size <= 1)
            return;
        while (list.chats[list.cursor] == LastOldWindow(manager))
        {
            ++list.cursor;
            if (list.cursor == size)
                return;
        }
        return;
    }
    if (arm != 1)
        return;
    // 0x4a5d37..0x4a5e00: the next one, wrapping to the start.
    if (list.cursor >= size)
    {
        list.cursor = 0;
        return;
    }
    ++list.cursor;
    if (list.cursor == size)
        list.cursor = 0;
}

void HideTheWindowList(OldWindow& list)
{
    if (list.base.state != ShownWindow)
        return;
    list.base.state = HiddenWindow;
    list.base.height = 0;
    list.base.y = list.list.bottom;
    list.list.blinkStep = WindowListBlinkDown;
    list.list.armed = 0;
}
} // namespace Client::Ui
