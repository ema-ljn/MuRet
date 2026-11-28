#include "Messenger/FriendWindows.h"

#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendModel.h"
#include "Platform/IClipboard.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditInput.h"
#include "Ui/OldEditLines.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Messenger
{
namespace
{
constexpr std::int32_t TopLeftRect = 1;
// 0x41ed10(0x19, 0, 0) and 0x41ed10(0x1c, 0, 0): dispatched in that order, and the last one stays in the diagnostics.
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;
constexpr Audio::SoundId OpenSound = Sound::CreateWindowSound;
// Text.bmd.
constexpr std::size_t FriendsTitleText = 990;
constexpr std::size_t ReconnectTitleText = 1066;
constexpr std::size_t FriendTabText = 1032;
constexpr std::size_t WindowsTabText = 1033;
constexpr std::size_t LettersTabText = 1034;
constexpr std::size_t AddButtonText = 1018;
constexpr std::size_t DeleteButtonText = 1019;
constexpr std::size_t ChatButtonText = 1020;
constexpr std::size_t WriteButtonText = 1015;
constexpr std::size_t WindowsButtonText = 1025;
constexpr std::size_t SecondLetterButtonText = 1027;
constexpr std::size_t ThirdLetterButtonText = 1010;
constexpr std::size_t FourthLetterButtonText = 1011;
constexpr std::size_t PlaceText = 1022;
constexpr std::size_t TypeTheNameText = 1023;
constexpr std::size_t ReallyDeleteText = 1024;
constexpr std::size_t StateButtonText = 1035;
constexpr std::size_t GoOfflineText = 1036;
constexpr std::size_t ConfirmText = 228;
constexpr std::size_t CancelText = 229;
// 0x460970: the width of a 10-character name (s_ZZZZZZZZZZ_006c7ad4) and the margin.
constexpr std::string_view WidestName = "ZZZZZZZZZZ";
constexpr std::int32_t ColumnMargin = 8;
// 0x4a2280: at (0x32, 0x32), 0xfa × 0xaa, which does not shrink below 0xfa × 0x96; each tab from (0, 0x15) of the
// inner area, with its width and 0x13 less height.
constexpr std::int32_t FriendsPlace = 0x32;
constexpr std::int32_t FriendsWidth = 0xfa;
constexpr std::int32_t FriendsHeight = 0xaa;
constexpr std::int32_t FriendsMinHeight = 0x96;
constexpr std::int32_t TabTop = 0x15;
constexpr std::int32_t TabHeightCut = -0x13;
// 0x49c8b0 and 0x49e5c0: the list hangs 0x16 from the bottom with 0x27 less height; the 0x32 × 0xe buttons, 0x11 from
// the bottom.
constexpr std::int32_t ListBottom = 0x16;
constexpr std::int32_t ListHeightCut = -0x27;
constexpr std::int32_t TabButtonWidth = 0x32;
constexpr std::int32_t TabButtonHeight = 0xe;
constexpr std::int32_t TabButtonY = 0x11;
constexpr std::array<std::int32_t, 4> TabButtonX{2, 0x35, 0x68, 0x9b};
// 0x463190: +0x130 of the letter list.
constexpr std::int32_t LetterListHeaders = 0x19;
constexpr std::size_t SenderColumnText = 1028;
constexpr std::size_t DateColumnText = 1029;
constexpr std::size_t SubjectColumnText = 1030;
// 0x464510: a letter list row's mark.
constexpr std::int32_t LetterMarkWidth = 0xc;
// 0x4a3030: the three 0x35 × 0x13 headers at y + 2 of the inner area; the state button, 0x10 wider than its text, 0x10
// from the right edge and at y + 4, 0x14 high.
constexpr std::int32_t HeaderWidth = 0x35;
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t HeaderY = 2;
constexpr std::array<std::int32_t, FriendsTabCount> HeaderX{0, 0x35, 0x6a};
constexpr std::int32_t StateButtonPad = 0x10;
constexpr std::int32_t StateButtonY = 4;
constexpr std::int32_t StateButtonHeight = 0x14;
// 0x49dcf0: the friend list's column headers.
constexpr std::int32_t ColumnHeight = 0x13;
// 0x4a35a0: the box at (0x32, 0x32), 0x96 × 100 with the bar, the border and the drag; the 0x50 × 0xe EDIT at
// (0x1e, 0xe) with the limit of 10 and filter 8; the 0x32 × 0x14 buttons at (0x12, 0x28) and (0x49, 0x28).
constexpr std::int32_t AddBoxPlace = 0x32;
constexpr std::int32_t AddBoxWidth = 0x96;
constexpr std::int32_t AddBoxHeight = 100;
constexpr std::int32_t AddBoxFlags = Ui::TitleFlag | Ui::BorderFlag | Ui::DragFlag;
constexpr std::int32_t EditWidth = 0x50;
constexpr std::int32_t EditHeight = 0xe;
constexpr std::int32_t EditX = 0x1e;
constexpr std::int32_t EditY = 0xe;
constexpr std::int32_t AddNameFilter = 8;
constexpr std::int32_t AddBoxButtonWidth = 0x32;
constexpr std::int32_t AddBoxButtonHeight = 0x14;
constexpr std::int32_t ConfirmX = 0x12;
constexpr std::int32_t CancelX = 0x49;
constexpr std::int32_t AddBoxButtonY = 0x28;
constexpr std::int32_t FirstCommand = 1;
constexpr std::int32_t SecondCommand = 2;
constexpr std::int32_t ThirdCommand = 3;
constexpr std::int32_t FourthCommand = 4;
// 0x461f00 and 0x462bd0: the row 13 high, 3 above its y.
constexpr std::int32_t RowHeight = 13;
constexpr std::int32_t RowLift = 3;
// 0x49d0b0: a friend with a state greater than 0xfc is absent.
constexpr std::uint8_t LastOnlineState = 0xfc;
// 0x4a2f70: F.
constexpr std::uint8_t FriendsKey = 0x46;

Ui::OldWindow* FriendsWindowOf(Ui::OldWindowManager& manager)
{
    if (manager.friends == 0)
        return nullptr;
    return Ui::FindTheOldWindow(manager, manager.friends);
}

std::size_t RowCount(const Ui::OldWindow& tab)
{
    if (tab.kind == Ui::OldWindowKind::FriendTab)
        return tab.tab.friends.size();
    if (tab.kind == Ui::OldWindowKind::WindowsTab)
        return tab.tab.windows.size();
    return tab.tab.letters.size();
}

// 0x45abc0 and 0x47a9d0 over a tab's list: the whole queue.
void DrainTheList(Ui::OldWindowManager& manager, Ui::OldWindow& tab)
{
    Ui::OldList& list = tab.tab.list;
    while (!list.base.queue.empty())
    {
        list.base.current = list.base.queue.front();
        list.base.queue.pop_front();
        Ui::HandleTheListMessage(list, RowCount(tab));
        Ui::Steps::PlaceAgainstTheParent(manager, list.base);
    }
}

// 0x45aa50 over a tab's list.
void PostToTheList(Ui::OldWindowManager& manager, Ui::OldWindow& tab, std::int32_t type)
{
    tab.tab.list.base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    DrainTheList(manager, tab);
}

// A button's queue with its step with 1 (0x45aa80): 0x45abc0 with each message.
void DrainTheButton(Ui::OldWindowManager& manager, Ui::OldButton& button)
{
    while (!button.base.queue.empty())
    {
        button.base.current = button.base.queue.front();
        button.base.queue.pop_front();
        Ui::Steps::PlaceAgainstTheParent(manager, button.base);
    }
}

// 0x45aa50 over the EDIT: its vtable + 0x28 is 0x45a640, which returns 0.
void PostToTheEdit(Ui::OldWindowManager& manager, Ui::OldEdit& edit, std::int32_t type)
{
    edit.base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!edit.base.queue.empty())
    {
        edit.base.current = edit.base.queue.front();
        edit.base.queue.pop_front();
        Ui::Steps::PlaceAgainstTheParent(manager, edit.base);
    }
}

// 0x45a210 of a list (0x460970, 0x462010 and 0x463190): it hangs from its bottom edge; the rest, as OldList.
Ui::OldList MakeTheList(Ui::OldWindowManager& manager)
{
    Ui::OldList list;
    list.base = Ui::Steps::MakeTheBase(manager);
    list.base.rectMode = 2;
    return list;
}

// A tab: the frame (0x491ca0), the list and the buttons, in their fields' order.
Ui::OldWindow MakeTheTab(Ui::OldWindowManager& manager, Ui::OldWindowKind kind, std::size_t buttons)
{
    Ui::OldWindow tab = Ui::Steps::MakeTheFrame(manager);
    tab.kind = kind;
    tab.tab.list = MakeTheList(manager);
    for (std::size_t at = 0; at < buttons; ++at)
        tab.tab.buttons.push_back(Ui::Steps::MakeTheButton(manager));
    return tab;
}

// A tab's init (0x49c8b0, 0x49e5c0 and 0x4a06b0): no flags, at (0x32, 0x32), 0xfa × 0xaa, the list and the buttons
// from the bottom.
void InitTheTab(Ui::OldWindow& tab, const std::string& title, std::int32_t parent,
                const std::array<std::size_t, 4>& labels, const Ui::OldWindowTexts& texts)
{
    Ui::OldWindowBase& base = tab.base;
    tab.frame.title = title;
    base.parent = parent;
    base.flags = 0;
    base.x = FriendsPlace;
    base.y = FriendsPlace;
    base.width = FriendsWidth;
    base.height = FriendsHeight;
    tab.frame.minWidth = FriendsWidth;
    tab.frame.minHeight = FriendsMinHeight;
    tab.frame.maxWidth = 0;
    tab.frame.maxHeight = 0;
    Ui::OldList& list = tab.tab.list;
    list.base.parent = base.id;
    list.base.placeMode = 2;
    list.base.placeX = 0;
    list.base.placeY = ListBottom;
    list.base.sizeMode = 3;
    list.base.sizeX = 0;
    list.base.sizeY = ListHeightCut;
    for (std::size_t at = 0; at < tab.tab.buttons.size(); ++at)
    {
        Ui::OldButton& button = tab.tab.buttons[at];
        button.base.width = TabButtonWidth;
        button.base.height = TabButtonHeight;
        button.command = static_cast<std::int32_t>(at) + 1;
        button.label = texts.At(labels[at]);
        button.base.parent = base.id;
        button.base.placeMode = 2;
        button.base.placeX = TabButtonX[at];
        button.base.placeY = TabButtonY;
    }
}

// 0x49cad0: the name and the state of the friend list's chosen one, or none.
const FriendEntry* SelectedFriend(Ui::OldWindow& tab)
{
    const std::optional<std::size_t> at = Ui::SelectedInTheList(tab.tab.list, tab.tab.friends.size());
    if (!at || *at >= tab.tab.friends.size())
        return nullptr;
    return &tab.tab.friends[*at];
}

// 0x49e740: the id of the window list's chosen one, or 0.
std::int32_t SelectedWindow(Ui::OldWindow& tab)
{
    const std::optional<std::size_t> at = Ui::SelectedInTheList(tab.tab.list, tab.tab.windows.size());
    if (!at || *at >= tab.tab.windows.size())
        return 0;
    return tab.tab.windows[*at].id;
}

// 0x49bd60: the list with the model's content and the place (from 1) of the last one with the name, or 0.
std::int32_t CopyTheModel(Ui::OldWindowManager& manager, Ui::OldWindow& tab, std::string_view name)
{
    tab.tab.friends.clear();
    std::int32_t found = 0;
    std::int32_t at = 0;
    for (const FriendEntry& entry : manager.friendModel.entries)
    {
        tab.tab.friends.push_back(entry);
        ++at;
        if (SameFriendName(entry.name, name))
            found = at;
    }
    return found;
}

// 0x49ddd0.
void RefreshTheTab(Ui::OldWindowManager& manager, Ui::OldWindow& tab)
{
    std::string name;
    if (tab.tab.list.selection > 0)
    {
        if (const FriendEntry* selected = SelectedFriend(tab); selected != nullptr)
            name = FriendName(selected->name);
    }
    tab.tab.list.refreshed = 1;
    const std::int32_t at = CopyTheModel(manager, tab, name);
    Ui::SelectInTheList(tab.tab.list, tab.tab.friends.size(), at);
    Ui::ScrollTheList(tab.tab.list, tab.tab.friends.size(), 0);
}

Core::Expected<void, std::string> Cut(std::string what)
{
    return Core::Unexpected(std::move(what));
}

Core::Expected<std::int32_t, std::string> CreateFromAStep(Ui::OldWindowManager& manager,
                                                          const Ui::OldWindowRequest& request,
                                                          const Ui::OldWindowGlobals& globals,
                                                          const Ui::OldWindowStepView& view, std::string_view where)
{
    if (view.texts == nullptr || view.measure == nullptr)
        return Core::Unexpected(std::string(where) + ": la fábrica de las ventanas viejas sin los textos ni la medida");
    return Ui::CreateTheOldWindow(manager, request, *view.texts, *view.measure, &globals, &view);
}

Core::Expected<void, std::string> Send(const Ui::OldWindowStepView& view, Ui::FriendSend packet, std::string_view where)
{
    if (!view.send)
        return Core::Unexpected(std::string(where) + ": el paquete de los amigos sin el cable");
    view.send(packet);
    return {};
}

// 0x49dc37..0x49dc62, the tail of cases 1, 2 and 4 of 0x49d0b0 (4, writing a letter): with the id that 0x48ee60 gave
// (0, no box), it finds it (0x490a00) and sends it the 8 and the 9, each with its step with 1 (0x45aa50).
Core::Expected<void, std::string> PlaceTheNewBox(Ui::OldWindowManager& manager, std::int32_t id,
                                                 const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view)
{
    if (id == 0)
        return {};
    Ui::OldWindow* box = Ui::FindTheOldWindow(manager, id);
    if (box == nullptr)
        return {};
    if (auto placed = Ui::Steps::PostAndStep(manager, *box, Ui::PlaceMessage, globals, view); !placed.HasValue())
        return placed;
    return Ui::Steps::PostAndStep(manager, *box, Ui::SizeMessage, globals, view);
}

// 0x461f00 (friends) and 0x462bd0 (windows), the vtable + 0x88: the row under the mouse. Returns 1: the step goes on.
std::int32_t HoverTheRow(Ui::OldWindowManager& manager, Ui::OldWindow& tab, std::int32_t row, std::size_t at,
                         const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view)
{
    Ui::OldList& list = tab.tab.list;
    if (tab.kind == Ui::OldWindowKind::FriendTab)
        return Ui::Steps::HoverAFriendRow(manager, list, tab.tab.friends, row, at, globals, view);
    const std::size_t size = RowCount(tab);
    const auto width =
        static_cast<std::int32_t>(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0);
    if (!Ui::Steps::MouseIn(view, list.base.x, Ui::ListRowY(list, size, row) - RowLift, width, RowHeight, TopLeftRect))
        return 1;
    const auto toggle = [](std::int32_t& selected) { selected = (selected + 1) % 2; };
    if (tab.kind == Ui::OldWindowKind::LettersTab)
    {
        // 0x464510, the letter list's vtable + 0x88: the click on the mark (0xc by 0xd at the edge) flips it in the
        // copy and in the model's letter (0x4a01a0); the click chooses the row. The double click, except over the mark,
        // is the 0xd to the parent.
        if (globals.leftClick)
        {
            if (Ui::Steps::MouseIn(view, list.base.x, Ui::ListRowY(list, size, row) - RowLift, LetterMarkWidth,
                                   RowHeight, TopLeftRect))
            {
                toggle(tab.tab.letters[at].selected);
                if (LetterEntry* letter = FindALetter(manager.letterModel, tab.tab.letters[at].index);
                    letter != nullptr)
                    letter->selected = tab.tab.letters[at].selected;
            }
            Ui::SelectInTheList(list, size, list.scroll + 1 + row);
            globals.leftClick = false;
        }
        if (globals.doubleClick && list.base.x + LetterMarkWidth < view.mouseX)
        {
            Ui::PostToTheWindow(manager, list.base.parent, Ui::ChosenRowMessage, list.base.id, 0);
            globals.doubleClick = false;
        }
        return 1;
    }
    if (tab.kind == Ui::OldWindowKind::WindowsTab)
    {
        if (globals.leftClick)
        {
            Ui::SelectInTheList(list, size, list.scroll + 1 + row);
            toggle(tab.tab.windows[at].selected);
            Ui::PostToTheWindow(manager, list.base.parent, Ui::ChosenRowMessage, list.base.id, 0);
            globals.leftClick = false;
        }
        return 1;
    }
    return 1;
}

// 0x45aa80 over a tab's list: the queue (0x47a9d0 and 0x45abc0), the mouse and 0x47b0f0 with its row.
void StepTheList(Ui::OldWindowManager& manager, Ui::OldWindow& tab, std::int32_t arg,
                 const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view)
{
    DrainTheList(manager, tab);
    if (arg == 1)
        return;
    Ui::OldList& list = tab.tab.list;
    if (!Ui::Steps::ClaimTheMouse(manager, list.base, globals, view))
        return;
    const Ui::OldListGlobals listGlobals{.leftClick = globals.leftClick,
                                         .wheel = globals.wheel,
                                         .capture = globals.capture,
                                         .focusedList = globals.focusedList,
                                         .mouseOnUi = globals.mouseOnUi,
                                         .sound = globals.sound,
                                         .drawn = manager.drawn == 1,
                                         .modal = manager.modal,
                                         .soundRequest = globals.soundRequest};
    const Ui::OldListView listView{.mouseX = view.mouseX, .mouseY = view.mouseY, .keyDown = [&view](std::uint8_t key) {
                                       return view.keyDown && view.keyDown(key);
                                   }};
    static_cast<void>(Ui::StepTheListMouse(list, RowCount(tab), listGlobals, listView,
                                           [&](std::int32_t row, std::size_t at)
                                           { return HoverTheRow(manager, tab, row, at, globals, view); }));
}

// 0x4a37e0: the EDIT's text (GetText of 0xff), the EDIT emptied and, with something typed, the 11 to the tab with the
// text (the 0x100 malloc buffer) and the 4 to the manager to close it.
void ConfirmTheName(Ui::OldWindowManager& manager, Ui::OldWindow& box)
{
    std::string name(box.add.edit.text.Value());
    box.add.edit.text.Clear();
    if (name.empty())
        return;
    if (Ui::OldWindow* owner = Ui::FindTheOldWindow(manager, box.add.owner); owner != nullptr)
        owner->base.queue.push_back(
            Ui::OldMessage{.type = Ui::NameMessage, .a = box.base.id, .b = 1, .text = std::move(name)});
    Ui::PostToTheManager(manager, Ui::CloseMessage, box.base.id, 0);
}

// 0x4a2be0, the friends window's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheFriendsWindowMessage(Ui::OldWindowManager& manager, Ui::OldWindow& window,
                                                                const Ui::OldWindowGlobals& globals,
                                                                const Ui::OldWindowStepView& view)
{
    const Ui::OldMessage& message = window.base.current;
    if (message.type == Ui::RaisedMessage)
    {
        globals.focusedList = window.tabs[static_cast<std::size_t>(window.friends.currentTab)].tab.list.base.id;
        return {};
    }
    // The answer to "go offline?": C4 00 and +0xe0 at 1. 0x4a7280 closes the chats with a friend (milestone 10).
    if (message.type == Ui::AnswerMessage && message.b == 1 && manager.offlineSent == 0)
    {
        if (auto sent =
                Send(view, Ui::FriendSend{.kind = Ui::FriendSendKind::State, .value = 0, .name = {}}, "0x4a2be0");
            !sent.HasValue())
            return sent;
        manager.offlineSent = 1;
    }
    return {};
}

