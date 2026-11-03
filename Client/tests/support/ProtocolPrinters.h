#pragma once

#include "Protocol/Dispatcher.h"
#include "Protocol/Outgoing.h"
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

inline void PrintTo(DispatchResult result, std::ostream* out)
{
    switch (result)
    {
    case DispatchResult::Handled:
        *out << "Handled";
        return;
    case DispatchResult::Unknown:
        *out << "Unknown";
        return;
    case DispatchResult::Malformed:
        *out << "Malformed";
        return;
    }
    *out << "DispatchResult fuera del enum";
}
} // namespace Client::Protocol
