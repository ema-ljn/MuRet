#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 04 § Mover y equipar ítems: el 24 en los dos sentidos, el 28, el 2A y los F3 13 y F3 14. Network entrega los C3
// que llegan ya descifrados como C1, con WasDecrypted; la escena decide cuándo mandar el 24.
namespace Client::Protocol::Inventory
{
inline constexpr std::uint8_t ItemMoveOpcode = 0x24;
inline constexpr std::uint8_t ItemDeleteOpcode = 0x28;
inline constexpr std::uint8_t DurabilityOpcode = 0x2a;
inline constexpr std::uint8_t WorldOpcode = 0xf3;
inline constexpr std::uint8_t EquipmentChangeSubcode = 0x13;
inline constexpr std::uint8_t ItemPutSubcode = 0x14;
// Los siete bytes de un ítem, como en el F3 10 (Protocol::Character::InventoryItemDataSize).
inline constexpr std::size_t ItemDataSize = 7;
// 0x4d1400: pkt[3] en 0xff, el movimiento rechazado.
inline constexpr std::uint8_t ItemMoveRejected = 0xff;
// 0x4c5ec0: pkt[3] en 0xff no borra nada.
inline constexpr std::uint8_t NoItemToDelete = 0xff;
// 0x5139f0 lee 17 bytes desde pkt[7] (el byte 6 no lo mira nadie).
inline constexpr std::size_t EquipmentChangeSize = 17;

using ItemData = std::array<std::byte, ItemDataSize>;

// El 24 del cliente, C1 0E 24 con el XOR32 y en C3 (0x449d80, 0x5ca250 y 0x5bec50): de dónde (la grilla, 0 el
// inventario, y el lugar), los siete bytes del ítem del cursor y adónde.
struct ItemMove
{
    std::uint8_t sourceFlag = 0;
    std::uint8_t sourceSlot = 0;
    ItemData item{};
    std::uint8_t targetFlag = 0;
    std::uint8_t targetSlot = 0;
};

[[nodiscard]] Outgoing WriteItemMove(const ItemMove& move);

// La respuesta del 24, C3 0C 24 descifrado (0x4d1400): pkt[3] es la grilla de destino (0 el inventario) o 0xff
// si el servidor lo rechaza, pkt[4] el lugar y pkt[5..11] el ítem.
struct ItemMoveResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
    ItemData item{};
};

[[nodiscard]] Core::Expected<ItemMoveResult, ProtocolError> ReadItemMoveResult(const Network::Packet& packet);

// El 28, C1 05 28 (0x4c5ec0): el lugar que se borra (0xff ninguno) y, distinto de 0, el fin de la espera de
// 0x5767954.
struct ItemDelete
{
    std::uint8_t slot = 0;
    std::uint8_t endsTheWait = 0;
};

[[nodiscard]] Core::Expected<ItemDelete, ProtocolError> ReadItemDelete(const Network::Packet& packet);

// El 2A, C1 06 2A (0x4d3cc0): el lugar, la durabilidad nueva y, distinto de 0, el fin de la espera.
struct Durability
{
    std::uint8_t slot = 0;
    std::uint8_t durability = 0;
    std::uint8_t endsTheWait = 0;
};

[[nodiscard]] Core::Expected<Durability, ProtocolError> ReadDurability(const Network::Packet& packet);

// El F3 13 (0x4e316e): la clave de un personaje (pkt[4] alto, pkt[5] bajo) y su equipo desde pkt[7], el de la
// lista de personajes, que lee 0x5139f0.
struct EquipmentChange
{
    std::uint16_t key = 0;
    std::array<std::byte, EquipmentChangeSize> equipment{};
};

[[nodiscard]] Core::Expected<EquipmentChange, ProtocolError> ReadEquipmentChange(const Network::Packet& packet);

// El F3 14 (0x4e31a3): un ítem que el servidor pone en el inventario, con el lugar en pkt[4] y el ítem desde
// pkt[5].
struct ItemPut
{
    std::uint8_t slot = 0;
    ItemData item{};
};

[[nodiscard]] Core::Expected<ItemPut, ProtocolError> ReadItemPut(const Network::Packet& packet);
} // namespace Client::Protocol::Inventory
