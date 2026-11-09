#include "App/TeleportHistory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::App
{
void TeleportHistory::Append(std::span<const std::byte> packet)
{
    if (m_count > 9)
    {
        m_count = 9;
        for (std::size_t i = 1; i < m_entries.size(); ++i)
        {
            std::copy_n(m_entries[i].bytes.begin(), m_entries[i].length, m_entries[i - 1].bytes.begin());
            m_entries[i - 1].length = m_entries[i].length;
        }
    }
    if (packet.size() > m_entries[0].bytes.size())
        return;
    auto& destination = m_entries[m_count];
    destination.length = static_cast<std::uint32_t>(packet.size());
    for (std::size_t i = 0; i < packet.size(); ++i)
        destination.bytes[i] = packet[i];
    ++m_count;
}

std::span<const TeleportHistoryEntry> TeleportHistory::Entries() const
{
    return std::span(m_entries).first(m_count);
}
} // namespace Client::App
