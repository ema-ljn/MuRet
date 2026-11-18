#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// 04 § Las ventanas de los NPC (CL9 hito 6): lo que abre un NPC (30), la lista de su grilla (31), comprar (32),
// vender (33), reparar (34), el almacén (81, 82 y 83) y la caja del caos (86 y 87). Los que llegan los atiende el
// dispatcher 0x4e2b10 (0x4e3c2a..0x4e3d24 y 0x4e4692..0x4e4862) o su handler; los que salen, las ventanas. El cierre
// de la tienda (C1 03 31) es Protocol::Connection::WriteWindowClose.
namespace Client::Protocol::NpcWindows
{
inline constexpr std::uint8_t TalkOpcode = 0x30;
inline constexpr std::uint8_t ItemListOpcode = 0x31;
inline constexpr std::uint8_t BuyOpcode = 0x32;
inline constexpr std::uint8_t SellOpcode = 0x33;
inline constexpr std::uint8_t RepairOpcode = 0x34;
inline constexpr std::uint8_t WarehouseMoneyOpcode = 0x81;
inline constexpr std::uint8_t WarehouseCloseOpcode = 0x82;
inline constexpr std::uint8_t WarehouseStateOpcode = 0x83;
inline constexpr std::uint8_t ChaosMixOpcode = 0x86;
inline constexpr std::uint8_t ChaosBoxCloseOpcode = 0x87;
inline constexpr std::uint8_t PersonalNumberOpcode = 0x99;
// 0x65a986..0x65a9d5: abrir la caja del caos manda C1 04 BD 09 (el opcode de Crywolf, Protocol::World).
inline constexpr std::uint8_t ChaosRateSubcode = 0x09;

// Los siete bytes de un ítem, como en el F3 10 y el 24.
inline constexpr std::size_t ItemDataSize = 7;
using ItemData = std::array<std::byte, ItemDataSize>;

// 0x4d253b..0x4d2566: con la caja del caos, pkt[4..9] van a los seis int de 0x6c9da8..0x6c9dbc.
inline constexpr std::size_t ChaosRateCount = 6;

// El 30 que llega (0x4d1e00): pkt[3] dice qué abre el NPC y pkt[4..9], con la caja del caos, sus seis tasas. El
// GameServer 0.97 lo manda en C3 de 8 bytes con cuatro (PMSG_TALKRESULT, Q-C920): las que el paquete no trae, el
// original las lee de lo que quedó en su buffer de recepción, y MuRet las deja en 0. pkt[4] también es lo que lee el
// caso 12 (0x4d25bc).
struct TalkResult
{
    std::uint8_t result = 0;
    std::array<std::uint8_t, ChaosRateCount> rates{};
    // Cuántas de las seis trajo el paquete.
    std::size_t ratesInPacket = 0;
};

[[nodiscard]] Core::Expected<TalkResult, ProtocolError> ReadTalkResult(const Network::Packet& packet);

// El aviso de un 30 que no llegó cifrado (0x4d1e30..0x4d24fb): C1 06 F1 03 00 30 con el XOR32 y en C3, como el del
// 24, sin leer el paquete.
[[nodiscard]] Outgoing WriteClearTalkReport();

// Una entrada de la lista del 31: el lugar y los siete bytes del ítem, de a 8 bytes desde pkt[6].
struct ListedItem
{
    std::uint8_t slot = 0;
    ItemData item{};
};

// El 31 que llega (0x4c5f60): pkt[4] es la grilla (3 la caja del caos, 5 la del entrenador y cualquier otro la de
// 0x78a7070, la tienda o el almacén) y pkt[5] la cuenta. El original lee en pkt[4] y pkt[5] sin mirar si es un C1 o
// un C2: MuRet tampoco.
struct ItemList
{
    std::uint8_t grid = 0;
    std::vector<ListedItem> items;
};

[[nodiscard]] Core::Expected<ItemList, ProtocolError> ReadItemList(const Network::Packet& packet);

// El 32 que llega (0x4e3c33..0x4e3c84): con pkt[3] distinto de 0xff, el lugar del inventario y los siete bytes desde
// pkt[4].
struct BuyResult
{
    std::uint8_t slot = 0;
    ItemData item{};
};

inline constexpr std::uint8_t BuyRejected = 0xff;

[[nodiscard]] Core::Expected<BuyResult, ProtocolError> ReadBuyResult(const Network::Packet& packet);

// El 33 que llega (0x4e3c8a..0x4e3cbb): con pkt[3] en 0, vender falló; con otro, el zen nuevo en pkt[4..7].
struct SellResult
{
    std::uint8_t result = 0;
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<SellResult, ProtocolError> ReadSellResult(const Network::Packet& packet);

// El 34 que llega (0x4e3cc1..0x4e3d24): el zen después de reparar, en pkt[4..7]; con 0, nada.
struct RepairResult
{
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<RepairResult, ProtocolError> ReadRepairResult(const Network::Packet& packet);

// El 81 que llega (0x4e4692..0x4e46de): con pkt[3] distinto de 0, el zen del almacén (pkt[4..7], a +0xed0 del registro
// propio) y el del inventario (pkt[8..11], a +0xecc).
struct WarehouseMoney
{
    std::uint8_t result = 0;
    std::uint32_t warehouseMoney = 0;
    std::uint32_t inventoryMoney = 0;
};

[[nodiscard]] Core::Expected<WarehouseMoney, ProtocolError> ReadWarehouseMoney(const Network::Packet& packet);

// El 82 y el 87 (0x4e4819..0x4e4862): el fin del almacén y de la caja del caos, sin campos.
struct NpcWindowClosed
{
    std::uint8_t opcode = 0;
};

[[nodiscard]] Core::Expected<NpcWindowClosed, ProtocolError> ReadNpcWindowClosed(const Network::Packet& packet);

// El 83 que llega (0x4e46df): pkt[3] es el estado de la traba del almacén.
struct WarehouseState
{
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<WarehouseState, ProtocolError> ReadWarehouseState(const Network::Packet& packet);

// El 86 que llega (0x4da280): pkt[3] es el resultado y, con 1 (0x4da2ff) y con 100 (0x4da378), los siete bytes del ítem
// desde pkt[4].
struct ChaosMixResult
{
    std::uint8_t result = 0;
    ItemData item{};
};

inline constexpr std::uint8_t ChaosMixSucceeded = 1;
// 0x4da373: la mezcla del entrenador, con su ítem para la caja del entrenador.
inline constexpr std::uint8_t TrainerMixResult = 100;

[[nodiscard]] Core::Expected<ChaosMixResult, ProtocolError> ReadChaosMixResult(const Network::Packet& packet);

// 0x5c4bdc..0x5c4e1e: comprar, C1 04 32 con el lugar de la grilla de la tienda, con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteBuy(std::uint8_t slot);

// 0x5d8e1a..0x5d905d: vender, C1 04 33 con el lugar de donde salió el ítem del cursor (0x78a6758), con el XOR32 y en
// C3.
[[nodiscard]] Outgoing WriteSell(std::uint8_t slot);

// 0x5c4485.. (un ítem, con 0x78abb10) y 0x5edaa7.. (todo, 0xff y 0): reparar, C1 05 34, con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteRepair(std::uint8_t position, std::uint8_t requestPosition);

// 0x5edaa7..: el lugar con que el botón de la tienda repara todo.
inline constexpr std::uint8_t RepairEverything = 0xff;

// 0x4e6f80(tipo, zen): C1 08 81 con el tipo y el zen, con el XOR32 y en claro.
[[nodiscard]] Outgoing WriteWarehouseMoney(std::uint8_t type, std::uint32_t money);

// 0x658567..0x658709, en 0x657ab0(0x40): cerrar el almacén, C1 03 82 en claro.
[[nodiscard]] Outgoing WriteWarehouseClose();

// El número que acompaña a la contraseña del almacén (0x78ab9fc..0x78aba05).
inline constexpr std::size_t WarehouseNumberSize = 10;
using WarehouseNumber = std::array<std::byte, WarehouseNumberSize>;

// 0x5e7e90..0x5e82e0: la contraseña del almacén, C1 10 83 con el tipo, la contraseña (0x78abb24) y el número, con el
// XOR32 y en C3.
[[nodiscard]] Outgoing WriteWarehousePassword(std::uint8_t type, std::uint16_t password, const WarehouseNumber& number);

// 0x5e8706..0x5e8bfd: el OK de la caja de la contraseña en el modo 6 (el del 0x8f), C1 0D 99 con los diez bytes de lo
// tecleado (0x78ab9fc), con el XOR32 y en C3. El GameServer 0.97 lee el 99 como otro pedido (04, Q-C925).
[[nodiscard]] Outgoing WritePersonalNumber(const WarehouseNumber& number);

// 0x5d9362..0x5d9527: mezclar, C1 04 86 con el tipo de mezcla (0x7d2dc98), con el XOR32 y en claro.
[[nodiscard]] Outgoing WriteChaosMix(std::uint8_t kind);

// 0x658767..0x658909, en 0x657ab0(0x100): cerrar la caja del caos, C1 03 87 en claro.
[[nodiscard]] Outgoing WriteChaosBoxClose();

// 0x65a956..0x65ac07: el pedido que abrir la caja del caos manda antes de abrir el inventario, C1 04 BD 09 con el
// XOR32 y en claro.
[[nodiscard]] Outgoing WriteChaosRateRequest();
} // namespace Client::Protocol::NpcWindows
