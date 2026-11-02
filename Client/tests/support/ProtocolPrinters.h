// Cómo imprime GoogleTest los enums de Client::Protocol en un mensaje: sin esto, un
// std::optional<ProtocolError> sale como "(1-byte object <01>)". Como NetworkPrinters.h.

#pragma once

#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <ostream>

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

inline void PrintTo(Delivery delivery, std::ostream* out)
{
    switch (delivery)
    {
    case Delivery::Clear:
        *out << "Clear";
        return;
    case Delivery::Encrypted:
        *out << "Encrypted";
        return;
    }
    *out << "Delivery fuera del enum";
}

} // namespace Client::Protocol
