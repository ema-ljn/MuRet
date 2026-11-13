#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 04 § Las tiendas personales: el 3F que llega (CL9 hito 7, fase 3). El GameServer 0.97 no lo tiene: el oráculo es el
// ASM del cliente. 0x4e2b10 despacha por el byte que sigue al opcode (pkt[3] en un C1, pkt[4] en un C2, 0x4e4e3a) y
// cada caso lee sus campos en lugares fijos del paquete, sin mirar el tipo: los lectores hacen lo mismo y solo miden
// que el paquete los traiga. Acá van los casos de los carteles y los avisos (0, 2, 3, 8, 0x10 y 0x12) y, desde la fase
// 4, los de las listas de ítems (1, 5, 6 y 0x13).
namespace Client::Protocol::PersonalShop
{
inline constexpr std::uint8_t Opcode = 0x3f;
inline constexpr std::uint8_t TitlesSubcode = 0x00;
inline constexpr std::uint8_t CreatedSubcode = 0x02;
inline constexpr std::uint8_t ClosedSubcode = 0x03;
inline constexpr std::uint8_t NoticeSubcode = 0x08;
inline constexpr std::uint8_t TitleSubcode = 0x10;
inline constexpr std::uint8_t OwnerLeftSubcode = 0x12;
// El que manda el comando 2 de la ventana de comandos (0x406b81).
inline constexpr std::uint8_t ItemsRequestSubcode = 0x05;
// Las listas de ítems (fase 4): el precio que no se puso (0x4db120), la lista de otro (0x4db890), la compra
// (0x4dbcb0) y la lista que se renueva (0x4dbb40).
inline constexpr std::uint8_t PriceResultSubcode = 0x01;
inline constexpr std::uint8_t ItemListSubcode = 0x05;
inline constexpr std::uint8_t PurchaseSubcode = 0x06;
inline constexpr std::uint8_t RefreshSubcode = 0x13;
// El que manda 0x5eb020 al cerrar la tienda de otro (0x5eb17f).
inline constexpr std::uint8_t ViewClosedSubcode = 0x07;
// 0x4db890 y 0x4dbb40: el resultado que trae la lista.
inline constexpr std::uint8_t ListedResult = 1;

// 0x4dae80 y 0x4dafe0: el título, de 36 bytes con el terminador en el 37 de la pila.
inline constexpr std::size_t TitleSize = 36;

using Name = std::array<std::byte, Character::NameSize>;

// Una entrada de 0x26 bytes del 3F 00 (0x4dae80): la clave en big-endian y el título.
struct ShopTitleEntry
{
    std::uint16_t key = 0;
    std::string title;
};

// El 3F 00 (0x4dae80): la cuenta en pkt[5] y las entradas desde pkt[6]. Con el subcódigo en pkt[4], es un C2.
struct ShopTitles
{
    std::vector<ShopTitleEntry> entries;
};

[[nodiscard]] Core::Expected<ShopTitles, ProtocolError> ReadShopTitles(const Network::Packet& packet);

// El 3F 02 (0x4db700): el resultado de abrir la propia en pkt[4] (1, abierta).
struct ShopCreated
{
    std::uint8_t result = 0;
};

[[nodiscard]] Core::Expected<ShopCreated, ProtocolError> ReadShopCreated(const Network::Packet& packet);

// El 3F 03 (0x4e4ea5): el resultado en pkt[4] y, con 1, la clave de pkt[5..6] en big-endian.
struct ShopClosed
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
};

[[nodiscard]] Core::Expected<ShopClosed, ProtocolError> ReadShopClosed(const Network::Packet& packet);

// El 3F 08 (0x4e4f5d): el nombre de pkt[5..14].
struct ShopNotice
{
    Name name{};
};

[[nodiscard]] Core::Expected<ShopNotice, ProtocolError> ReadShopNotice(const Network::Packet& packet);

// El 3F 10 (0x4dafe0): la clave de pkt[4..5] en big-endian, el título de pkt[6..0x29] y el nombre de pkt[0x2a..0x33].
struct ShopTitle
{
    std::uint16_t key = 0;
    std::string title;
    Name name{};
};

