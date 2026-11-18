#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Las cartas (CL9 hito 10; 04, "Las cartas y el chat con un amigo"): lo que llega (C5, C6 y C8 en un C1, C7 en un C2;
// 0x4dc840, 0x4dc9e0, 0x4dcb70 y 0x4dcd90) y lo que mandan las ventanas de las cartas (la de escribir, 0x498820, y la
// pestaña de las cartas, 0x4a11b0). La reconstrucción del GameServer 0.97 no tiene cartas: el oráculo es el ASM del
// cliente (*estático*). El dispatcher (0x4e2b10) no tiene caso para el C9.
namespace Client::Protocol::Letters
{
inline constexpr std::uint8_t LetterSendOpcode = 0xc5;
inline constexpr std::uint8_t LetterListOpcode = 0xc6;
inline constexpr std::uint8_t LetterReadOpcode = 0xc7;
inline constexpr std::uint8_t LetterDeleteOpcode = 0xc8;
inline constexpr std::uint8_t LetterListRequestOpcode = 0xc9;
// El nombre en 10 bytes, el asunto en 0x20 y el cuerpo hasta 1000 (0x3e8).
inline constexpr std::size_t LetterNameSize = 10;
inline constexpr std::size_t LetterSubjectSize = 0x20;
inline constexpr std::size_t LetterBodyLimit = 1000;
// C6: la fecha (pkt[0x10], strncpy de 10) y la hora (pkt[0x1b], strncpy de 8).
inline constexpr std::size_t LetterDateSize = 10;
inline constexpr std::size_t LetterTimeSize = 8;
// C7: el equipo del retrato (pkt[9..0x19]), como el de 0x5139f0.
inline constexpr std::size_t PortraitEquipmentSize = 17;
// C7: 0x4a03c0 guarda 0x404 bytes del paquete (la cabecera de 0x1c y el cuerpo de 1000).
inline constexpr std::size_t LetterCopySize = 0x404;

// C1 C5 (0x4dc840): pkt[3] el resultado y pkt[4..7] la ventana de escribir (+0x40).
struct LetterSendResult
{
    std::uint8_t result = 0;
    std::uint32_t window = 0;
};

// C1 C6 (0x4dc9e0): pkt[4] el número (u16), pkt[6] el que la manda, pkt[0x10] la fecha, pkt[0x1b] la hora, pkt[0x2e]
// el asunto y pkt[0x4e] el tipo (0 y 1, una de la lista; 2, una que llega ahora; más, ninguna).
struct LetterRow
{
    std::uint16_t index = 0;
    std::string name;
    std::string date;
    std::string time;
    std::string subject;
    std::uint8_t type = 0;
    friend bool operator==(const LetterRow&, const LetterRow&) = default;
};

// C2 C7 (0x4dcb70): pkt[4] el número, pkt[6] el largo (u16), pkt[8] la clase del retrato, pkt[9..0x19] su equipo,
// pkt[0x1a] la dirección (los 6 bits de abajo, el ángulo; los 2 de arriba, el zoom), pkt[0x1b] la acción y desde
// pkt[0x1c] el cuerpo. El original pone un 0 en pkt[0x1c + largo] sin mirar el tamaño (Q-C1000): MuRet rechaza el
// largo que no entra en el paquete.
struct LetterRead
{
    std::uint16_t index = 0;
    std::uint16_t length = 0;
    std::uint8_t portraitClass = 0;
    std::array<std::byte, PortraitEquipmentSize> equipment{};
    std::uint8_t direction = 0;
    std::uint8_t action = 0;
    // strncpy de 1000 desde pkt[0x1c] con el 0 en el largo: hasta el primer '\0'.
    std::string body;
    // Lo que guarda 0x4a03c0: los primeros 0x404 bytes del paquete (menos, si es más corto), con el 0 del largo.
    std::vector<std::byte> copy;
};

// C1 C8 (0x4dcd90): pkt[3] el resultado y pkt[4] el número (u16).
struct LetterDeleteResult
{
    std::uint8_t result = 0;
    std::uint16_t index = 0;
};

// Lo que arma la ventana de escribir con el 10 y 1 (0x498820): la ventana, el destinatario, el asunto, la dirección y
// la acción del retrato y el cuerpo ya convertido (sin '\0').
struct LetterToSend
{
    std::uint32_t window = 0;
    std::string name;
    std::string subject;
    std::uint8_t direction = 0;
    std::uint8_t action = 0;
    std::string body;
    friend bool operator==(const LetterToSend&, const LetterToSend&) = default;
};

[[nodiscard]] Core::Expected<LetterSendResult, ProtocolError> ReadLetterSendResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<LetterRow, ProtocolError> ReadLetterRow(const Network::Packet& packet);
[[nodiscard]] Core::Expected<LetterRead, ProtocolError> ReadLetterRead(const Network::Packet& packet);
// La copia de 0x4a03c0 que 0x4a11b0 (el 10 y 2) le vuelve a dar a 0x4dcb70: los mismos lugares sobre los bytes.
[[nodiscard]] Core::Expected<LetterRead, ProtocolError> ReadLetterCopy(std::span<const std::byte> bytes);
[[nodiscard]] Core::Expected<LetterDeleteResult, ProtocolError> ReadLetterDeleteResult(const Network::Packet& packet);

// 0x498820 con el 10 y 1: C2 C5, la ventana (u32), el nombre (10), el asunto (0x20), la dirección, la acción, el largo
// (u16, hasta 1000) y el cuerpo, con el XOR32 desde el byte 4, y sale cifrado como C4 (0x4992fe..0x499416).
[[nodiscard]] Outgoing WriteLetterSend(const LetterToSend& letter);
// 0x4a11b0 con el 10 y 2 sin la carta guardada: C1 06 C7, un 0 y el número (u16), con el XOR32.
[[nodiscard]] Outgoing WriteLetterReadRequest(std::uint16_t index);
// 0x4a11b0 con el 12 y 1: C1 06 C8, un 0 y el número de cada carta marcada, con el XOR32.
[[nodiscard]] Outgoing WriteLetterDelete(std::uint16_t index);
// 0x4a11b0 con el 10 y 5: C1 03 C9 en claro.
[[nodiscard]] Outgoing WriteLetterListRequest();

// El nombre o el asunto en `size` bytes con ceros detrás, cortado en el primer '\0' (strncpy).
[[nodiscard]] std::vector<std::byte> FixedText(std::string_view text, std::size_t size);
} // namespace Client::Protocol::Letters
