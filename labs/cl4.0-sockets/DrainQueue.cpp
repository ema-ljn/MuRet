#include "DrainQueue.h"

#include <algorithm>
#include <cstddef>

namespace Labs
{
bool DrainQueue::Push(std::span<const std::byte> bytes)
{
    if (bytes.size() > DrainQueueCapacity - Pending())
    {
        return false;
    }
    if (m_begin > 0)
    {
        // Lo pendiente al principio. std::copy puede correr bytes para atrás sobre el mismo
        // buffer, pero no con el destino adentro del origen: con m_begin en 0 serían el mismo.
        std::copy(m_buffer.begin() + static_cast<std::ptrdiff_t>(m_begin),
                  m_buffer.begin() + static_cast<std::ptrdiff_t>(m_end), m_buffer.begin());
        m_end -= m_begin;
        m_begin = 0;
    }
    std::copy(bytes.begin(), bytes.end(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_end));
    m_end += bytes.size();
    return true;
}

IoStatus DrainQueue::Flush(const SendFunction& send)
{
    while (Pending() > 0)
    {
        const std::span<const std::byte> pending = std::span<const std::byte>(m_buffer).subspan(m_begin, Pending());
        const IoResult result = send(pending);
        if (result.status != IoStatus::Done)
        {
            return result.status;
        }
        if (result.bytes == 0)
        {
            // No tomó nada sin decir por qué: se prueba en el próximo Flush, sin girar acá.
            return IoStatus::TryLater;
        }
        // Un send no toma más de lo pedido; si uno lo dijera, el cursor no pasa de m_end.
        m_begin += std::min(result.bytes, Pending());
    }
    m_begin = 0;
    m_end = 0;
    return IoStatus::Done;
}

std::size_t DrainQueue::Pending() const
{
    return m_end - m_begin;
}
} // namespace Labs