// 0x49d0b0, the friends tab's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheFriendTabMessage(Ui::OldWindowManager& manager, Ui::OldWindow& tab,
                                                            const Ui::OldWindowGlobals& globals,
                                                            const Ui::OldWindowStepView& view)
{
    Ui::OldMessage& message = tab.base.current;
    // A double click on a friend is the chat button.
    if (message.type == Ui::ChosenRowMessage)
    {
        globals.RequestSound(ButtonSound);
        message.type = Ui::CommandMessage;
        message.a = ThirdCommand;
    }
    if (message.type == Ui::NameMessage)
    {
        // The add box's name: C1 0D C1 and free of the buffer.
        if (message.b == 0)
            return {};
        return Send(view, Ui::FriendSend{.kind = Ui::FriendSendKind::Add, .value = 0, .name = FriendName(message.text)},
                    "0x49d0b0(11)");
    }
    if (message.type == Ui::AnswerMessage)
    {
        // "Delete?" with yes: C1 0D C3 with the chosen one.
        if (message.b != 1)
            return {};
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr)
            return {};
        return Send(view, Ui::FriendSend{.kind = Ui::FriendSendKind::Delete, .value = 0, .name = selected->name},
                    "0x49d0b0(12)");
    }
    if (message.type != Ui::CommandMessage || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case FirstCommand:
    {
        // Add: the box (0x48ee60(4)) with text 1023, from the tab; its id to +0xf0.
        const std::string title = view.texts != nullptr ? view.texts->At(TypeTheNameText) : std::string();
        auto created = CreateFromAStep(
            manager,
            Ui::OldWindowRequest{
                .kind = Ui::AddFriendCase, .x = -1, .y = -1, .data = title, .aux = tab.base.id, .flags = 0},
            globals, view, "0x49d0b0(1)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        manager.letter = created.Value();
        return PlaceTheNewBox(manager, created.Value(), globals, view);
    }
    case SecondCommand:
    {
        // Delete: "<name><1024>" in a modal two-button box (0x48ee60(5)) that answers the tab.
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr)
            return {};
        const std::string question =
            selected->name + (view.texts != nullptr ? view.texts->At(ReallyDeleteText) : std::string());
        auto created = CreateFromAStep(
            manager,
            Ui::OldWindowRequest{
                .kind = Ui::ModalQuestionCase, .x = -1, .y = -1, .data = question, .aux = tab.base.id, .flags = 0},
            globals, view, "0x49d0b0(2)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return PlaceTheNewBox(manager, created.Value(), globals, view);
    }
    case ThirdCommand:
    {
        // The chat with the chosen one, if online (0x4a6430, 0x4a6af0 and 0x4a6910, C1 CA; Messenger/LetterWindows).
        const FriendEntry* selected = SelectedFriend(tab);
        if (selected == nullptr || selected->state > LastOnlineState)
            return {};
        return Ui::Steps::ChatWithTheFriend(manager, selected->name, view);
    }
    case FourthCommand:
    {
        // 0x49dbd4: the writing window with the chosen one's name (Messenger/LetterWindows) and the tail 0x49dc37.
        auto opened = Ui::Steps::WriteToTheFriend(manager, SelectedFriend(tab), globals, view);
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return PlaceTheNewBox(manager, opened.Value(), globals, view);
    }
    default:
        return {};
    }
}

