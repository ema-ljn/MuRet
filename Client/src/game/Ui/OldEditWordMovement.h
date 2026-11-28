#pragma once

#include <cstddef>
#include <string_view>

// Los límites de palabra observados en EDIT de XP (Client/docs/research/11-edit-win32.md, «SetText y atajos»).
namespace Client::Ui::OldEditWordMovement
{
[[nodiscard]] std::size_t CursorAfterCtrlLeft(std::string_view text, std::size_t cursor);
[[nodiscard]] std::size_t CursorAfterCtrlRight(std::string_view text, std::size_t cursor);
} // namespace Client::Ui::OldEditWordMovement
