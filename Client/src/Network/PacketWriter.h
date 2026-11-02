// Arma un paquete C1 o C2 campo por campo, como el motor de envío del original.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Envío: el motor C1/C2 y el XOR32", y el
// plan de la CL2 (CL2.2).
//
// El motor del original empieza con el tipo y el opcode, agrega cada campo y al terminar escribe
// el tamaño (en un C2, big-endian). Además cifra con XOR32 cada byte que agrega: acá el XOR32 es
// una etapa aparte (Network/Crypto/Xor32.h), porque no todo lo que sale lo lleva (los F4 al
// ConnectServer salen en claro).
//
// Los enteros de los campos van little-endian, como los escribe el cliente en x86; PutU16Be es para
// los pocos campos que el protocolo lleva al revés (04-protocolo.md lo dice en cada uno).

#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Network
{

// El paquete más grande que arma el motor del original: con 0x800 bytes deja de agregar, y lo que
// sobra se ignora en silencio (Q-C06). Es también el tope de un C2 que arma MuRet.
inline constexpr std::size_t MaxWrittenPacketSize = 0x800;

// El C1 más grande: su tamaño ocupa un byte.
inline constexpr std::size_t MaxC1Size = 0xFF;

class PacketWriter
{
public:
    // Empieza un C1 (C1, el tamaño, el opcode) o un C2 (C2, el tamaño en dos bytes, el opcode). El
    // tamaño queda en 0 hasta Finish().
    [[nodiscard]] static PacketWriter C1(std::uint8_t opcode);
    [[nodiscard]] static PacketWriter C2(std::uint8_t opcode);

    PacketWriter& PutU8(std::uint8_t value);
    PacketWriter& PutU16Le(std::uint16_t value);
    PacketWriter& PutU16Be(std::uint16_t value);
    PacketWriter& PutU32Le(std::uint32_t value);
    PacketWriter& PutBytes(std::span<const std::byte> more);

    // El paquete, con su tamaño. TooLarge si un C1 pasa de MaxC1Size o un C2 de
    // MaxWrittenPacketSize: nunca sale un paquete truncado. El escritor queda como estaba, y se
    // puede seguir agregando.
    [[nodiscard]] Core::Expected<Packet, PacketError> Finish() const;

private:
    PacketWriter(PacketType packetType, std::uint8_t opcode);

    void PutLittleEndian(std::uint32_t value, std::size_t size);

    PacketType type;
    std::vector<std::byte> bytes;
};

} // namespace Client::Network
