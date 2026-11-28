#include "ChaosMix/JewelBundleList.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Math/X87.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Hud/GameHud.h"
#include "Hud/GameUi.h"
#include "Render/Frame/ITextDrawer.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLogWindow.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::ChaosMix
{
namespace
{
using Input::Key;
using Render::Color;

// 0x459f00: the common rectangle and the one hanging from its bottom edge (mode 2 of the list's +0x78).
constexpr std::int32_t TopLeftRect = 1;
constexpr std::int32_t BottomLeftRect = 2;
// 0x46e940: the cells it adds, 0 to 0x40 inclusive.
constexpr std::int32_t LastCell = 0x40;
// 0x46f289..0x46f290: the ones the left double click asks for, 1 to 0x3f (07 Q-C1152).
constexpr std::int32_t AskedCells = 0x40;
// 0x4834a0: the keyboard arrows' messages (0x483c21..0x483c84): next (vtable + 0x5c) and previous (+ 0x60).
constexpr std::int32_t NextMessage = 0xf;
constexpr std::int32_t PreviousMessage = 0x10;
// 0x483bc0: the keyboard arrows repeat from the sixth step; the bar's and the track's, from the 16th.
constexpr std::int32_t KeyRepeatSteps = 6;
constexpr std::int32_t RepeatSteps = 0x10;
// 0x483bc0: the bar's arrows, 13 wide, 0x15 from the right edge (8 less with mode 1) and, without mode 1, 8 further
// in; the thumb and the track, at 0x13.
constexpr std::int32_t ArrowSize = 0xd;
constexpr std::int32_t ArrowInset = 0x15;
constexpr std::int32_t TrackInset = 0x13;
constexpr std::int32_t BarModeInset = 8;
// 0x483bc0: the wheel scrolls 3 rows at a time.
constexpr std::int32_t WheelRows = 3;
// 0x41ed10(0x19, 0, 0): the bar's arrows.
constexpr Audio::SoundId ArrowSound = Sound::ButtonClickSound;
// 0x46f2c0: the rows, 13 high, from 10 above the bottom edge; 0x46f1a0 and 0x46f020 take them from 3 higher.
constexpr std::int32_t RowHeight = 0xd;
constexpr std::int32_t RowsBottom = 10;
constexpr std::int32_t RowRise = 3;
// 0x46f0f4..0x46f163: the text at x + 4 + 2.
constexpr std::int32_t RowTextX = 6;
// 0x46ebe0: the background, 0x491960(7, 0.4) (0x3ecccccd) one point bigger; the track's lines with the 2.
constexpr std::int32_t FramePalette = 7;
constexpr float FrameAlpha = 0.4f;
constexpr std::int32_t TrackPalette = 2;
// win_scrollbar.jpg: the arrows, 13 wide, 12 from the right edge, the top one a point above the top edge and the
// bottom one 12 above the bottom edge; flipped while clicked.
constexpr std::uint16_t ScrollbarSlot = 0x649;
constexpr float ArrowQuad = 13.0f;
constexpr float ArrowRight = 12.0f;
constexpr std::int32_t ArrowHitRight = 0xc;
constexpr float ArrowUv = 0.8125f;
constexpr float ArrowDv = 0.40625f;
constexpr float UpArrowV = 0.09375f;
constexpr float UpArrowPressedV = 0.90625f;
constexpr float DownArrowV = 0.5f;
// The thumb, 2 further in and 2 narrower (0x6b8818), with top and bottom edges of one point.
constexpr double ThumbInset = 2.0;
constexpr float ThumbDu = 0.6875f;
constexpr float ThumbDv = 0.03125f;
constexpr float ThumbV = 0.03125f;
constexpr float ThumbBottomV = 0.0625f;
// 0x46f020: the chosen one with a white veil of 0.5 and the text in black; the others in 0xffc8dce6.
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color ChosenVeil{1.0f, 1.0f, 1.0f, 0.5f};
constexpr std::uint32_t ChosenTextColor = 0xff000000u;
constexpr std::uint32_t RowTextColor = 0xffc8dce6u;
// 0x46f119..0x46f141: the jewel's name (0x7741644 and 0x7741770 in Text.bmd), "%s,  %d" (0x6c7afc) with (level + 1)
// · 10, in a 300-byte buffer (0x46f10d).
constexpr std::size_t BlessName = 1806;
constexpr std::size_t SoulName = 1807;
constexpr std::int32_t JewelsPerLevel = 10;
constexpr std::size_t RowRoom = 300;

// _ftol (0x69fd3c): truncates toward 0.
std::int32_t Truncate(double value)
{
    return static_cast<std::int32_t>(value);
}

using Math::F32;

float F32(std::int32_t value)
{
    return static_cast<float>(value);
}

std::int32_t Count(const Npc::JewelBundleList& list)
{
    return static_cast<std::int32_t>(list.rows.size());
}

// 0x4839c0, vtable + 0x58: chooses `row`, without going past the count.
void Select(Npc::JewelBundleList& list, std::int32_t row)
{
    list.selection = row;
    if (Count(list) < list.selection)
        list.selection = Count(list);
}

// 0x4839f0, vtable + 0x5c: with a chosen one, `step` higher, without going past the count. Without rows, none.
void SelectNext(Npc::JewelBundleList& list, std::int32_t step)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection += step;
    if (list.selection > Count(list))
        list.selection = Count(list);
}

// 0x483a50, vtable + 0x60: with a chosen one, `step` lower, down to 1. Without rows, none.
void SelectPrevious(Npc::JewelBundleList& list, std::int32_t step)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return;
    }
    if (list.selection == 0)
        return;
    list.selection -= step;
    if (list.selection < 1)
        list.selection = 1;
}

