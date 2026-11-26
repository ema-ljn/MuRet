#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// A whole C1/C2/C3/C4 packet, header included, owning its bytes (03-red-y-cifrado.md, "Cadena de recepción").
// C1 and C3 carry the size in byte 1, C2 and C4 in bytes 1 and 2, big-endian; the size counts the whole packet. C3
// and C4 are the encrypted ones: their opcode travels inside the encryption. Valid until TakeBytes or a move.
namespace Client::Network
{
enum class PacketType : std::uint8_t
{
    C1 = 0xC1,
    C2 = 0xC2,
    C3 = 0xC3,
    C4 = 0xC4,
};

// The original's receive buffer is 0x2000 (0x4e88af, cmp eax,0x2000): a larger packet never fit and the original
// stopped reading (Q-C40).
inline constexpr std::size_t MaxPacketSize = 0x2000;

// std::nullopt if it is not C1 to C4.
[[nodiscard]] ENGINE_API std::optional<PacketType> PacketTypeFromByte(std::byte first);

// The type and the size: 2 bytes in C1 and C3, 3 in C2 and C4.
[[nodiscard]] ENGINE_API std::size_t HeaderSize(PacketType type);

// The header and the opcode.
[[nodiscard]] ENGINE_API std::size_t MinPacketSize(PacketType type);

// std::nullopt until HeaderSize(type) bytes have arrived.
[[nodiscard]] ENGINE_API std::optional<std::size_t> DeclaredSize(PacketType type, std::span<const std::byte> header);

// The caller knows `size` fits: in a C1/C3 only its low byte is written.
ENGINE_API void AppendHeader(std::vector<std::byte>& out, PacketType type, std::size_t size);

// The rule FromBytes and PacketReader share: SizeTooSmall below MinPacketSize(type), TooLarge above MaxPacketSize.
[[nodiscard]] ENGINE_API std::optional<PacketError> CheckDeclaredSize(PacketType type, std::size_t declared);

class ENGINE_API Packet
{
public:
    // The errors, in this order: type, minimum size, maximum, then the declared size against the measured one. An empty
    // vector or one shorter than the header is SizeTooSmall. `wasDecrypted` keeps the provenance DecryptPacket grants
    // on success or that a transformation carries; passing it neither decrypts nor validates the payload.
    [[nodiscard]] static Core::Expected<Packet, PacketError> FromBytes(std::vector<std::byte> bytes,
                                                                       bool wasDecrypted = false);

    [[nodiscard]] PacketType Type() const;

    // Whether it comes from a successful C3/C4 decryption. It does not authenticate the sender and does not depend on
    // the FoxAnti mask.
    [[nodiscard]] bool WasDecrypted() const;

    [[nodiscard]] std::span<const std::byte> Bytes() const;

    // From the opcode in C1/C2, from the encryption in C3/C4. Changed in place (XOR32, the FoxAnti remaps), never the
    // type or the size.
    [[nodiscard]] std::span<std::byte> MutableBody();

    // For what changes the size (FoxAnti): the bytes come back with FromBytes. The Packet is left empty, and Opcode()
    // std::nullopt.
    [[nodiscard]] std::vector<std::byte> TakeBytes() &&;

    // pkt[2] in a C1 and pkt[3] in a C2; std::nullopt in C3 and C4, which carry it encrypted.
    [[nodiscard]] std::optional<std::uint8_t> Opcode() const;

private:
    Packet(PacketType type, std::vector<std::byte> bytes, bool wasDecrypted);

    PacketType m_type;
    std::vector<std::byte> m_bytes;
    bool m_wasDecrypted;
};
} // namespace Client::Network
