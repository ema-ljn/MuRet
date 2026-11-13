#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
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
// que el paquete los traiga. Acá van los casos de los carteles y los avisos (0, 2, 3, 8, 0x10 y 0x12); los de las
// listas de ítems (1, 5, 6 y 0x13) siguen sin registrar (08 P-C204 a).
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

// El 3F 05 del cliente (0x406050 desde 0x406b02, el comando 2 de la ventana de comandos, el texto 1124): C1 10 3F 05,
// la clave del otro en big-endian (+0x1f4) y su nombre (+0x1d9, 10 bytes), con el XOR32 y en C3. Lo que contesta el
// servidor (las listas, 08 P-C208 b) no está portado.
[[nodiscard]] Outgoing WriteShopItemsRequest(std::uint16_t key, const Name& name);
} // namespace Client::Protocol::PersonalShop
