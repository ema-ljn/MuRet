#include "Protocol/Items/InventoryList.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::Items
{
namespace
{
using EntryCount = Network::Field<std::uint8_t, 5>;
// MuRet's transport rebuilds the C4 as a C2 (ADR 0003), not 0x4c55a0.
constexpr Network::PacketLayout InventoryLayout{
    {Network::PacketType::C2, CharacterOpcode, Character::InventoryListSubcode}, EntryCount::End, 0x4c55a0};

InventoryEntry ReadEntry(std::span<const std::byte, InventoryEntrySize> bytes)
{
    InventoryEntry entry{.slot = std::to_integer<std::uint8_t>(bytes[0])};
    std::ranges::copy(bytes.subspan<1, InventoryItemDataSize>(), entry.item.begin());
    return entry;
}
} // namespace

Core::Expected<InventoryList, Network::ProtocolError> ReadInventoryList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, InventoryLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return InventoryList{Network::ReadList<InventoryEntrySize>(
                                       reader, EntryCount::End, reader.Read<EntryCount>(), ReadEntry)};
                               });
}
} // namespace Client::Protocol::Items
