#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Client::Render::Text
{
class ITextMeasure;
}

// The same subclass 0x467b10 and the Western Windows EDIT for the chat, the dialogs, friends and letters.
namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform
namespace Client::Ui
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

// Tab with a parent can ask for the focus in +0x98; EDITs without a parent forward it to the game from GameChat.
void ApplyKeyToTheEdit(OldEdit& edit, OldEditKey key, bool shift, bool ctrl, Platform::IClipboard* clipboard = nullptr,
                       const Render::Text::ITextMeasure* measure = nullptr,
                       const std::function<void(std::int32_t)>& focusNext = {});
void SetTheEditComposition(OldEdit& edit, std::string_view text, std::int32_t start, std::int32_t length);
} // namespace Client::Ui
