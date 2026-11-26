#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Cuts a TCP stream into C1/C2/C3/C4 packets (03-red-y-cifrado.md, "Cadena de recepción"). It only looks at bytes
// that arrived: with an incomplete header it waits, instead of reading stale bytes like the original (Q-C03). With an
// IObfuscation that masks what arrives (FoxAnti), each packet's first byte as it came off the wire picks the XOR of
// all its bytes, and they are unmasked here, before the cut, because the size travels with the XOR too.
namespace Client::Network
{
class ENGINE_API PacketReader
{
public:
    PacketReader();

    // The obfuscation must outlive the reader: both belong to the connection. A temporary does not compile.
    explicit PacketReader(const IObfuscation& obfuscation);
    PacketReader(const IObfuscation&&) = delete;

    // A moved-from reader is left as new: no bytes, no error, same obfuscation. The compiler's move would empty the
    // buffer and copy `m_consumed`, and Pending() would give a huge number.
    PacketReader(PacketReader&& other) noexcept;
    PacketReader& operator=(PacketReader&& other) noexcept;
    PacketReader(const PacketReader&) = default;
    PacketReader& operator=(const PacketReader&) = default;
    ~PacketReader() = default;

    // Copies the bytes. With an error, keeps nothing. Costs, on average, what it brings, even if packets are not taken
    // after each Feed.
    void Feed(std::span<const std::byte> bytes);

    // std::nullopt if the rest has not arrived. An error repeats from then on, even if more arrives: TCP has no
    // separators, so after a bad header the next packet's start is unknown (the original drops the buffer and keeps
    // guessing, Q-C38). The user closes the connection.
    [[nodiscard]] Core::Expected<std::optional<Packet>, PacketError> Next();

    // Bytes that arrived and have not left in a packet. After an error, 0: they were dropped.
    [[nodiscard]] std::size_t Pending() const;

private:
    Core::Expected<std::optional<Packet>, PacketError> Fail(PacketError reason);

    // Needs to have arrived.
    std::byte UnmaskFirstByte();

    // Up to `end` or to what arrived.
    void UnmaskUpTo(std::size_t end);

    // Never null: a pointer, so the reader can be assigned.
    const IObfuscation* m_obfuscation;
    std::vector<std::byte> m_buffer; // the first `m_consumed` already left
    std::size_t m_consumed = 0;
    // Unmasked up to here; if it is `m_consumed`, the mask is still missing.
    std::size_t m_unmasked = 0;
    std::byte m_mask{0}; // the mask of the packet that starts at `m_consumed`
    std::optional<PacketError> m_error;
};
} // namespace Client::Network
