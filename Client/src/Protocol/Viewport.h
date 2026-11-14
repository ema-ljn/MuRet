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
// Los monstruos y los NPC que entran (CL9 hito 2): el 0x13 (0x4c9c60), el 0x1F (0x4ca0b0) y el 0x68 (0x4e0980), los
// tres C2 con la cuenta en pkt[4] y los registros desde pkt[5] (04, "Los monstruos y los NPC que entran").
inline constexpr std::uint8_t MonstersOpcode = 0x13;
inline constexpr std::uint8_t SummonsOpcode = 0x1f;
inline constexpr std::uint8_t ObjectsOpcode = 0x68;
inline constexpr std::size_t MonsterRecordSize = 0x10;
inline constexpr std::size_t SummonRecordSize = 0x18;
inline constexpr std::size_t ObjectRecordSize = 0x1c;

// Un registro del 0x13, de 0x10 bytes; el último que lee 0x4c9c60 es +0xc.
struct MonsterRecord
{
    // +0 y +1, el alto primero: la clave sin el bit 15 y el bit 15 (0x4c9cc8), el recién llegado.
    std::uint16_t key = 0;
    bool isNew = false;
    // +2 entero: el bit 7 va a +0x42d y los bits 4..6 a +0x93; los bits 0..1 son el alto del tipo.
    std::uint8_t typeHigh = 0;
    // (+2 & 3) << 8 | +3.
    std::uint16_t type = 0;
    // +4..+7, el bajo primero (0x4e6380).
    std::uint32_t effects = 0;
    std::uint8_t x = 0;       // +8
    std::uint8_t y = 0;       // +9
    std::uint8_t targetX = 0; // +0xa
    std::uint8_t targetY = 0; // +0xb
    // +0xc: la dirección en el nibble alto (el de abajo no lo lee nadie).
    std::uint8_t direction = 0;
};

struct Monsters
{
    std::vector<MonsterRecord> records;
};

// Un registro del 0x1F, de 0x18 bytes; el último que lee 0x4ca0b0 es +0x16.
struct SummonRecord
{
    std::uint16_t key = 0;
    bool isNew = false;
    // +2 y +3, el alto primero, los 16 bits: de 0x98 a 0x9e van por 0x517400.
    std::uint16_t type = 0;
    std::uint32_t effects = 0; // +4..+7
    std::uint8_t x = 0;        // +8
    std::uint8_t y = 0;        // +9
    std::uint8_t targetX = 0;  // +0xa
    std::uint8_t targetY = 0;  // +0xb
    // +0xc: la dirección arriba y el PK abajo.
    std::uint8_t directionAndPk = 0;
    // +0xd..+0x16: el nombre del dueño, sin terminador si lo llena.
    std::array<std::byte, Character::NameSize> owner{};
};

struct Summons
{
    std::vector<SummonRecord> records;
};

// Un registro del 0x68, de 0x1c bytes: con `kind` 1, un jugador (0x20a) con la clase de +1 y el equipo; con 2 o 3, el
// monstruo del tipo (+1 << 8 | +2); con otro, nada.
struct ObjectRecord
{
    std::uint8_t kind = 0;            // +0
    std::uint8_t classOrTypeHigh = 0; // +1
    std::uint8_t typeLow = 0;         // +2
    // +3 y +4, el alto primero, sin el bit 15 (0x4e09b3).
    std::uint16_t key = 0;
    std::uint8_t x = 0; // +5
    std::uint8_t y = 0; // +6
    // +7..+0x17, los 17 bytes del equipo (0x5139f0).
    std::array<std::byte, Character::EquipmentSize> equipment{};
    std::uint32_t effects = 0; // +0x18..+0x1b
};

struct Objects
{
    std::vector<ObjectRecord> records;
};

// La skill que pega a varios (CL9 hito 2): el C1 0x1A (0x4cedd0, PMSG_MAGICATTACK_COUNT_SEND). La clave del que pega en
// pkt[3..4] (el alto primero, entera), la skill en pkt[5], la celda en pkt[6] y pkt[7] (que 0x4cedd0 no lee), la cuenta
// en pkt[8] y las claves de los blancos desde pkt[9], de a dos bytes con el alto primero (0x4ceff6..0x4cf009).
inline constexpr std::uint8_t MagicAttackCountOpcode = 0x1a;

struct MagicAttackCount
{
    std::uint16_t key = 0;
    // pkt[5]: lo que queda en el +0x38e del que pega (0x4ceee4).
    std::uint8_t magic = 0;
    std::vector<std::uint16_t> targets;
};

[[nodiscard]] Core::Expected<Players, ProtocolError> ReadPlayers(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Leave, ProtocolError> ReadLeave(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Transformed, ProtocolError> ReadTransformed(const Network::Packet& packet);
[[nodiscard]] Core::Expected<EquipmentShown, ProtocolError> ReadEquipmentShown(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Position, ProtocolError> ReadPosition(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Monsters, ProtocolError> ReadMonsters(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Summons, ProtocolError> ReadSummons(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Objects, ProtocolError> ReadObjects(const Network::Packet& packet);
// TooShort sin la cuenta o sin el último byte de la última clave (pkt[8 + 2 · cuenta]).
[[nodiscard]] Core::Expected<MagicAttackCount, ProtocolError> ReadMagicAttackCount(const Network::Packet& packet);
} // namespace Client::Protocol::Viewport
