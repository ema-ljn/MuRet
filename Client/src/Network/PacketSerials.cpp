#include "Network/PacketSerials.h"

namespace Client::Network
{

// Un std::uint8_t que suma 1 da la vuelta de 255 a 0: la suma es en int, y la conversión de vuelta
// a un tipo sin signo es módulo 256.
std::uint8_t PacketSerials::TakeSend()
{
    const std::uint8_t serial = send;
    ++send;
    return serial;
}

SerialCheck PacketSerials::Receive(std::uint8_t received)
{
    const SerialCheck check = received == receive ? SerialCheck::InOrder : SerialCheck::Resynced;
    receive = static_cast<std::uint8_t>(received + 1);
    return check;
}

void PacketSerials::SkipReceive()
{
    ++receive;
}

void PacketSerials::Reset()
{
    send = 0;
    receive = 0;
}

} // namespace Client::Network
