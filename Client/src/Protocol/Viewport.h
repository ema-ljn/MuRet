#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Los otros jugadores en el viewport (CL9 hito 1): el 0x12 (0x4c8860), el 0x14 (0x4ca780), el 0x45 (0x4c92a0), el 0x25
// (0x4c8380) y el 0xDF (0x4c6b40), como los lee el dispatcher 0x4e2b10, sin guardas de escena ni del flag de cifrado.
// Contrato: Client/docs/research/04-protocolo.md, "Los otros jugadores en el viewport". Los layouts son los del ASM
// del 1.02 (*estático*); el GS 0.97 arma otros (08, P-C160).
namespace Client::Protocol::Viewport
{
inline constexpr std::uint8_t PlayersOpcode = 0x12;
inline constexpr std::uint8_t LeaveOpcode = 0x14;
inline constexpr std::uint8_t EquipmentShownOpcode = 0x25;
inline constexpr std::uint8_t TransformedOpcode = 0x45;
inline constexpr std::uint8_t PositionOpcode = 0xdf;

// 0x4c926c y 0x4c9773: lo que avanza cada registro del 0x12 y del 0x45.
inline constexpr std::size_t PlayerRecordSize = 0x2c;
inline constexpr std::size_t TransformedRecordSize = 0x1c;
// El bit 15 de la clave de un registro: el que acaba de entrar (el efecto de llegada en vez del camino).
inline constexpr std::uint16_t NewBit = 0x8000;
inline constexpr std::uint16_t KeyMask = 0x7fff;

// Un registro del 0x12, desde pkt[5] de a 0x2c bytes. Los bytes +0x16, +0x17 y +0x29..+0x2b no los lee nadie.
struct PlayerRecord
{
    // +0 y +1, big-endian, sin el bit 15.
    std::uint16_t key = 0;
    // El bit 15 de la clave.
    bool isNew = false;
    // +2 y +3: la celda.
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    // +4: el nibble de abajo es el estado (1 a 4) y los de arriba la clase, ((b & 0x10) | (b >> 4)) >> 1 (0x4c89f3).
    std::uint8_t classAndState = 0;
    // +5..+0x15: el equipo que lee 0x5139f0 (el de la lista de personajes).
    std::array<std::byte, Character::EquipmentSize> equipment{};
    // +0x18, little-endian: las banderas de 0x4e6380 y 0x42ab40.
    std::uint32_t effects = 0;
    // +0x1c..+0x25: el nombre, sin terminador si lo llena.
    std::array<std::byte, Character::NameSize> name{};
    // +0x26 y +0x27: adónde va (0x4eac00).
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    // +0x28: la dirección en el nibble de arriba y el PK en el de abajo.
    std::uint8_t directionAndPk = 0;
};

// El C2 0x12: la cuenta en pkt[4] y los registros. `packet` son los bytes del paquete entero, que el dispatcher guarda
// en la cola de 0x77f3ba0 con su tamaño (0x4e372e..0x4e37b8).
struct Players
{
    std::vector<PlayerRecord> records;
    std::vector<std::byte> packet;
};

// El C1 0x14: la cuenta en pkt[3] y las claves desde pkt[4], de a dos bytes big-endian, con el bit 15 (0x4ca7a2 lo
// compara entero con 0x13d; 0x4ca7e1 lo saca para 0x510ff0).
struct Leave
{
    std::vector<std::uint16_t> keys;
};

// Un registro del 0x45, desde pkt[5] de a 0x1c bytes. +6, +7 y +0x19..+0x1b no los lee nadie.
struct TransformedRecord
{
    std::uint16_t key = 0;
    bool isNew = false;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    // +4 y +5, big-endian: el tipo para 0x5141e0.
    std::uint16_t type = 0;
    // +8, little-endian.
    std::uint32_t effects = 0;
    // +0xc..+0x15.
    std::array<std::byte, Character::NameSize> name{};
    // +0x16 y +0x17.
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    // +0x18.
    std::uint8_t directionAndPk = 0;
};

// El C2 0x45: la cuenta en pkt[4].
struct Transformed
{
    std::vector<TransformedRecord> records;
};

// El C1 0x25: la clave en pkt[3..4] (big-endian, entera: 0x4c838e no saca el bit 15) y el ítem desde pkt[5]. De sus
// siete bytes, 0x4c8380 lee el 1 (el lugar arriba y el nivel abajo), el 3 (el excelente, & 0x3f) y el 4 (el set), y
// 0x54b3a0 el 0, el 3 y el 5: el 6 (pkt[11]) no lo lee nadie y queda en 0.
struct EquipmentShown
{
    std::uint16_t key = 0;
    std::array<std::byte, Character::InventoryItemDataSize> item{};
};

// El C1 0xDF: la clave en pkt[3..4], entera, y la celda en pkt[5] y pkt[6].
struct Position
{
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// Ya pasaron por el framing y la decodificación de Network. WrongPacket con otro tipo u opcode; TooShort si falta
// un campo que el original lee (del último registro, hasta su último byte leído). Los bytes de más se ignoran.
[[nodiscard]] Core::Expected<Players, ProtocolError> ReadPlayers(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Leave, ProtocolError> ReadLeave(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Transformed, ProtocolError> ReadTransformed(const Network::Packet& packet);
[[nodiscard]] Core::Expected<EquipmentShown, ProtocolError> ReadEquipmentShown(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Position, ProtocolError> ReadPosition(const Network::Packet& packet);
} // namespace Client::Protocol::Viewport
