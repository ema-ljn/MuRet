#include "App/TeleportHistory.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace Client::App
{
void TeleportHistory::Record(std::span<const std::byte> packet)
{
    if (m_size == TeleportHistoryCapacity)
    {
        std::move(m_entries.begin() + 1, m_entries.end(), m_entries.begin());
        --m_size;
    }
    if (packet.size() > MaxTeleportHistoryPacketSize)
    {
        return;
    }
    m_entries[m_size].assign(packet.begin(), packet.end());
    ++m_size;
}

std::span<const std::vector<std::byte>> TeleportHistory::Entries() const
{
    return std::span(m_entries).first(m_size);
}
} // namespace Client::App
