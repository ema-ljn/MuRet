#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <optional>
#include <vector>

// The floor's items, picking up, dropping and using (04, "Los ítems del piso"): the 20 and the 21 (C2), the 22, the
// 23 and the 2C that arrive, and the C1 22, 23 and 26 that leave with the XOR32 and as C3s. The lengths are the ones
// the client reads, which is the oracle: the GameServer 0.97 sends 5-byte items and a one-byte 2C (07 Q-C1240).
namespace Client::Protocol::Items
{
inline constexpr Network::Opcode UseItemOpcode = LifeOpcode;
// 0x4d0cc0: pkt[3] at 0xff picks up nothing, at 0xfe it is the zen and at 0xfd an item that does not go to the grid.
inline constexpr std::uint8_t NothingPickedUp = 0xff;
inline constexpr std::uint8_t ZenPickedUp = 0xfe;
inline constexpr std::uint8_t PickedUpElsewhere = 0xfd;
// 0x4d10d0: pkt[3] at 0, the item was not dropped.
inline constexpr std::uint8_t DropRejected = 0;

// An entry of the 20 (0x4d0ba0, 11 bytes each from pkt[5]): the key with bit 15 for "just dropped", the cell and the
// item's seven bytes.
struct FloorItemEntry
{
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    Items::ItemData item{};
};

// The 20, C2 (0x4d0ba0): the count at pkt[4] and the entries.
struct FloorItemsCreate
{
    std::vector<FloorItemEntry> items;
};

[[nodiscard]] Core::Expected<FloorItemsCreate, Network::ProtocolError>
ReadFloorItemsCreate(const Network::Packet& packet);

// The 21, C2 (0x4e3849, in the dispatcher): the count at pkt[4] and the keys, 2 bytes each, high first.
struct FloorItemsDelete
{
    std::vector<std::uint16_t> keys;
};

[[nodiscard]] Core::Expected<FloorItemsDelete, Network::ProtocolError>
ReadFloorItemsDelete(const Network::Packet& packet);

// The 22 that arrives, C1 (C3 on the wire; 0x4d0cc0): pkt[3] the result; with 0xfe, the zen at pkt[4..7] high byte
// first; with a place (or 0xfd), the item's seven bytes from pkt[4]. With 0xff it reads nothing else.
struct PickUpResult
{
    std::uint8_t result = 0;
    std::uint32_t zen = 0;
    Items::ItemData item{};
};

[[nodiscard]] Core::Expected<PickUpResult, Network::ProtocolError> ReadPickUpResult(const Network::Packet& packet);

// The 23 that arrives, C1 (0x4d10d0): pkt[3] the result (0 rejected) and pkt[4] the place dropped. With 0 it does
// not read pkt[4] (0x4d10de): the place stays 0 if it did not arrive.
struct DropResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<DropResult, Network::ProtocolError> ReadDropResult(const Network::Packet& packet);

// The 2C, C1 (0x4de500): pkt[3] the result (0 to 5, 0x10, 0x21, 0x25 and 0x26), pkt[4] the points and pkt[5] the stat
// (0 the energy, 1 the vitality, 2 the agility, 3 the strength and 4 the command).
struct StatFruit
{
    std::uint8_t result = 0;
    std::uint8_t points = 0;
    std::uint8_t stat = 0;
};

[[nodiscard]] Core::Expected<StatFruit, Network::ProtocolError> ReadStatFruit(const Network::Packet& packet);

// A 22, 23 or 2C shorter than what the client reads (the GameServer 0.97, 07 Q-C1240): the original reads what was
// left in its receive buffer, which MuRet does not make up, but some writes happen on all its paths. 0x4d0cc0 leaves
// the pick-up request (0x6c82e8) at −1 (0x4d10b9) and 0x4de500 the use's wait (0x5767954) at 0 (0x4de7e4 and the
// other five exits, 0x4deb0e).
struct ShortPickUpResult
{
};

struct ShortStatFruit
{
};

// 0x4d10d0 leaves the dropped place (0x6c82ec) at −1 on all its paths (0x4d13d1 and 0x4d13e5) and, with pkt[3] other
// than 0, the cursor empty (0x7879fbc at 0, 0x4d13c7), on all of them too. `result` is pkt[3], if it arrived.
struct ShortDropResult
{
    std::optional<std::uint8_t> result;
};

// The 22 with 0xfe that ReadPickUpResult rejects as short: the GameServer 0.97's GCMoneySend (C3 08 22 FE, the zen on
// buying, selling or repairing, 07 Q-C1240). 0x4d0cc0 reads the zen from pkt[4..7], high byte first, which did arrive,
// and the sound's type from pkt[4..10] (0x4d1051), which did not.
struct ShortZenPickUp
{
    std::uint32_t zen = 0;
};

// WrongPacket if pkt[3] is not 0xfe, and TooShort without the zen's four bytes.
[[nodiscard]] Core::Expected<ShortZenPickUp, Network::ProtocolError> ReadShortZenPickUp(const Network::Packet& packet);

// The 23 that ReadDropResult rejects as short: only pkt[3], if it arrived.
[[nodiscard]] Core::Expected<ShortDropResult, Network::ProtocolError>
ReadShortDropResult(const Network::Packet& packet);

// The C1 05 22 (0x566755, case 1 of 0x5650c0): the floor item's index, high byte first.
[[nodiscard]] Outgoing WritePickUp(std::uint16_t index);

// The C1 06 23 (0x5dfe34..0x5e06b5): the cell it is dropped on and the place the cursor's item came from.
struct Drop
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Outgoing WriteDrop(const Drop& drop);

// The C1 06 26 (the quick slots of 0x59ff10, 0x5a2c01.. and 0x5a3623.., and the grid, 0x5c5937..): the item's place,
// the target (0 from the slots and the grid) and [0x78abb26], which two dialogs of 0x623e70 set (0x628bdc and
// 0x6293b5). The GameServer 0.97 reads 5 bytes (PMSG_USEITEM without the third; 07 Q-C1241).
struct UseItem
{
    std::uint8_t slot = 0;
    std::uint8_t target = 0;
    std::uint8_t kind = 0;
};

[[nodiscard]] Outgoing WriteUseItem(const UseItem& use);
} // namespace Client::Protocol::Items
