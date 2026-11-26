#include "Network/PacketSerials.h"

#include <cstdint>

namespace Client::Network
{
// A std::uint8_t plus 1 wraps from 255 to 0: the sum is in int, and converting back to an unsigned type is modulo
// 256.
std::uint8_t PacketSerials::TakeSend()
{
    const std::uint8_t serial = m_send;
    ++m_send;
    return serial;
}

SerialCheck PacketSerials::Receive(std::uint8_t received)
{
    const SerialCheck check = received == m_receive ? SerialCheck::InOrder : SerialCheck::Resynced;
    m_receive = static_cast<std::uint8_t>(received + 1);
    return check;
}

void PacketSerials::SkipReceive()
{
    ++m_receive;
}

void PacketSerials::Reset()
{
    m_send = 0;
    m_receive = 0;
}
} // namespace Client::Network
