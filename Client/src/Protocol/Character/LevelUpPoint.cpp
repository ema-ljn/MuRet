#include "Protocol/Character/LevelUpPoint.h"

#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

namespace Client::Protocol::Character
{
Outgoing WriteLevelUpPoint(std::uint8_t stat)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode).PutU8(LevelUpPointSubcode).PutU8(stat));
}
} // namespace Client::Protocol::Character
