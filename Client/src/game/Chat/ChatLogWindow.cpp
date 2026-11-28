#include "Chat/ChatLogWindow.h"

#include "Chat/ChatLog.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Chat
{
namespace
{
// 0x459f00: the common rectangle and the one that hangs from its bottom edge (+0x78 of the list).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x479960: the arrows and the track repeat from 16 steps with the button pressed.
constexpr std::int32_t RepeatSteps = 0xf;
constexpr std::int32_t ArrowSize = 0xd;
// 0x45cbb0 and 0x45dec0: the lines, 13 high from 0x10 above the bottom edge, the name at x + 10.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsBottom = 0x10;
constexpr std::int32_t NameX = 10;
// 0x479960: the height with the edge grabbed, in steps of 40 pixels and of 3 lines.
constexpr std::int32_t ResizeStep = 0x28;
constexpr std::int32_t ResizeSlack = 5;
constexpr std::int32_t MinRows = 3;
constexpr std::int32_t MaxRows = 0x1e;
// 0x45d760: the rectangle where the buttons appear and the height below which they leave.
constexpr std::int32_t ButtonsAreaX = 0xa0;
constexpr std::int32_t ButtonsAreaY = 0x1b4;
constexpr std::int32_t ButtonsAreaWidth = 0x140;
constexpr std::int32_t ButtonsAreaHeight = 0x32;
constexpr std::int32_t ButtonsHideY = 0x1a0;
// 0x6c7a90, 0x6c7a94 and 0x6c7a98: the first button and the step between them; 3.0 at 0x6b86b8.
constexpr float ButtonsX = 295.0f;
constexpr float ButtonsY = 417.0f;
constexpr float ButtonsStep = 18.0f;
constexpr float ButtonCount = 3.0f;
constexpr std::int32_t ButtonSize = 0x10;
// 0x45d990: the background's transparency goes up by 0.2 (0x6b8854) and past 0.9 (0x6b8a1c) goes back to 0.2.
constexpr float AlphaStep = 0.2f;
constexpr float AlphaTop = 0.9f;
// 0x47a230 and 0x479960: the thumb does not go below 2 (0x6b8818) and the drag's count adds 0.5 (0x6b882c).
constexpr float MinThumb = 2.0f;
constexpr double DragHalf = 0.5;

// _ftol (0x69fd3c): truncates toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

bool InRect(const ChatLogStepView& view, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
            std::int32_t mode)
{
    return MouseInRect(view.mouseX, view.mouseY, x, y, width, height, mode);
}

// 0x479960: an arrow or the track with the button pressed. At 0 it scrolls and starts the count, up to 15 it raises
// it (the list has no parent, +0x44 at 0) and from 16 it scrolls each step.
void Repeat(ChatLogState& log, ChatModes modes, std::int32_t& counter, std::int32_t lines,
            std::optional<Audio::SoundId> sound, ChatLogStepResult& result)
{
    if (counter == 0)
    {
        ScrollTheLog(log, modes, lines);
        if (sound)
            result.sounds.push_back(*sound);
        counter = 1;
    }
    else if (counter <= RepeatSteps)
        ++counter;
    else
        ScrollTheLog(log, modes, lines);
}

// 0x45d760, the vtable + 0x78: the right click on the selected one, the selection at 0, the buttons in view and
// whether the mouse is over the bar or over the buttons.
bool OverTheLog(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                const ChatLogStepView& view, ChatLogStepResult& result)
{
    if (log.selection != 0 && globals.rightDown)
    {
        // 0x6c7b8c is 1 in this main.exe: the path of the 0 (the name in 0x7798cf8) does not run.
        const std::size_t at = SelectedInTheLog(log, globals.modes);
        if (at < log.entries.size())
            result.whisperTo = NameOf(log.entries[at]);
        globals.rightDown = false;
    }
    SelectInTheLog(log, globals.modes, 0);
    if (InRect(view, ButtonsAreaX, ButtonsAreaY, ButtonsAreaWidth, ButtonsAreaHeight, TopLeftRect))
        log.buttonsShown = 1;
    else if (view.mouseY < ButtonsHideY)
        log.buttonsShown = 0;
    if (InRect(view, frame.width - 0x1e + frame.x, frame.y - 6, 0x21, frame.height - 2, BottomLeftRect))
        return true;
    return log.buttonsShown == 1 && InRect(view, Truncate(ButtonsX), Truncate(ButtonsY),
                                           Truncate(ButtonsStep * ButtonCount), ButtonSize, TopLeftRect);
}

// 0x45d990, the vtable + 0x8c: the top edge and the three buttons (F2, the size and the transparency).
void GrabOrPress(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                 const ChatLogStepView& view, ChatLogStepResult& result)
{
    if (InRect(view, frame.x, frame.y - frame.height, frame.width, 8, TopLeftRect) && globals.hovered == ChatLogId &&
        log.state == 0 && globals.capture == 0)
    {
        globals.capture = ChatLogId;
        log.state = 1;
        result.sounds.push_back(ChatButtonSound);
    }
    if (log.buttonsShown == 0 || view.chatOpen || globals.hovered != 0)
        return;
    const auto button = [&](float x)
    { return InRect(view, Truncate(x), Truncate(ButtonsY), ButtonSize, ButtonSize, TopLeftRect); };
    if (button(ButtonsX))
    {
        globals.modes.showChat = !globals.modes.showChat;
        result.sounds.push_back(ChatButtonSound);
        ScrollTheLog(log, globals.modes, 0);
        globals.leftClick = false;
    }
    if (globals.hovered != 0)
        return;
    // 0x45da90: 18 + 295 on the x87.
    if (button(ButtonsStep + ButtonsX))
    {
        CycleTheLogSize(log, globals.modes);
        result.sounds.push_back(ChatButtonSound);
        globals.leftClick = false;
    }
    if (globals.hovered != 0)
        return;
    // 0x45dae5: 18 + 18 + 295.
    if (button(ButtonsStep + ButtonsStep + ButtonsX))
    {
        // FST stores the sum as float and FCOMP compares the one left on the x87 stack, without rounding.
        const double alpha = static_cast<double>(log.alpha) + static_cast<double>(AlphaStep);
        log.alpha = static_cast<float>(alpha);
        if (alpha > static_cast<double>(AlphaTop))
            log.alpha = AlphaStep;
        result.sounds.push_back(ChatButtonSound);
        globals.leftClick = false;
    }
}

// 0x45dec0, the vtable + 0x88: the mouse over a line's name. It returns what the caller's line moves: 0 repeats it
// (the line is not shown), negative moves it too far and positive leaves the normal step.
std::int32_t HoverTheRow(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                         const ChatLogStepView& view, std::size_t at, std::int32_t row)
{
    ChatEntry& entry = log.entries[at];
    const ChatModes modes = globals.modes;
    // With the log in its window and at x = 0 (under another window), one line higher.
    if (modes.placement == 1 && frame.x == 0)
    {
        ++row;
        if (log.rows <= row)
            return 1;
    }
    if (!modes.showChat && entry.type == 3)
        return 0;
    if (modes.placement == 1)
    {
        if (entry.type == 1 || entry.type == 2)
            return 0;
    }
    else if (modes.placement == 0 && entry.age != 0 && log.scroll == 0)
    {
        const auto age = static_cast<std::int32_t>(entry.age);
        if (log.rows - age - row - 1 < 0)
            return row - log.rows;
        row += age;
    }
    // 0x6c7b8c at 1: without the safe zone or without the focus in the chat, nothing.
    if (!view.heroSafeZone || !view.chatEditFocused || view.measure == nullptr)
        return 1;
    const std::string name = NameOf(entry);
    // GetTextExtentPointA in screen pixels, without going to 640 × 480.
    const std::int32_t nameWidth = view.measure->Measure(view.dcFont, name).width;
    if (InRect(view, frame.x + NameX, frame.y - row * RowHeight - RowsBottom, nameWidth, RowHeight, TopLeftRect) &&
        name != view.heroName)
    {
        SelectInTheLog(log, modes, log.scroll + 1 + row);
        // (+0 + 1) % 2 with C's sign.
        entry.blink = static_cast<std::int32_t>(static_cast<std::uint32_t>(entry.blink) + 1u) % 2;
    }
    return 1;
}

// 0x479f84..0x47a03d: from the first visible one, up to `rows` lines or the end. 0x5762ebc is always 0.
void HoverTheRows(ChatLogState& log, const ChatLogFrame& frame, const ChatLogGlobals& globals,
                  const ChatLogStepView& view)
{
    std::size_t at = FirstShownInTheLog(log, globals.modes);
    for (std::int32_t row = 0; row < log.rows; ++row)
    {
        if (at >= log.entries.size())
            return;
        const std::int32_t moved = HoverTheRow(log, frame, globals, view, at, row);
        if (moved < 0)
            row -= moved;
        else if (moved == 0)
            --row;
        ++at;
    }
}

// 0x479960, the vtable + 0x20.
bool StepTheLogMouse(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals, const ChatLogStepView& view,
                     ChatLogStepResult& result)
{
    bool handled = false;
    // 0x479974..0x479a33: the keyboard arrows only with +0xbc at 1, which in the list is 0.
    if (OverTheLog(log, frame, globals, view, result))
    {
        if (globals.wheel != 0)
        {
            ScrollTheLog(log, globals.modes, globals.wheel * -3);
            globals.wheel = 0;
        }
        if (!globals.leftClick || log.state != 0)
        {
            log.arrowRepeat = 0;
            log.trackRepeat = 0;
        }
        else
        {
            globals.focusedList = ChatLogId;
            GrabOrPress(log, frame, globals, view, result);
            const std::int32_t inset = log.barMode == 1 ? 8 : 0;
            const std::int32_t margin = log.barMode != 1 ? 8 : 0;
            const std::int32_t arrowX = frame.x + inset - 0x15 + frame.width;
            if (InRect(view, arrowX, frame.y - frame.height + margin, ArrowSize, ArrowSize, TopLeftRect))
                Repeat(log, globals.modes, log.arrowRepeat, -1, ChatButtonSound, result);
            if (InRect(view, arrowX, frame.y - (margin + ArrowSize), ArrowSize, ArrowSize, TopLeftRect))
                Repeat(log, globals.modes, log.arrowRepeat, 1, ChatButtonSound, result);
            if (log.rows <= VisibleInTheLog(log, globals.modes))
            {
                const std::int32_t barX = frame.x + inset - 0x13 + frame.width;
                const std::int32_t thumbWidth = Truncate(frame.thumbWidth);
                if (InRect(view, barX, Truncate(frame.thumbY), thumbWidth, Truncate(frame.thumbHeight), TopLeftRect))
                {
                    if (log.state == 0 && globals.capture == 0)
                    {
                        globals.capture = ChatLogId;
                        log.state = 2;
                        log.dragOffset =
                            static_cast<float>(static_cast<double>(view.mouseY) - static_cast<double>(frame.thumbY));
                    }
                }
                else if (InRect(view, barX, Truncate(frame.barTop), thumbWidth,
                                Truncate(static_cast<double>(frame.thumbY) - static_cast<double>(frame.barTop)),
                                TopLeftRect))
                    Repeat(log, globals.modes, log.trackRepeat, -log.rows, std::nullopt, result);
                else if (InRect(view, barX,
                                Truncate(static_cast<double>(frame.thumbHeight) + static_cast<double>(frame.thumbY)),
                                thumbWidth,
                                Truncate(static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbY) -
                                         static_cast<double>(frame.thumbHeight)),
                                TopLeftRect))
                    Repeat(log, globals.modes, log.trackRepeat, log.rows, std::nullopt, result);
            }
        }
        globals.mouseOnUi = 1;
        handled = true;
    }
    if (log.state == 1)
    {
        if (!globals.leftClick)
        {
            log.state = 0;
            if (globals.capture == ChatLogId)
                globals.capture = 0;
        }
        else
        {
            globals.mouseOnUi = 1;
            log.rows = ((frame.y - view.mouseY + ResizeSlack) / ResizeStep) * 3;
            if (log.rows < MinRows)
                log.rows = MinRows;
            else if (log.rows > MaxRows)
                log.rows = MaxRows;
            const std::int32_t visible = VisibleInTheLog(log, globals.modes);
            if (visible >= log.rows && visible - log.scroll < log.rows)
                log.scroll = visible - log.rows;
        }
    }
    else if (log.state == 2)
    {
        if (!globals.leftClick)
        {
            log.state = 0;
            if (globals.capture == ChatLogId)
                globals.capture = 0;
        }
        else
        {
            globals.mouseOnUi = 1;
            // FILD, FSUB of the float and FST: the count goes on with the unrounded value and the thumb stays in float.
            const double top = static_cast<double>(view.mouseY) - static_cast<double>(log.dragOffset);
            frame.thumbY = static_cast<float>(top);
            const std::int32_t visible = VisibleInTheLog(log, globals.modes);
            if (top < static_cast<double>(frame.barTop))
                log.scroll = visible - log.rows;
            else if (top <= static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbHeight))
            {
                const double small = frame.thumbHeight <= MinThumb ? 1.0 : 0.0;
                const double below = static_cast<double>(frame.barBottom) - static_cast<double>(frame.thumbY) -
                                     static_cast<double>(frame.thumbHeight) + DragHalf;
                const double track = static_cast<double>(frame.barBottom) - static_cast<double>(frame.barTop) - small;
                log.scroll = Truncate(static_cast<double>(visible) * (below / track));
            }
            else
                log.scroll = 0;
        }
    }
    HoverTheRows(log, frame, globals, view);
    return handled;
}
} // namespace

