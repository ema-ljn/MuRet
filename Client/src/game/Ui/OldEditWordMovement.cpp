#include "Ui/OldEditWordMovement.h"

#include <cstddef>
#include <string_view>

namespace Client::UI::OldEditWordMovement
{
namespace
{
bool IsSpaceOrTab(char character)
{
    return character == '\t' || character == ' ';
}

bool IsLeftWordWhitespace(char character)
{
    return IsSpaceOrTab(character) || character == '\n';
}

bool IsRightWordWhitespace(char character)
{
    return IsSpaceOrTab(character) || character == '\r';
}

bool IsWordDelimiter(char character)
{
    return character == '\t' || character == '\r' || character == '\n' || character == ' ';
}
} // namespace

std::size_t CursorAfterCtrlLeft(std::string_view text, std::size_t cursor)
{
    // EDIT distingue CR de LF y salta el espacio previo antes de buscar el límite hacia atrás.
    cursor = cursor > text.size() ? text.size() : cursor;
    while (cursor > 0 && IsLeftWordWhitespace(text[cursor - 1]))
        --cursor;
    while (cursor > 0)
    {
        --cursor;
        if (text[cursor] == '\r')
            return cursor;
        if (IsLeftWordWhitespace(text[cursor]))
            return cursor + 1;
    }
    return cursor;
}

std::size_t CursorAfterCtrlRight(std::string_view text, std::size_t cursor)
{
    // El recorrido hacia adelante trata CR/LF de otra manera que el recorrido inverso del EDIT.
    cursor = cursor > text.size() ? text.size() : cursor;
    if (cursor < text.size() && text[cursor] == '\n')
        return cursor + 1;
    while (cursor < text.size() && IsRightWordWhitespace(text[cursor]))
        ++cursor;
    while (cursor < text.size() && !IsWordDelimiter(text[cursor]))
        ++cursor;
    if (cursor < text.size() && text[cursor] == '\n')
        return cursor + 1;
    while (cursor < text.size() && IsSpaceOrTab(text[cursor]))
        ++cursor;
    return cursor;
}
} // namespace Client::UI::OldEditWordMovement