// 0x49ea50, the windows tab's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheWindowsTabMessage(Ui::OldWindowManager& manager, Ui::OldWindow& tab,
                                                             const Ui::OldWindowGlobals& globals,
                                                             const Ui::OldWindowStepView& view)
{
    const Ui::OldMessage& message = tab.base.current;
    if (message.type == Ui::CommandMessage)
    {
        // Hide all but the friends window, without noting them for F6.
        if (manager.modal == 0 && message.a == FirstCommand)
            return Ui::HideTheOldWindows(manager, 1, 0, globals, &view);
        return {};
    }
    if (message.type != Ui::ChosenRowMessage)
        return {};
    const std::int32_t id = SelectedWindow(tab);
    const Ui::OldWindow* chosen = Ui::FindTheOldWindow(manager, id);
    // 0x49ea7f: 0x45a9c0 over what 0x490a00 gave, without checking for NULL.
    if (chosen == nullptr)
        return Cut("0x49ea7f: el renglón de una ventana que ya no está (0x45a9c0 sobre NULL)");
    // The top one on view is minimized; otherwise, it is raised.
    if (chosen->base.state != Ui::HiddenWindow && SelectedWindow(tab) == Ui::TopOldWindow(manager))
    {
        Ui::PostToTheManager(manager, Ui::MinimizeMessage, SelectedWindow(tab), 0);
        return {};
    }
    globals.RequestSound(ButtonSound);
    Ui::PostToTheManager(manager, Ui::RaiseMessage, SelectedWindow(tab), 0);
    Ui::ForgetTheHiddenWindows(manager);
    globals.leftHeld = false;
    return {};
}

