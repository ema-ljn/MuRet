#pragma once

#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/TextureSlot.h"
#include "Assets/SceneTextures.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

// What builds the input of the UI tests (CL5.5): the mouse at a point, a button or a key pressed or released, and
// typed text, as Platform would send them. And the texture buffer sizes, which SceneTextures::BufferSize gives in the
// client.
namespace Client::Tests
{
inline void MoveMouse(Input::InputState& input, int x, int y)
{
    input.Apply(Input::InputEvent{
        .kind = Input::InputEvent::Kind::Motion, .x = static_cast<float>(x), .y = static_cast<float>(y)});
}

inline void PressLeft(Input::InputState& input)
{
    input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::ButtonDown, .button = Input::MouseButton::Left});
}

inline void ReleaseLeft(Input::InputState& input)
{
    input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::ButtonUp, .button = Input::MouseButton::Left});
}

// Press and release in the same step, over (x, y).
inline void ClickAt(Input::InputState& input, int x, int y)
{
    MoveMouse(input, x, y);
    PressLeft(input);
    ReleaseLeft(input);
}

inline void PressKey(Input::InputState& input, Input::Key key)
{
    input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::KeyDown, .key = key});
}

inline void ReleaseKey(Input::InputState& input, Input::Key key)
{
    input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::KeyUp, .key = key});
}

// Pressed and released in the same step.
inline void TapKey(Input::InputState& input, Input::Key key)
{
    PressKey(input, key);
    ReleaseKey(input, key);
}

inline void TypeText(Input::InputState& input, std::string text)
{
    Input::InputEvent event{.kind = Input::InputEvent::Kind::Text};
    event.text = std::move(text);
    input.Apply(event);
}

// A test's texture buffers: those of `sizes`, and 1 × 1 the rest, like BufferSize with a slot that was not uploaded.
inline Ui::TextureSizes FakeTextureSizes(std::map<std::uint16_t, Assets::TextureBufferSize> sizes)
{
    return [sizes = std::move(sizes)](Render::TextureSlot slot)
    {
        const auto found = sizes.find(slot.value);
        return found == sizes.end() ? Assets::TextureBufferSize{} : found->second;
    };
}
} // namespace Client::Tests
