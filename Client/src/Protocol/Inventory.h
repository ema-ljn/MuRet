#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 04 § Mover y equipar ítems: el 24 en los dos sentidos, el 28, el 2A y los F3 13 y F3 14; y 04 § La vida y el maná:
// el 26 y el 27. Network entrega los C3 que llegan ya descifrados como C1, con WasDecrypted; la escena decide cuándo
// mandar el 24.
namespace Client::Protocol::Inventory
{
inline constexpr std::uint8_t ItemMoveOpcode = 0x24;
inline constexpr std::uint8_t LifeOpcode = 0x26;
inline constexpr std::uint8_t ManaOpcode = 0x27;
inline constexpr std::uint8_t ItemDeleteOpcode = 0x28;
inline constexpr std::uint8_t DurabilityOpcode = 0x2a;
inline constexpr std::uint8_t WorldOpcode = 0xf3;
inline constexpr std::uint8_t EquipmentChangeSubcode = 0x13;
inline constexpr std::uint8_t ItemPutSubcode = 0x14;
// Los siete bytes de un ítem, como en el F3 10 (Protocol::Character::InventoryItemDataSize).
inline constexpr std::size_t ItemDataSize = 7;
// 0x4d167f: el byte que cierra el aviso del 24 sin cifrar.
inline constexpr std::uint8_t ClearItemMoveHead = 0x16;
// 0x4d1400: pkt[3] en 0xff, el movimiento rechazado.
inline constexpr std::uint8_t ItemMoveRejected = 0xff;
// 0x4c5ec0: pkt[3] en 0xff no borra nada.
inline constexpr std::uint8_t NoItemToDelete = 0xff;
// 0x5139f0 lee 17 bytes desde pkt[7] (el byte 6 no lo mira nadie).
inline constexpr std::size_t EquipmentChangeSize = 17;
// pkt[3] del 26 y del 27 cuando no es una casilla: 0xfd (solo el 26, 0x4d35dc) termina la espera de 0x5767954, 0xfe
// trae los máximos y 0xff los actuales. Cualquier otro valor es el lugar de la poción que se usó.
inline constexpr std::uint8_t EndTheWaitPlace = 0xfd;
inline constexpr std::uint8_t MaximumPlace = 0xfe;
inline constexpr std::uint8_t CurrentPlace = 0xff;

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

// El aviso de un 24 que no llegó cifrado (0x4d1400, 0x4d166b..0x4d1690): C1 06 F1 03 00 16 con el XOR32 y en C3, sin
// leer el paquete. No suelta la traba.
[[nodiscard]] Outgoing WriteClearItemMoveReport();

// El 28, C1 05 28 (0x4c5ec0): el lugar que se borra (0xff ninguno) y, distinto de 0, el fin de la espera de
// 0x5767954.
struct ItemDelete
{
    std::uint8_t slot = 0;
    std::uint8_t endsTheWait = 0;
};

[[nodiscard]] Core::Expected<ItemDelete, ProtocolError> ReadItemDelete(const Network::Packet& packet);

// El 26, C1 07 26 (0x4d3510; PMSG_REFILL en el GameServer): el lugar en pkt[3] y, con 0xfe o 0xff, la vida en pkt[4]
// (alto) y pkt[5] (bajo). Con otro lugar el original no lee la vida, y pkt[6] (el Flag, siempre 0) no lo lee nunca.
struct LifeRefill
{
    std::uint8_t place = 0;
    std::uint16_t life = 0;
};

[[nodiscard]] Core::Expected<LifeRefill, ProtocolError> ReadLifeRefill(const Network::Packet& packet);

// El 27, C1 08 27 (0x4d3820; PMSG_MANASEND): el lugar en pkt[3], el maná en pkt[4..5] y, con 0xfe o 0xff, el BP en
// pkt[6..7], los dos con el byte alto primero. Con otro lugar el original no lee el BP.
struct ManaRefill
{
    std::uint8_t place = 0;
    std::uint16_t mana = 0;
    std::uint16_t bp = 0;
};

[[nodiscard]] Core::Expected<ManaRefill, ProtocolError> ReadManaRefill(const Network::Packet& packet);

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