bool MouseInRect(std::int32_t mouseX, std::int32_t mouseY, std::int32_t x, std::int32_t y, std::int32_t width,
                 std::int32_t height, std::int32_t mode)
{
    if (x > mouseX || mouseX >= x + width)
        return false;
    if (mode == BottomLeftRect)
        return y - height <= mouseY && mouseY < y;
    return y <= mouseY && mouseY < y + height;
}

OldListBar PlaceTheListBar(std::int32_t y, std::int32_t height, std::int32_t barMode, std::int32_t rows,
                           std::int32_t count, std::int32_t scroll)
{
    OldListBar bar;
    if (barMode == 1)
    {
        bar.barTop = static_cast<float>(y - height + 0xc);
        bar.barBottom = static_cast<float>(y - 0xc);
    }
    else
    {
        bar.barTop = static_cast<float>(y - height + 0x15);
        bar.barBottom = static_cast<float>(y - 0x11);
    }
    double ratio = 1.0;
    if (count > 0)
    {
        // FILD of the lines stored as float and FDIVR with the FILD of the visible ones.
        const double shown = static_cast<double>(static_cast<float>(rows)) / static_cast<double>(count);
        if (shown < 1.0)
            ratio = shown;
    }
    const double thumb = (static_cast<double>(bar.barBottom) - static_cast<double>(bar.barTop)) * ratio;
    bar.thumbHeight = static_cast<float>(thumb);
    if (thumb < static_cast<double>(MinThumb))
        bar.thumbHeight = MinThumb;
    double scrolled = 0.0;
    if (count > 0)
        scrolled = static_cast<double>(static_cast<float>(scroll)) / static_cast<double>(count);
    const double small = bar.thumbHeight <= MinThumb ? static_cast<double>(bar.thumbHeight) : 0.0;
    const double track = static_cast<double>(bar.barBottom) - static_cast<double>(bar.barTop) - small;
    bar.thumbY = static_cast<float>(static_cast<double>(bar.barBottom) - track * scrolled -
                                    static_cast<double>(bar.thumbHeight));
    return bar;
}

