#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// El wire F3/10: Client/docs/research/04-protocolo.md, "El F3 10, el inventario".
namespace Client::Protocol::Character
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

// Lee un C2 F3/10 reconstruido por el transporte; WasDecrypted no decide la admisión aquí.
// El Packet debe permanecer vivo y estable durante la llamada. La salida posee sus bytes,
// conserva orden, duplicados y cualquier slot, e ignora el trailing que no cuenta la cantidad.
// TooShort si falta la cantidad o una entrada completa, sin resultado parcial (ADR0010);
// WrongPacket para otro tipo/opcode/subcódigo, según FieldsAfterSubcode.
// Es una proyección pura del wire, no el handler 0x4c55a0 ni el decoder de los datos opacos: la
// inicialización, el flag de admisión y la aplicación son de Scenes::ApplyInventory y de
// Scenes::PlaceInventoryItem (06, "El F3 10, el inventario").
[[nodiscard]] Core::Expected<InventoryList, ProtocolError> ReadInventoryList(const Network::Packet& packet);
} // namespace Client::Protocol::Character
