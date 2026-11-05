#include "Protocol/Character/CharacterDelete.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/CharacterName.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Protocol::Character
{
namespace
{
constexpr PacketKind DeleteKind{Network::PacketType::C1, CharacterOpcode, DeleteSubcode};
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
    return Xor32C1(
        Network::PacketWriter::C1(CharacterOpcode).PutU8(DeleteSubcode).PutBytes(NameUpToItsEnd(name)).PutBytes(code));
}

Core::Expected<std::uint8_t, ProtocolError> ReadCharacterDeleteResult(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, DeleteKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    const std::optional<std::uint8_t> result = fields.Value().ReadU8();
    if (!result)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    return *result;
}
} // namespace Client::Protocol::Character