// 0x4a3850, the add box's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheAddBoxMessage(Ui::OldWindowManager& manager, Ui::OldWindow& box,
                                                         const Ui::OldWindowGlobals& globals)
{
    const Ui::OldMessage& message = box.base.current;
    if (message.type == Ui::RaisedMessage)
        Ui::Steps::FocusTheEdit(manager, box.add.edit, globals);
    else if (message.type == Ui::EnterMessage || (message.type == Ui::CommandMessage && message.a == FirstCommand))
        ConfirmTheName(manager, box);
    else if (message.type == Ui::CommandMessage && message.a == SecondCommand)
        Ui::PostToTheManager(manager, Ui::CloseMessage, box.base.id, 0);
    return {};
}

// 0x4a3030, the friends window's vtable + 0x58: the tabs' headers and the state button.
Core::Expected<void, std::string> AfterTheFriendsWindowMove(Ui::OldWindowManager& manager, Ui::OldWindow& window,
                                                            const Ui::OldWindowGlobals& globals,
                                                            const Ui::OldWindowStepView& view)
{
    if (window.base.state == Ui::ResizedWindow)
        return {};
    std::optional<std::int32_t> header;
    for (std::size_t at = 0; at < HeaderX.size(); ++at)
    {
        if (Ui::Steps::MouseIn(view, Ui::Steps::ClientX(window, HeaderX[at]), Ui::Steps::ClientY(window, HeaderY),
                               HeaderWidth, HeaderHeight, TopLeftRect))
        {
            header = static_cast<std::int32_t>(at);
            break;
        }
    }
    if (!header)
    {
        window.friends.hoverTab = window.friends.currentTab;
        if (view.measure == nullptr || view.texts == nullptr)
            return Cut("0x4a3030: el botón del estado sin la medida de su texto");
        const std::string label = view.texts->At(StateButtonText);
        const Render::Text::TextExtent extent = view.measure->measure.Measure(view.measure->dcFont, label);
        const std::int32_t width = Render::Text::DesignX(extent.width, view.measure->screen);
        const std::int32_t x = Ui::Steps::ClientX(window, 0) + Ui::Steps::ClientWidth(window) - StateButtonPad - width;
        if (!Ui::Steps::MouseIn(view, x, Ui::Steps::ClientY(window, StateButtonY), width + StateButtonPad,
                                StateButtonHeight, TopLeftRect))
            return {};
        if (!globals.releasedClick)
            return {};
        globals.RequestSound(ButtonSound);
        if (manager.offlineSent != 1)
        {
            // "Go offline?": the answer comes back to the window as the 12 (0x4a2be0).
            const std::string question = view.texts->At(GoOfflineText);
            auto created = CreateFromAStep(manager,
                                           Ui::OldWindowRequest{.kind = Ui::ModalQuestionCase,
                                                                .x = -1,
                                                                .y = -1,
                                                                .data = question,
                                                                .aux = window.base.id,
                                                                .flags = 0},
                                           globals, view, "0x4a3030");
            if (!created.HasValue())
                return Core::Unexpected(std::move(created).Error());
            globals.releasedClick = false;
            return {};
        }
        if (auto sent =
                Send(view, Ui::FriendSend{.kind = Ui::FriendSendKind::State, .value = 1, .name = {}}, "0x4a3030");
            !sent.HasValue())
            return sent;
        manager.offlineSent = 0;
        globals.releasedClick = false;
        return {};
    }
    window.friends.hoverTab = *header;
    if (!globals.leftHeld)
        return {};
    const bool changed = window.friends.currentTab != *header;
    if (changed)
        globals.RequestSound(ButtonSound);
    window.friends.currentTab = *header;
    globals.focusedList = window.tabs[static_cast<std::size_t>(*header)].tab.list.base.id;
    globals.leftHeld = false;
    // 0x4a23c0 puts the friend list back at 0x5762ec0 (Q-C883).
    if (auto settled = Ui::Steps::SettleTheFriendsKinds(manager, window, globals, view); !settled.HasValue())
        return settled;
    if (!changed)
        return {};
    for (const std::size_t at :
         {std::size_t{FriendTabNumber}, std::size_t{WindowsTabNumber}, std::size_t{LettersTabNumber}})
    {
        if (auto settled = Ui::Steps::SettleTheFriendsKinds(manager, window.tabs[at], globals, view);
            !settled.HasValue())
            return settled;
    }
    return {};
}

// 0x49dcf0, the friends tab's vtable + 0x58: the column headers sort by name or by state.
void AfterTheFriendTabMove(Ui::OldWindowManager& manager, Ui::OldWindow& tab, const Ui::OldWindowGlobals& globals,
                           const Ui::OldWindowStepView& view)
{
    if (!globals.leftHeld)
        return;
    const Ui::OldList& list = tab.tab.list;
    const std::int32_t x = Ui::Steps::ClientX(tab, 0);
    const std::int32_t y = Ui::Steps::ClientY(tab, 0);
    if (Ui::Steps::MouseIn(view, x, y, list.nameWidth, ColumnHeight, TopLeftRect))
    {
        if (manager.friendModel.sortMode != ByNameSort)
        {
            globals.RequestSound(ButtonSound);
            SortTheFriends(manager.friendModel, ByNameSort);
            RefreshTheTab(manager, tab);
        }
        globals.leftHeld = false;
        return;
    }
    if (!Ui::Steps::MouseIn(view, x + list.nameWidth, y, list.placeWidth, ColumnHeight, TopLeftRect))
        return;
    if (manager.friendModel.sortMode != ByStateSort)
    {
        globals.RequestSound(ButtonSound);
        SortTheFriends(manager.friendModel, ByNameSort);
        SortTheFriends(manager.friendModel, ByStateSort);
        RefreshTheTab(manager, tab);
    }
    globals.leftHeld = false;
}

