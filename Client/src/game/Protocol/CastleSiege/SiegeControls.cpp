#include "Protocol/CastleSiege/SiegeControls.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <cstdint>

namespace Client::Protocol::SiegeControls
{
Outgoing WriteWeapon(std::uint16_t key, std::uint8_t choice)
{
    return Xor32C1(Network::PacketWriter::C1(SiegeWeaponOpcode, SiegeWeapon::UseSubcode)
                       .PutU8(static_cast<std::uint8_t>(key >> 8u))
                       .PutU8(static_cast<std::uint8_t>(key))
                       .PutU8(choice));
}
Outgoing WriteGate(std::uint8_t open, std::uint16_t key)
{
    return Xor32C1(Network::PacketWriter::C1(CastleOpcode, SiegeWeapon::UseSubcode)
                       .PutU8(open)
                       .PutU8(static_cast<std::uint8_t>(key >> 8u))
                       .PutU8(static_cast<std::uint8_t>(key)));
}
Outgoing WriteCommand(std::uint8_t row, std::uint8_t x, std::uint8_t y, std::uint8_t command)
{
    return Xor32C1(
        Network::PacketWriter::C1(CastleOpcode, Castle::CommandSubcode).PutU8(row).PutU8(x).PutU8(y).PutU8(command));
}
} // namespace Client::Protocol::SiegeControls
