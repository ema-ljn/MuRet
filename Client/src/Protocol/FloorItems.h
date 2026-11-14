#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <optional>
#include <vector>

// Los ítems del piso, levantar, tirar y usar (CL9 hito 5; 04 § Los ítems del piso): el 20 y el
// 21 (C2), el 22, el 23 y el 2C que llegan, y los C1 22, 23 y 26 que salen con el XOR32 y en C3. Los largos son los
// que lee el cliente, que es el oráculo: el GameServer 0.97 manda ítems de 5 bytes y un 2C de un byte (07 Q-C1240).
namespace Client::Protocol::FloorItems
{
inline constexpr std::uint8_t CreateOpcode = 0x20;
inline constexpr std::uint8_t DeleteOpcode = 0x21;
inline constexpr std::uint8_t PickUpOpcode = 0x22;
inline constexpr std::uint8_t DropOpcode = 0x23;
inline constexpr std::uint8_t UseItemOpcode = 0x26;
inline constexpr std::uint8_t StatFruitOpcode = 0x2c;
// 0x4d0cc0: pkt[3] en 0xff no levanta nada, en 0xfe es el zen y en 0xfd un ítem que no va a la grilla.
inline constexpr std::uint8_t NothingPickedUp = 0xff;
inline constexpr std::uint8_t ZenPickedUp = 0xfe;
inline constexpr std::uint8_t PickedUpElsewhere = 0xfd;
// 0x4d10d0: pkt[3] en 0, el ítem no se tiró.
inline constexpr std::uint8_t DropRejected = 0;

// Una entrada del 20 (0x4d0ba0, de a 11 bytes desde pkt[5]): la clave con el bit 15 de "recién caído", la celda y los
// siete bytes del ítem.
struct FloorItemEntry
{
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    Inventory::ItemData item{};
};

// El 20, C2 (0x4d0ba0): la cuenta en pkt[4] y las entradas.
struct FloorItemsCreate
{
    std::vector<FloorItemEntry> items;
};

[[nodiscard]] Core::Expected<FloorItemsCreate, ProtocolError> ReadFloorItemsCreate(const Network::Packet& packet);

// El 21, C2 (0x4e3849, en el dispatcher): la cuenta en pkt[4] y las claves, de a 2 bytes con el alto primero.
struct FloorItemsDelete
{
    std::vector<std::uint16_t> keys;
};

[[nodiscard]] Core::Expected<FloorItemsDelete, ProtocolError> ReadFloorItemsDelete(const Network::Packet& packet);

// El 22 que llega, C1 (C3 en el cable; 0x4d0cc0): pkt[3] el resultado; con 0xfe, el zen en pkt[4..7] con el byte alto
// primero; con un lugar (o 0xfd), los siete bytes del ítem desde pkt[4]. Con 0xff no lee más.
struct PickUpResult
{
    std::uint8_t result = 0;
    std::uint32_t zen = 0;
    Inventory::ItemData item{};
};

[[nodiscard]] Core::Expected<PickUpResult, ProtocolError> ReadPickUpResult(const Network::Packet& packet);

// El 23 que llega, C1 (0x4d10d0): pkt[3] el resultado (0 rechazado) y pkt[4] el lugar que se tiró. Con 0 no lee
// pkt[4] (0x4d10de): el lugar queda en 0 si no llegó.
struct DropResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<DropResult, ProtocolError> ReadDropResult(const Network::Packet& packet);

// El 2C, C1 (0x4de500): pkt[3] el resultado (0 a 5, 0x10, 0x21, 0x25 y 0x26), pkt[4] los puntos y pkt[5] el stat
// (0 la energía, 1 la vitalidad, 2 la agilidad, 3 la fuerza y 4 el comando).
struct StatFruit
{
    std::uint8_t result = 0;
    std::uint8_t points = 0;
    std::uint8_t stat = 0;
};

[[nodiscard]] Core::Expected<StatFruit, ProtocolError> ReadStatFruit(const Network::Packet& packet);

// Un 22, un 23 o un 2C más cortos de lo que lee el cliente (el GameServer 0.97, 07 Q-C1240): el original lee lo que
// quedó en su buffer de recepción, que MuRet no inventa, pero hay escrituras que hace en todos sus caminos. 0x4d0cc0
// deja el pedido de levantar (0x6c82e8) en −1 (0x4d10b9) y 0x4de500 la espera del uso (0x5767954) en 0 (0x4de7e4 y
// las otras cinco salidas, 0x4deb0e).
struct ShortPickUpResult
{
};

struct ShortStatFruit
{
};

// 0x4d10d0 deja el lugar tirado (0x6c82ec) en −1 en todos sus caminos (0x4d13d1 y 0x4d13e5) y, con pkt[3] distinto de
// 0, el cursor vacío (0x7879fbc en 0, 0x4d13c7), también en todos. `result` es pkt[3], si llegó.
struct ShortDropResult
{
    std::optional<std::uint8_t> result;
};

// El 23 que ReadDropResult rechaza por corto: solo pkt[3], si llegó.
[[nodiscard]] Core::Expected<ShortDropResult, ProtocolError> ReadShortDropResult(const Network::Packet& packet);

// El C1 05 22 (0x566755, el caso 1 de 0x5650c0): el índice del ítem del piso, el byte alto primero.
[[nodiscard]] Outgoing WritePickUp(std::uint16_t index);

// El C1 06 23 (0x5dfe34..0x5e06b5): la celda adonde se tira y el lugar de donde salió el ítem del cursor.
struct Drop
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Outgoing WriteDrop(const Drop& drop);

// El C1 06 26 (las ranuras rápidas de 0x59ff10, 0x5a2c01.. y 0x5a3623.., y la grilla, 0x5c5937..): el lugar del ítem,
// el destino (0 desde las ranuras y la grilla) y [0x78abb26], que ponen dos diálogos de 0x623e70 (0x628bdc y
// 0x6293b5). El GameServer 0.97 lee 5 bytes (PMSG_USEITEM sin el tercero; 07 Q-C1241).
struct UseItem
{
    std::uint8_t slot = 0;
    std::uint8_t target = 0;
    std::uint8_t kind = 0;
};

[[nodiscard]] Outgoing WriteUseItem(const UseItem& use);
} // namespace Client::Protocol::FloorItems