// 0x4833e0, vtable + 0x34: the first visible row moves down `rows` (up when negative), between 0 and the count minus
// the visible ones. With fewer rows than the visible ones (unsigned), nothing.
void Scroll(Npc::JewelBundleList& list, std::int32_t rows)
{
    if (static_cast<std::uint32_t>(Count(list)) < static_cast<std::uint32_t>(list.shownRows))
        return;
    list.scroll -= rows;
    if (list.scroll < 0)
    {
        list.scroll = 0;
        return;
    }
    if (Count(list) - list.shownRows < list.scroll)
        list.scroll = Count(list) - list.shownRows;
}

// 0x46f2c0, vtable + 0x98: the y of row `row` from the first visible one, from bottom to top.
std::int32_t RowY(const Npc::JewelBundleList& list, std::int32_t row)
{
    const std::int32_t shown = list.shownRows < Count(list) ? list.shownRows : Count(list);
    return (shown - row) * RowHeight - list.height - RowsBottom + list.y;
}

// 0x46f1a0 and 0x46f020: a row's width, ftol(width − thumb width + 1).
double RowWidth(const Npc::JewelBundleList& list)
{
    return static_cast<double>(list.width) - static_cast<double>(list.thumbWidth) + 1.0;
}

// 0x4834a0, vtable + 0x28: one message from the queue. 9 and 0xe never reach this list.
void HandleTheMessage(Npc::JewelBundleList& list, std::int32_t message)
{
    // +0xbc is 1 (the constructor).
    if ((message != NextMessage && message != PreviousMessage) || list.rows.empty())
        return;
    if (message == NextMessage)
        SelectNext(list, 1);
    else
        SelectPrevious(list, 1);
    if (static_cast<std::uint32_t>(list.shownRows) >= static_cast<std::uint32_t>(Count(list)))
        return;
    // With no chosen one it is −1: the first visible one goes back to 0 (07 Q-C1153).
    const std::int32_t chosen = list.selection - 1;
    if (list.scroll + list.shownRows <= chosen)
        Scroll(list, list.scroll - (chosen - list.shownRows + 1));
    else if (chosen < list.scroll)
        Scroll(list, list.scroll - chosen);
}

// GetAsyncKeyState(vk) with the high byte at 0x80: the key is down. The read clears the low bit (Q-C45).
bool ReadTheKey(const Input::InputState& input, Key key)
{
    static_cast<void>(input.TakePress(key));
    return input.IsDown(key);
}

