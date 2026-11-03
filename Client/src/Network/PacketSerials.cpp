#include "Network/PacketSerials.h"

#include <cstdint>

namespace Client::Network
{
// Un std::uint8_t que suma 1 da la vuelta de 255 a 0: la suma es en int, y la conversión de vuelta
// a un tipo sin signo es módulo 256.
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
