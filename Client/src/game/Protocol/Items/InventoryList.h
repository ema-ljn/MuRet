#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The inventory, F3 10 (04, "El F3 10, el inventario").
namespace Client::Protocol::Items
{
inline constexpr std::size_t InventoryItemDataSize = 7;
inline constexpr std::size_t InventoryEntrySize = InventoryItemDataSize + sizeof(std::uint8_t);

struct InventoryEntry
{
    std::uint8_t slot = 0;
    std::array<std::byte, InventoryItemDataSize> item{};
};

struct InventoryList
{
    std::vector<InventoryEntry> items;
};

// The C2 MuRet's transport rebuilds from the C4, whatever WasDecrypted says. The list keeps the packet's order, its
// duplicates and any slot, and ignores what follows the counted entries. TooShort without the count or a whole entry,
// with no partial result (ADR 0010). Only the wire: 0x4c55a0's initialization, its admission flag and the placing are
// Items::ApplyInventory's and Items::PlaceInventoryItem's (06, "El F3 10, el inventario").
[[nodiscard]] Core::Expected<InventoryList, Network::ProtocolError> ReadInventoryList(const Network::Packet& packet);
} // namespace Client::Protocol::Items