// The 8s, the 9 twice and the 0xe of 0x49ca40, 0x49e690 and 0x4a0830: the buttons and the list against the tab.
void SettleTheTab(Ui::OldWindowManager& manager, Ui::OldWindow& tab)
{
    for (Ui::OldButton& button : tab.tab.buttons)
        Ui::Steps::PostAndSettle(manager, button, Ui::PlaceMessage);
    PostToTheList(manager, tab, Ui::PlaceMessage);
    PostToTheList(manager, tab, Ui::SizeMessage);
    PostToTheList(manager, tab, Ui::SizeMessage);
    PostToTheList(manager, tab, Ui::ToTheEndMessage);
}

// 0x45aa50: the step with 1 handles the WHOLE queue, including the commands that came from the children's step.
Core::Expected<void, std::string> PlaceTheTabForTheDraw(Ui::OldWindowManager& manager, Ui::OldWindow& tab,
                                                        std::int32_t type, const Ui::OldWindowDrawView& view)
{
    if (view.globals != nullptr && view.stepView != nullptr)
        return Ui::Steps::PostAndStep(manager, tab, type, *view.globals, *view.stepView);
    if (std::ranges::any_of(tab.base.queue, [](const Ui::OldMessage& message)
                            { return message.type != Ui::PlaceMessage && message.type != Ui::SizeMessage; }))
        return Cut("0x45aa50: un mensaje funcional de dibujo sin los servicios del paso");
    tab.base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!tab.base.queue.empty())
    {
        tab.base.current = tab.base.queue.front();
        tab.base.queue.pop_front();
        Ui::Steps::PlaceAgainstTheParent(manager, tab.base);
    }
    DrainTheList(manager, tab);
    for (Ui::OldButton& button : tab.tab.buttons)
        DrainTheButton(manager, button);
    return {};
}
} // namespace

} // namespace Client::Messenger

