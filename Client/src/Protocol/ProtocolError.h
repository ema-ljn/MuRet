#pragma once

#include <cstdint>

// Lo que puede fallar al leer o escribir un paquete del protocolo.
// Contrato: el plan de la CL2 (CL2.5).
namespace Client::Protocol
{
enum class ProtocolError : std::uint8_t
{
    WrongPacket,  // otro tipo, otro opcode u otro subcódigo que los del paquete que se lee
    TooShort,     // faltan campos, o la lista dice más entradas que las que trae
    FieldTooLong, // al escribir, un texto que no entra en su campo (la cuenta o la clave del login)
};
} // namespace Client::Protocol
