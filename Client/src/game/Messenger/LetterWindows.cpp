#include "Messenger/LetterWindows.h"

#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatLog.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldControls.h"
#include "Ui/OldEditLines.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Crt/SprintfFormat.h"
#include "Protocol/Movement/Movement.h"
#include "Ui/TextField.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The vtable of the windows of cases 1, 2, 6 and 7 of 0x48ee60 (CL9 milestone 10): the chat with a friend
// (0x6b9dc8), reading a letter (0x6b9c58) and writing it (0x6b9bfc); the letters tab (0x4a11b0) and buttons 3 and 4
// of the friends tab (0x49d0b0). The drawings (0x493fc0, 0x499d60, 0x498480 and F5's rows, 0x4a5750) are in
// Messenger/LetterWindowsDraw, and the letters tab's (0x4a0a30), in Messenger/FriendWindowsDraw; the portrait's
// (0x497c10) is in Messenger/LetterPortrait (P-C212).
namespace Client::Messenger
{
namespace
{
using Ui::OldDraw::RatioX;
using Ui::OldDraw::Truncate;

// The queue's messages (06 § Las ventanas viejas).
constexpr std::int32_t RaisedType = 6;
constexpr std::int32_t EnterType = 7;
constexpr std::int32_t PlaceType = 8;
constexpr std::int32_t SizeType = 9;
constexpr std::int32_t CommandType = 10;
constexpr std::int32_t AnswerType = 12;
constexpr std::int32_t ChosenRowType = 13;
constexpr std::int32_t ToTheEndType = 14;
// 0x45a9b0's states.
constexpr std::int32_t HiddenState = 3;
// 0x41ed10.
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;

// Text.bmd.
constexpr std::size_t InviteButtonText = 993;
constexpr std::size_t HideInvitesText = 996;
constexpr std::size_t InviteFullText = 1074;
constexpr std::size_t FilteredText = 570;
constexpr std::size_t ToText = 1000;
constexpr std::array<std::size_t, 4> WriteButtonTexts{1001, 1002, 1003, 1004};
constexpr std::array<std::int32_t, 4> WriteButtonCommands{1, 2, 4, 5};
constexpr std::array<std::int32_t, 4> WriteButtonX{0xc, 0x3f, 0xfa, 0x12d};
constexpr std::size_t NoNameText = 1006;
constexpr std::size_t NoSubjectText = 1007;
constexpr std::size_t NoBodyText = 1008;
constexpr std::size_t DiscardText = 1009;
constexpr std::array<std::size_t, 5> ReadButtonTexts{1010, 1011, 1002, 1012, 1013};
constexpr std::array<std::int32_t, 5> ReadButtonCommands{1, 2, 3, 5, 6};
constexpr std::array<std::int32_t, 5> ReadButtonX{2, 0x35, 0xba, 0x68, 0x91};
constexpr std::array<std::int32_t, 5> ReadButtonWidth{0x32, 0x32, 0x32, 0x28, 0x28};
constexpr std::size_t ReplyText = 1016;
constexpr std::size_t ReallyDeleteLettersText = 1017;
constexpr std::size_t NoneCheckedText = 1031;
constexpr std::size_t WriteTitleText = 1071;
// 0x49d0b0 and 0x4a11b0: sprintf(1071, 1000).
constexpr std::int32_t LetterCost = 1000;

// What 0x493220 (the chat), 0x4999d0 (read) and 0x497f50 (write) set.
constexpr std::int32_t WindowPlace = 0x32;
constexpr std::int32_t ChatWidth = 0xfa;
constexpr std::int32_t ChatHeight = 0xaa;
constexpr std::int32_t ChatMinHeight = 0x96;
constexpr std::int32_t ChatEditFilter = 4;
constexpr std::size_t ChatEditLimit = 100;
constexpr std::int32_t ChatInvitesWidth = 0x14a;
// 0x494d1a: a growing chat does not pass 640's right edge.
constexpr std::int32_t ScreenWidth = 0x280;
// +0x4c of the letter windows: 0x1b (without the resize corner or the stretch button).
constexpr std::int32_t LetterFlags = 0x1b;
constexpr std::int32_t ReadHeight = 0xb6;
constexpr std::int32_t WriteHeight = 0xd8;
constexpr std::int32_t LetterMinWidth = 0x172;
constexpr std::int32_t PortraitWidth = 0x78;
constexpr std::int32_t ReadPortraitMode = 2;
constexpr std::int32_t WriteMinHeight = 0x96;
constexpr std::size_t ToLimit = 10;
constexpr std::size_t SubjectLimit = 0x20;
constexpr std::size_t BodyLimit = 1000;
// 0x497f50: action 0x7a and angle 90 (0x42b40000).
constexpr std::int32_t WritePortraitAction = Protocol::Movement::FacingAction;
constexpr float WritePortraitAngle = 90.0f;
// The writing window's 4 and its 5: the portrait's action, one back and one forward (0x497910).
constexpr std::int32_t PreviousAction = -1;
constexpr std::int32_t NextAction = 1;
// 0x4a2070: the height of the letter list's headers and the width of the all mark.
constexpr std::int32_t HeaderHeight = 0x13;
constexpr std::int32_t LetterMarkColumn = 0xa;
// 0x460130 and 0x465450: under 20 bytes, the whole line; otherwise, split at the list's width minus 0x1e, into up to
// 10 (the chat, with the name's indent plus 5) or 0x50 (a letter).
constexpr std::size_t ShortLine = 0x14;
constexpr std::int32_t WrapInset = 0x1e;
constexpr std::int32_t NameGap = 5;
constexpr std::int32_t ChatWrapLines = 10;
constexpr std::int32_t LetterWrapLines = 0x50;
// 0x464ae0: strncpy 0x3e9 of each line of a letter.
constexpr std::size_t LetterLineBytes = 0x3e9;
// 0x461600: up to 3 names and then "...".
constexpr std::int32_t TitleNames = 3;
constexpr std::string_view TitleSeparator = ", ";
constexpr std::string_view TitleMore = "...";

Core::Expected<void, std::string> Cut(std::string what)
{
    return Core::Unexpected(std::move(what));
}

std::string TextOf(const Ui::OldWindowStepView& view, std::size_t index)
{
    return view.texts != nullptr ? view.texts->At(index) : std::string();
}

Core::Expected<void, std::string> Send(const Ui::OldWindowStepView& view, Ui::FriendSend packet, std::string_view where)
{
    if (!view.send)
        return Core::Unexpected(std::string(where) + ": el paquete sin el cable");
    view.send(packet);
    return {};
}

Core::Expected<void, std::string> CallThePortrait(const Ui::OldWindowStepView* view, Ui::OldPortraitPart& portrait,
                                                  Ui::PortraitCallKind call, std::string_view where,
                                                  std::int32_t delta = 0)
{
    if (view == nullptr || !view->portrait)
        return Cut(std::string(where) + ": el personaje del retrato de las cartas (vtable 0x6ba008) es de Scenes "
                                        "(P-C212)");
    return view->portrait(portrait, call, delta);
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

// 0x45aa50 over an EDIT or a list: the message, and its vtable + 0x28 (0x45a640 or 0x47a9d0) and 0x45abc0.
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

// 0x45aa50 over the portrait: its vtable + 0x28 (0x45a640) returns 0 and 0x45abc0 remains; the + 0x1c (0x403160)
// does nothing.
void PostToTheBase(Ui::OldWindowManager& manager, Ui::OldWindowBase& base, std::int32_t type)
{
    base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        Ui::Steps::PlaceAgainstTheParent(manager, base);
    }
}

// +0x48 at 2 (0x497aa0): the portrait being turned with the mouse.
constexpr std::int32_t TurnedPortrait = 2;
// 0x497a00: the help icon, 16 × 16 at (x + 1; y + height − 0x11).
constexpr std::int32_t HelpIconSide = 0x10;
constexpr std::int32_t HelpIconBottom = 0x11;
// 0x6b89e4, 0x6b8830 and 0x6b8a38: the wheel times 0.02 and the zoom between 0.8 and 1.1 (0x3f4ccccd and 0x3f8ccccd).
constexpr float WheelZoomStep = 0.02f;
constexpr float NearestZoom = 1.1f;
constexpr float FarthestZoom = 0.8f;

// 0x497a71..0x497b4b: the click, the right click or the wheel over the portrait.
void HandleThePortraitPointer(Ui::OldPortraitPart& portrait, const Ui::OldWindowGlobals& globals,
                              const Ui::OldWindowStepView& view)
{
    Ui::OldWindowBase& base = portrait.base;
    if (globals.leftClick)
    {
        portrait.help = 0;
        if (base.state != Ui::ShownWindow || globals.capture != 0)
            return;
        // 0x497a9b..0x497ac0: grabs it, sets it turning (the vtable + 4 with 2), stores X and SetCapture (MuRet has no
        // Windows window).
        globals.capture = base.id;
        base.state = TurnedPortrait;
        portrait.dragX = static_cast<float>(view.mouseX);
        return;
    }
    if (globals.rightClick)
    {
        portrait.help = 0;
        portrait.angle = portrait.baseAngle;
        portrait.zoom = portrait.baseZoom;
        return;
    }
    if (globals.wheel == 0)
        return;
    portrait.help = 0;
    // 0x497b01..0x497b41: the computation in the x87, stored as float; the limits are compared with the one on the x87
    // stack.
    const double zoom =
        static_cast<double>(globals.wheel) * static_cast<double>(WheelZoomStep) + static_cast<double>(portrait.zoom);
    portrait.zoom = static_cast<float>(zoom);
    if (zoom > static_cast<double>(NearestZoom))
        portrait.zoom = NearestZoom;
    else if (zoom < static_cast<double>(FarthestZoom))
        portrait.zoom = FarthestZoom;
    globals.wheel = 0;
}

// 0x497970 (the portrait's vtable + 0x20) after the queue and 0x45aa80: always returns 1.
Core::Expected<void, std::string> StepThePortraitMouse(Ui::OldPortraitPart& portrait,
                                                       const Ui::OldWindowGlobals& globals,
                                                       const Ui::OldWindowStepView& view, std::string_view where)
{
    Ui::OldWindowBase& base = portrait.base;
    // 0x497973..0x4979a0: blank, only the mouse over the UI.
    if (portrait.blank == 1)
    {
        if (Ui::Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
            globals.mouseOnUi = 1;
        return {};
    }
    // 0x4979a7..0x4979bd: following the hero, with 0x78abaac at 1, the vtable + 0x3c.
    if (portrait.follow == 1 && view.field78abaac == 1)
    {
        if (auto copied = CallThePortrait(&view, portrait, Ui::PortraitCallKind::CopyTheHero, where);
            !copied.HasValue())
            return copied;
    }
    // 0x4979c0..0x4979cf.
    if (auto stepped = CallThePortrait(&view, portrait, Ui::PortraitCallKind::Step, where); !stepped.HasValue())
        return stepped;
    if ((base.flags & 1) == 0)
    {
        // 0x497bbe..0x497bfc.
        if (base.state == Ui::ShownWindow && Ui::Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
        {
            globals.mouseOnUi = 1;
            if (globals.leftClick)
            {
                globals.leftClick = false;
                globals.leftHeld = false;
            }
        }
        return {};
    }
    // 0x4979df..0x497a44: on view, the help icon turns it on or off with the click.
    const bool onTheIcon =
        base.state == Ui::ShownWindow &&
        Ui::Steps::MouseIn(view, base.x + 1, base.y + base.height - HelpIconBottom, HelpIconSide, HelpIconSide, 1);
    if (onTheIcon)
    {
        globals.mouseOnUi = 1;
        if (globals.leftClick)
        {
            portrait.help = (portrait.help + 1) % 2;
            globals.leftClick = false;
            globals.leftHeld = false;
        }
    }
    else if (Ui::Steps::MouseIn(view, base.x, base.y, base.width, base.height, 1))
    {
        // 0x497a49..0x497b4b.
        globals.mouseOnUi = 1;
        HandleThePortraitPointer(portrait, globals, view);
    }
    // 0x497b51..0x497bb6: while turning, the click turns it by what the mouse moved; without a click, it lets go.
    if (base.state != TurnedPortrait)
        return {};
    if (globals.leftClick)
    {
        globals.mouseOnUi = 1;
        const double mouseX = static_cast<double>(view.mouseX);
        portrait.angle =
            static_cast<float>(mouseX - static_cast<double>(portrait.dragX) + static_cast<double>(portrait.angle));
        portrait.dragX = static_cast<float>(view.mouseX);
        return {};
    }
    base.state = Ui::ShownWindow;
    if (globals.capture == base.id)
        globals.capture = 0;
    return {};
}

// 0x45aa80 over the portrait (the vtable + 0x18): the queue (0x45a640 returns 0 and 0x45abc0 handles it), the
// vtable + 0x1c (0x403160, nothing), the mouse and the vtable + 0x20 (0x497970).
Core::Expected<void, std::string> StepThePortrait(Ui::OldWindowManager& manager, Ui::OldPortraitPart& portrait,
                                                  std::int32_t arg, const Ui::OldWindowGlobals& globals,
                                                  const Ui::OldWindowStepView& view, std::string_view where)
{
    Ui::OldWindowBase& base = portrait.base;
    while (!base.queue.empty())
    {
        base.current = base.queue.front();
        base.queue.pop_front();
        Ui::Steps::PlaceAgainstTheParent(manager, base);
    }
    if (arg == 1)
        return {};
    if (!Ui::Steps::ClaimTheMouse(manager, base, globals, view))
        return {};
    return StepThePortraitMouse(portrait, globals, view, where);
}

void DrainTheList(Ui::OldWindowManager& manager, Ui::OldList& list, std::size_t size)
{
    while (!list.base.queue.empty())
    {
        list.base.current = list.base.queue.front();
        list.base.queue.pop_front();
        Ui::HandleTheListMessage(list, size);
        Ui::Steps::PlaceAgainstTheParent(manager, list.base);
    }
}

void PostToTheList(Ui::OldWindowManager& manager, Ui::OldList& list, std::size_t size, std::int32_t type)
{
    list.base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    DrainTheList(manager, list, size);
}

// strncpy(dest, text, n) over a zeroed buffer: up to the first '\0' and at most n bytes.
std::string Bounded(std::string_view text, std::size_t bytes)
{
    return std::string(text.substr(0, std::min(text.find('\0'), bytes)));
}

// 0x460130, the vtable + 0x98 of the chat's lines (0x45fea0 passes it with the name at +4 and the text at +0xf of each
// line): with under 20 bytes between the name and the text (unsigned, 0x46019a), the whole line at the front of
// `wrappedRows`; otherwise, the text split into up to 10 lines of the list's width minus 0x1e, the first with indent
// ftol((name cx + 5) / rx) (GetTextExtentPointA of the name alone, without ": ", over the DC); the first, with the
// name, and the others without it (0x460318 clears their first byte) go to the front if not empty. With too much text
// (0x459c70 returns 11), 0x460308 reads line 11 outside its ten buffers, from EBP − 0x14 (the this): MuRet discards it
// (Q-C1012).
Core::Expected<void, std::string> WrapAChatLine(Ui::OldChatPart& chat, const Ui::ChatLine& line,
                                                const Ui::OldWindowMeasure* measure)
{
    std::deque<Ui::ChatLine>& rows = chat.wrappedRows;
    if (line.name.size() + line.text.size() < ShortLine)
    {
        rows.push_front(line);
        return {};
    }
    if (measure == nullptr)
        return Cut("0x4601a8: partir un renglón del chat sin la medida");
    const Render::Text::TextExtent name = measure->measure.Measure(measure->dcFont, line.name);
    // FILD of cx + 5, FDIV of the float and _ftol.
    const std::int32_t indent = Truncate(static_cast<double>(name.width + NameGap) / RatioX(measure->screen));
    auto wrapped = Chat::WrapByWidth(line.text, measure->measure, measure->dcFont, measure->screen,
                                     chat.lines.base.width - WrapInset, ChatWrapLines, indent);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    const Chat::WrappedText& pieces = wrapped.Value();
    const std::int32_t used = std::min(pieces.result, ChatWrapLines);
    Ui::ChatLine piece = line;
    if (!pieces.lines[0].empty())
    {
        piece.text = Bounded(pieces.lines[0], Chat::ChatTextBytes);
        rows.push_front(piece);
    }
    piece.name.clear();
    for (std::int32_t at = 1; at < used; ++at)
    {
        const std::string& text = pieces.lines[static_cast<std::size_t>(at)];
        if (text.empty())
            continue;
        piece.text = Bounded(text, Chat::ChatTextBytes);
        rows.push_front(piece);
    }
    return {};
}

// 0x465450, the vtable + 0x98 of a letter's body (from 0x465270 with the text at +4): under 20 bytes (0x465475), the
// whole line at the front; otherwise, split into up to 0x50 of the list's width minus 0x1e, without indent, in the
// global buffer 0x575de34, and each non-empty one at the front. With too much text (0x51), the original reads the
// extra one past the buffer (0x5762e34): MuRet discards it (Q-C1012).
Core::Expected<void, std::string> WrapALetterLine(Ui::OldLetterPart& letter, const std::string& line,
                                                  const Ui::OldWindowMeasure* measure)
{
    std::deque<std::string>& rows = letter.wrappedRows;
    if (line.size() < ShortLine)
    {
        rows.push_front(Bounded(line, Chat::ChatTextBytes));
        return {};
    }
    if (measure == nullptr)
        return Cut("0x465494: partir un renglón de una carta sin la medida");
    auto wrapped = Chat::WrapByWidth(line, measure->measure, measure->dcFont, measure->screen,
                                     letter.lines.base.width - WrapInset, LetterWrapLines, 0);
    if (!wrapped.HasValue())
        return Core::Unexpected(std::move(wrapped).Error());
    const Chat::WrappedText& pieces = wrapped.Value();
    const std::int32_t used = std::min(pieces.result, LetterWrapLines);
    for (std::int32_t at = 0; at < used; ++at)
    {
        const std::string& text = pieces.lines[static_cast<std::size_t>(at)];
        if (!text.empty())
            rows.push_front(Bounded(text, Chat::ChatTextBytes));
    }
    return {};
}

// 0x45fea0 and 0x465270, the vtable + 0x90 of the chat's lines and a letter's body: 0x45abc0's 9 against the parent
// (without checking the message), empties the second deque (0x472db0 and 0x473ac0) and passes each line of the first,
// back to front, to the vtable + 0x98 (from [+0x90] to [+0x80] in the original: oldest to newest, which in MuRet is
// `lineRows` front to back). +0xf8 is the second deque's size.
Core::Expected<void, std::string> RewrapTheLines(Ui::OldWindowManager& manager, Ui::OldChatPart& chat,
                                                 const Ui::OldWindowMeasure* measure)
{
    Ui::Steps::SizeAgainstTheParent(manager, chat.lines.base);
    chat.wrappedRows.clear();
    Core::Expected<void, std::string> done;
    for (const Ui::ChatLine& line : chat.lineRows)
    {
        done = WrapAChatLine(chat, line, measure);
        if (!done.HasValue())
            break;
    }
    chat.lines.fixedCountValue = static_cast<std::int32_t>(chat.wrappedRows.size());
    return done;
}

Core::Expected<void, std::string> RewrapTheLines(Ui::OldWindowManager& manager, Ui::OldLetterPart& letter,
                                                 const Ui::OldWindowMeasure* measure)
{
    Ui::Steps::SizeAgainstTheParent(manager, letter.lines.base);
    letter.wrappedRows.clear();
    Core::Expected<void, std::string> done;
    for (const std::string& line : letter.lineRows)
    {
        done = WrapALetterLine(letter, line, measure);
        if (!done.HasValue())
            break;
    }
    letter.lines.fixedCountValue = static_cast<std::int32_t>(letter.wrappedRows.size());
    return done;
}

// The queue of the chat's lines (0x479240) and a letter's body (0x47c150): like DrainTheList, but the 9 runs the
// vtable + 0x90 first, so the lines are counted with the height already measured. Both read +0xa8 (the first deque's
// size, `lineRows`) and the count, +0xf8.
template <typename Part>
Core::Expected<void, std::string> DrainTheWrappedList(Ui::OldWindowManager& manager, Part& part,
                                                      const Ui::OldWindowMeasure* measure)
{
    Ui::OldList& list = part.lines;
    while (!list.base.queue.empty())
    {
        list.base.current = list.base.queue.front();
        list.base.queue.pop_front();
        if (list.base.current.type == SizeType)
        {
            if (auto rewrapped = RewrapTheLines(manager, part, measure); !rewrapped.HasValue())
                return rewrapped;
        }
        Ui::HandleTheListMessage(list, part.lineRows.size());
        Ui::Steps::PlaceAgainstTheParent(manager, list.base);
    }
    return {};
}

template <typename Part>
Core::Expected<void, std::string> PostToTheWrappedList(Ui::OldWindowManager& manager, Part& part, std::int32_t type,
                                                       const Ui::OldWindowMeasure* measure)
{
    part.lines.base.queue.push_back(Ui::OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
    return DrainTheWrappedList(manager, part, measure);
}

// 0x45aa80's mouse over a list of one of these windows, after its queue, with its row (the vtable + 0x88).
void StepTheListMouseAfterTheQueue(Ui::OldWindowManager& manager, Ui::OldList& list, std::size_t size, std::int32_t arg,
                                   const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view,
                                   const Ui::OldListRowHover& hover)
{
    if (arg == 1)
        return;
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
    static_cast<void>(Ui::StepTheListMouse(list, size, listGlobals, listView, hover));
}

// 0x45aa80 over the chat's members list or invite list (0x460970): the queue and the mouse, with 0x461f00's row (the
// click chooses, the double click is the 0xd to the chat and the right one, the whisper).
void StepAFriendList(Ui::OldWindowManager& manager, Ui::OldList& list, std::deque<FriendEntry>& friends,
                     std::int32_t arg, const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view)
{
    DrainTheList(manager, list, friends.size());
    StepTheListMouseAfterTheQueue(manager, list, friends.size(), arg, globals, view,
                                  [&](std::int32_t row, std::size_t at) {
                                      return Ui::Steps::HoverAFriendRow(manager, list, friends, row, at, globals, view);
                                  });
}

// 0x45aa80 over the chat's lines or a letter's body: its queue (DrainTheWrappedList) and the mouse, which goes over the
// visible lines of the second deque.
template <typename Part>
Core::Expected<void, std::string> StepTheWrappedList(Ui::OldWindowManager& manager, Part& part, std::int32_t arg,
                                                     const Ui::OldWindowGlobals& globals,
                                                     const Ui::OldWindowStepView& view)
{
    if (auto drained = DrainTheWrappedList(manager, part, view.measure); !drained.HasValue())
        return drained;
    // 0x45f3e0 and 0x4647d0, the vtable + 0x88 of the chat's lines and the body: they return 1 without checking the
    // mouse.
    StepTheListMouseAfterTheQueue(manager, part.lines, part.wrappedRows.size(), arg, globals, view,
                                  [](std::int32_t, std::size_t) { return 1; });
    return {};
}

Ui::OldList MakeTheList(Ui::OldWindowManager& manager)
{
    Ui::OldList list;
    list.base = Ui::Steps::MakeTheBase(manager);
    list.base.rectMode = 2;
    return list;
}

// 0x45f240 and 0x464620: what they change from the base list (0x45e2e0): no arrows (+0xbc at 0), the count at +0xf8
// (+0xc8 at 1) and, a letter body's, the bottom kept in view when the lines change (+0x128 at 1).
Ui::OldList MakeTheWrappedList(Ui::OldWindowManager& manager, std::int32_t keepBottom)
{
    Ui::OldList list = MakeTheList(manager);
    list.keyboard = 0;
    list.fixedCount = 1;
    list.keepBottom = keepBottom;
    return list;
}

// The chat's EDIT and the writing window's three: the background at 0 (the vtable + 0x44 with four 0s, at 0x4932e5,
// 0x498032, 0x4980b2 and 0x49814d), transparent like the add-a-friend one, and XP's CP1252 like the other EDITs.
// 0x459e30(text, cut, NULL, 0x20), at 0x49a6d0 and 0x4a1cd4: copies one _mbclen character at a time (0x6a01fa: one
// byte at a time with the CRT's code page 1252, 12-codificacion) while the cut does not pass 0x20 bytes, up to the
// first 0.
std::string CutTheSubject(std::string_view text)
{
    const std::size_t end = std::min(text.find('\0'), text.size());
    return std::string(text.substr(0, std::min(end, SubjectLimit)));
}

Ui::OldEdit MakeTheEdit(Ui::OldWindowManager& manager, std::size_t limit)
{
    Ui::OldEdit edit;
    edit.base = Ui::Steps::MakeTheBase(manager);
    edit.text = Ui::TextField(limit, false, Ui::TextField::Charset::Windows1252Xp);
    edit.backgroundColor = 0;
    return edit;
}

void PlaceTheBase(Ui::OldWindowBase& base, std::int32_t parent, std::int32_t mode, std::int32_t x, std::int32_t y)
{
    base.parent = parent;
    base.placeMode = mode;
    base.placeX = x;
    base.placeY = y;
}

void SizeTheBase(Ui::OldWindowBase& base, std::int32_t mode, std::int32_t x, std::int32_t y)
{
    base.sizeMode = mode;
    base.sizeX = x;
    base.sizeY = y;
}

void SetUpTheButton(Ui::OldButton& button, std::int32_t parent, std::int32_t command, std::string label,
                    std::int32_t mode, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    button.command = command;
    button.label = std::move(label);
    PlaceTheBase(button.base, parent, mode, x, y);
    button.base.width = width;
    button.base.height = height;
}

// 0x461600: "<994>" and, back to front, the members who are not the hero separated with ", "; at the third, "...",
// even if there are no more (Q-C1004).
std::string ChatTitle(const Ui::OldWindow& chat, const Ui::OldWindowTexts& texts, std::string_view hero)
{
    std::string title = texts.At(ChatTitleText);
    std::int32_t names = 0;
    for (auto it = chat.chat.members.rbegin(); it != chat.chat.members.rend(); ++it)
    {
        if (SameFriendName(it->name, hero))
            continue;
        if (names > 0)
            title += TitleSeparator;
        title += FriendName(it->name);
        ++names;
        if (names >= TitleNames)
        {
            title += TitleMore;
            break;
        }
    }
    return title;
}

// The vtable + 0x10 of the lines list (+0x2a0): wide with under 3 members, or narrower (for the members list) with 3
// or more; with the invite list on view, 0xa0 less (0x493c90, 0x493e90 and 0x4944c0).
void SizeTheLines(Ui::OldWindow& chat, bool withMembers)
{
    const std::int32_t cut = chat.chat.mode < 2 ? (withMembers ? -0x50 : 0) : -0xa0;
    SizeTheBase(chat.chat.lines.base, 3, cut, -0x10);
}

const LetterEntry* SelectedLetter(Ui::OldWindow& tab)
{
    const std::optional<std::size_t> at = Ui::SelectedInTheList(tab.tab.list, tab.tab.letters.size());
    if (!at || *at >= tab.tab.letters.size())
        return nullptr;
    return &tab.tab.letters[*at];
}

// 0x48ee60(7, 100, 100, sprintf(1071, 1000), 0, 0).
Core::Expected<std::int32_t, std::string> OpenTheWriteWindow(Ui::OldWindowManager& manager,
                                                             const Ui::OldWindowGlobals& globals,
                                                             const Ui::OldWindowStepView& view, std::string_view where)
{
    const std::array<Crt::SprintfArgument, 1> cost{Crt::SprintfArgument(LetterCost)};
    const std::string title = Crt::FormatLikeSprintf(TextOf(view, WriteTitleText), cost);
    constexpr std::int32_t WritePlace = 100;
    return CreateFromAStep(
        manager,
        Ui::OldWindowRequest{
            .kind = Ui::LetterWriteCase, .x = WritePlace, .y = WritePlace, .data = title, .aux = 0, .flags = 0},
        globals, view, where);
}

// 0x4a177a: with the window built, the 8 and the 9 with 0x45aa50.
Core::Expected<void, std::string> PlaceTheNewWindow(Ui::OldWindowManager& manager, std::int32_t id,
                                                    const Ui::OldWindowGlobals& globals,
                                                    const Ui::OldWindowStepView& view)
{
    Ui::OldWindow* window = Ui::FindTheOldWindow(manager, id);
    if (window == nullptr)
        return {};
    if (auto placed = Ui::Steps::PostAndStep(manager, *window, PlaceType, globals, view); !placed.HasValue())
        return placed;
    return Ui::Steps::PostAndStep(manager, *window, SizeType, globals, view);
}

// 0x4983e0 and 0x498420: the name (or the subject) to the EDIT, the focus to the next one and +0x77c.
void PutTheName(Ui::OldWindowManager& manager, Ui::OldWindow& write, std::string_view name,
                const Ui::OldWindowGlobals& globals)
{
    write.letter.to.text.SetText(name);
    Ui::Steps::FocusTheEdit(manager, write.letter.subject, globals);
    write.letter.prepared = 1;
}

void PutTheSubject(Ui::OldWindowManager& manager, Ui::OldWindow& write, std::string_view subject,
                   const Ui::OldWindowGlobals& globals)
{
    write.letter.subject.text.SetText(subject);
    Ui::Steps::FocusTheEdit(manager, write.letter.text, globals);
    write.letter.prepared = 2;
}

// 0x498820 with the 10 and 1 (0x4988b2..0x499590): the three EDITs, the filter, the body with "\r\n" turned into
// "\n", and the C5.
Core::Expected<void, std::string> SendTheLetter(Ui::OldWindowManager& manager, Ui::OldWindow& write,
                                                const Ui::OldWindowGlobals& globals, const Ui::OldWindowStepView& view)
{
    Ui::OldLetterPart& letter = write.letter;
    if (letter.sending != 0)
        return {};
    // The vtable + 0x50 (GetWindowText) with 0xb, 0x21 and 0x3e9 (0x49893f): up to 10, 0x20 and 1000 bytes.
    std::string to(letter.to.text.Value().substr(0, ToLimit));
    std::string subject(letter.subject.text.Value().substr(0, SubjectLimit));
    std::string body(letter.text.text.Value().substr(0, BodyLimit));
    // 0x498948 and 0x498982: 0x622b40 with 0 over the subject and the body; if it trips, text 570.
    if (view.badWord && view.badWord(subject))
        subject = TextOf(view, FilteredText);
    if (view.badWord && view.badWord(body))
        body = TextOf(view, FilteredText);
    // 0x4989b6..0x498a10: without a name, subject or body, the modal box (8) and the focus to that EDIT.
    const auto missing = [&](std::size_t text, Ui::OldEdit& edit,
                             std::int32_t field) -> Core::Expected<void, std::string>
    {
        auto created = CreateFromAStep(
            manager,
            Ui::OldWindowRequest{
                .kind = Ui::ModalNoticeCase, .x = -1, .y = -1, .data = TextOf(view, text), .aux = 0, .flags = 0},
            globals, view, "0x498820(1)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        Ui::Steps::FocusTheEdit(manager, edit, globals);
        letter.prepared = field;
        return {};
    };
    if (to.empty())
        return missing(NoNameText, letter.to, 0);
    if (subject.empty())
        return missing(NoSubjectText, letter.subject, 1);
    if (body.empty())
        return missing(NoBodyText, letter.text, 2);
    // 0x498a30..0x498a99: '\r' goes out as '\n' and eats the next one; "\r\n\r\n" leaves a space in between (Q-C1003).
    // It cuts at more than 1000 on either side and copies up to the '\0' inclusive.
    std::string converted;
    std::size_t from = 0;
    std::size_t into = 0;
    while (into <= BodyLimit && from <= BodyLimit && from <= body.size())
    {
        const char at = from < body.size() ? body[from] : '\0';
        if (at == '\r')
        {
            converted.resize(into + 1);
            converted[into] = '\n';
            const char next = from + 1 < body.size() ? body[from + 1] : '\0';
            if (next == '\n' && from + 3 < body.size() && body[from + 2] == '\r' && body[from + 3] == '\n')
            {
                ++into;
                converted.resize(into + 1);
                converted[into] = ' ';
            }
            ++from;
        }
        else
        {
            converted.resize(into + 1);
            converted[into] = at;
        }
        ++from;
        ++into;
    }
    converted = converted.substr(0, converted.find('\0'));
    if (converted.size() > BodyLimit)
        converted.resize(BodyLimit);
    letter.sending = 1;
    // 0x498ad0..0x498b27: the portrait's direction and action. The computations run in the x87, without rounding to
    // float between them, and ftol truncates.
    constexpr float AngleStep = 1.0f / 6.0f;
    constexpr float ZoomScale = 100.0f;
    constexpr float ZoomBase = 80.0f;
    constexpr float ZoomStep = 5.0f;
    constexpr float ZoomUnit = 0.1f;
    constexpr std::int32_t ActionBase = 0x78;
    const std::int32_t angle = Truncate(static_cast<double>(letter.portrait.angle) * static_cast<double>(AngleStep));
    const std::int32_t zoom = Truncate((static_cast<double>(letter.portrait.zoom) * static_cast<double>(ZoomScale) -
                                        static_cast<double>(ZoomBase) + static_cast<double>(ZoomStep)) *
                                       static_cast<double>(ZoomUnit));
    const auto direction = static_cast<std::uint8_t>((angle & 0x3f) | (zoom << 6));
    const auto action = static_cast<std::uint8_t>(letter.portrait.action - ActionBase);
    return Send(view,
                Ui::FriendSend{.kind = Ui::FriendSendKind::LetterSend,
                               .value = direction,
                               .name = std::move(to),
                               .index = 0,
                               .room = 0,
                               .window = write.base.id,
                               .action = action,
                               .subject = std::move(subject),
                               .body = std::move(converted)},
                "0x498820(1)");
}

// 0x499840, the writing window's vtable + 0x4c: without subject or body, 1 (it closes); otherwise, box 5 with the
// 1009.
Core::Expected<bool, std::string> MayCloseTheLetter(Ui::OldWindowManager& manager, Ui::OldWindow& write,
                                                    const Ui::OldWindowGlobals& globals,
                                                    const Ui::OldWindowStepView& view)
{
    constexpr std::size_t Peek = 0xf;
    if (write.letter.subject.text.Value().substr(0, Peek).empty() &&
        write.letter.text.text.Value().substr(0, Peek).empty())
        return true;
    auto created = CreateFromAStep(manager,
                                   Ui::OldWindowRequest{.kind = Ui::ModalQuestionCase,
                                                        .x = -1,
                                                        .y = -1,
                                                        .data = TextOf(view, DiscardText),
                                                        .aux = write.base.id,
                                                        .flags = 0},
                                   globals, view, "0x499840");
    if (!created.HasValue())
        return Core::Unexpected(std::move(created).Error());
    return false;
}

// 0x498820, the writing window's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheWriteMessage(Ui::OldWindowManager& manager, Ui::OldWindow& write,
                                                        const Ui::OldWindowGlobals& globals,
                                                        const Ui::OldWindowStepView& view)
{
    const Ui::OldMessage& message = write.base.current;
    Ui::OldLetterPart& letter = write.letter;
    if (message.type == RaisedType)
    {
        // 0x4996e3: the focus to the EDIT at +0x77c, which goes back to 0 (Q-C1006).
        Ui::OldEdit& edit = letter.prepared == 2 ? letter.text : letter.prepared == 1 ? letter.subject : letter.to;
        if (letter.prepared == 0 || letter.prepared == 1 || letter.prepared == 2)
        {
            Ui::Steps::FocusTheEdit(manager, edit, globals);
            letter.prepared = 0;
        }
        return {};
    }
    if (message.type == AnswerType)
    {
        if (message.b == 1)
            Ui::PostToTheManager(manager, Ui::CloseMessage, write.base.id, 0);
        return {};
    }
    if (message.type != CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
        return SendTheLetter(manager, write, globals, view);
    case 2:
    {
        auto may = MayCloseTheLetter(manager, write, globals, view);
        if (!may.HasValue())
            return Core::Unexpected(std::move(may).Error());
        if (may.Value())
            Ui::PostToTheManager(manager, Ui::CloseMessage, write.base.id, 0);
        return {};
    }
    case 4:
        return CallThePortrait(&view, letter.portrait, Ui::PortraitCallKind::TurnAction, "0x49969b", PreviousAction);
    case 5:
        return CallThePortrait(&view, letter.portrait, Ui::PortraitCallKind::TurnAction, "0x4996bf", NextAction);
    default:
        return {};
    }
}

// 0x4944c0 with the 7: the EDIT's text, the filter and, if different from the last and not empty, the message through
// the room's connection; the EDIT emptied and, with under 2 members, 0x495630(1).
Core::Expected<void, std::string> SayInTheChat(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                               const Ui::OldWindowStepView& view)
{
    Ui::OldChatPart& part = chat.chat;
    std::string text(part.edit.text.Value());
    if (view.badWord && view.badWord(text))
        text = TextOf(view, FilteredText);
    if (text.substr(0, 0xff) != std::string_view(part.lastText).substr(0, 0xff))
    {
        part.lastText = text.substr(0, 0xff);
        // 0x493510: without the room's connection (0x49e420 returns 0), it sends nothing; with it, the C1 04
        // (FlushTheChatRoomOrders takes it out).
        if (!text.empty() && FindTheChatRoom(manager.chatRooms, part.room) != nullptr)
            manager.chatRooms.orders.push_back(
                ChatRoomOrder{.kind = ChatRoomOrder::Kind::Message, .room = part.room, .text = text});
    }
    part.edit.text.Clear();
    if (part.members.size() < 2)
    {
        if (view.texts == nullptr)
            return Cut("0x495630(1): el título del chat sin los textos");
        CloseTheChat(chat, 1, *view.texts);
    }
    return {};
}

// 0x4944c0 with the 10 and 1: shows or hides the invite list.
Core::Expected<void, std::string> ToggleTheInvites(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                   const Ui::OldWindowGlobals& globals,
                                                   const Ui::OldWindowStepView& view)
{
    Ui::OldChatPart& part = chat.chat;
    Ui::OldFrame& frame = chat.frame;
    // 0x494c4b..0x494c5d and 0x494d6a..0x494d76: the maximum width, ftol(540 / rx (+ 80 with the invite list)).
    constexpr double MaxWidthBase = 540.0;
    constexpr double MaxWidthWithInvites = 80.0;
    const double rx = view.measure != nullptr ? RatioX(view.measure->screen) : 1.0;
    if (part.mode == 1)
    {
        part.mode = 2;
        chat.base.width += 0x50;
        frame.minWidth = ChatInvitesWidth;
        frame.minHeight = ChatMinHeight;
        frame.maxWidth = Truncate(MaxWidthBase / rx + MaxWidthWithInvites);
        frame.maxHeight = 0;
        SizeTheBase(part.lines.base, 3, -0xa0, -0x10);
        part.users.base.placeX = 0x9b;
        part.inviteButton.base.placeX = 0x86;
        // 0x494cce..0x494cf8: the 9 to the lines and the 8 to the send button (+0x6e8) and the invite list (+0x5a0).
        if (auto posted = PostToTheWrappedList(manager, part, SizeType, view.measure); !posted.HasValue())
            return posted;
        Ui::Steps::PostAndSettle(manager, part.sendButton, PlaceType);
        PostToTheList(manager, part.invite, part.invitable.size(), PlaceType);
        part.inviteButton.label = TextOf(view, HideInvitesText);
        FillTheInvites(manager, chat, view.heroName);
        if (chat.base.width + chat.base.x > ScreenWidth)
            chat.base.x = ScreenWidth - chat.base.width;
        return Ui::Steps::SettleTheWindow(manager, chat, globals, view);
    }
    if (part.mode > 1)
    {
        part.mode = 1;
        chat.base.width -= 0x50;
        frame.minWidth = ChatWidth;
        frame.minHeight = ChatMinHeight;
        frame.maxWidth = Truncate(MaxWidthBase / rx);
        frame.maxHeight = 0;
        SizeTheLines(chat, part.members.size() >= 3);
        part.users.base.placeX = 0x4b;
        part.inviteButton.base.placeX = 0x36;
        // 0x494df6..0x494e20: the same three as when showing it; without the window's vtable + 0x30.
        if (auto posted = PostToTheWrappedList(manager, part, SizeType, view.measure); !posted.HasValue())
            return posted;
        Ui::Steps::PostAndSettle(manager, part.sendButton, PlaceType);
        PostToTheList(manager, part.invite, part.invitable.size(), PlaceType);
        part.inviteButton.label = TextOf(view, InviteButtonText);
    }
    return {};
}

// 0x4944c0 with the 10 and 2: the invite list's chosen one, with 2..0x1d members, to the C1 CB.
Core::Expected<void, std::string> InviteTheChosen(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                  const Ui::OldWindowStepView& view)
{
    Ui::OldChatPart& part = chat.chat;
    const std::optional<std::size_t> at = Ui::SelectedInTheList(part.invite, part.invitable.size());
    if (!at || *at >= part.invitable.size())
        return {};
    const auto members = static_cast<std::int32_t>(part.members.size());
    if (members <= 1)
        return {};
    constexpr std::int32_t MostMembers = 0x1d;
    if (members > MostMembers)
        return AddAChatLine(manager, chat, 0xff, TextOf(view, InviteFullText), 1, 0, view.measure);
    return Send(view,
                Ui::FriendSend{.kind = Ui::FriendSendKind::Invite,
                               .value = 0,
                               .name = FriendName(part.invitable[*at].name),
                               .index = 0,
                               .room = static_cast<std::uint16_t>(part.room),
                               .window = chat.base.id,
                               .action = 0,
                               .subject = {},
                               .body = {}},
                "0x4944c0(10, 2)");
}

// 0x4944c0, the chat's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheChatMessage(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                       const Ui::OldWindowGlobals& globals,
                                                       const Ui::OldWindowStepView& view)
{
    Ui::OldMessage& message = chat.base.current;
    // 0x4944d9: a double click on the invite list is the 10 with 2.
    if (message.type == ChosenRowType && message.a == chat.chat.invite.base.id)
    {
        globals.RequestSound(ButtonSound);
        message.type = CommandType;
        message.a = 2;
    }
    if (message.type == RaisedType)
    {
        Ui::Steps::FocusTheEdit(manager, chat.chat.edit, globals);
        globals.focusedList = chat.chat.users.base.id;
        return {};
    }
    if (message.type == EnterType)
        return SayInTheChat(manager, chat, view);
    if (message.type != CommandType || manager.modal != 0)
        return {};
    if (message.a == 1)
        return ToggleTheInvites(manager, chat, globals, view);
    // 0x4944c0 with the 2 (0x49455e): with the chat open (the EDIT's vtable + 0x5c, +0xb4, returns 0).
    if (message.a == 2 && chat.chat.edit.closed == 0)
        return InviteTheChosen(manager, chat, view);
    return {};
}

// 0x4a09d0: the mark of all the letters of the copy (the list's vtable + 0x48, 0x47f200) and of the model (0x4a0270),
// and +0x414.
void CheckAllTheLetters(Ui::OldWindowManager& manager, Ui::OldWindow& tab, std::int32_t checked)
{
    for (LetterEntry& letter : tab.tab.letters)
        letter.selected = checked;
    for (LetterEntry& letter : manager.letterModel.letters)
        letter.selected = checked;
    tab.tab.allChecked = checked;
}

// The friends window's letters tab.
Ui::OldWindow* TheLettersTab(Ui::OldWindowManager& manager)
{
    Ui::OldWindow* friends = Ui::FindTheOldWindow(manager, manager.friends);
    if (friends == nullptr || friends->tabs.size() != FriendsTabCount)
        return nullptr;
    return &friends->tabs[LettersTabNumber];
}

// 0x491570: the letter into the read windows' map (+0x94 of [0x5763514]) with the window at 0, if it was not there;
// returns whether it was.
bool ReserveTheLetterWindow(Ui::OldWindowManager& manager, std::uint16_t index)
{
    return !manager.letterWindows.try_emplace(index, 0).second;
}

// 0x491780: the read window noted for the letter, or 0.
std::int32_t LetterWindowOf(const Ui::OldWindowManager& manager, std::uint16_t index)
{
    const auto found = manager.letterWindows.find(index);
    return found != manager.letterWindows.end() ? found->second : 0;
}

// 0x4a17b9 and 0x49a839: 0x491570 notes it (with the window at 0) if it was not there; if it was, 0x491780 gives its
// window, which is raised (with 0, nothing: the C7 has not arrived yet). Otherwise, 0x4a04a0's copy goes back to
// 0x4dcb70 or the C1 06 C7 goes out.
Core::Expected<void, std::string> ReadTheLetter(Ui::OldWindowManager& manager, std::uint16_t index,
                                                const Ui::OldWindowStepView& view, std::string_view where)
{
    if (ReserveTheLetterWindow(manager, index))
    {
        if (const std::int32_t open = LetterWindowOf(manager, index); open != 0)
            Ui::PostToTheManager(manager, Ui::RaiseMessage, open, 0);
        return {};
    }
    if (const std::vector<std::byte>* kept = KeptLetter(manager.letterModel, index); kept != nullptr)
    {
        if (!view.replayLetter)
            return Cut(std::string(where) + ": 0x4dcb70 sobre la carta guardada sin el juego");
        const std::vector<std::byte> copy = *kept;
        return view.replayLetter(copy);
    }
    return Send(view,
                Ui::FriendSend{.kind = Ui::FriendSendKind::LetterRead,
                               .value = 0,
                               .name = {},
                               .index = index,
                               .room = 0,
                               .window = 0,
                               .action = 0,
                               .subject = {},
                               .body = {}},
                where);
}

// 0x4a0600: the letter's place in the model, from 1; 0 if absent.
std::int32_t PlaceOfTheLetter(const LetterModel& model, std::uint16_t index)
{
    std::int32_t place = 0;
    for (const LetterEntry& letter : model.letters)
    {
        ++place;
        if (letter.index == index)
            return place;
    }
    return 0;
}

// 0x49ff50 and 0x4a0070: the number of the letter after (or before) `index` in the model; 0 if absent or there is no
// other. A letter with number 0 counts as none (Q-C1007).
std::uint16_t NeighbourOfTheLetter(const LetterModel& model, std::uint16_t index, bool next)
{
    const auto found = std::ranges::find(model.letters, index, &LetterEntry::index);
    if (found == model.letters.end())
        return 0;
    if (next)
    {
        const auto after = std::next(found);
        return after != model.letters.end() ? after->index : std::uint16_t{0};
    }
    return found != model.letters.begin() ? std::prev(found)->index : std::uint16_t{0};
}

// 0x4a0a10 with 0x4a0600 over the letters tab, if the friends window exists: the list chooses that letter (the
// vtable + 0x58, 0x47f5b0, like 0x47aef0).
void ChooseTheLetterInTheTab(Ui::OldWindowManager& manager, std::uint16_t index)
{
    Ui::OldWindow* tab = TheLettersTab(manager);
    if (tab == nullptr)
        return;
    Ui::SelectInTheList(tab->tab.list, tab->tab.letters.size(), PlaceOfTheLetter(manager.letterModel, index));
}

// 0x49a7a4 and 0x49ac6c, the read window's 10 with 5 and with 6: with another letter, it chooses it in the tab,
// closes, releases its own (0x4916b0), leaves its place to the incoming one (0x5762f0c and 0x5762f10) and reads the
// other; without another, the tab chooses its own.
Core::Expected<void, std::string> ReadTheNeighbour(Ui::OldWindowManager& manager, Ui::OldWindow& read, bool next,
                                                   const Ui::OldWindowStepView& view, std::string_view where)
{
    const std::uint16_t index = read.letter.entry.index;
    const std::uint16_t other = NeighbourOfTheLetter(manager.letterModel, index, next);
    if (other == 0)
    {
        ChooseTheLetterInTheTab(manager, index);
        return {};
    }
    ChooseTheLetterInTheTab(manager, other);
    Ui::PostToTheManager(manager, Ui::CloseMessage, read.base.id, 0);
    manager.letterWindows.erase(index);
    manager.nextReadX = read.base.x;
    manager.nextReadY = read.base.y;
    return ReadTheLetter(manager, other, view, where);
}

// 0x49a1f0, the read window's vtable + 0x28. Returns 0.
Core::Expected<void, std::string> HandleTheReadMessage(Ui::OldWindowManager& manager, Ui::OldWindow& read,
                                                       const Ui::OldWindowGlobals& globals,
                                                       const Ui::OldWindowStepView& view)
{
    const Ui::OldMessage& message = read.base.current;
    const LetterEntry& entry = read.letter.entry;
    if (message.type == AnswerType)
    {
        // 0x49a22b: the yes to the 1017 question: C1 06 C8 with the letter (0x49a3ab) and the 4 to itself.
        if (message.b != 1)
            return {};
        if (auto sent = Send(view,
                             Ui::FriendSend{.kind = Ui::FriendSendKind::LetterDelete,
                                            .value = 0,
                                            .name = {},
                                            .index = entry.index,
                                            .room = 0,
                                            .window = 0,
                                            .action = 0,
                                            .subject = {},
                                            .body = {}},
                             "0x49a1f0(12)");
            !sent.HasValue())
            return sent;
        Ui::PostToTheManager(manager, Ui::CloseMessage, read.base.id, 0);
        return {};
    }
    if (message.type != CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
    {
        // 0x49a625: reply: the writing window (without the 8 and the 9) with the sender and sprintf(1016, subject) cut
        // at 0x20 by 0x459e30.
        auto opened = OpenTheWriteWindow(manager, globals, view, "0x49a1f0(1)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        Ui::OldWindow* write = Ui::FindTheOldWindow(manager, opened.Value());
        if (write == nullptr)
            return {};
        PutTheName(manager, *write, entry.name, globals);
        const std::array<Crt::SprintfArgument, 1> subject{Crt::SprintfArgument(std::string_view(entry.subject))};
        const std::string reply = Crt::FormatLikeSprintf(TextOf(view, ReplyText), subject);
        PutTheSubject(manager, *write, CutTheSubject(reply), globals);
        return {};
    }
    case 2:
    {
        // 0x49a702: the 1017 question, with itself as owner.
        auto created = CreateFromAStep(manager,
                                       Ui::OldWindowRequest{.kind = Ui::ModalQuestionCase,
                                                            .x = -1,
                                                            .y = -1,
                                                            .data = TextOf(view, ReallyDeleteLettersText),
                                                            .aux = read.base.id,
                                                            .flags = 0},
                                       globals, view, "0x49a1f0(2)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return {};
    }
    case 3:
        Ui::PostToTheManager(manager, Ui::CloseMessage, read.base.id, 0);
        return {};
    case 5:
        return ReadTheNeighbour(manager, read, true, view, "0x49a7a4");
    case 6:
        return ReadTheNeighbour(manager, read, false, view, "0x49ac6c");
    default:
        return {};
    }
}
} // namespace

} // namespace Client::Messenger

namespace Client::Ui::Steps
{
bool IsALetterKind(OldWindowKind kind)
{
    return kind == OldWindowKind::ChatWindow || kind == OldWindowKind::LetterReadWindow ||
           kind == OldWindowKind::LetterWriteWindow;
}

Core::Expected<OldWindow, std::string> MakeTheLetterKind(OldWindowManager& manager, const OldWindowRequest& request,
                                                         const OldWindowTexts& texts, const OldWindowMeasure& measure,
                                                         const OldWindowStepView* view)
{
    OldWindow window = MakeTheFrame(manager);
    OldWindowBase& base = window.base;
    const std::int32_t id = base.id;
    if (request.kind == ChatCase || request.kind == CalledChatCase)
    {
        // 0x48eee4: the frame, the EDIT, the lines, the members, a button, the invite list and another button; +0xb0
        // at 1.
        window.kind = OldWindowKind::ChatWindow;
        OldChatPart& chat = window.chat;
        chat.edit = Messenger::MakeTheEdit(manager, Messenger::ChatEditLimit);
        chat.lines = Messenger::MakeTheWrappedList(manager, 0);
        chat.users = Messenger::MakeTheList(manager);
        chat.inviteButton = MakeTheButton(manager);
        chat.invite = Messenger::MakeTheList(manager);
        chat.sendButton = MakeTheButton(manager);
        chat.mode = 1;
        // 0x493220.
        chat.lastText.clear();
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = Messenger::WindowPlace;
        base.y = Messenger::WindowPlace;
        base.width = Messenger::ChatWidth;
        base.height = Messenger::ChatHeight;
        window.frame.minWidth = Messenger::ChatWidth;
        window.frame.minHeight = Messenger::ChatMinHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        chat.edit.base.width = 0xb4;
        chat.edit.base.height = 0xe;
        chat.edit.filter = Messenger::ChatEditFilter;
        Messenger::PlaceTheBase(chat.edit.base, id, 2, 2, 0xc);
        chat.edit.base.state = 0;
        Messenger::PlaceTheBase(chat.lines.base, id, 2, 0, 0x10);
        Messenger::SizeTheBase(chat.lines.base, 3, 0, -0x10);
        Messenger::PlaceTheBase(chat.users.base, id, 3, 0x4b, 0x10);
        Messenger::SizeTheBase(chat.users.base, 2, 0x4b, -0x10);
        Messenger::PlaceTheBase(chat.invite.base, id, 3, 0x4b, 0x10);
        Messenger::SizeTheBase(chat.invite.base, 2, 0x4b, -0x10);
        Messenger::SetUpTheButton(chat.inviteButton, id, 1, texts.At(Messenger::InviteButtonText), 3, 0x36, 0xe, 0x35,
                                  0xd);
        Messenger::SetUpTheButton(chat.sendButton, id, 2, texts.At(Messenger::InviteButtonText), 3, 0x4a, 0xe, 0x49,
                                  0xd);
        chat.aux = 0;
        return window;
    }
    OldLetterPart& letter = window.letter;
    letter.portrait.base = MakeTheBase(manager);
    if (request.kind == LetterReadCase)
    {
        // 0x48f41f: the frame, the portrait (+0xb0), +0x774 at 2, the body list (+0x8b0) and five buttons.
        window.kind = OldWindowKind::LetterReadWindow;
        letter.portraitMode = Messenger::ReadPortraitMode;
        letter.lines = Messenger::MakeTheWrappedList(manager, 1);
        for (std::size_t at = 0; at < Messenger::ReadButtonTexts.size(); ++at)
            letter.buttons.push_back(MakeTheButton(manager));
        // 0x4999d0.
        window.frame.title = std::string(request.data);
        base.parent = request.aux;
        base.x = Messenger::WindowPlace;
        base.y = Messenger::WindowPlace;
        base.width = Messenger::ChatWidth;
        base.height = Messenger::ReadHeight;
        window.frame.minWidth = Messenger::ChatWidth;
        window.frame.minHeight = Messenger::ReadHeight;
        base.flags = Messenger::LetterFlags;
        Messenger::PlaceTheBase(letter.lines.base, id, 2, 0, 0x14);
        Messenger::SizeTheBase(letter.lines.base, 3, 0, -0x24);
        for (std::size_t at = 0; at < letter.buttons.size(); ++at)
            Messenger::SetUpTheButton(letter.buttons[at], id, Messenger::ReadButtonCommands[at],
                                      texts.At(Messenger::ReadButtonTexts[at]), 2, Messenger::ReadButtonX[at], 0x10,
                                      Messenger::ReadButtonWidth[at], 0xe);
        if (auto built = Messenger::CallThePortrait(view, letter.portrait, PortraitCallKind::Build, "0x4999d0");
            !built.HasValue())
            return Core::Unexpected(std::move(built).Error());
        letter.portrait.base.flags = 1;
        Messenger::PlaceTheBase(letter.portrait.base, id, 1, 0x77, 0xf);
        Messenger::SizeTheBase(letter.portrait.base, 2, 0x77, -0xf);
        Messenger::SizeTheBase(letter.lines.base, 3, -Messenger::PortraitWidth, -0x24);
        base.width += Messenger::PortraitWidth;
        window.frame.minWidth = Messenger::LetterMinWidth;
        window.frame.minHeight = Messenger::ReadHeight;
        window.frame.maxWidth = 0;
        window.frame.maxHeight = 0;
        return window;
    }
    // 0x48f4ee: the frame, the portrait (+0xb4), the three EDITs and four buttons.
    window.kind = OldWindowKind::LetterWriteWindow;
    letter.to = Messenger::MakeTheEdit(manager, Messenger::ToLimit);
    letter.subject = Messenger::MakeTheEdit(manager, Messenger::SubjectLimit);
    letter.text = Messenger::MakeTheEdit(manager, Messenger::BodyLimit);
    // 0x49817a..0x498192: Tab goes from the name to the subject, to the body and back to the name.
    letter.to.next = letter.subject.base.id;
    letter.subject.next = letter.text.base.id;
    letter.text.next = letter.to.base.id;
    for (std::size_t at = 0; at < Messenger::WriteButtonTexts.size(); ++at)
        letter.buttons.push_back(MakeTheButton(manager));
    // 0x497f50.
    window.frame.title = std::string(request.data);
    base.parent = request.aux;
    base.x = Messenger::WindowPlace;
    base.y = Messenger::WindowPlace;
    base.width = Messenger::ChatWidth;
    base.height = Messenger::WriteHeight;
    window.frame.minWidth = Messenger::ChatWidth;
    window.frame.minHeight = Messenger::WriteMinHeight;
    base.flags = Messenger::LetterFlags;
    // GetTextExtentPointA of the 1000 with the DC's font ([0x57634f4]) and 0x497fd3..0x497fe2: ftol(cx / rx + 0.5).
    constexpr double HalfPixel = 0.5;
    const std::int32_t toWidth = OldDraw::Truncate(
        static_cast<double>(measure.measure.Measure(measure.dcFont, texts.At(Messenger::ToText)).width) /
            OldDraw::RatioX(measure.screen) +
        HalfPixel);
    letter.to.base.width = 0xee;
    letter.to.base.height = 0xe;
    Messenger::PlaceTheBase(letter.to.base, id, 0, toWidth, 3);
    letter.subject.base.width = 0xee;
    letter.subject.base.height = 0xe;
    Messenger::PlaceTheBase(letter.subject.base, id, 0, toWidth, 0x12);
    // 0x4980ef: the vtable + 0x38 with 1 (+0xc4) before 0xee × 0x87 and creating it (0x467fe0).
    letter.text.base.width = 0xee;
    letter.text.base.height = 0x87;
    MakeTheEditMultiline(letter.text, measure.measure, measure.dcFont, measure.screen);
    Messenger::PlaceTheBase(letter.text.base, id, 0, 5, 0x21);
    for (std::size_t at = 0; at < letter.buttons.size(); ++at)
        Messenger::SetUpTheButton(letter.buttons[at], id, Messenger::WriteButtonCommands[at],
                                  texts.At(Messenger::WriteButtonTexts[at]), 2, Messenger::WriteButtonX[at], 0x10, 0x32,
                                  0xe);
    // The name EDIT's +0x30(0): the focus, which MuRet gives it after building the window (Steps::FocusTheChatEdit with
    // the 7); +0x77c at 0 and +0x9c at 1.
    letter.prepared = 0;
    window.frame.editField = 1;
    if (auto built = Messenger::CallThePortrait(view, letter.portrait, PortraitCallKind::Build, "0x497f50");
        !built.HasValue())
        return Core::Unexpected(std::move(built).Error());
    letter.portrait.base.flags = 1;
    Messenger::PlaceTheBase(letter.portrait.base, id, 1, 0x77, 0x1e);
    letter.portrait.base.width = 0x77;
    letter.portrait.base.height = ClientHeight(window) - 0x31;
    if (auto copied = Messenger::CallThePortrait(view, letter.portrait, PortraitCallKind::CopyTheHero, "0x497f50");
        !copied.HasValue())
        return Core::Unexpected(std::move(copied).Error());
    letter.portrait.follow = 1;
    letter.portrait.action = Messenger::WritePortraitAction;
    if (auto acted = Messenger::CallThePortrait(view, letter.portrait, PortraitCallKind::SetAction, "0x497f50");
        !acted.HasValue())
        return Core::Unexpected(std::move(acted).Error());
    if (auto turned = Messenger::SetThePortraitAngle(view, letter.portrait, Messenger::WritePortraitAngle, "0x497f50");
        !turned.HasValue())
        return Core::Unexpected(std::move(turned).Error());
    letter.withPortrait = 1;
    base.width += Messenger::PortraitWidth;
    window.frame.minWidth = Messenger::LetterMinWidth;
    window.frame.minHeight = Messenger::WriteMinHeight;
    window.frame.maxWidth = 0;
    window.frame.maxHeight = 0;
    return window;
}

Core::Expected<void, std::string> HandleTheLetterKindsMessage(OldWindowManager& manager, OldWindow& window,
                                                              const OldWindowGlobals& globals,
                                                              const OldWindowStepView& view)
{
    switch (window.kind)
    {
    case OldWindowKind::ChatWindow:
        return Messenger::HandleTheChatMessage(manager, window, globals, view);
    case OldWindowKind::LetterReadWindow:
        return Messenger::HandleTheReadMessage(manager, window, globals, view);
    case OldWindowKind::LetterWriteWindow:
        return Messenger::HandleTheWriteMessage(manager, window, globals, view);
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheLetterKindsChildren(OldWindowManager& manager, OldWindow& window,
                                                             std::int32_t arg, const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view)
{
    if (window.kind == OldWindowKind::ChatWindow)
    {
        // 0x495450: with the width changed since the previous step (+0x774), the EDIT to what is left beside the
        // buttons; then the buttons, the lists and the EDIT.
        OldChatPart& chat = window.chat;
        if (chat.aux != 0 && chat.aux != window.base.width)
        {
            std::int32_t width = Steps::ClientWidth(window) + (-7 - chat.inviteButton.base.width);
            if (chat.mode == 2)
                width += -7 - chat.sendButton.base.width;
            chat.edit.base.width = width;
            chat.edit.base.height = 0xe;
        }
        chat.aux = window.base.width;
        Steps::StepTheButton(manager, chat.inviteButton, arg, globals, view);
        if (auto stepped = Messenger::StepTheWrappedList(manager, chat, arg, globals, view); !stepped.HasValue())
            return stepped;
        Messenger::StepAFriendList(manager, chat.users, chat.members, arg, globals, view);
        Messenger::StepAFriendList(manager, chat.invite, chat.invitable, arg, globals, view);
        Steps::StepTheEdit(manager, chat.edit, arg, globals, view);
        Steps::StepTheButton(manager, chat.sendButton, arg, globals, view);
        return {};
    }
    OldLetterPart& letter = window.letter;
    if (window.kind == OldWindowKind::LetterReadWindow)
    {
        // 0x49b180: the body list, the five buttons and the portrait.
        if (auto stepped = Messenger::StepTheWrappedList(manager, letter, arg, globals, view); !stepped.HasValue())
            return stepped;
        for (OldButton& button : letter.buttons)
            Steps::StepTheButton(manager, button, arg, globals, view);
        return Messenger::StepThePortrait(manager, letter.portrait, arg, globals, view, "0x49b180");
    }
    // 0x499780: the three EDITs, the first two buttons and, with +0xb0 at 1, the other two and the portrait.
    Steps::StepTheEdit(manager, letter.to, arg, globals, view);
    Steps::StepTheEdit(manager, letter.subject, arg, globals, view);
    Steps::StepTheEdit(manager, letter.text, arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[0], arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[1], arg, globals, view);
    if (letter.withPortrait != 1)
        return {};
    Steps::StepTheButton(manager, letter.buttons[2], arg, globals, view);
    Steps::StepTheButton(manager, letter.buttons[3], arg, globals, view);
    return Messenger::StepThePortrait(manager, letter.portrait, arg, globals, view, "0x499780");
}

Core::Expected<void, std::string> SettleTheLetterKinds(OldWindowManager& manager, OldWindow& window,
                                                       const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (window.kind == OldWindowKind::ChatWindow)
    {
        // 0x493410: the 8 to the six children; the 9 twice to the lines, the members and the invite list; the 0xe to
        // the members and the invite list; +0x9c at 1 and the members list to 0x5762ec0.
        OldChatPart& chat = window.chat;
        if (auto posted = Messenger::PostToTheWrappedList(manager, chat, Messenger::PlaceType, view.measure);
            !posted.HasValue())
            return posted;
        Messenger::PostToTheList(manager, chat.users, chat.members.size(), Messenger::PlaceType);
        Messenger::PostToTheList(manager, chat.invite, chat.invitable.size(), Messenger::PlaceType);
        Steps::PostAndSettle(manager, chat.inviteButton, Messenger::PlaceType);
        Steps::PostAndSettle(manager, chat.sendButton, Messenger::PlaceType);
        Messenger::PostToTheEdit(manager, chat.edit, Messenger::PlaceType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
        {
            if (auto posted = Messenger::PostToTheWrappedList(manager, chat, Messenger::SizeType, view.measure);
                !posted.HasValue())
                return posted;
        }
        for (std::int32_t twice = 0; twice < 2; ++twice)
            Messenger::PostToTheList(manager, chat.users, chat.members.size(), Messenger::SizeType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
            Messenger::PostToTheList(manager, chat.invite, chat.invitable.size(), Messenger::SizeType);
        Messenger::PostToTheList(manager, chat.users, chat.members.size(), Messenger::ToTheEndType);
        Messenger::PostToTheList(manager, chat.invite, chat.invitable.size(), Messenger::ToTheEndType);
        window.frame.editField = 1;
        globals.focusedList = chat.users.base.id;
        return {};
    }
    OldLetterPart& letter = window.letter;
    if (window.kind == OldWindowKind::LetterReadWindow)
    {
        // 0x499c00: the 8 to the list, the five buttons and the portrait; the 9 twice to the list and the portrait.
        if (auto posted = Messenger::PostToTheWrappedList(manager, letter, Messenger::PlaceType, view.measure);
            !posted.HasValue())
            return posted;
        for (OldButton& button : letter.buttons)
            Steps::PostAndSettle(manager, button, Messenger::PlaceType);
        Messenger::PostToTheBase(manager, letter.portrait.base, Messenger::PlaceType);
        for (std::int32_t twice = 0; twice < 2; ++twice)
        {
            if (auto posted = Messenger::PostToTheWrappedList(manager, letter, Messenger::SizeType, view.measure);
                !posted.HasValue())
                return posted;
        }
        Messenger::PostToTheBase(manager, letter.portrait.base, Messenger::SizeType);
        return {};
    }
    // 0x498350: the 8 to the three EDITs, the four buttons and the portrait.
    Messenger::PostToTheEdit(manager, letter.to, Messenger::PlaceType);
    Messenger::PostToTheEdit(manager, letter.subject, Messenger::PlaceType);
    Messenger::PostToTheEdit(manager, letter.text, Messenger::PlaceType);
    for (OldButton& button : letter.buttons)
        Steps::PostAndSettle(manager, button, Messenger::PlaceType);
    Messenger::PostToTheBase(manager, letter.portrait.base, Messenger::PlaceType);
    return {};
}

void FocusTheChatEdit(OldWindowManager& manager, OldWindow& window, const OldWindowGlobals& globals)
{
    if (window.kind == OldWindowKind::ChatWindow)
        Steps::FocusTheEdit(manager, window.chat.edit, globals);
    if (window.kind == OldWindowKind::LetterWriteWindow)
        Steps::FocusTheEdit(manager, window.letter.to, globals);
}

Core::Expected<bool, std::string> MayCloseTheWindow(OldWindowManager& manager, OldWindow& window,
                                                    const OldWindowGlobals& globals, const OldWindowStepView& view)
{
    if (window.kind != OldWindowKind::LetterWriteWindow)
        return true;
    return Messenger::MayCloseTheLetter(manager, window, globals, view);
}

Core::Expected<void, std::string> ForgetTheLetterKind(OldWindowManager& manager, const OldWindow& window)
{
    if (window.kind == OldWindowKind::ChatWindow)
        return Messenger::LeaveTheChatRoom(manager, window);
    // 0x4916b0 with +0x77c: the letter's number. The two padding bytes stay at 0 (*static*: 0x4dcac9 and 0x4dcaf5 read
    // it with MOVZX and 0x49ec59 copies it into a dword).
    if (window.kind == OldWindowKind::LetterReadWindow)
        manager.letterWindows.erase(window.letter.entry.index);
    return {};
}

Core::Expected<void, std::string> HandleTheLettersTabMessage(OldWindowManager& manager, OldWindow& tab,
                                                             const OldWindowGlobals& globals,
                                                             const OldWindowStepView& view)
{
    OldMessage& message = tab.base.current;
    // 0x4a11c8: a double click on a letter reads it.
    if (message.type == Messenger::ChosenRowType)
    {
        globals.RequestSound(Messenger::ButtonSound);
        message.type = Messenger::CommandType;
        message.a = 2;
    }
    if (message.type == Messenger::AnswerType)
    {
        // "Delete the checked ones?" with yes: one C1 C8 for each (0x4a1349..0x4a17a6).
        if (message.b != 1)
            return {};
        for (const Messenger::LetterEntry& letter : tab.tab.letters)
        {
            if (letter.selected == 0)
                continue;
            if (auto sent = Messenger::Send(view,
                                            FriendSend{.kind = FriendSendKind::LetterDelete,
                                                       .value = 0,
                                                       .name = {},
                                                       .index = letter.index,
                                                       .room = 0,
                                                       .window = 0,
                                                       .action = 0,
                                                       .subject = {},
                                                       .body = {}},
                                            "0x4a11b0(12)");
                !sent.HasValue())
                return sent;
        }
        return {};
    }
    if (message.type != Messenger::CommandType || manager.modal != 0)
        return {};
    switch (message.a)
    {
    case 1:
    {
        auto opened = Messenger::OpenTheWriteWindow(manager, globals, view, "0x4a11b0(1)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return Messenger::PlaceTheNewWindow(manager, opened.Value(), globals, view);
    }
    case 2:
    {
        // 0x4a17b9: 0x4a08c0, the chosen one.
        const Messenger::LetterEntry* selected = Messenger::SelectedLetter(tab);
        if (selected == nullptr)
            return {};
        return Messenger::ReadTheLetter(manager, selected->index, view, "0x4a11b0(2)");
    }
    case 3:
    {
        const Messenger::LetterEntry* selected = Messenger::SelectedLetter(tab);
        if (selected == nullptr)
            return {};
        const std::string name = selected->name;
        const std::string subject = selected->subject;
        auto opened = Messenger::OpenTheWriteWindow(manager, globals, view, "0x4a11b0(3)");
        if (!opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        OldWindow* write = FindTheOldWindow(manager, opened.Value());
        if (write == nullptr)
            return {};
        Messenger::PutTheName(manager, *write, name, globals);
        const std::array<Crt::SprintfArgument, 1> argument{Crt::SprintfArgument(std::string_view(subject))};
        const std::string reply =
            Messenger::CutTheSubject(Crt::FormatLikeSprintf(Messenger::TextOf(view, Messenger::ReplyText), argument));
        Messenger::PutTheSubject(manager, *write, reply, globals);
        return Messenger::PlaceTheNewWindow(manager, opened.Value(), globals, view);
    }
    case 4:
    {
        const bool anyChecked = std::ranges::any_of(tab.tab.letters, [](const Messenger::LetterEntry& letter)
                                                    { return letter.selected == 1; });
        const bool question = anyChecked;
        auto created = Messenger::CreateFromAStep(
            manager,
            OldWindowRequest{.kind = question ? ModalQuestionCase : ModalNoticeCase,
                             .x = -1,
                             .y = -1,
                             .data = Messenger::TextOf(view, question ? Messenger::ReallyDeleteLettersText
                                                                      : Messenger::NoneCheckedText),
                             .aux = question ? tab.base.id : 0,
                             .flags = 0},
            globals, view, "0x4a11b0(4)");
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        return Messenger::PlaceTheNewWindow(manager, created.Value(), globals, view);
    }
    case 5:
        return Messenger::Send(view, FriendSend{.kind = FriendSendKind::LetterList, .name = {}}, "0x4a11b0(5)");
    default:
        return {};
    }
}

void AfterTheLettersTabMove(OldWindowManager& manager, OldWindow& tab, const OldWindowGlobals& globals,
                            const OldWindowStepView& view)
{
    // 0x4a2070: with the left button held ([0x7d36874]), over the headers (0x13 high): the mark (0xa wide) checks or
    // unchecks all (0x4a09d0) except with a modal box; each column (+0x130..+0x13c of the list; the first without the
    // mark) sorts by its mode (0x49f870) if it is not the current one, and copies the model (0x4a0960). Outside the
    // headers the button stays held.
    if (!globals.leftHeld)
        return;
    const OldList& list = tab.tab.list;
    const std::int32_t x = Steps::ClientX(tab, 0);
    const std::int32_t y = Steps::ClientY(tab, 0);
    if (Steps::MouseIn(view, x, y, Messenger::LetterMarkColumn, Messenger::HeaderHeight, 1))
    {
        if (manager.modal == 0)
        {
            globals.RequestSound(Messenger::ButtonSound);
            Messenger::CheckAllTheLetters(manager, tab, tab.tab.allChecked == 0 ? 1 : 0);
        }
        globals.leftHeld = false;
        return;
    }
    const std::array<std::int32_t, 4> from{x + Messenger::LetterMarkColumn, x + list.headers,
                                           x + list.headers + list.nameWidth,
                                           x + list.headers + list.nameWidth + list.placeWidth};
    const std::array<std::int32_t, 4> widths{list.headers - Messenger::LetterMarkColumn, list.nameWidth,
                                             list.placeWidth, list.subjectWidth};
    for (std::size_t column = 0; column < from.size(); ++column)
    {
        if (!Steps::MouseIn(view, from[column], y, widths[column], Messenger::HeaderHeight, 1))
            continue;
        const auto mode = static_cast<std::int32_t>(column);
        if (manager.letterModel.sortMode != mode)
        {
            globals.RequestSound(Messenger::ButtonSound);
            Messenger::SortTheLetters(manager.letterModel, mode);
            RefreshTheLetters(manager, tab);
        }
        globals.leftHeld = false;
        return;
    }
}

void RefreshTheLetters(OldWindowManager& manager, OldWindow& tab)
{
    // 0x4a0960: the chosen one's number (the list's vtable + 0x68 and + 0x64; −1 without one), +0x1f0 at 1, the copy of
    // the model (0x49f500) that gives that letter's place (from 1; 0 if absent), chosen with the vtable + 0x58, the
    // view at the top (+0x34 with 0) and 0x4a09d0(0).
    std::optional<std::uint16_t> chosen;
    if (const Messenger::LetterEntry* selected = Messenger::SelectedLetter(tab); selected != nullptr)
        chosen = selected->index;
    tab.tab.list.refreshed = 1;
    tab.tab.letters.clear();
    std::int32_t at = 0;
    std::int32_t found = 0;
    for (const Messenger::LetterEntry& letter : manager.letterModel.letters)
    {
        tab.tab.letters.push_back(letter);
        ++at;
        if (chosen && letter.index == *chosen)
            found = at;
    }
    SelectInTheList(tab.tab.list, tab.tab.letters.size(), found);
    ScrollTheList(tab.tab.list, tab.tab.letters.size(), 0);
    Messenger::CheckAllTheLetters(manager, tab, 0);
}

Core::Expected<void, std::string> ChatWithTheFriend(OldWindowManager& manager, std::string_view name,
                                                    const OldWindowStepView& view)
{
    const std::int32_t open = Messenger::FindTheChatWith(manager, name, view.heroName);
    if (open == 0)
    {
        if (manager.offlineSent != 0 || Messenger::IsAPendingChat(manager, name))
            return {};
        Messenger::AddAPendingChat(manager, name);
        return Messenger::Send(
            view, FriendSend{.kind = FriendSendKind::RoomRequest, .value = 0, .name = Messenger::FriendName(name)},
            "0x49d0b0(3)");
    }
    if (open == -1)
        return {};
    // 0x49dba4: the vtable + 4 with 3 (hidden) and the 1 to the manager, which shows it and raises it.
    if (OldWindow* chat = FindTheOldWindow(manager, open); chat != nullptr)
        chat->base.state = Messenger::HiddenState;
    PostToTheManager(manager, RaiseMessage, open, 0);
    return {};
}

Core::Expected<std::int32_t, std::string> WriteToTheFriend(OldWindowManager& manager,
                                                           const Messenger::FriendEntry* selected,
                                                           const OldWindowGlobals& globals,
                                                           const OldWindowStepView& view)
{
    auto opened = Messenger::OpenTheWriteWindow(manager, globals, view, "0x49d0b0(4)");
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    const std::int32_t id = opened.Value();
    // 0x49cad0: the chosen one's name, if any, to the one it built (0x490a00).
    if (OldWindow* write = id != 0 ? FindTheOldWindow(manager, id) : nullptr; write != nullptr && selected != nullptr)
        Messenger::PutTheName(manager, *write, Messenger::FriendName(selected->name), globals);
    return id;
}
} // namespace Client::Ui::Steps

namespace Client::Messenger
{

std::int32_t FindTheChatWith(Ui::OldWindowManager& manager, std::string_view name, std::string_view hero)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        const Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id);
        // The original asks whatever 0x490a00 gives for the friend without checking: one that no longer exists does not
        // stay in +0xb4 (0x4a4b30).
        if (chat == nullptr)
            continue;
        std::int32_t kind = 0;
        const std::string friendName = ChatFriend(*chat, hero, kind);
        if (kind == 2 || kind == 0)
            continue;
        if (friendName.empty())
            return -1;
        if (SameFriendName(friendName, name))
            return id;
    }
    return 0;
}

bool IsAPendingChat(const Ui::OldWindowManager& manager, std::string_view name)
{
    return std::ranges::any_of(manager.windowList.list.pending,
                               [name](const std::string& pending) { return SameFriendName(pending, name); });
}

void AddAPendingChat(Ui::OldWindowManager& manager, std::string_view name)
{
    manager.windowList.list.pending.push_back(FriendName(name));
}

void ForgetAPendingChat(Ui::OldWindowManager& manager, std::string_view name)
{
    std::deque<std::string>& pending = manager.windowList.list.pending;
    const auto found =
        std::ranges::find_if(pending, [name](const std::string& entry) { return SameFriendName(entry, name); });
    if (found != pending.end())
        pending.erase(found);
}

void AddTheChatToTheList(Ui::OldWindowManager& manager, std::int32_t id)
{
    manager.windowList.list.chats.push_back(id);
}

void MarkTheChatWaiting(Ui::OldWindowManager& manager, std::int32_t id, std::optional<Audio::SoundId>& sound,
                        std::function<void(Audio::SoundId)> soundRequest)
{
    if (soundRequest)
        soundRequest(ChatWaitingSound);
    else
        sound = ChatWaitingSound;
    std::deque<std::int32_t>& waiting = manager.windowList.list.waiting;
    if (std::ranges::find(waiting, id) == waiting.end())
        waiting.push_back(id);
}

std::string ChatFriend(const Ui::OldWindow& chat, std::string_view hero, std::int32_t& kind)
{
    if (chat.chat.members.size() >= 3)
    {
        kind = 2;
        return {};
    }
    for (const FriendEntry& member : chat.chat.members)
    {
        if (!SameFriendName(member.name, hero))
        {
            kind = 1;
            return FriendName(member.name);
        }
    }
    kind = 0;
    return {};
}

Core::Expected<void, std::string> AddAChatLine(Ui::OldWindowManager& manager, Ui::OldWindow& chat, std::uint8_t speaker,
                                               std::string_view text, std::int32_t kind, std::int32_t extra,
                                               const Ui::OldWindowMeasure* measure)
{
    std::string name;
    if (speaker != 0xff)
    {
        for (const FriendEntry& member : chat.chat.members)
        {
            if (member.flag == speaker)
            {
                name = FriendName(member.name);
                break;
            }
        }
    }
    // 0x45f6f0: the name (strncpy 0xb) and the text (strncpy 0x100) to the first deque if either is not empty, and the
    // vtable + 0x90.
    Ui::ChatLine line{
        .name = std::move(name), .text = Bounded(text, Chat::ChatTextBytes), .kind = kind, .extra = extra};
    if (line.name.empty() && line.text.empty())
        return {};
    chat.chat.lineRows.push_back(std::move(line));
    return RewrapTheLines(manager, chat.chat, measure);
}

Core::Expected<std::int32_t, std::string> AddAChatMember(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                         std::string_view name, std::uint8_t index, std::uint8_t state,
                                                         const Ui::OldWindowTexts& texts, std::string_view hero,
                                                         const Ui::OldWindowMeasure* measure)
{
    std::deque<FriendEntry>& members = chat.chat.members;
    const auto found = std::ranges::find_if(members, [name](const FriendEntry& member)
                                            { return FriendName(member.name) == FriendName(name); });
    if (found != members.end())
    {
        found->flag = index;
        found->state = state;
    }
    else
    {
        // 0x493d8e: 0x460e70, the members list's vtable + 0x94: at the front (0x461052..0x461071); with more than the
        // rows, scrolled one more, clamped; with any, the first chosen and the scroll at 0 (0x4610c9..0x4610de).
        members.push_front(FriendEntry{.selected = 0, .name = FriendName(name), .flag = index, .state = state});
        Ui::OldList& list = chat.chat.users;
        const auto count = static_cast<std::int32_t>(members.size());
        if (count > list.rows)
            ++list.scroll;
        if (count >= list.rows && count - list.scroll < list.rows)
            list.scroll = count - list.rows;
        Ui::SelectInTheList(list, members.size(), 1);
        list.scroll = 0;
    }
    chat.frame.title = ChatTitle(chat, texts, hero);
    Ui::Steps::FillTheWindowsTab(manager);
    if (members.size() >= 2)
        CloseTheChat(chat, 0, texts);
    if (members.size() >= 3)
    {
        SizeTheLines(chat, true);
        if (auto posted = PostToTheWrappedList(manager, chat.chat, SizeType, measure); !posted.HasValue())
            return Core::Unexpected(std::move(posted).Error());
    }
    return static_cast<std::int32_t>(members.size());
}

Core::Expected<void, std::string> RemoveAChatMember(Ui::OldWindowManager& manager, Ui::OldWindow& chat,
                                                    std::string_view name, const Ui::OldWindowTexts& texts,
                                                    std::string_view hero, const Ui::OldWindowMeasure* measure)
{
    std::deque<FriendEntry>& members = chat.chat.members;
    if (members.size() > 2)
    {
        const auto found = std::ranges::find_if(members, [name](const FriendEntry& member)
                                                { return SameFriendName(member.name, name); });
        if (found != members.end())
            members.erase(found);
        chat.frame.title = ChatTitle(chat, texts, hero);
        Ui::Steps::FillTheWindowsTab(manager);
    }
    if (members.size() >= 3)
        return {};
    SizeTheLines(chat, false);
    return PostToTheWrappedList(manager, chat.chat, SizeType, measure);
}

void FillTheInvites(Ui::OldWindowManager& manager, Ui::OldWindow& chat, std::string_view hero)
{
    std::deque<FriendEntry>& invitable = chat.chat.invitable;
    invitable.assign(manager.friendModel.entries.begin(), manager.friendModel.entries.end());
    const auto remove = [&invitable](std::string_view name)
    {
        const auto found = std::ranges::find_if(invitable, [name](const FriendEntry& entry)
                                                { return FriendName(entry.name) == FriendName(name); });
        if (found != invitable.end())
            invitable.erase(found);
    };
    for (const FriendEntry& member : chat.chat.members)
    {
        if (FriendName(member.name) != FriendName(hero))
            remove(member.name);
    }
    for (std::size_t at = 0; at < invitable.size();)
    {
        if (invitable[at].state < FirstOfflineFriendState)
        {
            ++at;
            continue;
        }
        invitable.erase(invitable.begin() + static_cast<std::ptrdiff_t>(at));
    }
    // 0x4944a1..0x4944a7: the list's vtable + 0x34 with 0 (0x47a910), which clamps the scroll.
    Ui::ScrollTheList(chat.chat.invite, invitable.size(), 0);
}

void CloseTheChat(Ui::OldWindow& chat, std::int32_t closed, const Ui::OldWindowTexts& texts)
{
    const std::string prefix = texts.At(ChatClosedText);
    std::string title = chat.frame.title.value_or(std::string());
    // The EDIT's vtable + 0x58 (0x495653 and 0x4956e1): +0xb4 at 1 or at 0. The EDIT's state (+0x48) does not change.
    if (closed != 1)
    {
        chat.chat.edit.closed = 0;
        if (title.starts_with(prefix))
            chat.frame.title = title.substr(prefix.size(), 0x80);
        return;
    }
    chat.chat.edit.closed = 1;
    std::string shown = title.starts_with(prefix) ? std::string() : prefix;
    shown += title.substr(0, 0x80);
    chat.frame.title = shown;
}

void RefreshTheInvites(Ui::OldWindowManager& manager, std::string_view hero)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id);
        if (chat != nullptr && chat->chat.mode == 2)
            FillTheInvites(manager, *chat, hero);
    }
}

Core::Expected<void, std::string> CloseTheChats(Ui::OldWindowManager& manager, const Ui::OldWindowTexts& texts,
                                                const Ui::OldWindowMeasure* measure)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id);
        if (chat == nullptr)
            continue;
        if (auto added = AddAChatLine(manager, *chat, 0xff, texts.At(ChatLeftText), 1, 0, measure); !added.HasValue())
            return added;
        CloseTheChat(*chat, 1, texts);
    }
    return {};
}

void RefreshTheLettersTab(Ui::OldWindowManager& manager)
{
    if (Ui::OldWindow* tab = TheLettersTab(manager); tab != nullptr)
        Ui::Steps::RefreshTheLetters(manager, *tab);
}

Core::Expected<void, std::string> SetTheLetter(Ui::OldWindowManager& manager, Ui::OldWindow& read,
                                               const LetterEntry& entry, std::string_view body,
                                               const Ui::OldWindowMeasure* measure)
{
    Ui::OldLetterPart& letter = read.letter;
    letter.entry = entry;
    // strtok(copy, "\n") (0x6c6484): empty lines do not stay; each one to the list's vtable + 0x94 (0x464ae0): strncpy
    // 0x3e9 to the first deque, the vtable + 0x58 with 0 (0x47c670, the choice at 0) and the + 0x90.
    body = body.substr(0, body.find('\0'));
    std::size_t from = 0;
    while (from < body.size())
    {
        const std::size_t end = std::min(body.find('\n', from), body.size());
        if (end > from)
        {
            letter.lineRows.push_back(Bounded(body.substr(from, end - from), LetterLineBytes));
            Ui::SelectInTheList(letter.lines, letter.lineRows.size(), 0);
            if (auto rewrapped = RewrapTheLines(manager, letter, measure); !rewrapped.HasValue())
                return rewrapped;
        }
        from = end + 1;
    }
    return PostToTheWrappedList(manager, letter, ToTheEndType, measure);
}

Core::Expected<void, std::string> LeaveTheChatRoom(Ui::OldWindowManager& manager, const Ui::OldWindow& chat)
{
    // 0x493510: without the room's connection it does not send the C1 03 01; with it, the leave and 0x49dfd0, which
    // closes it.
    if (FindTheChatRoom(manager.chatRooms, chat.chat.room) != nullptr)
        manager.chatRooms.orders.push_back(
            ChatRoomOrder{.kind = ChatRoomOrder::Kind::Leave, .room = chat.chat.room, .text = {}});
    DropTheChatRoom(manager.chatRooms, chat.chat.room);
    return {};
}

Core::Expected<void, std::string> SettleTheLetterKindsForTheDraw(Ui::OldWindowManager& manager, Ui::OldWindow& window,
                                                                 const Ui::OldWindowMeasure& measure)
{
    const std::int32_t state = window.base.state;
    if (state != Ui::DraggedWindow && state != Ui::ResizedWindow)
        return {};
    if (window.kind == Ui::OldWindowKind::ChatWindow)
    {
        // 0x493fd7..0x49408d: the 8 to the lines, the members, the invite list, the two buttons and the EDIT; the 9 to
        // the three lists.
        Ui::OldChatPart& chat = window.chat;
        if (auto posted = PostToTheWrappedList(manager, chat, PlaceType, &measure); !posted.HasValue())
            return posted;
        PostToTheList(manager, chat.users, chat.members.size(), PlaceType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), PlaceType);
        Ui::Steps::PostAndSettle(manager, chat.inviteButton, PlaceType);
        Ui::Steps::PostAndSettle(manager, chat.sendButton, PlaceType);
        PostToTheEdit(manager, chat.edit, PlaceType);
        if (state != Ui::ResizedWindow)
            return {};
        if (auto posted = PostToTheWrappedList(manager, chat, SizeType, &measure); !posted.HasValue())
            return posted;
        PostToTheList(manager, chat.users, chat.members.size(), SizeType);
        PostToTheList(manager, chat.invite, chat.invitable.size(), SizeType);
        return {};
    }
    Ui::OldLetterPart& letter = window.letter;
    if (window.kind == Ui::OldWindowKind::LetterReadWindow)
    {
        // 0x499d94..0x499e30: the 8 to the body list, the five buttons and the portrait; the 9 to the list and the
        // portrait.
        if (auto posted = PostToTheWrappedList(manager, letter, PlaceType, &measure); !posted.HasValue())
            return posted;
        for (Ui::OldButton& button : letter.buttons)
            Ui::Steps::PostAndSettle(manager, button, PlaceType);
        PostToTheBase(manager, letter.portrait.base, PlaceType);
        if (state != Ui::ResizedWindow)
            return {};
        if (auto posted = PostToTheWrappedList(manager, letter, SizeType, &measure); !posted.HasValue())
            return posted;
        PostToTheBase(manager, letter.portrait.base, SizeType);
        return {};
    }
    if (window.kind != Ui::OldWindowKind::LetterWriteWindow || letter.buttons.size() != WriteButtonTexts.size())
        return {};
    // 0x4984af..0x498532: the 8 to buttons +0xa44 and +0xacc, the three EDITs, buttons +0xb54 and +0xbdc and the
    // portrait
    // (+0xb4). No 9, not even while resizing.
    Ui::Steps::PostAndSettle(manager, letter.buttons[0], PlaceType);
    Ui::Steps::PostAndSettle(manager, letter.buttons[1], PlaceType);
    PostToTheEdit(manager, letter.to, PlaceType);
    PostToTheEdit(manager, letter.subject, PlaceType);
    PostToTheEdit(manager, letter.text, PlaceType);
    Ui::Steps::PostAndSettle(manager, letter.buttons[2], PlaceType);
    Ui::Steps::PostAndSettle(manager, letter.buttons[3], PlaceType);
    PostToTheBase(manager, letter.portrait.base, PlaceType);
    return {};
}

void PingTheChatRooms(Ui::OldWindowManager& manager)
{
    for (const std::int32_t id : manager.windowList.list.chats)
    {
        const Ui::OldWindow* chat = Ui::FindTheOldWindow(manager, id);
        if (chat == nullptr || FindTheChatRoom(manager.chatRooms, chat->chat.room) == nullptr)
            continue;
        manager.chatRooms.orders.push_back(
            ChatRoomOrder{.kind = ChatRoomOrder::Kind::Ping, .room = chat->chat.room, .text = {}});
    }
}
Core::Expected<void, std::string> SetThePortraitAngle(const Ui::OldWindowStepView* view, Ui::OldPortraitPart& portrait,
                                                      float angle, std::string_view where)
{
    if (portrait.built == 0)
        return {};
    if (auto turned = CallThePortrait(view, portrait, Ui::PortraitCallKind::SetAngle, where); !turned.HasValue())
        return turned;
    portrait.baseAngle = angle;
    portrait.angle = angle;
    return {};
}

void SetThePortraitZoom(Ui::OldPortraitPart& portrait, float zoom)
{
    portrait.baseZoom = zoom;
    portrait.zoom = zoom;
}
} // namespace Client::Messenger
