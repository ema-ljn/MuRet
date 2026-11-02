// Leer los campos de un paquete que llegó: primero se mira que sea el que se espera (tipo, opcode y
// subcódigo), y después se leen en orden, campo por campo, con un Core::ByteReader. Nada de
// reinterpret_cast ni de #pragma pack: el layout del original lo fija el research, no un struct de
// C++ (la pregunta 3 del plan de la ola 1 del servidor).

#pragma once

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Protocol
{

// Qué paquete es: su tipo, su opcode y su subcódigo, que va justo después del opcode (04, "El
// dispatcher": pkt[3] en un C1, pkt[4] en un C2). F1 00 es el saludo y F4 06, la lista.
struct PacketKind
{
    Network::PacketType type;
    std::uint8_t opcode;
    std::uint8_t subcode;
};

// Un lector de los campos que siguen al subcódigo. WrongPacket si el tipo, el opcode o el subcódigo
// no son los de `kind`, y TooShort si el paquete termina antes del subcódigo. Lee los bytes de
// `packet`, que tiene que seguir vivo mientras se lea.
[[nodiscard]] Core::Expected<Core::ByteReader, ProtocolError> FieldsAfterSubcode(const Network::Packet& packet,
                                                                                 const PacketKind& kind);

// Con un Packet temporal, el lector miraría los bytes de un paquete que muere al terminar la línea,
// como con el constructor que Core::ByteReader rechaza: así no compila.
Core::Expected<Core::ByteReader, ProtocolError> FieldsAfterSubcode(const Network::Packet&& packet,
                                                                   const PacketKind& kind) = delete;

// Un texto de largo fijo, hasta el primer \0, o el campo entero si no tiene ninguno: un campo lleno
// no lleva terminador (Q-C07).
[[nodiscard]] std::string TextField(std::span<const std::byte> field);

} // namespace Client::Protocol
