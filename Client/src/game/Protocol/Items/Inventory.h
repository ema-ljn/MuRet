#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Moving and equipping items (04, "Mover y equipar ítems"): the 24 both ways, the 28, the 2A and the F3 13 and F3 14;
// and the life and the mana (04, "La vida y el maná"): the 26 and the 27. Network hands over the C3s that arrive
// already decrypted as C1s, with WasDecrypted; the scene decides when to send the 24.
namespace Client::Protocol::Items
{
// An item's seven bytes, as in the F3 10 (Protocol::Items::InventoryItemDataSize).
inline constexpr std::size_t ItemDataSize = 7;
// 0x4d167f: the byte that closes the report of an unencrypted 24.
inline constexpr std::uint8_t ClearItemMoveHead = 0x16;
// 0x4d1400: pkt[3] at 0xff, the move rejected.
inline constexpr std::uint8_t ItemMoveRejected = 0xff;
// 0x4c5ec0: pkt[3] at 0xff deletes nothing.
inline constexpr std::uint8_t NoItemToDelete = 0xff;
// 0x5139f0 reads 17 bytes from pkt[7] (nobody looks at byte 6).
inline constexpr std::size_t EquipmentChangeSize = 17;
// pkt[3] of the 26 and the 27 when it is not a slot: 0xfd (only the 26, 0x4d35dc) ends 0x5767954's wait, 0xfe
// carries the maximums and 0xff the current values. Any other value is the place of the potion used.
inline constexpr std::uint8_t EndTheWaitPlace = 0xfd;
inline constexpr std::uint8_t MaximumPlace = 0xfe;
inline constexpr std::uint8_t CurrentPlace = 0xff;

using ItemData = std::array<std::byte, ItemDataSize>;

// The client's 24, C1 0E 24 with the XOR32 and as a C3 (0x449d80, 0x5ca250 and 0x5bec50): from where (the grid, 0 the
// inventory, and the place), the seven bytes of the cursor's item and where to.
struct ItemMove
{
    std::uint8_t sourceFlag = 0;
    std::uint8_t sourceSlot = 0;
    ItemData item{};
    std::uint8_t targetFlag = 0;
    std::uint8_t targetSlot = 0;
};

[[nodiscard]] Outgoing WriteItemMove(const ItemMove& move);

// The 24's reply, C3 0C 24 decrypted (0x4d1400): pkt[3] is the target grid (0 the inventory) or 0xff if the server
// rejects it, pkt[4] the place and pkt[5..11] the item.
struct ItemMoveResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
    ItemData item{};
};

[[nodiscard]] Core::Expected<ItemMoveResult, Network::ProtocolError> ReadItemMoveResult(const Network::Packet& packet);

// The report of a 24 that did not arrive encrypted (0x4d1400, 0x4d166b..0x4d1690): C1 06 F1 03 00 16 with the XOR32
// and as a C3, without reading the packet. It does not release the lock.
[[nodiscard]] Outgoing WriteClearItemMoveReport();

// The 28, C1 05 28 (0x4c5ec0): the place deleted (0xff none) and, other than 0, the end of 0x5767954's wait.
struct ItemDelete
{
    std::uint8_t slot = 0;
    std::uint8_t endsTheWait = 0;
};

[[nodiscard]] Core::Expected<ItemDelete, Network::ProtocolError> ReadItemDelete(const Network::Packet& packet);

// The 26, C1 07 26 (0x4d3510; PMSG_REFILL in the GameServer): the place at pkt[3] and, with 0xfe or 0xff, the life at
// pkt[4] (high) and pkt[5] (low). With another place the original does not read the life, and it never reads pkt[6]
// (the Flag, always 0).
struct LifeRefill
{
    std::uint8_t place = 0;
    std::uint16_t life = 0;
};

[[nodiscard]] Core::Expected<LifeRefill, Network::ProtocolError> ReadLifeRefill(const Network::Packet& packet);

// The 27, C1 08 27 (0x4d3820; PMSG_MANASEND): the place at pkt[3], the mana at pkt[4..5] and, with 0xfe or 0xff, the
// BP at pkt[6..7], both high byte first. With another place the original does not read the BP.
struct ManaRefill
{
    std::uint8_t place = 0;
    std::uint16_t mana = 0;
    std::uint16_t bp = 0;
};

[[nodiscard]] Core::Expected<ManaRefill, Network::ProtocolError> ReadManaRefill(const Network::Packet& packet);

// The 2A, C1 06 2A (0x4d3cc0): the place, the new durability and, other than 0, the end of the wait.
struct Durability
{
    std::uint8_t slot = 0;
    std::uint8_t durability = 0;
    std::uint8_t endsTheWait = 0;
};

[[nodiscard]] Core::Expected<Durability, Network::ProtocolError> ReadDurability(const Network::Packet& packet);

// The F3 13 (0x4e316e): a character's key (pkt[4] high, pkt[5] low) and its equipment from pkt[7], the character
// list's, which 0x5139f0 reads.
struct EquipmentChange
{
    std::uint16_t key = 0;
    std::array<std::byte, EquipmentChangeSize> equipment{};
};

[[nodiscard]] Core::Expected<EquipmentChange, Network::ProtocolError>
ReadEquipmentChange(const Network::Packet& packet);

// The F3 14 (0x4e31a3): an item the server puts in the inventory, with the place at pkt[4] and the item from pkt[5].
struct ItemPut
{
    std::uint8_t slot = 0;
    ItemData item{};
};

[[nodiscard]] Core::Expected<ItemPut, Network::ProtocolError> ReadItemPut(const Network::Packet& packet);
} // namespace Client::Protocol::Items
