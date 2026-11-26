#include "Protocol/FrontEnd/CharacterDelete.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Protocol::FrontEnd
{
namespace
{
using DeleteResult = Network::Field<std::uint8_t, 4>;
// 0x4e2f93: the dispatcher's F3 case reads it itself.
constexpr Network::PacketLayout DeleteResultLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::DeleteSubcode}, DeleteResult::End, 0x4e2f93};
} // namespace

std::array<std::byte, DeleteCodeSize> DeleteCodeFromText(std::string_view text)
{
    std::array<std::byte, DeleteCodeSize> code{};
    std::ranges::transform(text.substr(0, DeleteCodeSize), code.begin(),
                           [](char value) { return static_cast<std::byte>(value); });
    return code;
}

Outgoing WriteCharacterDelete(std::span<const std::byte, NameSize> name,
                              std::span<const std::byte, DeleteCodeSize> code)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::DeleteSubcode)
                       .PutBytes(NameUpToItsEnd(name))
                       .PutBytes(code));
}

Core::Expected<std::uint8_t, Network::ProtocolError> ReadCharacterDeleteResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DeleteResultLayout,
                               [](Network::LayoutReader& reader) { return reader.Read<DeleteResult>(); });
}
} // namespace Client::Protocol::FrontEnd
