#include "Protocol/Character/CharacterSelect.h"

#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <span>

namespace Client::Protocol::Character
{
Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode).PutU8(SelectSubcode).PutBytes(name));
}
} // namespace Client::Protocol::Character
