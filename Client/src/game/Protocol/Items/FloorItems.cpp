#include "Protocol/Items/FloorItems.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Items
{
namespace
{
using ListCount = Network::Field<std::uint8_t, 4>;
constexpr std::size_t KeySize = 2;
// 0x4d0ba0: the key (2), the cell (2) and the item (7).
constexpr std::size_t CreateEntrySize = KeySize + 2 + Items::ItemDataSize;
constexpr Network::PacketLayout FloorItemsCreateLayout{
    {Network::PacketType::C2, FloorItemCreateOpcode, std::nullopt}, ListCount::End, 0x4d0ba0};
constexpr Network::PacketLayout FloorItemsDeleteLayout{
    {Network::PacketType::C2, FloorItemDeleteOpcode, std::nullopt}, ListCount::End, 0x4e3849};

using PickUpCode = Network::Field<std::uint8_t, 3>;
using PickedItem = Network::Field<Items::ItemData, 4>;
// 0x4d0d9b..0x4d0dc7: pkt[4] is the high byte.
using PickedZen = Network::Field<std::uint32_t, 4, std::endian::big>;
constexpr Network::PacketLayout PickUpLayout{
    {Network::PacketType::C1, PickUpOpcode, std::nullopt}, PickUpCode::End, 0x4d0cc0};

using DropCode = Network::Field<std::uint8_t, 3>;
using DropSlot = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout DropLayout{
    {Network::PacketType::C1, DropOpcode, std::nullopt}, DropCode::End, 0x4d10d0};
constexpr Network::PacketLayout ShortDropLayout{
    {Network::PacketType::C1, DropOpcode, std::nullopt}, DropCode::Offset, 0x4d10d0};

// 0x4de50f..0x4de51a: the three bytes, always, before the switch.
using FruitResult = Network::Field<std::uint8_t, 3>;
using FruitPoints = Network::Field<std::uint8_t, 4>;
using FruitStat = Network::Field<std::uint8_t, 5>;
constexpr Network::PacketLayout StatFruitLayout{
    {Network::PacketType::C1, StatFruitOpcode, std::nullopt}, FruitStat::End, 0x4de500};

std::uint16_t ReadKey(std::span<const std::byte, KeySize> bytes)
{
    return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(bytes[0]) << 8) |
                                      std::to_integer<std::uint16_t>(bytes[1]));
}

FloorItemEntry ReadCreateEntry(std::span<const std::byte, CreateEntrySize> bytes)
{
    FloorItemEntry entry{.key = ReadKey(bytes.first<KeySize>()),
                         .x = std::to_integer<std::uint8_t>(bytes[KeySize]),
                         .y = std::to_integer<std::uint8_t>(bytes[KeySize + 1])};
    std::ranges::copy(bytes.subspan<KeySize + 2, Items::ItemDataSize>(), entry.item.begin());
    return entry;
}
} // namespace

Core::Expected<FloorItemsCreate, Network::ProtocolError> ReadFloorItemsCreate(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, FloorItemsCreateLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return FloorItemsCreate{Network::ReadList<CreateEntrySize>(
                                       reader, ListCount::End, reader.Read<ListCount>(), ReadCreateEntry)};
                               });
}

Core::Expected<FloorItemsDelete, Network::ProtocolError> ReadFloorItemsDelete(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, FloorItemsDeleteLayout,
                               [](Network::LayoutReader& reader) {
                                   return FloorItemsDelete{Network::ReadList<KeySize>(
                                       reader, ListCount::End, reader.Read<ListCount>(), ReadKey)};
                               });
}

// 0x4d1051: the sound reads the type from the bytes at pkt[4] with the zen too, so the original reads seven bytes in
// any case.
Core::Expected<PickUpResult, Network::ProtocolError> ReadPickUpResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PickUpLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   PickUpResult pickUp{.result = reader.Read<PickUpCode>()};
                                   if (pickUp.result == NothingPickedUp)
                                   {
                                       return pickUp;
                                   }
                                   pickUp.item = reader.Read<PickedItem>();
                                   if (pickUp.result == ZenPickedUp)
                                   {
                                       pickUp.zen = reader.Read<PickedZen>();
                                   }
                                   return pickUp;
                               });
}

// 0x4d10d9..0x4d10e4: pkt[4] only with pkt[3] other than 0.
Core::Expected<DropResult, Network::ProtocolError> ReadDropResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DropLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   const std::uint8_t result = reader.Read<DropCode>();
                                   const bool slotRead = result != DropRejected || reader.Size() >= DropSlot::End;
                                   return DropResult{.result = result,
                                                     .slot = slotRead ? reader.Read<DropSlot>() : std::uint8_t{0}};
                               });
}

Core::Expected<ShortZenPickUp, Network::ProtocolError> ReadShortZenPickUp(const Network::Packet& packet)
{
    Network::ProtocolResult<Network::LayoutReader> opened = Network::LayoutReader::Open(packet, PickUpLayout);
    if (!opened.HasValue())
    {
        return Core::Unexpected(opened.Error());
    }
    Network::LayoutReader& reader = opened.Value();
    if (reader.Read<PickUpCode>() != ZenPickedUp)
    {
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
    return reader.Finish(ShortZenPickUp{reader.Read<PickedZen>()});
}

Core::Expected<ShortDropResult, Network::ProtocolError> ReadShortDropResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ShortDropLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return ShortDropResult{.result = reader.Size() >= DropCode::End
                                                                        ? std::optional{reader.Read<DropCode>()}
                                                                        : std::nullopt};
                               });
}

Core::Expected<StatFruit, Network::ProtocolError> ReadStatFruit(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, StatFruitLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return StatFruit{.result = reader.Read<FruitResult>(),
                                                    .points = reader.Read<FruitPoints>(),
                                                    .stat = reader.Read<FruitStat>()};
                               });
}

Outgoing WritePickUp(std::uint16_t index)
{
    return EncryptedC1(Network::PacketWriter::C1(PickUpOpcode).PutU16Be(index));
}

Outgoing WriteDrop(const Drop& drop)
{
    return EncryptedC1(Network::PacketWriter::C1(DropOpcode).PutU8(drop.x).PutU8(drop.y).PutU8(drop.slot));
}

Outgoing WriteUseItem(const UseItem& use)
{
    return EncryptedC1(Network::PacketWriter::C1(UseItemOpcode).PutU8(use.slot).PutU8(use.target).PutU8(use.kind));
}
} // namespace Client::Protocol::Items
