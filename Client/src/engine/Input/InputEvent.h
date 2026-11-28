#pragma once

#include "Input/Key.h"

#include <cstdint>
#include <string>

// Each kind uses only its fields: KeyDown and KeyUp the key, ButtonDown/Up/DoubleClick the button, Wheel the notches,
// Motion the position, Text the text and Composition the provisional text with its range. An unfilled InputEvent does
// nothing: it is the KeyDown of Key::Count, which is not a key.
namespace Client::Input
{
struct InputEvent
{
    enum class Kind : std::uint8_t
    {
        KeyDown,
        KeyUp,
        ButtonDown,
        ButtonUp,
        Wheel,
        Motion,
        FocusLost,
        // From SDL_EVENT_TEXT_INPUT, with the layout, Shift and dead keys already applied, which the UI text field need
        // not know.
        Text,
        // The IME's provisional UTF-8 text and its range in code points; -1 means not given. Also the empty text that
        // cancels a composition.
        Composition,
        // The game's WM_LBUTTONDBLCLK: SDL's second left DOWN, without another ordinary click.
        ButtonDoubleClick,
        // A platform event with no other input: it still clears the previous message's double click (0x4b3444).
        Message,
    };

    Kind kind = Kind::KeyDown;
    // A physical key: Platform sends the sides of Shift, Ctrl and Alt, not the keys themselves (Key.h).
    Key key = Key::Count;
    MouseButton button{};
    // Positive forward. From `integer_y`, with the sign of `direction` (InputEvents.h).
    float wheel = 0.0f;
    // In framebuffer pixels: Platform multiplies the window's by its density. Unclamped (Q-C47): the original has two
    // positions. The new UI's (0x640940) clamps it to the window size in unscaled pixels (+0x28 and +0x2c, which
    // 0x4ba84a passes from [0x6d5d60] and [0x6d5d64]); Ui::MouseInPixels does that. The WndProc's scales it to 640 x
    // 480 unclamped (0x4b358f..0x4b35c8), left for CL8. With the mouse captured SDL can give negatives or values past
    // the edge.
    float x = 0.0f;
    float y = 0.0f;
    // Copied in UTF-8 with no ANSI conversion: SDL's lives only until the next SDL_PollEvent. Last so the designated
    // initializers do not change.
    std::string text{};
    // SDL_KEYDOWN repeat: not a new physical edge. Backspace, Tab and Enter use Text.
    bool repeat = false;
    // The SDL_TextEditingEvent range in UTF-8 code points, not bytes; -1 means not given.
    std::int32_t compositionStart = 0;
    std::int32_t compositionLength = 0;
};

// The bytes Backspace and Tab put in the typed text, where they arrived: the WM_CHAR 8 and 9 Windows gives a field's
// EDIT (0x467b10), with keyboard repeat. SDL does not send them as text (SDL_EVENT_TEXT_INPUT carries only printable
// ones): InputState::Apply adds the press and Platform sends the repeat as a Text (InputEvents.h).
inline constexpr char BackspaceChar = '\b';
inline constexpr char TabChar = '\t';
// And Enter's: the WM_CHAR 0xd the game window handles at 0x4b50b0 (the chat input, CL8 milestone 3), which the EDIT
// subclass (0x467b10) passes on to it. Fields that do not take it drop it, like any control byte.
inline constexpr char EnterChar = '\r';
} // namespace Client::Input
