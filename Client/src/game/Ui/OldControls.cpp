#include "Ui/OldControls.h"

#include "Math/FloatConstants.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/OldWindowBase.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Ui
{
namespace
{
// 0x459f00: the common rectangle and the one hanging from its bottom edge (the list's +0x78, 0x47bb80).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x47b0f0: the bar's 13 × 13 arrows; they repeat from 16 steps with the button pressed.
constexpr std::int32_t ArrowSize = 0xd;
constexpr std::int32_t RepeatSteps = 0xf;
// 0x47b0f0: the arrow keys repeat from 6 steps.
constexpr std::int32_t KeyRepeatSteps = 5;
// 0x47b0f0: GetAsyncKeyState of the up and down arrows (the side ones are read and not used).
constexpr std::uint8_t UpKey = 0x26;
constexpr std::uint8_t DownKey = 0x28;
// 0x41ed10(0x19, 0, 0).
constexpr Audio::SoundId ButtonSound = Sound::ButtonClickSound;
// 0x47b0f0: the height with the border grabbed, in steps of 40 pixels and of 3 rows.
constexpr std::int32_t ResizeStep = 0x28;
constexpr std::int32_t ResizeSlack = 5;
constexpr std::int32_t MinRows = 3;
constexpr std::int32_t MaxRows = 0x1e;
// 0x47a9d0: the 9 counts the rows with 1/13 (0x6b9bf4, float) and 0.5 (0x6b882c).
constexpr float RowsPerPixel = 1.0f / 13.0f;
using Math::Half;
// 0x47a9d0: the 0xe scrolls −1000 and picks the 1000.
constexpr std::int32_t FarAway = 1000;
// 0x461c70: rows of 13, from 10 above the last one's top edge.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsTop = 10;
// 0x47b9c0 and 0x47b0f0: the thumb does not go below 2 (0x6b8818) and the drag's arithmetic adds 0.5 (0x6b882c).
constexpr float MinThumb = 2.0f;
constexpr double DragHalf = 0.5;

// _ftol (0x69fd3c): toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

bool InRect(const OldListView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
            std::int32_t mode)
{
    return Chat::MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

// 0x459f70 on the same list.
void PostToItself(OldList& list, std::int32_t type)
{
    list.base.queue.push_back(OldMessage{.type = type, .a = 0, .b = 0, .text = {}});
}

// 0x47b11f..0x47b1c8: with the list that takes the arrow keys, a pressed arrow sends its message: on press, at 6
// steps and from then on each step. Releasing both resets the count to 0.
void ReadTheArrows(OldList& list, const OldListGlobals& globals, const OldListView& view)
{
    if (list.keyboard != 1 || globals.focusedList != list.base.id || !view.keyDown)
        return;
    bool up = false;
    if (view.keyDown(UpKey))
    {
        up = true;
        if (list.keyRepeat == 0)
        {
            PostToItself(list, ArrowUpMessage);
            list.keyRepeat = 1;
        }
        else if (list.keyRepeat > KeyRepeatSteps)
            PostToItself(list, ArrowUpMessage);
        else
            ++list.keyRepeat;
    }
    if (view.keyDown(DownKey))
    {
        if (list.keyRepeat == 0)
        {
            PostToItself(list, ArrowDownMessage);
            list.keyRepeat = 1;
        }
        else if (list.keyRepeat > KeyRepeatSteps)
            PostToItself(list, ArrowDownMessage);
        else
            ++list.keyRepeat;
    }
    else if (!up)
        list.keyRepeat = 0;
}

// The counts of a list with a parent only go up once the manager has drawn (0x47b2b0..0x47b2c2).
bool MayCount(const OldList& list, const OldListGlobals& globals)
{
    return list.base.parent == 0 || globals.drawn;
}

// 0x47b277..0x47b2c3: a bar arrow. At 0 it scrolls with the sound and starts the count, up to 15 it raises it and from
// 16 it scrolls each step.
void RepeatTheArrow(OldList& list, std::size_t size, const OldListGlobals& globals, std::int32_t lines)
{
    if (list.arrowRepeat == 0)
    {
        ScrollTheList(list, size, lines);
        globals.RequestSound(ButtonSound);
        list.arrowRepeat = 1;
    }
    else if (list.arrowRepeat > RepeatSteps)
        ScrollTheList(list, size, lines);
    else if (MayCount(list, globals))
        ++list.arrowRepeat;
}

// 0x47b427..0x47b48c: the track. With a parent and no draw, nothing; otherwise, like the arrows without sound.
void RepeatTheTrack(OldList& list, std::size_t size, const OldListGlobals& globals, std::int32_t lines)
{
    if (!MayCount(list, globals))
        return;
    if (list.trackRepeat == 0)
    {
        ScrollTheList(list, size, lines);
        list.trackRepeat = 1;
    }
    else if (list.trackRepeat > RepeatSteps)
        ScrollTheList(list, size, lines);
    else
        ++list.trackRepeat;
}

// 0x47b201..0x47b545: the click on the list, with it still: the arrow keys' focus, both arrows and, with as many as
// the rows or more, the bar and the track.
void PressTheBar(OldList& list, std::size_t size, const OldListGlobals& globals, const OldListView& view)
{
    OldWindowBase& base = list.base;
    globals.focusedList = base.id;
    // The CL9.4 lists' vtable + 0x8c is 0x472120, which returns 1 without doing anything.
    const std::int32_t inset = list.barMode == 1 ? 8 : 0;
    const std::int32_t margin = list.barMode != 1 ? 8 : 0;
    const std::int32_t arrowX = base.x + inset - 0x15 + base.width;
    if (InRect(view, arrowX, base.y - base.height + margin, ArrowSize, ArrowSize, TopLeftRect))
        RepeatTheArrow(list, size, globals, -1);
    if (InRect(view, arrowX, base.y - (margin + ArrowSize), ArrowSize, ArrowSize, TopLeftRect))
        RepeatTheArrow(list, size, globals, 1);
    if (ListCount(list, size) < list.rows)
        return;
    const std::int32_t barX = base.x + inset - 0x13 + base.width;
    const std::int32_t thumbWidth = Truncate(list.thumbWidth);
    if (InRect(view, barX, Truncate(list.thumbY), thumbWidth, Truncate(list.thumbHeight), TopLeftRect))
    {
        if (base.state != ShownWindow || globals.capture != 0)
            return;
        globals.capture = base.id;
        base.state = 2;
        list.dragOffset = static_cast<float>(static_cast<double>(view.mouseY) - static_cast<double>(list.thumbY));
        return;
    }
    if (InRect(view, barX, Truncate(list.barTop), thumbWidth,
               Truncate(static_cast<double>(list.thumbY) - static_cast<double>(list.barTop)), TopLeftRect))
    {
        RepeatTheTrack(list, size, globals, -list.rows);
        return;
    }
    if (InRect(view, barX, Truncate(static_cast<double>(list.thumbHeight) + static_cast<double>(list.thumbY)),
               thumbWidth,
               Truncate(static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                        static_cast<double>(list.thumbHeight)),
               TopLeftRect))
        RepeatTheTrack(list, size, globals, list.rows);
}

// vtable + 4 (0x45a9b0) with 0, and release the mouse if it had it.
void LetGo(OldList& list, const OldListGlobals& globals)
{
    list.base.state = ShownWindow;
    if (globals.capture == list.base.id)
        globals.capture = 0;
}
} // namespace

std::int32_t ListCount(const OldList& list, std::size_t size)
{
    return list.fixedCount == 1 ? list.fixedCountValue : static_cast<std::int32_t>(size);
}

void ScrollTheList(OldList& list, std::size_t size, std::int32_t lines)
{
    const std::int32_t count = ListCount(list, size);
    // Unsigned CMP (JC).
    if (static_cast<std::uint32_t>(count) < static_cast<std::uint32_t>(list.rows))
        return;
    list.scroll -= lines;
    if (list.scroll < 0)
    {
        list.scroll = 0;
        return;
    }
    if (list.scroll > count - list.rows)
        list.scroll = count - list.rows;
}

void SetTheListRows(OldList& list, std::size_t size, std::int32_t rows)
{
    const std::int32_t count = ListCount(list, size);
    if (rows < list.rows && rows < count)
    {
        list.rows = rows;
        ++list.scroll;
        return;
    }
    if (rows > count)
        list.scroll = 0;
    list.rows = rows;
}

void GrowTheListRows(OldList& list, std::int32_t steps)
{
    list.rows += steps * 3;
    if (list.rows < MinRows)
        list.rows = MinRows;
}

void SelectInTheList(OldList& list, std::size_t size, std::int32_t selection)
{
    list.selection = selection;
    const std::int32_t count = ListCount(list, size);
    if (list.selection > count)
        list.selection = count;
}

void SelectTheNext(OldList& list, std::size_t size, std::int32_t steps)
{
    if (size == 0)
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection += steps;
    const std::int32_t count = ListCount(list, size);
    if (list.selection > count)
        list.selection = count;
}

void SelectThePrevious(OldList& list, std::size_t size, std::int32_t steps)
{
    if (size == 0)
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection -= steps;
    if (list.selection < 1)
        list.selection = 1;
}

std::optional<std::size_t> SelectedInTheList(OldList& list, std::size_t size)
{
    // 0x47afe3
    if (size == 0)
    {
        list.selection = 0;
        return std::nullopt;
    }
    if (list.selection <= 0)
        return std::nullopt;
    const auto at = static_cast<std::size_t>(list.selection) - 1;
    if (at >= size)
        return std::nullopt;
    return at;
}

std::size_t FirstShownInTheList(const OldList& list)
{
    return list.scroll < 0 ? 0 : static_cast<std::size_t>(list.scroll);
}

std::int32_t ListRowY(const OldList& list, std::size_t size, std::int32_t row)
{
    const std::int32_t count = ListCount(list, size);
    const std::int32_t shown = list.rows < count ? list.rows : count;
    return (shown - row) * RowHeight - list.base.height - RowsTop + list.base.y;
}

void HandleTheListMessage(OldList& list, std::size_t size)
{
    const std::int32_t type = list.base.current.type;
    switch (type)
    {
    case SizeMessage:
    {
        // The CL9.4 lists' vtable + 0x90 is 0x472130, a ret; that of the chat rows and of a letter (0x45fea0, 0x465270)
        // is run before by the caller (Messenger/LetterWindows, DrainTheWrappedList).
        if (list.base.parent == 0)
            return;
        const std::int32_t before = list.rows;
        // FILD of the height, FMUL and FSUB of the two floats on the x87, and _ftol.
        const double rows =
            static_cast<double>(list.base.height) * static_cast<double>(RowsPerPixel) - static_cast<double>(Half);
        SetTheListRows(list, size, Truncate(rows));
        if (list.keepBottom == 1 &&
            static_cast<std::uint32_t>(list.fixedCountValue) >= static_cast<std::uint32_t>(list.rows))
            list.scroll += before - list.rows;
        ScrollTheList(list, size, 0);
        return;
    }
    case ToTheEndMessage:
        ScrollTheList(list, size, -FarAway);
        if (list.keyboard == 1 && size != 0)
            SelectInTheList(list, size, FarAway);
        return;
    case ArrowUpMessage:
    case ArrowDownMessage:
    {
        if (list.keyboard != 1 || size < 1)
            return;
        if (type == ArrowUpMessage)
            SelectTheNext(list, size, 1);
        else
            SelectThePrevious(list, size, 1);
        if (size <= static_cast<std::size_t>(list.rows < 0 ? 0 : list.rows))
            return;
        // vtable + 0x68 (0x472100) returns +0xc4.
        const std::int32_t selected = list.selection - 1;
        if (selected >= list.scroll + list.rows)
            ScrollTheList(list, size, list.scroll - (selected - list.rows + 1));
        else if (selected < list.scroll)
            ScrollTheList(list, size, list.scroll - selected);
        return;
    }
    default:
        return;
    }
}

void PlaceTheListBar(OldList& list, std::size_t size)
{
    const OldWindowBase& base = list.base;
    if (list.barMode == 1)
    {
        list.barTop = static_cast<float>(base.y - base.height + 0xc);
        list.barBottom = static_cast<float>(base.y - 0xc);
    }
    else
    {
        list.barTop = static_cast<float>(base.y - base.height + 0x15);
        list.barBottom = static_cast<float>(base.y - 0x11);
    }
    const std::int32_t count = ListCount(list, size);
    double ratio = 1.0;
    if (count > 0)
    {
        // FILD of the rows stored as float and FDIVR with the FILD of the count.
        const double shown = static_cast<double>(static_cast<float>(list.rows)) / static_cast<double>(count);
        if (shown < 1.0)
            ratio = shown;
    }
    const double thumb = (static_cast<double>(list.barBottom) - static_cast<double>(list.barTop)) * ratio;
    list.thumbHeight = static_cast<float>(thumb);
    if (thumb < static_cast<double>(MinThumb))
        list.thumbHeight = MinThumb;
    double scrolled = 0.0;
    if (count > 0)
        scrolled = static_cast<double>(static_cast<float>(list.scroll)) / static_cast<double>(count);
    const double small = list.thumbHeight <= MinThumb ? static_cast<double>(list.thumbHeight) : 0.0;
    const double track = static_cast<double>(list.barBottom) - static_cast<double>(list.barTop) - small;
    list.thumbY = static_cast<float>(static_cast<double>(list.barBottom) - track * scrolled -
                                     static_cast<double>(list.thumbHeight));
}

bool StepTheListMouse(OldList& list, std::size_t size, const OldListGlobals& globals, const OldListView& view,
                      const OldListRowHover& hover)
{
    OldWindowBase& base = list.base;
    bool handled = false;
    ReadTheArrows(list, globals, view);
    if (InRect(view, base.x, base.y, base.width, base.height, BottomLeftRect))
    {
        if (globals.wheel != 0)
        {
            ScrollTheList(list, size, globals.wheel * -3);
            globals.wheel = 0;
        }
        if (!globals.leftClick || base.state != ShownWindow)
        {
            list.arrowRepeat = 0;
            list.trackRepeat = 0;
        }
        else
            PressTheBar(list, size, globals, view);
        globals.mouseOnUi = 1;
        handled = true;
    }
    if (base.state == 1)
    {
        if (!globals.leftClick)
            LetGo(list, globals);
        else
        {
            globals.mouseOnUi = 1;
            list.rows = ((base.y - view.mouseY + ResizeSlack) / ResizeStep) * 3;
            if (list.rows < MinRows)
                list.rows = MinRows;
            else if (list.rows > MaxRows)
                list.rows = MaxRows;
            const std::int32_t count = ListCount(list, size);
            if (count >= list.rows && count - list.scroll < list.rows)
                list.scroll = count - list.rows;
        }
    }
    else if (base.state == 2)
    {
        if (!globals.leftClick)
            LetGo(list, globals);
        else
        {
            globals.mouseOnUi = 1;
            // FILD, FSUB of the float and FST: the arithmetic goes on with the unrounded value and the thumb stays as
            // float.
            const double top = static_cast<double>(view.mouseY) - static_cast<double>(list.dragOffset);
            list.thumbY = static_cast<float>(top);
            const std::int32_t count = ListCount(list, size);
            if (top < static_cast<double>(list.barTop))
                list.scroll = count - list.rows;
            else if (top <= static_cast<double>(list.barBottom) - static_cast<double>(list.thumbHeight))
            {
                const double small = list.thumbHeight <= MinThumb ? 1.0 : 0.0;
                const double below = static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                                     static_cast<double>(list.thumbHeight) + DragHalf;
                const double track = static_cast<double>(list.barBottom) - static_cast<double>(list.barTop) - small;
                list.scroll = Truncate(static_cast<double>(count) * (below / track));
            }
            else
                list.scroll = 0;
        }
    }
    // 0x47b714..0x47b7c6: from the first shown (vtable + 0x74), up to the rows or the end.
    if (globals.modal != 0 || !hover)
        return handled;
    std::size_t at = FirstShownInTheList(list);
    for (std::int32_t row = 0; row < list.rows; ++row)
    {
        if (at >= size)
            break;
        const std::int32_t moved = hover(row, at);
        if (moved < 0)
            row -= moved;
        else if (moved == 0)
            --row;
        ++at;
    }
    return handled;
}

std::optional<char> FilteredByTheEdit(std::int32_t filter, char typed)
{
    constexpr char Escape = 0x1b;
    constexpr char FirstIgnored = 0x19;
    constexpr char LastIgnored = 0x1c;
    if (typed == Escape || (typed >= FirstIgnored && typed <= LastIgnored))
        return std::nullopt;
    const bool digit = typed >= '0' && typed <= '9';
    if ((filter & DigitsOnlyEdit) != 0)
        return digit ? std::optional<char>(typed) : std::nullopt;
    if ((filter & UppercaseLettersAndDigitsEdit) == 0 || digit || (typed >= 'A' && typed <= 'Z'))
        return typed;
    if (typed < 'a' || typed > 'z')
        return std::nullopt;
    return static_cast<char>(typed - ('a' - 'A'));
}

void TypeInTheEdit(OldEdit& edit, std::string_view typed)
{
    // 0x467b9d: each WM_CHAR zeroes +0x80 before the filter. ES_AUTOVSCROLL: the multiline one follows the caret.
    edit.draws = 0;
    edit.followCaret = true;
    if ((edit.filter & (DigitsOnlyEdit | UppercaseLettersAndDigitsEdit)) == 0)
    {
        edit.text.Type(typed);
        return;
    }
    std::string passed;
    for (const char letter : typed)
    {
        if (const std::optional<char> kept = FilteredByTheEdit(edit.filter, letter))
            passed.push_back(*kept);
    }
    edit.text.Type(passed);
}
void LimitTheEdit(OldEdit& edit, std::size_t limit, bool arrowHeld)
{
    edit.text.SetLimit(limit);
    if (arrowHeld)
        edit.draws = 0;
}
} // namespace Client::Ui