[[nodiscard]] Core::Expected<ShopTitle, ProtocolError> ReadShopTitle(const Network::Packet& packet);

// El 3F 12 (0x4e4fcc): no lee nada más.
struct ShopOwnerLeft
{
};

[[nodiscard]] Core::Expected<ShopOwnerLeft, ProtocolError> ReadShopOwnerLeft(const Network::Packet& packet);

// El 3F 01 (0x4db120, "ReceiveSetPriceResult"): el resultado en pkt[4] y el lugar en pkt[5].
struct PriceResult
{
    std::uint8_t result = 0;
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<PriceResult, ProtocolError> ReadPriceResult(const Network::Packet& packet);

// Una entrada de 0xc bytes de las listas (0x4db9e2.. y 0x4dbc01..): el lugar, los siete bytes del ítem y el precio
// (un DWORD LE en +8, con signo).
struct ShopListEntry
{
    std::uint8_t slot = 0;
    Inventory::ItemData item{};
    std::int32_t price = 0;
};

// El 3F 05 (0x4db890, "ReceivePersonalShopItemList") y el 3F 0x13 (0x4dbb40, "ReceiveRefreshItemList"): el resultado
// en pkt[5], la clave del dueño en pkt[6..7] (BE), el título en pkt[0x12..0x35] (0x4db92b, strncpy de 0x24), la cuenta
// en pkt[0x36] y las entradas desde pkt[0x37]. El 0x13 no lee ni la clave ni el título. Sin el resultado 1 solo se lee
// pkt[5]. Los dos llegan como C2 (el subcódigo en pkt[4]).
struct ShopItemList
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
    // Los 36 bytes tal cual: strncpy no corta ni termina en el 36.
    std::array<std::byte, TitleSize> title{};
    std::vector<ShopListEntry> entries;
};

[[nodiscard]] Core::Expected<ShopItemList, ProtocolError> ReadShopItemList(const Network::Packet& packet);

// El 3F 0x13: lo mismo sin la clave ni el título, en su propio tipo para el despacho.
struct ShopRefresh
{
    std::uint8_t result = 0;
    std::vector<ShopListEntry> entries;
};

[[nodiscard]] Core::Expected<ShopRefresh, ProtocolError> ReadShopRefresh(const Network::Packet& packet);

// El 3F 06 (0x4dbcb0, "ReceivePurchaseItem"): el resultado en pkt[4] y, con 1, los siete bytes del ítem en pkt[7..0xd]
// y su lugar en pkt[0xe]. pkt[5] y pkt[6] no se leen.
struct ShopPurchase
{
    std::uint8_t result = 0;
    Inventory::ItemData item{};
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<ShopPurchase, ProtocolError> ReadShopPurchase(const Network::Packet& packet);

// El C1 04 F3 10 con el XOR32 y en C3 ("SendRequestInventory"), que mandan abrir una tienda (0x65ac87 y 0x65b359) con
// 0x6c931c distinto de −1 y el 3F 01 que falla (0x4db1a2).
[[nodiscard]] Outgoing WriteInventoryRequest();

// El 3F 07 de 0x5eb020, al cerrar la tienda de otro con su clave (0x78a6514) distinta de 0: C1 10 3F 07, la clave en
// big-endian y el nombre (0x78a64f9, 10 bytes), con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteViewClosed(std::uint16_t key, const Name& name);

// El 3F 05 del cliente (0x406050 desde 0x406b02, el comando 2 de la ventana de comandos, el texto 1124): C1 10 3F 05,
// la clave del otro en big-endian (+0x1f4) y su nombre (+0x1d9, 10 bytes), con el XOR32 y en C3. Lo que contesta el
// servidor es el 3F 05 de arriba (ReadShopItemList).
[[nodiscard]] Outgoing WriteShopItemsRequest(std::uint16_t key, const Name& name);
} // namespace Client::Protocol::PersonalShop