// 0x483bc0: a keyboard arrow down. At 0 it sends the message and starts the count, up to 5 it increments it and from
// 6 it sends on every step.
void RepeatTheKey(Npc::JewelBundleList& list, std::int32_t message)
{
    if (list.keyRepeat == 0)
    {
        list.messages.push_back(message);
        list.keyRepeat = 1;
    }
    else if (list.keyRepeat < KeyRepeatSteps)
        ++list.keyRepeat;
    else
        list.messages.push_back(message);
}

// 0x483bc0: an arrow or the track with the button held. At 0 it scrolls (the arrows, with the sound) and starts the
// count, up to 15 it increments it (the list has no parent, +0x44 at 0) and from 16 it scrolls on every step.
void Repeat(Npc::JewelBundleList& list, Kernel::WorldRuntime& runtime, std::int32_t& counter, std::int32_t rows,
            bool sound)
{
    if (counter == 0)
    {
        Scroll(list, rows);
        if (sound)
            runtime.RequestSound(ArrowSound); // (CL10)
        counter = 1;
    }
    else if (counter < RepeatSteps)
        ++counter;
    else
        Scroll(list, rows);
}

// 0x483bc0 with the click over the list: the bar's arrows and, with more rows than visible (or as many), the thumb
// and the track. The thumb is not grabbed: 0x402af0 left the mouse grabbed by the list (07 Q-C1151).
void PressTheBar(const Hud::GameUiContext& context, Npc::JewelBundleList& list, Kernel::WorldRuntime& runtime)
{
    const auto in = [&](std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
    { return Chat::MouseInRect(context.mouseX, context.mouseY, x, y, width, height, TopLeftRect); };
    const std::int32_t inset = list.barMode == 1 ? BarModeInset : 0;
    const std::int32_t margin = list.barMode != 1 ? BarModeInset : 0;
    const std::int32_t arrowX = list.x + inset - ArrowInset + list.width;
    if (in(arrowX, margin - list.height + list.y, ArrowSize, ArrowSize))
        Repeat(list, runtime, list.arrowRepeat, -1, true);
    if (in(arrowX, list.y - (margin + ArrowSize), ArrowSize, ArrowSize))
        Repeat(list, runtime, list.arrowRepeat, 1, true);
    if (list.shownRows > Count(list))
        return;
    const std::int32_t barX = list.x + inset - TrackInset + list.width;
    const std::int32_t thumbWidth = Truncate(list.thumbWidth);
    if (in(barX, Truncate(list.thumbY), thumbWidth, Truncate(list.thumbHeight)))
        return;
    if (in(barX, Truncate(list.barTop), thumbWidth,
           Truncate(static_cast<double>(list.thumbY) - static_cast<double>(list.barTop))))
        Repeat(list, runtime, list.trackRepeat, -list.shownRows, false);
    else if (in(barX, Truncate(static_cast<double>(list.thumbHeight) + static_cast<double>(list.thumbY)), thumbWidth,
                Truncate(static_cast<double>(list.barBottom) - static_cast<double>(list.thumbY) -
                         static_cast<double>(list.thumbHeight))))
        Repeat(list, runtime, list.trackRepeat, list.shownRows, false);
}

// 0x4845d0 (vtable + 0x74) and 0x483bc0's row with 0x46f1a0 (vtable + 0x88), from the first visible one up to
// `shownRows` rows or the end (0x5762ebc is always 0). The click chooses and flips +0; the left double click chooses
// and returns the row for 0x402a90 if its level is not 0xff and its cell is in [1, 0x3f].
std::optional<Npc::JewelBundleList::Row> HoverTheRows(const Hud::GameUiContext& context, Npc::JewelBundleList& list,
                                                      bool& click, bool& rightReleased)
{
    std::optional<Npc::JewelBundleList::Row> asked{};
    const std::int32_t width = Truncate(RowWidth(list));
    auto at = static_cast<std::size_t>(list.scroll);
    for (std::int32_t row = 0; row < list.shownRows && at < list.rows.size(); ++row, ++at)
    {
        if (!Chat::MouseInRect(context.mouseX, context.mouseY, list.x, RowY(list, row) - RowRise, width, RowHeight,
                               TopLeftRect))
            continue;
        if (click)
        {
            Select(list, list.scroll + 1 + row);
            // (+0 + 1) % 2 with C's sign.
            list.rows[at].flag = (list.rows[at].flag + 1) % 2;
            click = false;
        }
        if (rightReleased)
        {
            Select(list, list.scroll + 1 + row);
            // The chosen one has just landed on a row: 0x46f27e does not read the end.
            const std::optional<Npc::JewelBundleList::Row> chosen = ChosenBundle(list);
            if (chosen && chosen->level != 0xff && chosen->cell > 0 && chosen->cell < AskedCells)
                asked = chosen;
            rightReleased = false;
        }
    }
    return asked;
}

// 0x621b60(win_scrollbar, x, y, w, h, u, v, du, dv, 1, 1, 0): scaled, with whatever color is set.
void Quad(Render::SpriteBatch& sprites, float x, float y, float width, float height, float u, float v, float du,
          float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{ScrollbarSlot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = u,
                                      .v = v,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// 0x491960(index, alpha).
void Palette(Render::SpriteBatch& sprites, std::int32_t index, float alpha)
{
    if (const std::optional<Color> color = Ui::OldWindowPalette(index, alpha))
        sprites.SetColor(*color);
}

// 0x459ec0: glColor4f(1, 1, 1, 1) and glEnable(GL_TEXTURE_2D).
void WhiteWithTexture(Render::SpriteBatch& sprites)
{
    sprites.SetColor(White);
    sprites.EnableTexture();
}

// 0x46ebe0, vtable + 0x7c: the background, the bar (0x484490, which it saves for the step), the arrows (flipped while
// clicked, 0x7d366d4) and the track with the thumb; with fewer rows than visible, the thumb fills the track.
void DrawTheFrame(Render::SpriteBatch& sprites, Npc::JewelBundleList& list, bool click, Ui::Point mouse)
{
    const auto in = [&](std::int32_t x, std::int32_t y)
    { return Chat::MouseInRect(mouse.x, mouse.y, x, y, ArrowSize, ArrowSize, TopLeftRect); };
    sprites.SetBlend(Render::BlendMode::Alpha);
    Palette(sprites, FramePalette, FrameAlpha);
    sprites.ColorQuad(F32(list.x - 1), F32(list.y - list.height - 1), F32(list.width + 1), F32(list.height + 2));
    WhiteWithTexture(sprites);
    const Chat::OldListBar bar =
        Chat::PlaceTheListBar(list.y, list.height, list.barMode, list.shownRows, Count(list), list.scroll);
    list.barTop = bar.barTop;
    list.barBottom = bar.barBottom;
    list.thumbY = bar.thumbY;
    list.thumbHeight = bar.thumbHeight;

    const std::int32_t right = list.width + list.x;
    const float arrowX = F32(right - static_cast<std::int32_t>(ArrowRight));
    if (click && in(right - ArrowHitRight, list.y - list.height - 1))
        Quad(sprites, arrowX, F32(list.y - list.height - 1), ArrowQuad, ArrowQuad, ArrowUv, UpArrowPressedV, -ArrowUv,
             -ArrowDv);
    else
        Quad(sprites, arrowX, F32(list.y - list.height - 1), ArrowQuad, ArrowQuad, 0.0f, UpArrowV, ArrowUv, ArrowDv);
    const float downY = F32(list.y - static_cast<std::int32_t>(ArrowRight));
    if (click && in(right - ArrowHitRight, list.y - ArrowHitRight))
        Quad(sprites, arrowX, downY, ArrowQuad, ArrowQuad, ArrowUv, DownArrowV, -ArrowUv, -ArrowDv);
    else
        Quad(sprites, arrowX, downY, ArrowQuad, ArrowQuad, 0.0f, DownArrowV, ArrowUv, ArrowDv);

    sprites.SetBlend(Render::BlendMode::Alpha);
    Palette(sprites, TrackPalette, 1.0f);
    const float trackHeight = F32(static_cast<double>(list.barBottom) - static_cast<double>(list.barTop));
    sprites.ColorQuad(F32(static_cast<double>(right) - static_cast<double>(list.thumbWidth) + 1.0), list.barTop, 1.0f,
                      trackHeight);
    sprites.ColorQuad(F32(right), list.barTop, 1.0f, trackHeight);
    WhiteWithTexture(sprites);

    const float thumbX = F32(static_cast<double>(right) - static_cast<double>(list.thumbWidth) + ThumbInset);
    const float thumbWidth = F32(static_cast<double>(list.thumbWidth) - ThumbInset);
    float top = list.thumbY;
    float height = list.thumbHeight;
    if (Count(list) < list.shownRows)
    {
        top = list.barTop;
        height = trackHeight;
    }
    Quad(sprites, thumbX, top, thumbWidth, height, 0.0f, ThumbV, ThumbDu, ThumbDv);
    Quad(sprites, thumbX, top, thumbWidth, 1.0f, 0.0f, 0.0f, ThumbDu, ThumbDv);
    Quad(sprites, thumbX, F32(static_cast<double>(list.thumbHeight) + static_cast<double>(top) - 1.0), thumbWidth, 1.0f,
         0.0f, ThumbBottomV, ThumbDu, ThumbDv);
}

// 0x46f020, vtable + 0x80: row `row` from the first visible one, with the chosen one's veil, and its text with
// 0x465950 in the DC's font. Leaves blending off (0x620980).
Core::Expected<void, std::string> DrawTheRow(Hud::GameHudContext& context, const Npc::JewelBundleList& list,
                                             std::int32_t row, const Npc::JewelBundleList::Row& entry,
                                             std::uint8_t kind)
{
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    sprites.SetBlend(Render::BlendMode::Alpha);
    if (list.selection == list.scroll + 1 + row)
    {
        sprites.SetColor(ChosenVeil);
        sprites.ColorQuad(F32(list.x), F32(RowY(list, row) - RowRise), F32(RowWidth(list)), F32(RowHeight));
        sprites.SetColor(White);
        hud.textColors.text = ChosenTextColor;
    }
    else
        hud.textColors.text = RowTextColor;
    sprites.EnableTexture();
    hud.textColors.background = 0;
    const std::size_t index = kind == 0 ? BlessName : SoulName;
    const std::optional<std::string_view> name = Ui::TextAt(context.texts, index);
    if (!name)
        return Core::Unexpected(std::format("0x46f152: sin el texto {} de Text.bmd", index));
    std::string text = std::format("{},  {}", *name, (static_cast<std::int8_t>(entry.level) + 1) * JewelsPerLevel);
    if (text.size() >= RowRoom)
        return Core::Unexpected(std::format("0x46f152: el sprintf escribe {} bytes en un buffer de {} y pisa la pila",
                                            text.size() + 1, RowRoom));
    context.text.Draw(list.x + RowTextX, RowY(list, row), text,
                      Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace

void ClearTheBundles(Npc::JewelBundleList& list)
{
    list.rows.clear();
    // 0x483610 (+0x48) with 0, over no row.
    Select(list, 0);
    list.scroll = 0;
}

void AddTheBundle(Npc::JewelBundleList& list, std::int32_t cell, std::uint8_t level)
{
    if (cell < 0 || cell > LastCell)
        return;
    if (std::ranges::any_of(list.rows, [cell](const Npc::JewelBundleList::Row& row) { return row.cell == cell; }))
        return;
    list.rows.push_back(Npc::JewelBundleList::Row{.flag = 0, .cell = cell, .level = level});
    // 0x4842b0 (+0x6c) clips to 150 rows (+0xfc): with the cells of [0, 0x40] unrepeated, it never goes past 65.
    Select(list, 0);
    if (list.shownRows < Count(list))
        ++list.scroll;
    if (list.shownRows <= Count(list) && Count(list) - list.scroll < list.shownRows)
        list.scroll = Count(list) - list.shownRows;
    // 0x46e4c0: the STL sort with 0x46e4a0 (by cell, less); the cells do not repeat.
    std::ranges::sort(list.rows, {}, &Npc::JewelBundleList::Row::cell);
}

std::optional<Npc::JewelBundleList::Row> ChosenBundle(Npc::JewelBundleList& list)
{
    if (list.rows.empty())
    {
        list.selection = 0;
        return std::nullopt;
    }
    if (list.selection < 1 || list.selection > Count(list))
        return std::nullopt;
    return list.rows[static_cast<std::size_t>(list.selection - 1)];
}

BundleListStep StepTheBundleList(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Npc::JewelBundleList& list = runtime.npcWindows.jewels.bundles;
    bool& click = runtime.mouse.pendingClick;
    ui.capture = list.id;
    // 0x45aa8c..0x45aab1: the queue, in order. vtable + 0x24 (0x45abc0) only handles 8, and + 0x1c (0x403160) does
    // nothing.
    while (!list.messages.empty())
    {
        const std::int32_t message = list.messages.front();
        list.messages.erase(list.messages.begin());
        HandleTheMessage(list, message);
    }
    const bool over =
        Chat::MouseInRect(context.mouseX, context.mouseY, list.x, list.y, list.width, list.height, BottomLeftRect);
    // 0x45aad0..0x45abb3: with the mouse grabbed by the list itself, the list always goes on. With the left button
    // down, 0x490aa0 over the manager (0x5763514) does not find it: it is not registered there (*inferred*, like the
    // chat log).
    if (over && ui.hovered == 0)
        ui.hovered = list.id;

    // 0x483bc0, vtable + 0x20.
    if (ui.focusedList == list.id)
    {
        static_cast<void>(ReadTheKey(context.input, Key::Left));
        static_cast<void>(ReadTheKey(context.input, Key::Right));
        const bool up = ReadTheKey(context.input, Key::Up);
        if (up)
            RepeatTheKey(list, NextMessage);
        if (ReadTheKey(context.input, Key::Down))
            RepeatTheKey(list, PreviousMessage);
        else if (!up)
            list.keyRepeat = 0;
    }
    // 0x484650, vtable + 0x78: the mouse over the list.
    if (over)
    {
        if (ui.wheel != 0)
        {
            Scroll(list, ui.wheel * -WheelRows);
            ui.wheel = 0;
        }
        if (!click)
        {
            list.arrowRepeat = 0;
            list.trackRepeat = 0;
        }
        else
        {
            ui.focusedList = list.id;
            PressTheBar(context, list, runtime);
        }
        ui.mouseOnUi = 1;
    }
    BundleListStep result{};
    result.asked = HoverTheRows(context, list, click, ui.doubleClick);
    ui.capture = 0;
    return result;
}

Core::Expected<void, std::string> DrawTheBundleList(Hud::GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // A MuRet intermediate draw writes neither the corner nor the bar the step reads: it draws a copy.
    Npc::JewelBundleList projected{};
    if (context.projecting)
        projected = runtime.npcWindows.jewels.bundles;
    Npc::JewelBundleList& list = context.projecting ? projected : runtime.npcWindows.jewels.bundles;
    // 0x45a9d0(x, y).
    list.x = x;
    list.y = y;
    Render::SpriteBatch& sprites = context.sprites;
    DrawTheFrame(sprites, list, runtime.mouse.pendingClick, context.mouse);
    // 0x4832ef..0x483312: glColor3f(1, 1, 1) and the normal font in the DC.
    sprites.SetColor(White);
    context.hud.dcFont = Render::Text::FontKind::Normal;
    auto at = static_cast<std::size_t>(list.scroll);
    for (std::int32_t row = 0; row < list.shownRows && at < list.rows.size(); ++row, ++at)
    {
        if (auto drawn = DrawTheRow(context, list, row, list.rows[at], runtime.npcWindows.jewels.kind);
            !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace Client::ChaosMix
