#pragma once

#include "Chat/ChatInput.h"
#include "Ui/OldEditInput.h" // IWYU pragma: export

#include <cstdint>
#include <string_view>

namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform

namespace Client::Render::Text
{
class ITextMeasure;
} // namespace Client::Render::Text

namespace Client::Ui
{
struct OldWindowManager;

// The old windows' EDIT with the focus (SetFocus of 0x4682a0): what the player types (WM_CHAR), delete and Enter,
// which the subclass 0x467b10 sends to its parent as the 7 if the window list is not armed (+0x150).
[[nodiscard]] bool HasAnOldEditFocused(const OldWindowManager& manager, ChatFocus focus);
void TypeInTheOldEdit(OldWindowManager& manager, std::string_view typed);
void BackspaceInTheOldEdit(OldWindowManager& manager);
// With +0xc4 at 1, the arrows, Home, End, PgUp, PgDn, delete and paste are the multiline EDIT's (Ui/OldEditLines.h,
// with `measure` for the lines). Tab passes the focus to the EDIT at +0x98 (0x467d07) and leaves the arrows' list on
// it (`focusedList`, 0x5762ec0).
void ApplyKeyToTheOldEdit(OldWindowManager& manager, OldEditKey key, bool shift, bool ctrl,
                          Platform::IClipboard* clipboard = nullptr,
                          const Render::Text::ITextMeasure* measure = nullptr, std::int32_t* focusedList = nullptr);
void SetTheOldEditComposition(OldWindowManager& manager, std::string_view text, std::int32_t start,
                              std::int32_t length);
void PressEnterInTheOldEdit(OldWindowManager& manager);
} // namespace Client::Ui
