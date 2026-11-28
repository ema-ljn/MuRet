#pragma once

#include "Core/Expected.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Render::Frame
{
class ITextDrawer;
}

// The old windows' draw (CL8 milestone 7; 06 § HUD y ventanas): 0x490390 (0x637673) draws the manager's from bottom
// to top, and 0x637680 the window list, always, with its vtable + 0x14 (0x492500, the frame). All in 640 × 480.
namespace Client::Ui
{
// The Interface\ slots that 0x61ccb0 loads: the title bar (win_titlebar.jpg), the button (win_button.tga), the bar's
// buttons (win_size.jpg) and the resize grip (win_resize.tga).
inline constexpr std::uint16_t WindowTitleSlot = 0x645;
inline constexpr std::uint16_t WindowButtonSlot = 0x646;
inline constexpr std::uint16_t WindowTitleButtonsSlot = 0x647;
inline constexpr std::uint16_t WindowResizeSlot = 0x648;

// What the draw uses and leaves: the batch, the texts, the DC's font (0x57634f4) and 0x465950's colors.
struct OldWindowDrawContext
{
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    // 0x6c8e5c and 0x6c8e64.
    Render::Text::TextColors& textColors;
    // GetAsyncKeyState of the arrows (0x25..0x28) at draw time: the EDITs' subclass (0x467b10) checks it with the
    // WM_GETTEXT that 0x468320 sends them.
    bool arrowHeld = false;
};

// What the draw reads from the game.
struct OldWindowDrawView
{
    // 0x7d3682c and 0x7d36828, in 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x7d366d4: the click's edge (0x492460).
    bool leftClick = false;
    // 0x659290(1): panel 1, the friends window. Without it, all are drawn as if they were not the top one.
    bool friendsOpen = false;
    // The friends window's Text.bmd texts (the column headers and the state button). Without them, empty.
    const OldWindowTexts* texts = nullptr;
    // 0x5762ec0: the list that takes the arrow keys; its chosen row is lighter (0x461cd0).
    std::int32_t focusedList = 0;
    // 0x468320's GetFocus(): the id of the EDIT with the Windows focus (OldWindowManager::focusedEdit with
    // ChatFocus::OldWindow), or 0.
    std::int32_t focusedEdit = 0;
    // 0x6c7a88: with 1, the chat log in its window (ChatModes::placement); the rows of the chat with a friend
    // (0x460550) and of the letter (0x465770) drop the background of a text with a black background.
    std::int32_t chatPlacement = 0;
    // The letters' portrait's vtable + 0x14 (0x497c10), which the game gives (Messenger::DrawTheLetterPortrait): the
    // character belongs to Scenes. Empty, a portrait in view fails.
    std::function<Core::Expected<void, std::string>(OldPortraitPart&)> portrait{};
    // 0x45aa50 also runs functional messages when it re-places: they are this pass's step services.
    const OldWindowGlobals* globals = nullptr;
    const OldWindowStepView* stepView = nullptr;
};

// 0x491960(index, alpha): the glColor4ub of each of the windows' 19 colors, with the alpha at ftol(alpha · 255) (the
// 3, the shadow, at ftol(alpha · 179)). With another index, none.
[[nodiscard]] std::optional<Render::Color> OldWindowPalette(std::int32_t index, float alpha);

// 0x45b2e0, the button's vtable + 0x14, for those that draw their buttons on their own (the panels' dialog,
// 0x660830): win_button.tga in the mouse's gray and the text centered; then the opaque blend, unless disabled.
void DrawTheOldButton(const OldWindowDrawContext& context, const OldButton& button, const OldWindowDrawView& view);

// 0x490390: the windows in view, from bottom to top. Then the projection (glMatrixMode, glLoadIdentity and
// gluOrtho2D(0, W, 0, H)), which in MuRet is the batch's, and +0x40 at 1.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheOldWindows(OldWindowManager& manager, const OldWindowDrawContext& context, const OldWindowDrawView& view);

// 0x637680: the window list's frame (0x492500), even when hidden.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheWindowList(OldWindowManager& manager, const OldWindowDrawContext& context, const OldWindowDrawView& view);
} // namespace Client::Ui
