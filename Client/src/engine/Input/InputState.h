#pragma once

#include "EngineApi.h"
#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"
#include "Input/TextInputEvent.h"

#include <bitset>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Built from events, not polled: a press and its release between two reads are still seen (SDL_GetKeyboardState misses
// them, and so does the original, Q-C45). Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El teclado y el mouse:
// en el paso y en el frame (P-C08)". The edges (WasPressed, WasReleased) count from the last ConsumeEdges, which
// GameLoop calls after the frame's first step, so one step sees a press, as in the original where the first reader
// clears the low bit; a frame without steps (the original has none) waits for the next. Shift, Ctrl and Alt are the OR
// of their sides, like VK_SHIFT, with the OR's edges. A logical KeyDown or KeyUp is ignored: Platform sends sides, and
// a lone logical event would not say which side is still down.
namespace Client::Input
{
class ENGINE_API InputState
{
public:
    // A KeyDown with the key already down is not an edge, nor a KeyUp with it up (same for buttons). FocusLost releases
    // everything down, with its WasReleased, and clears the WasPressed, the wheel and the text: the next step sees no
    // click from before the alt-tab (like MuMain, Winmain.cpp). A non-finite wheel (NaN, infinity) is dropped: it would
    // poison the sum until ConsumeEdges.
    void Apply(const InputEvent& event);

    [[nodiscard]] bool IsDown(Key key) const;
    // Since the last ConsumeEdges.
    [[nodiscard]] bool WasPressed(Key key) const;
    [[nodiscard]] bool WasReleased(Key key) const;
    // GetAsyncKeyState(vk) == 0x8001: WasPressed, if no reader took it earlier in this step. The original clears the
    // low bit on each read (Q-C45): of those reading the same press with 0x8001, the first sees it. The new UI
    // (0x65fc30) reads before the scene step, so the scene does not see an Esc a window took (CL8, milestone 8).
    // ConsumeEdges clears the mark. const like the original's query, and it marks the key as taken.
    [[nodiscard]] bool TakePress(Key key) const;
    [[nodiscard]] bool IsDown(MouseButton button) const;
    [[nodiscard]] bool WasPressed(MouseButton button) const;
    [[nodiscard]] bool WasReleased(MouseButton button) const;
    // The game WndProc's mouse state, separate from the button's physical edges.
    [[nodiscard]] const MouseMessageState& MouseMessages() const;
    // One update of 0x7d36849 from the new messages: true with DBL, false with the next message. Published to the game
    // once; with no messages, what its consumer left stays. ConsumeEdges does not clear it before it is published.
    // const like TakePress: it only marks that update as taken.
    [[nodiscard]] std::optional<bool> TakeDoubleClickMessage() const;
    // Summed since the last ConsumeEdges.
    [[nodiscard]] float Wheel() const;
    // The last wheel event's notches since the last ConsumeEdges, or 0 with none: what the last WM_MOUSEWHEEL leaves in
    // the original's global, which does not sum.
    [[nodiscard]] float LastWheel() const;
    // In framebuffer pixels (InputEvent.h); 0 until the first Motion.
    [[nodiscard]] float MouseX() const;
    [[nodiscard]] float MouseY() const;
    // Typed since the last ConsumeEdges, UTF-8 and in order: each Text's text and, in its place, a BackspaceChar,
    // TabChar or EnterChar per press of those keys (InputEvent.h), so what comes before and after a Tab or Backspace in
    // the same step does not mix. Valid until the next Apply or ConsumeEdges. FocusLost clears it, like the presses.
    [[nodiscard]] std::string_view Text() const;
    // In arrival order. The view is valid until the next Apply or ConsumeEdges; a frame without a step does not consume
    // it.
    [[nodiscard]] std::span<const TextInputEvent> TextInputEvents() const;

    // GameLoop calls it after a step: clears the edges, the wheel and the text, not what is down.
    void ConsumeEdges();

private:
    void Press(Key key);
    void Release(Key key);
    void Press(MouseButton button);
    void Release(MouseButton button);
    void ReleaseEverything();
    // Sets a side's logical key (Shift for ShiftLeft) to the OR of its two sides, with its edge if it changed. Does
    // nothing for a key that is not a side.
    void UpdateLogical(Key side);

    std::bitset<KeyCount> m_keysDown;
    std::bitset<KeyCount> m_keysPressed;
    std::bitset<KeyCount> m_keysReleased;
    // The presses a TakePress already took in this step.
    mutable std::bitset<KeyCount> m_keysTaken;
    std::bitset<MouseButtonCount> m_buttonsDown;
    std::bitset<MouseButtonCount> m_buttonsPressed;
    std::bitset<MouseButtonCount> m_buttonsReleased;
    MouseMessageState m_mouseMessages;
    mutable std::optional<bool> m_doubleClickMessage;
    float m_wheel = 0.0f;
    float m_lastWheel = 0.0f;
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    std::string m_text;
    std::vector<TextInputEvent> m_textInputEvents;
};
} // namespace Client::Input