void PlaceTheLogBar(const ChatLogState& log, ChatModes modes, ChatLogFrame& frame)
{
    const OldListBar bar =
        PlaceTheListBar(frame.y, frame.height, log.barMode, log.rows, VisibleInTheLog(log, modes), log.scroll);
    frame.barTop = bar.barTop;
    frame.barBottom = bar.barBottom;
    frame.thumbY = bar.thumbY;
    frame.thumbHeight = bar.thumbHeight;
}

ChatLogStepResult StepTheChatLog(ChatLogState& log, ChatLogFrame& frame, const ChatLogGlobals& globals,
                                 const ChatLogStepView& view)
{
    ChatLogStepResult result;
    // 0x45aa8c..0x45aab1: the window's message queue (+0x30) is empty: nobody sends anything to the list. The vtable +
    // 0x1c is 0x403160, a ret 4.
    if (InRect(view, frame.x, frame.y, frame.width, frame.height, BottomLeftRect))
    {
        if (globals.hovered == 0)
        {
            if (globals.capture != 0 && globals.capture != ChatLogId)
                return result;
            globals.hovered = ChatLogId;
        }
        if (log.state == 0 && globals.hovered != ChatLogId && globals.capture != ChatLogId)
            return result;
        // 0x45ab18..0x45abb3: with the left button down, 0x490aa0 on the window manager (0x5763514) looks for the list
        // to bring it to the front; WinMain does not register it there (0x4bb3e5), so it does nothing (*inferred*).
    }
    result.handled = StepTheLogMouse(log, frame, globals, view, result);
    return result;
}
} // namespace Client::Chat
