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

bool IsPrintableAscii(char byte)
{
    const auto value = static_cast<unsigned char>(byte);
    return value >= FirstPrintable && value <= LastPrintable;
}
} // namespace

TextField::TextField(std::size_t maxBytes, bool masked) : m_maxBytes(maxBytes), m_masked(masked) {}

void TextField::Type(std::string_view utf8)
{
    for (const char byte : utf8)
    {
        if (m_value.size() >= m_maxBytes)
        {
            return;
        }
        if (IsPrintableAscii(byte))
        {
            m_value.push_back(byte);
        }
    }
}

void TextField::Backspace()
{
    if (!m_value.empty())
    {
        m_value.pop_back();
    }
}

void TextField::Clear()
{
    m_value.clear();
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
