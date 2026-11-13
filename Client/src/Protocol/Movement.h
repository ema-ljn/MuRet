#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <span>

// 04 § Caminar y pegar: D3 C->S/S->C y D7 S->C. El escritor es la parte pura de
// SendMove (0x56acd0); sus condiciones de UI, estado y envío pertenecen a la escena.
namespace Client::Protocol::Movement
{
inline constexpr std::size_t MaxWalkCells = 15;
inline constexpr std::size_t MaxWireWalkCells = 14; // 0x56ad8d..0x56ad96: no recorta el camino local.
inline constexpr std::uint8_t WalkOpcode = 0xd3;
inline constexpr std::uint8_t DamageOpcode = 0xd7;
// 04 § La acción: C1 05 18 dir acción del cliente (PMSG_ACTION del GS) y C1 07 18 clave:2 dir acción del servidor
// (PMSG_ACTIONRESULT).
inline constexpr std::uint8_t ActionOpcode = 0x18;
// 0x59b74c: el giro del héroe quieto. 0x4f2416..0x4f2bea y 0x4e137a: sentarse.
inline constexpr std::uint8_t FacingAction = 0x7a;
inline constexpr std::uint8_t SittingAction = 0x82;
// C1 05 18 direction action con el XOR32 y en claro: el giro (59b333..59b83a, con FacingAction), sentarse
// (0x4f2416..0x4f2bea, con SittingAction), el emote (0x576a60 y cada caso de 0x576eb0) y el BD 03 con el resultado 1
// (0x4e11e1..0x4e1467, CL9 hito 11: sentarse en el altar de Crywolf, con SittingAction). La dirección va tal cual.
[[nodiscard]] Outgoing WriteAction(std::uint8_t direction, std::uint8_t action);

struct Cell
{
    std::uint8_t x;
    std::uint8_t y;
};

enum class WalkWriteError : std::uint8_t
{
    InvalidPathSize,
    InvalidAngle,
};

struct Walk
{
    std::uint16_t characterId;
    Cell cell;
    std::uint8_t direction;
};

// El 0x18 del servidor (0x4cb750): pkt[3..4] la clave, big-endian, pkt[5] la dirección y pkt[6] la acción.
struct Action
{
    std::uint16_t key;
    std::uint8_t direction;
    std::uint8_t action;
};

struct Damage
{
    std::uint16_t characterId;
    bool shock;
    std::uint16_t amount;
    std::uint8_t type;
    bool flag;
    bool doubleHit = false; // pkt[7] & 0x40, 0x4cab40 y 0x4cb61c.
};

// Camino de 1..15 celdas, incluido el inicio. Sale C1 con XOR32 y Delivery::Clear.
// El blanco es una entrada explícita; el cable no permite reconstruirlo.
// Solo con una celda se lee angle. MuRet calcula en double con las constantes y el
// orden de 0x56b0b7..0x56b0cc; la precisión nativa depende de la CW y no se mide acá.
// La conversión original fuerza truncamiento (0x69fd4b) y usa FISTP m64int
// (0x69fd55, retorno EDX:EAX); luego SendMove toma los bits bajos &7.
// MuRet rechaza no finitos y cuentas fuera de [-2^63,2^63): son errores de su API,
// no una validación del original ni una afirmación sobre sus máscaras de excepción.
[[nodiscard]] Core::Expected<Outgoing, WalkWriteError> WriteWalk(std::span<const Cell> path, Cell target, float angle);

// El rumbo de `angle` como 0x56b0b7..0x56b0cc (y 0x4e1260..0x4e128a, que le suma un AND 0xff): (ángulo + 22,5) ·
// 0x3cb60b61 + 1, el ftol y &7, con los mismos errores que WriteWalk.
[[nodiscard]] Core::Expected<std::uint8_t, WalkWriteError> DirectionFromAngle(float angle);

// 0x56ad1d..0x56ad4f: cantidad ORIGINAL (1..15), antes del límite wire de 14.
[[nodiscard]] Core::Expected<std::uint32_t, WalkWriteError> WalkUpdateTimeMax(std::size_t pathNum);

// Ya pasaron por el framing y la decodificación de Network. WrongPacket para tipo/opcode
// distintos, TooShort si faltan campos; como los demás lectores, ignoran bytes sobrantes.
[[nodiscard]] Core::Expected<Walk, ProtocolError> ReadWalk(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Damage, ProtocolError> ReadDamage(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Action, ProtocolError> ReadAction(const Network::Packet& packet);
} // namespace Client::Protocol::Movement
