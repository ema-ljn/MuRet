#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Builds a C1 or C2 field by field, like the original's send engine (03-red-y-cifrado.md, "Envío: el motor C1/C2 y
// el XOR32"), which also XOR32s each byte it adds: here XOR32 is a separate stage (Network/Crypto/Xor32.h), because
// not everything that leaves carries it (the F4 to the ConnectServer go in the clear). Fields are little-endian, as
// the client writes them on x86; PutU16Be is for the fields the protocol carries the other way (04-protocolo.md).
namespace Client::Network
{
// The largest packet the original's engine builds: before each field it checks it fits in 0x800 bytes, and if not,
// skips it whole and goes on to the next (Q-C06). Also the cap of a C2 that MuRet builds.
inline constexpr std::size_t MaxWrittenPacketSize = 0x800;

inline constexpr std::size_t MaxC1Size = 0xFF;

class ENGINE_API PacketWriter
{
public:
    // The size is written in Finish().
    [[nodiscard]] static PacketWriter C1(std::uint8_t opcode);
    [[nodiscard]] static PacketWriter C2(std::uint8_t opcode);

    PacketWriter& PutU8(std::uint8_t value) &;
    PacketWriter& PutU16Le(std::uint16_t value) &;
    PacketWriter& PutU16Be(std::uint16_t value) &;
    PacketWriter& PutU32Le(std::uint32_t value) &;
    PacketWriter& PutBytes(std::span<const std::byte> more) &;

    // On a temporary, returns another writer with the field added, taking the temporary's bytes. A reference would
    // dangle in `auto& w = PacketWriter::C1(op).PutU8(x);` without a warning; this way it does not compile.
    [[nodiscard]] PacketWriter PutU8(std::uint8_t value) &&;
    [[nodiscard]] PacketWriter PutU16Le(std::uint16_t value) &&;
    [[nodiscard]] PacketWriter PutU16Be(std::uint16_t value) &&;
    [[nodiscard]] PacketWriter PutU32Le(std::uint32_t value) &&;
    [[nodiscard]] PacketWriter PutBytes(std::span<const std::byte> more) &&;

    // TooLarge if a C1 exceeds MaxC1Size or a C2 MaxWrittenPacketSize: a packet never leaves truncated or missing a
    // field. The writer stays as it was and can keep adding.
    [[nodiscard]] Core::Expected<Packet, PacketError> Finish() const;

private:
    PacketWriter(PacketType type, std::uint8_t opcode);

    template <std::unsigned_integral T> void PutUnsigned(T value, std::endian order);

    // Apart from the fields: a writer whose bytes were moved still builds a C1 or C2 with its opcode, without fields.
    PacketType m_type;
    std::uint8_t m_opcode;
    std::vector<std::byte> m_fields;
};
} // namespace Client::Network
