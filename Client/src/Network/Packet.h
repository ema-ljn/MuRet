#pragma once

#include "Core/Expected.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Un paquete C1/C2/C3/C4 entero, con su cabecera, dueño de sus bytes.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción" (el corte).
//
// C1 y C3 llevan el tamaño en el byte 1, y C2 y C4 en los bytes 1 y 2, big-endian. El tamaño es el
// del paquete entero, la cabecera incluida. C3 y C4 son los cifrados (CL2.3): a este nivel son
// bytes como cualquiera, y su opcode viaja adentro del cifrado.
//
// Un Packet solo se arma con FromBytes, que mira el tipo, el tamaño mínimo y el máximo, y que el
// tamaño que declara sea el que mide. Es un paquete válido hasta que se le sacan los bytes
// (TakeBytes) o se mueve, y desde ahí no se usa más.
namespace Client::Network
{
// El primer byte del paquete.
enum class PacketType : std::uint8_t
{
    C1 = 0xC1,
    C2 = 0xC2,
    C3 = 0xC3,
    C4 = 0xC4,
};

// El paquete más grande que se lee o se escribe: el buffer de recepción del original mide 0x2000
// (0x4e88af, cmp eax,0x2000), así que uno más grande nunca le entraba y el original dejaba de leer
// (Q-C40, y el plan de la CL2, pregunta 3).
inline constexpr std::size_t MaxPacketSize = 0x2000;

// El tipo de un primer byte, o std::nullopt si no es C1 a C4.
[[nodiscard]] std::optional<PacketType> PacketTypeFromByte(std::byte first);

// El tipo y el tamaño: 2 bytes en C1 y C3, 3 en C2 y C4.
[[nodiscard]] std::size_t HeaderSize(PacketType type);

// Lo mínimo que mide un paquete: la cabecera y el opcode.
[[nodiscard]] std::size_t MinPacketSize(PacketType type);

// El tamaño que declara la cabecera, leído de sus primeros HeaderSize(type) bytes. std::nullopt
// si todavía no llegaron todos.
[[nodiscard]] std::optional<std::size_t> DeclaredSize(PacketType type, std::span<const std::byte> header);

// Agrega a `out` la cabecera que lee DeclaredSize: el tipo y `size`, en el byte 1 en un C1/C3, y en
// los bytes 1 y 2, big-endian, en un C2/C4. Quien llama sabe que `size` entra: en un C1/C3 se
// escribe solo su byte de abajo.
void AppendHeader(std::vector<std::byte>& out, PacketType type, std::size_t size);

// Si un tamaño declarado no sirve: SizeTooSmall si no llega a MinPacketSize(type), TooLarge si pasa
// de MaxPacketSize, y std::nullopt si sirve. Es la regla que usan FromBytes y el PacketReader.
[[nodiscard]] std::optional<PacketError> CheckDeclaredSize(PacketType type, std::size_t declared);

class Packet
{
public:
    // Valida el tipo, el tamaño mínimo y el máximo, y que el tamaño que declara sea el que mide.
    // Los errores, en ese orden: un vector vacío o con menos bytes que la cabecera es SizeTooSmall.
    [[nodiscard]] static Core::Expected<Packet, PacketError> FromBytes(std::vector<std::byte> bytes);

    [[nodiscard]] PacketType Type() const;

    // Todo el paquete, con la cabecera.
    [[nodiscard]] std::span<const std::byte> Bytes() const;

    // Lo que sigue a la cabecera: desde el opcode en C1/C2, desde el cifrado en C3/C4. Se puede
    // cambiar en el lugar (el XOR32, los remapeos de FoxAnti), pero no el tipo ni el tamaño.
    [[nodiscard]] std::span<std::byte> MutableBody();

    // Para lo que cambia el tamaño (FoxAnti): saca los bytes, que vuelven con FromBytes. El Packet
    // queda vacío: Bytes() y MutableBody() vacíos, y Opcode() en std::nullopt.
    [[nodiscard]] std::vector<std::byte> TakeBytes() &&;

    // pkt[2] en un C1 y pkt[3] en un C2; std::nullopt en C3 y C4, que lo llevan cifrado.
    [[nodiscard]] std::optional<std::uint8_t> Opcode() const;

private:
    Packet(PacketType type, std::vector<std::byte> bytes);

    PacketType m_type;
    std::vector<std::byte> m_bytes;
};
} // namespace Client::Network
