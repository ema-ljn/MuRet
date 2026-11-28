#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Client::Render::Text
{
class ITextMeasure;
}

// La misma subclase 0x467b10 y el EDIT occidental de Windows para el chat, los diálogos, amigos y cartas.
namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform
namespace Client::UI
{
struct OldEdit;

enum class OldEditKey : std::uint8_t
{
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Up,
    Down,
    Tab,
    Copy,
    Cut,
    Paste,
    CtrlA,
    PageUp,
    PageDown,
};

// Tab con padre puede pedir el foco en +0x98; los EDIT sin padre lo reenvían al juego desde GameChat.
void ApplyKeyToTheEdit(OldEdit& edit, OldEditKey key, bool shift, bool ctrl, Platform::IClipboard* clipboard = nullptr,
                       const Render::Text::ITextMeasure* measure = nullptr,
                       const std::function<void(std::int32_t)>& focusNext = {});
void SetTheEditComposition(OldEdit& edit, std::string_view text, std::int32_t start, std::int32_t length);
} // namespace Client::UI
