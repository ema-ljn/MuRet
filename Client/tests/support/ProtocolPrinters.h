#pragma once
// Lo que trae se usa sin nombrarlo: GoogleTest busca PrintTo por ADL, e include-cleaner no lo
// ve. Por eso cada include de este header se queda.
// IWYU pragma: always_keep

#include "Protocol/Dispatcher.h"
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
