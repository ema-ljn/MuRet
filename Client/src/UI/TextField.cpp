#include "UI/TextField.h"

#include "Core/Windows1252BestFit.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace Client::UI
{
namespace
{
// El ASCII imprimible, del espacio a la tilde.
constexpr unsigned char FirstPrintable = 0x20;
constexpr unsigned char LastPrintable = 0x7e;
constexpr char Star = '*';
// Las letras de Latin-1 que toma el campo de ese juego: de U+00A0 a U+00FF, en UTF-8 con C2 o C3
// delante.
constexpr unsigned char Latin1Lead = 0xc2;
constexpr unsigned char Latin1HighLead = 0xc3;
constexpr unsigned char ContinuationMask = 0xc0;
constexpr unsigned char ContinuationBits = 0x80;
constexpr unsigned LeadBits = 0x1f;
constexpr unsigned TrailBits = 0x3f;
constexpr unsigned FirstLatin1Letter = 0xa0;

bool IsPrintableAscii(char byte)
{
    const auto value = static_cast<unsigned char>(byte);
    return value >= FirstPrintable && value <= LastPrintable;
}
} // namespace

TextField::TextField(std::size_t maxBytes, bool masked, Charset charset)
    : m_maxBytes(maxBytes), m_masked(masked), m_charset(charset)
{
}

void TextField::Type(std::string_view utf8)
{
    if (m_charset == Charset::Windows1252Xp)
    {
        const auto encoded = Core::EncodeWindows1252Xp(utf8);
        if (encoded.has_value())
        {
            InsertBytes(*encoded);
        }
        return;
    }

    const auto insert = [this](char byte) { InsertBytes(std::string_view(&byte, 1)); };

    for (std::size_t index = 0; index < utf8.size(); ++index)
    {
        const char byte = utf8[index];
        if (IsPrintableAscii(byte))
        {
            insert(byte);
            continue;
        }
        // U+00A0 a U+00FF en UTF-8: C2 A0 a C3 BF. Lo demás, de a un byte, no entra.
        const auto lead = static_cast<unsigned char>(byte);
        if (m_charset != Charset::Latin1 || (lead != Latin1Lead && lead != Latin1HighLead) || index + 1 >= utf8.size())
        {
            continue;
        }
        const auto trail = static_cast<unsigned char>(utf8[index + 1]);
        if ((trail & ContinuationMask) != ContinuationBits)
        {
            continue;
        }
        const unsigned value = ((lead & LeadBits) << 6U) | (trail & TrailBits);
        if (value >= FirstLatin1Letter)
        {
            insert(static_cast<char>(value));
        }
        ++index;
    }
}

void TextField::InsertBytes(std::string_view bytes)
{
    if (bytes.empty())
        return;

    const std::size_t erasedBytes = HasSelection() ? SelectionEnd() - SelectionBegin() : 0;
    const std::size_t retainedBytes = m_value.size() - erasedBytes;
    if (retainedBytes >= m_maxBytes)
        return;

    const std::size_t insertBytes = std::min(bytes.size(), m_maxBytes - retainedBytes);
    if (insertBytes == 0)
        return;
    const std::string inserted(bytes.substr(0, insertBytes));

    if (HasSelection())
        EraseSelection();
    m_value.insert(m_cursor, inserted);
    m_cursor += inserted.size();
    m_anchor = m_cursor;
    m_allSelected = false;
}

void TextField::PasteUTF8(std::string_view utf8)
{
    const auto encoded = Core::EncodeWindows1252Xp(utf8);
    if (!encoded.has_value())
    {
        return;
    }

    std::size_t prefixLength = 0;
    while (prefixLength < encoded->size() && (*encoded)[prefixLength] != '\r' && (*encoded)[prefixLength] != '\0')
    {
        ++prefixLength;
    }
    if (HasSelection())
    {
        EraseSelection();
    }
    m_allSelected = false;
    InsertBytes(std::string_view(*encoded).substr(0, prefixLength));
}

void TextField::Backspace()
{
    if (HasSelection() || m_allSelected)
    {
        if (HasSelection())
        {
            EraseSelection();
        }
        else
        {
            Clear();
        }
        return;
    }
    if (m_cursor == 0)
    {
        return;
    }
    m_value.erase(m_cursor - 1, 1);
    --m_cursor;
    m_anchor = m_cursor;
    m_allSelected = false;
}

void TextField::Delete()
{
    if (HasSelection())
    {
        EraseSelection();
        return;
    }
    if (m_cursor >= m_value.size())
    {
        return;
    }
    m_value.erase(m_cursor, 1);
}

void TextField::Clear()
{
    m_value.clear();
    m_anchor = 0;
    m_cursor = 0;
    m_allSelected = false;
}

void TextField::SetText(std::string_view raw)
{
    m_value.assign(raw);
    m_anchor = m_value.size();
    m_cursor = m_value.size();
    m_allSelected = false;
}

void TextField::SelectAll()
{
    m_anchor = 0;
    m_cursor = m_value.size();
    m_allSelected = true;
}

void TextField::Deselect()
{
    MoveEnd();
}

bool TextField::AllSelected() const
{
    return m_allSelected;
}

std::size_t TextField::Cursor() const
{
    return m_cursor;
}

std::size_t TextField::SelectionBegin() const
{
    return std::min(m_anchor, m_cursor);
}

std::size_t TextField::SelectionEnd() const
{
    return std::max(m_anchor, m_cursor);
}

bool TextField::HasSelection() const
{
    return m_anchor != m_cursor;
}

void TextField::SetCursor(std::size_t position, bool extend)
{
    position = std::min(position, m_value.size());
    if (!extend)
    {
        m_anchor = position;
    }
    else if (!HasSelection())
    {
        m_anchor = m_cursor;
    }
    m_cursor = position;
    m_allSelected = HasSelection() && SelectionBegin() == 0 && SelectionEnd() == m_value.size();
}

void TextField::Select(std::size_t anchor, std::size_t caret)
{
    m_anchor = std::min(anchor, m_value.size());
    m_cursor = std::min(caret, m_value.size());
    m_allSelected = HasSelection() && SelectionBegin() == 0 && SelectionEnd() == m_value.size();
}

void TextField::MoveLeft(bool extend)
{
    SetCursor(m_cursor == 0 ? 0 : m_cursor - 1, extend);
}

void TextField::MoveRight(bool extend)
{
    SetCursor(m_cursor < m_value.size() ? m_cursor + 1 : m_value.size(), extend);
}

void TextField::MoveHome(bool extend)
{
    SetCursor(0, extend);
}

void TextField::MoveEnd(bool extend)
{
    SetCursor(m_value.size(), extend);
}

void TextField::EraseSelection()
{
    const std::size_t begin = SelectionBegin();
    m_value.erase(begin, SelectionEnd() - begin);
    m_anchor = begin;
    m_cursor = begin;
    m_allSelected = false;
}

std::string_view TextField::Value() const&
{
    return m_value;
}

std::string_view TextField::Selected() const&
{
    return std::string_view(m_value).substr(SelectionBegin(), SelectionEnd() - SelectionBegin());
}

std::string TextField::Shown() const
{
    return m_masked ? std::string(m_value.size(), Star) : m_value;
}

std::size_t TextField::Size() const
{
    return m_value.size();
}

bool TextField::Masked() const
{
    return m_masked;
}
} // namespace Client::UI
