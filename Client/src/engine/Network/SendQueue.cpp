#include "Network/SendQueue.h"

#include "Core/Expected.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

namespace Client::Network
{
std::optional<SendQueueError> SendQueue::Push(std::span<const std::byte> bytes)
{
    if (bytes.size() > MaxPendingSend - Pending())
    {
        return SendQueueError::Full;
    }
    if (bytes.size() > MaxPendingSend - m_end)
    {
        Compact();
    }
    std::ranges::copy(bytes, m_buffer.begin() + static_cast<std::ptrdiff_t>(m_end));
    m_end += bytes.size();
    return std::nullopt;
}

std::optional<SendQueueError> SendQueue::Flush(ISocket& socket)
{
    while (Pending() > 0)
    {
        const std::span<const std::byte> pending = std::span(m_buffer).subspan(m_sent, Pending());
        const Core::Expected<std::size_t, SocketError> taken = socket.Send(pending);
        if (!taken.HasValue())
        {
            m_lastSocketError = taken.Error();
            return SendQueueError::Socket;
        }
        if (taken.Value() == 0)
        {
            break;
        }
        // Un socket no toma más de lo que se le dio; el min es para que uno roto no corra el
        // cursor afuera del buffer.
        m_sent += std::min(taken.Value(), pending.size());
    }
    if (Pending() == 0)
    {
        Clear();
    }
    return std::nullopt;
}

std::size_t SendQueue::Pending() const
{
    return m_end - m_sent;
}

std::optional<SocketError> SendQueue::LastSocketError() const
{
    return m_lastSocketError;
}

void SendQueue::Clear()
{
    m_sent = 0;
    m_end = 0;
}

// Hacia adelante y sin pisar: el destino empieza antes que lo que se copia. Con el cursor en 0 no
// hay nada que correr (y std::copy no acepta un destino dentro de lo que copia).
void SendQueue::Compact()
{
    if (m_sent == 0)
    {
        return;
    }
    const auto first = m_buffer.begin() + static_cast<std::ptrdiff_t>(m_sent);
    const auto last = m_buffer.begin() + static_cast<std::ptrdiff_t>(m_end);
    std::copy(first, last, m_buffer.begin());
    m_end -= m_sent;
    m_sent = 0;
}
} // namespace Client::Network
