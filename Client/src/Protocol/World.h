#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// 04 § Los cambios de mapa y § El clima y Crywolf: formatos puros de warp, revivir y clima.
// Network entrega los C3 entrantes ya descifrados como C1; la escena decide cuándo enviar.
namespace Client::Protocol::World
{
inline constexpr std::uint8_t TeleportOpcode = 0x1c;
inline constexpr std::uint8_t WorldOpcode = 0xf3;
inline constexpr std::uint8_t RegenSubcode = 0x04;
inline constexpr std::uint8_t TeleportDoneSubcode = 0x12;
inline constexpr std::uint8_t CrywolfOpcode = 0xbd;
inline constexpr std::uint8_t WeatherOpcode = 0x0f;

// 0x4e36b5: pkt[3] entero. El nibble alto selecciona la rama; >1 no cambia la meta.
struct Weather
{
    std::uint8_t code;
};

struct Teleport
{
    std::uint8_t gate;
    std::uint8_t map;
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t direction;
};

struct Regen
{
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t map;
    std::uint8_t direction;
    std::uint16_t life;
    std::uint16_t mana;
    std::uint16_t bp;
    std::uint32_t experience;
    std::uint32_t zen;
};

// Tipo/opcode/subcódigo distintos → WrongPacket; campos incompletos → TooShort.
// Ignoran bytes sobrantes y devuelven la dirección cruda, sin convertirla a yaw.
[[nodiscard]] Core::Expected<Teleport, ProtocolError> ReadTeleport(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Regen, ProtocolError> ReadRegen(const Network::Packet& packet);

// 04 § El clima y Crywolf: C1 04 0F xy. Conserva el byte crudo, incluso ramas >1; no aplica estado.
// Tipo/opcode distintos -> WrongPacket; sin xy -> TooShort. Los bytes sobrantes se ignoran.
[[nodiscard]] Core::Expected<Weather, ProtocolError> ReadWeather(const Network::Packet& packet);

// 0x599ef0: C1 06 1C gate 00 00, con XOR32 y Delivery::Encrypted (C3).
[[nodiscard]] Outgoing WriteGateRequest(std::uint8_t gate);

// 0x4c6e10: C1 04 F3 12, con XOR32 y Delivery::Clear.
[[nodiscard]] Outgoing WriteTeleportDone();

// 0x4c6e32: sin cifrar, C1 06 F1 03 00 1C, con XOR32 y entrega C3.
[[nodiscard]] Outgoing WriteRejectedTeleport();

// 04 § El clima y Crywolf, 0x4c421a/0x4c785d: C1 04 BD 00, XOR32 y Delivery::Clear.
[[nodiscard]] Outgoing WriteCrywolfState();
} // namespace Client::Protocol::World
