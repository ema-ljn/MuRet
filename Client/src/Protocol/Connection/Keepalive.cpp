#include "Protocol/Connection/Keepalive.h"

#include "Network/PacketWriter.h"
#include "Protocol/Connection/Opcodes.h"

namespace Client::Protocol::Connection
{

namespace
{

// Lo que resta cada bit (04 § El flujo de conexión).
constexpr std::uint16_t Adjustment20 = 20;
constexpr std::uint16_t Adjustment25 = 25;

// El byte que sigue al opcode, siempre en 0: en el struct del GameServer (PMSG_CLIENTTIME) es el
// relleno que deja el tick, de 4 bytes, alineado a 4.
constexpr std::uint8_t KeepalivePadding = 0x00;

std::uint16_t AdjustmentFor(std::uint8_t characterFlags)
{
    if ((characterFlags & KeepaliveAdjust20) != 0)
    {
        return Adjustment20;
    }
    if ((characterFlags & KeepaliveAdjust25) != 0)
    {
        return Adjustment25;
    }
    return 0;
}

} // namespace

std::uint16_t KeepaliveSpeed(std::uint16_t speed, std::uint8_t characterFlags)
{
    // La resta se hace en int y vuelve a 16 bits: da la vuelta, como el WORD del original.
    return static_cast<std::uint16_t>(speed - AdjustmentFor(characterFlags));
}

Outgoing WriteKeepalive(const Keepalive& keepalive)
{
    Network::PacketWriter writer = Network::PacketWriter::C1(KeepaliveOpcode);
    writer.PutU8(KeepalivePadding)
        .PutU32Le(keepalive.tick)
        .PutU16Le(keepalive.attackSpeed)
        .PutU16Le(keepalive.magicSpeed);
    return EncryptedC1(writer);
}

} // namespace Client::Protocol::Connection
