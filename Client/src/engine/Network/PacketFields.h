#pragma once

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <cstddef>
#include <span>
#include <string>

// Fields are read in order with a Core::ByteReader, with no reinterpret_cast or #pragma pack: the research fixes the
// original's layout, not a C++ struct.
namespace Client::Network
{
// The subcode goes right after the opcode (04, "El dispatcher": pkt[3] in a C1, pkt[4] in a C2). F1 00 is the
// greeting and F4 06 the list.
struct PacketKind
{
    Network::PacketType type;
    Opcode opcode;
    Subcode subcode;
};

// WrongPacket if the type, opcode or subcode is not `kind`'s, TooShort if the packet ends before the subcode.
// `packet` must outlive the reader.
[[nodiscard]] ENGINE_API Core::Expected<Core::ByteReader, ProtocolError>
FieldsAfterSubcode(const Network::Packet& packet, const PacketKind& kind);

// With a temporary Packet the reader would see bytes that die at the end of the line.
Core::Expected<Core::ByteReader, ProtocolError> FieldsAfterSubcode(const Network::Packet&& packet,
                                                                   const PacketKind& kind) = delete;

// Up to the first \0, or the whole field if it has none: a full field carries no terminator (Q-C07).
[[nodiscard]] ENGINE_API std::string TextField(std::span<const std::byte> field);
} // namespace Client::Network