namespace Client::Ui::Steps
{
void FocusTheEdit(OldWindowManager& manager, OldEdit& edit, const OldWindowGlobals& globals)
{
    manager.orphanedEdit.reset();
    manager.focusedEditIsOrphan = false;
    globals.keyboardFocus = Ui::ChatFocus::OldWindow;
    manager.focusedEdit = edit.base.id;
    globals.focusedList = edit.base.id;
}

std::int32_t HoverAFriendRow(OldWindowManager& manager, OldList& list, std::deque<Messenger::FriendEntry>& friends,
                             std::int32_t row, std::size_t at, const OldWindowGlobals& globals,
                             const OldWindowStepView& view)
{
    const std::size_t size = friends.size();
    const auto width =
        static_cast<std::int32_t>(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0);
    if (!MouseIn(view, list.base.x, ListRowY(list, size, row) - Messenger::RowLift, width, Messenger::RowHeight,
                 Messenger::TopLeftRect))
        return 1;
    // [+0xb4] is the chosen one that the vtable + 0x58 left: inc, and 1.
    const auto toggle = [](std::int32_t& selected) { selected = (selected + 1) % 2; };
    if (globals.leftClick)
    {
        SelectInTheList(list, size, list.scroll + 1 + row);
        toggle(friends[at].selected);
        globals.leftClick = false;
    }
    if (globals.doubleClick)
    {
        PostToTheWindow(manager, list.base.parent, ChosenRowMessage, list.base.id, 0);
        globals.doubleClick = false;
    }
    if (globals.rightClick)
    {
        SelectInTheList(list, size, list.scroll + 1 + row);
        toggle(friends[at].selected);
        globals.rightClick = false;
        if (view.whisperTo)
            view.whisperTo(Messenger::FriendName(friends[at].name));
    }
    return 1;
}

bool IsAFriendsKind(OldWindowKind kind)
{
    return kind == OldWindowKind::FriendsWindow || kind == OldWindowKind::FriendTab ||
           kind == OldWindowKind::WindowsTab || kind == OldWindowKind::LettersTab ||
           kind == OldWindowKind::AddFriendBox;
}

Core::Expected<void, std::string> HandleTheFriendsMessage(OldWindowManager& manager, OldWindow& window,
                                                          const OldWindowGlobals& globals,
                                                          const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
        return Messenger::HandleTheFriendsWindowMessage(manager, window, globals, view);
    case OldWindowKind::FriendTab:
        return Messenger::HandleTheFriendTabMessage(manager, window, globals, view);
    case OldWindowKind::WindowsTab:
        return Messenger::HandleTheWindowsTabMessage(manager, window, globals, view);
    case OldWindowKind::LettersTab:
        return HandleTheLettersTabMessage(manager, window, globals, view);
    case OldWindowKind::AddFriendBox:
        return Messenger::HandleTheAddBoxMessage(manager, window, globals);
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheFriendsChildren(OldWindowManager& manager, OldWindow& window, std::int32_t arg,
                                                         const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
    {
        // 0x4a2f70: the tab on view, if it is not dragged or resized; then F closes it.
        const std::int32_t current = window.friends.currentTab;
        if (window.base.state != DraggedWindow && window.base.state != ResizedWindow && current >= 0 &&
            current < static_cast<std::int32_t>(Messenger::FriendsTabCount))
        {
            if (auto stepped =
                    StepTheWindow(manager, window.tabs[static_cast<std::size_t>(current)], arg, globals, view);
                !stepped.HasValue())
                return stepped;
        }
        if (!view.chatOpen && LastOldWindow(manager) == window.base.id && view.keyEdge &&
            view.keyEdge(Messenger::FriendsKey))
        {
            const OldWindow* self = FindTheOldWindow(manager, window.base.id);
            if (self != nullptr && self->base.state == ShownWindow)
                PostToTheManager(manager, CloseMessage, window.base.id, 0);
        }
        return {};
    }
    case OldWindowKind::FriendTab:
    case OldWindowKind::WindowsTab:
        // 0x49dc90 and 0x49eb20.
        Messenger::StepTheList(manager, window, arg, globals, view);
        for (OldButton& button : window.tab.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    case OldWindowKind::LettersTab:
        // 0x4a2010.
        Messenger::StepTheList(manager, window, arg, globals, view);
        for (OldButton& button : window.tab.buttons)
            StepTheButton(manager, button, arg, globals, view);
        return {};
    case OldWindowKind::AddFriendBox:
        // 0x4a38a0: the two buttons and the EDIT.
        for (OldButton& button : window.add.buttons)
            StepTheButton(manager, button, arg, globals, view);
        Steps::StepTheEdit(manager, window.add.edit, arg, globals, view);
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> AfterTheFriendsMove(OldWindowManager& manager, OldWindow& window,
                                                      const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (window.kind == OldWindowKind::FriendsWindow)
        return Messenger::AfterTheFriendsWindowMove(manager, window, globals, view);
    // 0x49eb50 and 0x4a38e0 do nothing; 0x4a2070, in Messenger/LetterWindows.
    if (window.kind == OldWindowKind::FriendTab)
        Messenger::AfterTheFriendTabMove(manager, window, globals, view);
    if (window.kind == OldWindowKind::LettersTab)
        AfterTheLettersTabMove(manager, window, globals, view);
    return {};
}

Core::Expected<void, std::string> SettleTheFriendsKinds(OldWindowManager& manager, OldWindow& window,
                                                        const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::FriendsWindow:
    {
        // 0x4a23c0: the 8 and the 9 to the three (+0xb8, +0x4d0 and +0x738), their vtable + 0x30 and the friend list to
        // 0x5762ec0.
        constexpr std::array<std::size_t, Messenger::FriendsTabCount> Order{
            Messenger::FriendTabNumber, Messenger::WindowsTabNumber, Messenger::LettersTabNumber};
        for (const std::int32_t type : {PlaceMessage, SizeMessage})
        {
            for (const std::size_t at : Order)
            {
                if (auto stepped = PostAndStep(manager, window.tabs[at], type, globals, view); !stepped.HasValue())
                    return stepped;
            }
        }
        for (const std::size_t at : Order)
        {
            if (auto settled = SettleTheFriendsKinds(manager, window.tabs[at], globals, view); !settled.HasValue())
                return settled;
        }
        globals.focusedList = window.tabs[Messenger::FriendTabNumber].tab.list.base.id;
        return {};
    }
    case OldWindowKind::FriendTab:
    case OldWindowKind::WindowsTab:
    case OldWindowKind::LettersTab:
        Messenger::SettleTheTab(manager, window);
        return {};
    case OldWindowKind::AddFriendBox:
        // 0x4a3700: the buttons and the EDIT, the focus to the EDIT and +0x9c at 1.
        for (OldButton& button : window.add.buttons)
            PostAndSettle(manager, button, PlaceMessage);
        Messenger::PostToTheEdit(manager, window.add.edit, PlaceMessage);
        Steps::FocusTheEdit(manager, window.add.edit, globals);
        window.frame.editField = 1;
        return {};
    default:
        return {};
    }
}

Core::Expected<OldWindow, std::string> MakeTheFriendsWindow(OldWindowManager& manager, const OldWindowRequest& request,
                                                            const OldWindowTexts& texts,
                                                            const OldWindowMeasure& measure)
{
    OldWindow window = MakeTheFrame(manager);
    window.kind = OldWindowKind::FriendsWindow;
    window.tabs.resize(Messenger::FriendsTabCount);
    // The fields in order: +0xb8 (friends, 0x48fa60), +0x4d0 (windows, 0x48fc80) and +0x738 (letters, 0x48fd90; its
    // list and its buttons are from milestone 10). Each over the frame (0x491ca0) with the tabs' vtable (0x6b9e80), its
    // list (0x460970, 0x462010 or 0x463190) and its buttons (0x45ad80).
    window.tabs[Messenger::FriendTabNumber] = Messenger::MakeTheTab(manager, OldWindowKind::FriendTab, 4);
    window.tabs[Messenger::WindowsTabNumber] = Messenger::MakeTheTab(manager, OldWindowKind::WindowsTab, 1);
    window.tabs[Messenger::LettersTabNumber] = Messenger::MakeTheTab(manager, OldWindowKind::LettersTab, 4);
    // 0x460970: the two column widths.
    OldList& friendList = window.tabs[Messenger::FriendTabNumber].tab.list;
    friendList.nameWidth =
        Render::Text::DesignX(measure.measure.Measure(measure.dcFont, Messenger::WidestName).width, measure.screen) +
        Messenger::ColumnMargin;
    friendList.placeWidth =
        Render::Text::DesignX(measure.measure.Measure(measure.dcFont, texts.At(Messenger::PlaceText)).width,
                              measure.screen) +
        Messenger::ColumnMargin;
    // 0x463190: +0x130 (the mark column's width) at 0x19 and +0x134..+0x13c, the widths of texts 1028, 1029 and 1030
    // like those of 0x460970.
    OldList& letterList = window.tabs[Messenger::LettersTabNumber].tab.list;
    letterList.headers = Messenger::LetterListHeaders;
    for (const auto& [width, text] : {std::pair{&letterList.nameWidth, Messenger::SenderColumnText},
                                      std::pair{&letterList.placeWidth, Messenger::DateColumnText},
                                      std::pair{&letterList.subjectWidth, Messenger::SubjectColumnText}})
        *width = Render::Text::DesignX(measure.measure.Measure(measure.dcFont, texts.At(text)).width, measure.screen) +
                 Messenger::ColumnMargin;
    // 0x4a2280.
    OldWindowBase& base = window.base;
    window.frame.title = std::string(request.data);
    base.parent = request.aux;
    base.x = Messenger::FriendsPlace;
    base.y = Messenger::FriendsPlace;
    base.width = Messenger::FriendsWidth;
    base.height = Messenger::FriendsHeight;
    window.frame.maxWidth = 0;
    window.frame.maxHeight = 0;
    window.frame.minWidth = Messenger::FriendsWidth;
    window.frame.minHeight = Messenger::FriendsMinHeight;
    Messenger::InitTheTab(
        window.tabs[Messenger::FriendTabNumber], texts.At(Messenger::FriendTabText), base.id,
        {Messenger::AddButtonText, Messenger::DeleteButtonText, Messenger::ChatButtonText, Messenger::WriteButtonText},
        texts);
    window.tabs[Messenger::FriendTabNumber].tab.list.headers = 1;
    // 0x49ddd0 from 0x49c8b0: the list with the model.
    Messenger::RefreshTheTab(manager, window.tabs[Messenger::FriendTabNumber]);
    Messenger::InitTheTab(window.tabs[Messenger::WindowsTabNumber], texts.At(Messenger::WindowsTabText), base.id,
                          {Messenger::WindowsButtonText, 0, 0, 0}, texts);
    // 0x4a06b0: 0x4a0960 copies the model's letters. The buttons, like the friends tab's (*inferred*).
    Messenger::InitTheTab(window.tabs[Messenger::LettersTabNumber], texts.At(Messenger::LettersTabText), base.id,
                          {Messenger::WriteButtonText, Messenger::SecondLetterButtonText,
                           Messenger::ThirdLetterButtonText, Messenger::FourthLetterButtonText},
                          texts);
    RefreshTheLetters(manager, window.tabs[Messenger::LettersTabNumber]);
    for (OldWindow& tab : window.tabs)
    {
        // 0x4908d0: to the second map.
        manager.tabs.try_emplace(tab.base.id, OldTabReference{base.id, window.allocationId});
        tab.base.placeMode = 0;
        tab.base.placeX = 0;
        tab.base.placeY = Messenger::TabTop;
        tab.base.sizeMode = 3;
        tab.base.sizeX = 0;
        tab.base.sizeY = Messenger::TabHeightCut;
    }
    return window;
}

OldWindow MakeTheAddFriendBox(OldWindowManager& manager, const OldWindowRequest& request, const OldWindowTexts& texts)
{
    OldWindow box = MakeTheFrame(manager);
    box.kind = OldWindowKind::AddFriendBox;
    box.add.owner = 0;
    box.add.edit.base = MakeTheBase(manager);
    for (OldButton& button : box.add.buttons)
        button = MakeTheButton(manager);
    // 0x4a35a0.
    OldWindowBase& base = box.base;
    box.frame.title = std::string(request.data);
    base.parent = 0;
    box.add.owner = request.aux;
    base.x = Messenger::AddBoxPlace;
    base.y = Messenger::AddBoxPlace;
    base.width = Messenger::AddBoxWidth;
    base.height = Messenger::AddBoxHeight;
    box.frame.minWidth = Messenger::AddBoxWidth;
    box.frame.minHeight = Messenger::AddBoxHeight;
    box.frame.maxWidth = 0;
    box.frame.maxHeight = 0;
    base.flags = Messenger::AddBoxFlags;
    OldEdit& edit = box.add.edit;
    edit.base.width = Messenger::EditWidth;
    edit.base.height = Messenger::EditHeight;
    edit.base.parent = base.id;
    edit.filter = Messenger::AddNameFilter;
    // 0x4a3645 and 0x4a365e: the normal font (+0x34 with 0x5763554) and the background at 0 (+0x44(0, 0, 0, 0)):
    // transparent.
    edit.backgroundColor = 0;
    edit.base.placeMode = 0;
    edit.base.placeX = Messenger::EditX;
    edit.base.placeY = Messenger::EditY;
    edit.base.state = ShownWindow;
    const auto setUp = [&base, &texts](OldButton& button, std::int32_t command, std::size_t label, std::int32_t x)
    {
        button.command = command;
        button.label = texts.At(label);
        button.base.parent = base.id;
        button.base.placeMode = 0;
        button.base.placeX = x;
        button.base.placeY = Messenger::AddBoxButtonY;
        button.base.width = Messenger::AddBoxButtonWidth;
        button.base.height = Messenger::AddBoxButtonHeight;
    };
    setUp(box.add.buttons[0], Messenger::FirstCommand, Messenger::ConfirmText, Messenger::ConfirmX);
    setUp(box.add.buttons[1], Messenger::SecondCommand, Messenger::CancelText, Messenger::CancelX);
    return box;
}

void AddTheWindowRow(OldWindowManager& manager, std::int32_t id, std::string_view title)
{
    OldWindow* friends = Messenger::FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // 0x4624b0, the list's vtable + 0x94: at the front, with the title at 0x40.
    OldWindow& tab = friends->tabs[Messenger::WindowsTabNumber];
    OldList& list = tab.tab.list;
    std::deque<WindowRow>& rows = tab.tab.windows;
    const std::size_t end = title.find('\0');
    rows.push_front(WindowRow{
        .selected = 0, .id = id, .title = std::string(title.substr(0, std::min(end, std::size_t{0x40}))), .aux = 0});
    SelectInTheList(list, rows.size(), 0);
    if (list.scroll != 0)
        ++list.scroll;
    const auto count = static_cast<std::int32_t>(rows.size());
    if (list.rows <= count && count - list.scroll < list.rows)
        list.scroll = count - list.rows;
    // With one or more, the first chosen and the scroll at 0: the top one does not stay (Q-C887).
    SelectInTheList(list, rows.size(), 1);
    list.scroll = 0;
}

void ForgetTheWindowRow(OldWindowManager& manager, std::int32_t id)
{
    OldWindow* friends = Messenger::FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // 0x462c70, the vtable + 0x98: the first with the id; then the vtable + 0x34 with 0.
    OldWindow& tab = friends->tabs[Messenger::WindowsTabNumber];
    std::deque<WindowRow>& rows = tab.tab.windows;
    if (const auto found = std::ranges::find(rows, id, &WindowRow::id); found != rows.end())
        rows.erase(found);
    ScrollTheList(tab.tab.list, rows.size(), 0);
}

void FillTheWindowsTab(OldWindowManager& manager)
{
    OldWindow* friends = Messenger::FriendsWindowOf(manager);
    if (friends == nullptr)
        return;
    // The list's vtable + 0x2c (0x47d320): empties it, marks every row at 0 (+0x48, 0x47da60: without rows, nothing),
    // chooses 0 (+0x58, 0x47de10, like 0x47aef0) and puts the scroll (+0x100) back to 0.
    OldTabPart& tab = friends->tabs[Messenger::WindowsTabNumber].tab;
    tab.windows.clear();
    SelectInTheList(tab.list, 0, 0);
    tab.list.scroll = 0;
    for (const auto& [id, window] : manager.windows)
    {
        if (id == manager.friends || window.base.state == MinimizedWindow)
            continue;
        AddTheWindowRow(manager, id, window.frame.title.value_or(std::string()));
    }
}

void SaveTheFriendsPlace(OldWindowManager& manager, const OldWindow& window)
{
    manager.friendsX = window.base.x;
    manager.friendsY = window.base.y;
    manager.friendsWidth = window.base.width;
    manager.friendsHeight = window.base.height;
    manager.friendsStretched = window.frame.stretched;
    manager.friendsSavedY = window.frame.savedY;
    manager.friendsSavedHeight = window.frame.savedHeight;
    manager.lastTab = window.friends.currentTab;
}
} // namespace Client::Ui::Steps

namespace Client::Messenger
{

Core::Expected<bool, std::string> OpenTheFriendsWindow(Ui::OldWindowManager& manager, std::int32_t x, std::int32_t y,
                                                       const Ui::OldWindowGlobals& globals,
                                                       const Ui::OldWindowStepView& view)
{
    if (manager.friends != 0)
    {
        // 0x491318..0x49137f: hidden (3), shown with the sounds; shown (0), released (0x4914e0); with another state,
        // nothing. 0x45a9c0 reads the state of what 0x490a00 gave without checking for NULL.
        const Ui::OldWindow* friends = Ui::FindTheOldWindow(manager, manager.friends);
        if (friends == nullptr)
            return Core::Unexpected(std::string("0x49131f: 0x45a9c0 sobre la ventana de amigos que ya no está"));
        const std::int32_t state = friends->base.state;
        if (state == Ui::HiddenWindow)
        {
            Ui::Steps::ShowTheWindow(manager, manager.friends, true);
            // 0x41ed10(0x19) and 0x41ed10(0x1c): the last one stays.
            globals.RequestSound(ButtonSound);
            globals.RequestSound(OpenSound);
            return false;
        }
        if (state == Ui::ShownWindow)
        {
            if (auto dropped = Ui::DropTheFriendsWindow(manager, globals.pressedButton); !dropped.HasValue())
                return Core::Unexpected(std::move(dropped).Error());
        }
        return false;
    }
    if (view.texts == nullptr || view.measure == nullptr)
        return Core::Unexpected(std::string("0x491080: la ventana de amigos sin los textos ni la medida"));
    const std::string title = view.texts->At(static_cast<std::size_t>(manager.titleText));
    const bool first = manager.friendsWidth == 0;
    Ui::OldWindowRequest request{.kind = Ui::FriendsCase,
                                 .x = first ? x : manager.friendsX,
                                 .y = first ? y : manager.friendsY,
                                 .data = title,
                                 .aux = 0,
                                 .flags = 0};
    auto created = Ui::CreateTheOldWindow(manager, request, *view.texts, *view.measure, &globals, &view);
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    Ui::PostToTheManager(manager, Ui::RaiseMessage, manager.friends, 0);
    if (first)
    {
        Ui::Steps::FillTheWindowsTab(manager);
        globals.RequestSound(ButtonSound);
        globals.RequestSound(OpenSound);
        return true;
    }
    Ui::OldWindow* friends = Ui::FindTheOldWindow(manager, manager.friends);
    if (friends == nullptr)
        return false;
    // The vtable + 8 (0x45a9f0): the size from before; and the stretched one.
    friends->base.width = manager.friendsWidth;
    friends->base.height = manager.friendsHeight;
    friends->frame.savedY = manager.friendsSavedY;
    friends->frame.stretched = manager.friendsStretched;
    friends->frame.savedHeight = manager.friendsSavedHeight;
    Ui::Steps::FillTheWindowsTab(manager);
    if (auto settled = Ui::Steps::SettleTheWindow(manager, *friends, globals, view); !settled.HasValue())
        return Core::Unexpected(std::move(settled).Error());
    globals.RequestSound(ButtonSound);
    globals.RequestSound(OpenSound);
    if (auto stepped = Ui::StepTheOldWindows(manager, globals, view); !stepped.HasValue())
        return Core::Unexpected(std::move(stepped).Error());
    return true;
}

void RefreshTheFriendTab(Ui::OldWindowManager& manager)
{
    if (Ui::OldWindow* friends = FriendsWindowOf(manager); friends != nullptr)
        RefreshTheTab(manager, friends->tabs[FriendTabNumber]);
}

void MarkTheFriendList(Ui::OldWindowManager& manager, std::int32_t received, const Ui::OldWindowTexts& texts)
{
    manager.listReceived = received;
    const std::int32_t title =
        received == 1 ? static_cast<std::int32_t>(FriendsTitleText) : static_cast<std::int32_t>(ReconnectTitleText);
    if (manager.titleText == title)
        return;
    manager.titleText = title;
    if (Ui::OldWindow* friends = FriendsWindowOf(manager); friends != nullptr)
        friends->frame.title = texts.At(static_cast<std::size_t>(title));
}

void RememberTheFriendRequest(Ui::OldWindowManager& manager, std::int32_t box, std::string_view name)
{
    if (Ui::OldWindow* window = Ui::FindTheOldWindow(manager, box); window != nullptr)
        window->box.friendName = FriendName(name);
}

namespace
{
Ui::OldEdit* EditWithId(Ui::OldWindowManager& manager, std::int32_t id)
{
    for (auto& [windowId, window] : manager.windows)
    {
        if (window.kind == Ui::OldWindowKind::AddFriendBox && window.add.edit.base.id == id)
            return &window.add.edit;
        // CL9 milestone 10: the EDIT of the chat with a friend and the three of the letter-writing window.
        if (window.kind == Ui::OldWindowKind::ChatWindow && window.chat.edit.base.id == id)
            return &window.chat.edit;
        if (window.kind != Ui::OldWindowKind::LetterWriteWindow)
            continue;
        for (Ui::OldEdit* edit : {&window.letter.to, &window.letter.subject, &window.letter.text})
        {
            if (edit->base.id == id)
                return edit;
        }
    }
    return nullptr;
}

Ui::OldEdit* FocusedEdit(Ui::OldWindowManager& manager)
{
    if (manager.focusedEditIsOrphan)
        return manager.orphanedEdit ? &*manager.orphanedEdit : nullptr;
    return EditWithId(manager, manager.focusedEdit);
}
} // namespace

bool HasAnOldEditFocused(const Ui::OldWindowManager& manager, Ui::ChatFocus focus)
{
    if (focus != Ui::ChatFocus::OldWindow)
        return false;
    if (manager.focusedEditIsOrphan)
        return manager.orphanedEdit.has_value();
    for (const auto& [id, window] : manager.windows)
    {
        if (window.kind == Ui::OldWindowKind::AddFriendBox && window.add.edit.base.id == manager.focusedEdit)
            return true;
        // CL9 milestone 10: the EDIT of the chat with a friend and the three of the letter-writing window.
        if (window.kind == Ui::OldWindowKind::ChatWindow && window.chat.edit.base.id == manager.focusedEdit)
            return true;
        if (window.kind == Ui::OldWindowKind::LetterWriteWindow &&
            (window.letter.to.base.id == manager.focusedEdit || window.letter.subject.base.id == manager.focusedEdit ||
             window.letter.text.base.id == manager.focusedEdit))
            return true;
    }
    return false;
}

void TypeInTheOldEdit(Ui::OldWindowManager& manager, std::string_view typed)
{
    if (Ui::OldEdit* edit = FocusedEdit(manager); edit != nullptr)
    {
        edit->compositionText.clear();
        edit->compositionStart = 0;
        edit->compositionLength = 0;
        Ui::TypeInTheEdit(*edit, typed);
    }
}

void BackspaceInTheOldEdit(Ui::OldWindowManager& manager)
{
    if (Ui::OldEdit* edit = FocusedEdit(manager); edit != nullptr)
    {
        edit->draws = 0;
        if (!edit->compositionText.empty())
            return;
        edit->compositionText.clear();
        edit->compositionStart = 0;
        edit->compositionLength = 0;
        Ui::BackspaceInTheEdit(*edit);
    }
}

void ApplyKeyToTheOldEdit(Ui::OldWindowManager& manager, Ui::OldEditKey key, bool shift, bool ctrl,
                          Platform::IClipboard* clipboard, const Render::Text::ITextMeasure* measure,
                          std::int32_t* focusedList)
{
    Ui::OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    Ui::ApplyKeyToTheEdit(*edit, key, shift, ctrl, clipboard, measure,
                          [&manager, focusedList](std::int32_t id)
                          {
                              if (Ui::OldEdit* next = EditWithId(manager, id); next != nullptr)
                              {
                                  manager.orphanedEdit.reset();
                                  manager.focusedEditIsOrphan = false;
                                  manager.focusedEdit = next->base.id;
                                  if (focusedList != nullptr)
                                      *focusedList = next->base.id;
                                  next->text.SetCursor(next->text.Size());
                                  next->draws = 0;
                              }
                          });
}

void SetTheOldEditComposition(Ui::OldWindowManager& manager, std::string_view text, std::int32_t start,
                              std::int32_t length)
{
    Ui::OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    Ui::SetTheEditComposition(*edit, text, start, length);
}

void PressEnterInTheOldEdit(Ui::OldWindowManager& manager)
{
    Ui::OldEdit* edit = FocusedEdit(manager);
    if (edit == nullptr)
        return;
    edit->draws = 0;
    if (!edit->compositionText.empty())
        return;
    // 0x467b10: with +0x150 of the window list at 1 (F5, which also reads it), with the vtable + 0x5c at 1 (+0xb4, the
    // chat with a friend closed, 0x467c83) or with +0xc4 at 1 (0x467c8f), Enter goes to the Windows EDIT: the multiline
    // one breaks the line and the single-line one does nothing. Otherwise, the 7 to the parent.
    if (manager.windowList.list.armed == 1 || edit->closed == 1 || edit->multiline == 1)
    {
        Ui::BreakTheEditLine(*edit);
        return;
    }
    if (manager.focusedEditIsOrphan)
        return;
    Ui::PostToTheWindow(manager, edit->base.parent, Ui::EnterMessage, 0, 0);
}

Core::Expected<void, std::string> SettleTheFriendsForTheDraw(Ui::OldWindowManager& manager, Ui::OldWindow& window,
                                                             const Ui::OldWindowDrawView& view)
{
    const bool moving = window.base.state == Ui::DraggedWindow || window.base.state == Ui::ResizedWindow;
    if (window.kind == Ui::OldWindowKind::AddFriendBox)
    {
        // 0x4a3750.
        if (!moving)
            return {};
        for (Ui::OldButton& button : window.add.buttons)
            Ui::Steps::PostAndSettle(manager, button, Ui::PlaceMessage);
        PostToTheEdit(manager, window.add.edit, Ui::PlaceMessage);
        return {};
    }
    if (window.kind != Ui::OldWindowKind::FriendsWindow)
        return {};
    constexpr std::array<std::size_t, FriendsTabCount> Order{FriendTabNumber, WindowsTabNumber, LettersTabNumber};
    // 0x4a2620: the 8 to the three and their state at 4; while resizing, also the 9 and the state at 1.
    if (moving)
    {
        for (const std::size_t at : Order)
        {
            if (auto placed = PlaceTheTabForTheDraw(manager, window.tabs[at], Ui::PlaceMessage, view);
                !placed.HasValue())
                return placed;
        }
        for (const std::size_t at : Order)
            window.tabs[at].base.state = Ui::DraggedWindow;
        if (window.base.state == Ui::ResizedWindow)
        {
            for (const std::size_t at : Order)
            {
                if (auto placed = PlaceTheTabForTheDraw(manager, window.tabs[at], Ui::SizeMessage, view);
                    !placed.HasValue())
                    return placed;
            }
            for (const std::size_t at : Order)
                window.tabs[at].base.state = Ui::ResizedWindow;
        }
    }
    // The drawing of the tab on view (0x49cc20, 0x4a0a30 or 0x49e7f0): dragged or resizing, the 8 to the list and the
    // buttons and, resizing, the 9 to the list.
    const std::int32_t current = window.friends.currentTab;
    if (current < 0 || current >= static_cast<std::int32_t>(FriendsTabCount))
        return {};
    Ui::OldWindow& tab = window.tabs[static_cast<std::size_t>(current)];
    if (tab.base.state != Ui::DraggedWindow && tab.base.state != Ui::ResizedWindow)
        return {};
    PostToTheList(manager, tab, Ui::PlaceMessage);
    for (Ui::OldButton& button : tab.tab.buttons)
        Ui::Steps::PostAndSettle(manager, button, Ui::PlaceMessage);
    if (tab.base.state == Ui::ResizedWindow)
        PostToTheList(manager, tab, Ui::SizeMessage);
    return {};
}
} // namespace Client::Messenger
