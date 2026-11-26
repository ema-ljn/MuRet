#include "Protocol/Items/Inventory.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Items
{
namespace
{
using MoveResult = Network::Field<std::uint8_t, 3>;
using MoveSlot = Network::Field<std::uint8_t, 4>;
using MoveItem = Network::Field<ItemData, 5>;
constexpr Network::PacketLayout ItemMoveResultLayout{
    {Network::PacketType::C1, ItemMoveOpcode, std::nullopt}, MoveItem::End, 0x4d1400};

using DeleteSlot = Network::Field<std::uint8_t, 3>;
using DeleteEndsTheWait = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout ItemDeleteLayout{
    {Network::PacketType::C1, ItemDeleteOpcode, std::nullopt}, DeleteEndsTheWait::End, 0x4c5ec0};

using RefillPlace = Network::Field<std::uint8_t, 3>;
using Life = Network::Field<std::uint16_t, 4, std::endian::big>;
constexpr Network::PacketLayout LifeRefillLayout{
    {Network::PacketType::C1, LifeOpcode, std::nullopt}, RefillPlace::End, 0x4d3510};

using Mana = Network::Field<std::uint16_t, 4, std::endian::big>;
using Bp = Network::Field<std::uint16_t, 6, std::endian::big>;
constexpr Network::PacketLayout ManaRefillLayout{
    {Network::PacketType::C1, ManaOpcode, std::nullopt}, Mana::End, 0x4d3820};

using DurabilitySlot = Network::Field<std::uint8_t, 3>;
using DurabilityValue = Network::Field<std::uint8_t, 4>;
using DurabilityEndsTheWait = Network::Field<std::uint8_t, 5>;
constexpr Network::PacketLayout DurabilityLayout{
    {Network::PacketType::C1, DurabilityOpcode, std::nullopt}, DurabilityEndsTheWait::End, 0x4d3cc0};

// 0x4e316e: nobody reads pkt[6].
using ChangeKey = Network::Field<std::uint16_t, 4, std::endian::big>;
using ChangeEquipment = Network::Field<std::array<std::byte, EquipmentChangeSize>, 7>;
constexpr Network::PacketLayout EquipmentChangeLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::EquipmentChangeSubcode}, ChangeEquipment::End, 0x4e316e};

using PutSlot = Network::Field<std::uint8_t, 4>;
using PutItem = Network::Field<ItemData, 5>;
constexpr Network::PacketLayout ItemPutLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::ItemPutSubcode}, PutItem::End, 0x4e31a3};

// 0x4d36eb..0x4d3719 and 0x4d3a0c..0x4d3a5d: the places that carry the values instead of naming a slot.
bool CarriesTheValues(std::uint8_t place)
{
    return place == MaximumPlace || place == CurrentPlace;
}
} // namespace

// 0x449d80, and inline 0x5ca46c.. and 0x5bf294..: the kind, the opcode and twelve bytes, with the XOR32 from pkt[3].
Outgoing WriteItemMove(const ItemMove& move)
{
    return EncryptedC1(Network::PacketWriter::C1(ItemMoveOpcode)
                           .PutU8(move.sourceFlag)
                           .PutU8(move.sourceSlot)
                           .PutBytes(move.item)
                           .PutU8(move.targetFlag)
                           .PutU8(move.targetSlot));
}

Outgoing WriteClearItemMoveReport()
{
    return EncryptedC1(
        Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(ClearItemMoveHead));
}

Core::Expected<ItemMoveResult, Network::ProtocolError> ReadItemMoveResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, ItemMoveResultLayout, [](Network::LayoutReader& reader)
        { return ItemMoveResult{reader.Read<MoveResult>(), reader.Read<MoveSlot>(), reader.Read<MoveItem>()}; });
}

Core::Expected<ItemDelete, Network::ProtocolError> ReadItemDelete(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ItemDeleteLayout, [](Network::LayoutReader& reader)
                               { return ItemDelete{reader.Read<DeleteSlot>(), reader.Read<DeleteEndsTheWait>()}; });
}

Core::Expected<LifeRefill, Network::ProtocolError> ReadLifeRefill(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, LifeRefillLayout,
        [](Network::LayoutReader& reader)
        {
            const std::uint8_t place = reader.Read<RefillPlace>();
            return LifeRefill{.place = place, .life = CarriesTheValues(place) ? reader.Read<Life>() : std::uint16_t{0}};
        });
}

Core::Expected<ManaRefill, Network::ProtocolError> ReadManaRefill(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, ManaRefillLayout,
        [](Network::LayoutReader& reader)
        {
            const std::uint8_t place = reader.Read<RefillPlace>();
            const std::uint16_t mana = reader.Read<Mana>();
            return ManaRefill{
                .place = place, .mana = mana, .bp = CarriesTheValues(place) ? reader.Read<Bp>() : std::uint16_t{0}};
        });
}

Core::Expected<Durability, Network::ProtocolError> ReadDurability(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DurabilityLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return Durability{reader.Read<DurabilitySlot>(), reader.Read<DurabilityValue>(),
                                                     reader.Read<DurabilityEndsTheWait>()};
                               });
}

Core::Expected<EquipmentChange, Network::ProtocolError> ReadEquipmentChange(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, EquipmentChangeLayout, [](Network::LayoutReader& reader)
                               { return EquipmentChange{reader.Read<ChangeKey>(), reader.Read<ChangeEquipment>()}; });
}

Core::Expected<ItemPut, Network::ProtocolError> ReadItemPut(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ItemPutLayout, [](Network::LayoutReader& reader)
                               { return ItemPut{reader.Read<PutSlot>(), reader.Read<PutItem>()}; });
}
} // namespace Client::Protocol::Items
