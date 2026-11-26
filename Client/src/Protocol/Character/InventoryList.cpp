#include "Protocol/Character/InventoryList.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Character
{
namespace
{
// C4 rearmado como C2 por el transporte de MuRet (ADR0003), no por 0x4c55a0.
constexpr Network::PacketKind InventoryKind{Network::PacketType::C2, CharacterOpcode, InventoryListSubcode};
} // namespace

Core::Expected<InventoryList, Network::ProtocolError> ReadInventoryList(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, InventoryKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> count = reader.ReadU8();
    if (!count || *count > reader.Remaining() / InventoryEntrySize)
    {
        return Core::Unexpected(Network::ProtocolError::TooShort);
    }
    InventoryList list;
    list.items.reserve(*count);
    // Se comprobó toda la lista antes de reservar: ninguna lectura puede quedar incompleta.
    for (std::size_t index = 0; index < *count; ++index)
    {
        InventoryEntry entry;
        entry.slot = *reader.ReadU8();
        const std::span<const std::byte> item = *reader.ReadBytes(InventoryItemDataSize);
        std::ranges::copy(item, entry.item.begin());
        list.items.push_back(entry);
    }
    return list;
}
} // namespace Client::Protocol::Character
