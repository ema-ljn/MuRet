#include "UI/TextField.h"

#include <cstddef>
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
    for (std::size_t index = 0; index < utf8.size(); ++index)
    {
        if (!m_allSelected && m_value.size() >= m_maxBytes)
        {
            return;
        }
        const char byte = utf8[index];
        if (IsPrintableAscii(byte))
        {
            ReplaceTheSelection();
            m_value.push_back(byte);
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
            ReplaceTheSelection();
            m_value.push_back(static_cast<char>(value));
        }
        ++index;
    }
}

void TextField::Backspace()
{
    if (m_allSelected)
    {
        Clear();
        return;
    }
    if (!m_value.empty())
    {
        m_value.pop_back();
    }
}

void TextField::Clear()
{
    m_value.clear();
    m_allSelected = false;
}

void TextField::SetText(std::string_view raw)
{
    m_value.assign(raw);
    m_allSelected = false;
}

void TextField::Insert(std::string_view raw)
{
    if (!m_allSelected && m_value.size() + raw.size() > m_maxBytes)
    {
        return;
    }
    if (m_allSelected && raw.size() > m_maxBytes)
    {
        return;
    }
    ReplaceTheSelection();
    m_value.append(raw);
}

void TextField::SelectAll()
{
    m_allSelected = true;
}

void TextField::Deselect()
{
    m_allSelected = false;
}

bool TextField::AllSelected() const
{
    return m_allSelected;
}

void TextField::ReplaceTheSelection()
{
    if (m_allSelected)
    {
        Clear();
    }
}

std::string_view TextField::Value() const&
{
    return m_value;
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
