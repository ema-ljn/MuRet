// Arma un paquete C1 o C2 campo por campo, como el motor de envío del original.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Envío: el motor C1/C2 y el XOR32", y el
// plan de la CL2 (CL2.2).
//
// El motor del original empieza con el tipo y el opcode, agrega cada campo y al terminar escribe
// el tamaño (en un C2, big-endian). Además cifra con XOR32 cada byte que agrega: acá el XOR32 es
// una etapa aparte (Network/Crypto/Xor32.h), porque no todo lo que sale lo lleva (los F4 al
// ConnectServer salen en claro).
//
// Los enteros de los campos van little-endian, como los escribe el cliente en x86. PutU16Be es para
// los campos que el protocolo lleva al revés, como el id de un jugador en lo que manda el servidor
// (04-protocolo.md); cada paquete de la CL2.5 dice cuáles.

#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Network
{

// El paquete más grande que arma el motor del original. Antes de cada campo mira si entra en
// 0x800 bytes, y si no, lo saltea entero y sigue con el próximo (Q-C06). Es también el tope de un
// C2 que arma MuRet.
inline constexpr std::size_t MaxWrittenPacketSize = 0x800;

// El C1 más grande: su tamaño ocupa un byte.
inline constexpr std::size_t MaxC1Size = 0xFF;

class PacketWriter
{
public:
    // Empieza un C1 (C1, el tamaño, el opcode) o un C2 (C2, el tamaño en dos bytes, el opcode). El
    // tamaño se escribe en Finish().
    [[nodiscard]] static PacketWriter C1(std::uint8_t opcode);
    [[nodiscard]] static PacketWriter C2(std::uint8_t opcode);

    // En un escritor con nombre, cada Put agrega su campo y devuelve el mismo escritor, para
    // encadenar el siguiente.
    PacketWriter& PutU8(std::uint8_t value) &;
    PacketWriter& PutU16Le(std::uint16_t value) &;
    PacketWriter& PutU16Be(std::uint16_t value) &;
    PacketWriter& PutU32Le(std::uint32_t value) &;
    PacketWriter& PutBytes(std::span<const std::byte> more) &;

    // En un temporal, como `PacketWriter::C1(op).PutU8(x)`, devuelve otro escritor con el campo
    // agregado, que se lleva los bytes del temporal. Si devolviera una referencia, el temporal
    // moriría al terminar la línea y `auto& w = PacketWriter::C1(op).PutU8(x);` quedaría colgando
    // sin que el compilador avise; así no compila.
    [[nodiscard]] PacketWriter PutU8(std::uint8_t value) &&;
    [[nodiscard]] PacketWriter PutU16Le(std::uint16_t value) &&;
    [[nodiscard]] PacketWriter PutU16Be(std::uint16_t value) &&;
    [[nodiscard]] PacketWriter PutU32Le(std::uint32_t value) &&;
    [[nodiscard]] PacketWriter PutBytes(std::span<const std::byte> more) &&;

    // El paquete, con su tamaño. TooLarge si un C1 pasa de MaxC1Size o un C2 de
    // MaxWrittenPacketSize: nunca sale un paquete truncado ni con un campo de menos. El escritor
    // queda como estaba, y se puede seguir agregando.
    [[nodiscard]] Core::Expected<Packet, PacketError> Finish() const;

private:
    PacketWriter(PacketType packetType, std::uint8_t packetOpcode);

    template <std::unsigned_integral T> void PutUnsigned(T value, std::endian order);

    // El tipo y el opcode van aparte de los campos: un escritor del que se movieron los bytes
    // sigue armando un C1 o un C2 con su opcode, sin campos.
    PacketType type;
    std::uint8_t opcode;
    std::vector<std::byte> fields;
};

} // namespace Client::Network
