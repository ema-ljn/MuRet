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

// 04 § El comercio: del 36 al 3D en los dos sentidos (CL9 hito 7). Los lectores son los campos que leen 0x4d4af0
// (36), 0x4d5570 (37) y los casos en línea del dispatcher 0x4e2b10 (38 a 3C, 0x4e3d6a..0x4e3e7f; el 3D va a 0x4d57f0);
// los escritores, lo que arman 0x406050 y 0x56c440 (36), 0x623e70 (37, 3A y 3C), 0x5ecde0 (3C y 3D) y 0x657ab0 (3D).
namespace Client::Protocol::Trade
{
inline constexpr std::uint8_t RequestOpcode = 0x36;
inline constexpr std::uint8_t ResponseOpcode = 0x37;
inline constexpr std::uint8_t ItemRemovedOpcode = 0x38;
inline constexpr std::uint8_t ItemAddedOpcode = 0x39;
inline constexpr std::uint8_t MoneyOpcode = 0x3a;
inline constexpr std::uint8_t OtherMoneyOpcode = 0x3b;
inline constexpr std::uint8_t OkButtonOpcode = 0x3c;
inline constexpr std::uint8_t ResultOpcode = 0x3d;

using Name = std::array<std::byte, Character::NameSize>;

// El 36 del GS (0x4d4af0): C1 0D 36 y el nombre del que pide, 10 bytes desde pkt[3].
struct TradeRequest
{
    Name name{};
};

[[nodiscard]] Core::Expected<TradeRequest, ProtocolError> ReadTradeRequest(const Network::Packet& packet);

// El 36 que llegó en claro (0x4d4b1c, [EBP+0xc] en 0): no se lee; 0x4d4af0 solo manda el aviso
// (WriteClearTradeRequestReport).
struct ClearTradeRequest
{
};

// El 37 del GS (0x4d5570): C1 14 37, la respuesta en pkt[3] (0 no, 1 sí, 2 el otro está ocupado), el nombre en
// pkt[4..13], el nivel en pkt[0xe] (WORD) y el gremio en pkt[0x10] (DWORD), los dos en little-endian.
struct TradeResponse
{
    std::uint8_t response = 0;
    Name name{};
    std::uint16_t level = 0;
    std::uint32_t guild = 0;
};

[[nodiscard]] Core::Expected<TradeResponse, ProtocolError> ReadTradeResponse(const Network::Packet& packet);

// El 38 (0x4e3d6a): C1 04 38 y el lugar de la grilla del otro que se saca.
struct TradeItemRemoved
{
    std::uint8_t slot = 0;
};

[[nodiscard]] Core::Expected<TradeItemRemoved, ProtocolError> ReadTradeItemRemoved(const Network::Packet& packet);

// El 39 (0x4e3d99): C1 0B 39, el lugar en pkt[3] y los siete bytes del ítem desde pkt[4] (08 P-C200: el GS 0.97 los
// manda en otro largo).
struct TradeItemAdded
{
    std::uint8_t slot = 0;
    Inventory::ItemData item{};
};

[[nodiscard]] Core::Expected<TradeItemAdded, ProtocolError> ReadTradeItemAdded(const Network::Packet& packet);

// El 3A del GS (0x4e3dd0): C1 04 3A y si aceptó el zen que se puso (distinto de 0) o no.
struct TradeMoneyResult
{
    std::uint8_t accepted = 0;
};

[[nodiscard]] Core::Expected<TradeMoneyResult, ProtocolError> ReadTradeMoneyResult(const Network::Packet& packet);

// El 3B (0x4e3df3): C1 08 3B, un byte que nadie lee y el zen del otro (DWORD little-endian) en pkt[4].
struct TradeOtherMoney
{
    std::uint32_t money = 0;
};

[[nodiscard]] Core::Expected<TradeOtherMoney, ProtocolError> ReadTradeOtherMoney(const Network::Packet& packet);

// El 3C del GS (0x4e3e0a): C1 04 3C y el estado del OK del otro (0 apagado, 1 prendido, 2 se apaga el propio).
struct TradeOkState
{
    std::uint8_t state = 0;
};

[[nodiscard]] Core::Expected<TradeOkState, ProtocolError> ReadTradeOkState(const Network::Packet& packet);

// El 3D del GS (0x4d57f0): C1 04 3D y el resultado (0 cancelado, 1 hecho, 2 sin lugar, 3 el tiempo). Con la traba
// 0x78abb39, 0x4d57f0 guarda los cuatro primeros bytes del paquete en 0x57635bc y 0x4d1400 lo repite: `head` son
// esos bytes.
struct TradeResult
{
    std::uint8_t result = 0;
    std::array<std::uint8_t, 4> head{};
};

[[nodiscard]] Core::Expected<TradeResult, ProtocolError> ReadTradeResult(const Network::Packet& packet);

// El 36 del cliente (0x406050 en 0x4064f0..0x406af0 y 0x56c440): C1 05 36, la clave del otro en big-endian (+0x1f4
// >> 8 y después el byte bajo), con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteTradeRequest(std::uint16_t key);

// El 37 del cliente: C1 04 37 y la respuesta (1 sí, 0 no), con el XOR32 y en claro. Lo arman el diálogo 0x78 del
// paso 0x623e70 (sí 0x62c20a, no 0x62d4a2) y 0x4d4af0 (el 0, con la tienda personal abierta).
[[nodiscard]] Outgoing WriteTradeResponse(std::uint8_t response);

// El 3A del cliente (0x623e70, 0x626b30..0x626e36): C1 08 3A 00 y el zen (DWORD little-endian), con el XOR32 y en
// claro.
[[nodiscard]] Outgoing WriteTradeMoney(std::uint32_t money);

// El 3C del cliente: C1 04 3C y el OK (1 prendido, 0 apagado), con el XOR32 y en C3 (0x5ecde0, 0x5ecf9c..0x5ed2b5).
[[nodiscard]] Outgoing WriteTradeOkButton(std::uint8_t state);

// El 3D del cliente: C1 03 3D, con el XOR32 y en C3 (0x5ecde0, 0x5ed4ec..; 0x657ab0 al cerrar el panel 0x20).
[[nodiscard]] Outgoing WriteTradeCancel();

// El aviso del 36 que no llegó cifrado (0x4d4af0, 0x4d4b22..0x4d5238): C1 06 F1 03 00 36 con el XOR32 y en C3, como
// el del 24 (Inventory::WriteClearItemMoveReport).
[[nodiscard]] Outgoing WriteClearTradeRequestReport();
} // namespace Client::Protocol::Trade
