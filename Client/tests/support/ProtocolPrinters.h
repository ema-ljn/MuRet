#pragma once

#include "Protocol/ProtocolError.h"

#include <ostream>

// Cómo imprime GoogleTest los enums de Client::Protocol en un mensaje: sin esto, un
// std::optional<ProtocolError> sale como "(1-byte object <01>)". Como NetworkPrinters.h.
namespace Client::Protocol
{
inline void PrintTo(ProtocolError error, std::ostream* out)
{
    switch (error)
    {
    case ProtocolError::WrongPacket:
        *out << "WrongPacket";
        return;
    case ProtocolError::TooShort:
        *out << "TooShort";
        return;
    case ProtocolError::FieldTooLong:
        *out << "FieldTooLong";
        return;
    }
    *out << "ProtocolError fuera del enum";
}
} // namespace Client::Protocol
