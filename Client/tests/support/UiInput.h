#pragma once

#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneTextures.h"
#include "UI/Widget.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

// Lo que arma la entrada de los tests de la UI (CL5.5): el mouse en un punto, un botón o una
// tecla apretados o soltados, y un texto tecleado, como los mandaría Platform. Y los tamaños de
// los buffers de las texturas, que en el cliente da SceneTextures::BufferSize.
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

// Apretar y soltar en el mismo paso, encima de (x, y).
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

// Una tecla apretada y soltada en el mismo paso.
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

// Los buffers de las texturas de un test: los de `sizes`, y 1 × 1 los demás, como BufferSize con
// un slot que no se subió.
inline UI::TextureSizes FakeTextureSizes(std::map<std::uint16_t, Scenes::TextureBufferSize> sizes)
{
    return [sizes = std::move(sizes)](Render::TextureSlot slot)
    {
        const auto found = sizes.find(slot.value);
        return found == sizes.end() ? Scenes::TextureBufferSize{} : found->second;
    };
}
} // namespace Client::Tests
