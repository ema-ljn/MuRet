// Cómo imprime GoogleTest los errores de Client::Network en un mensaje: sin esto, un
// std::optional<PacketError> sale como "(1-byte object <01>)". GoogleTest busca PrintTo en el
// namespace del tipo, así que va en Client::Network.

#pragma once

#include "Network/PacketError.h"

#include <ostream>

namespace Client::Network
{

inline void PrintTo(PacketError error, std::ostream* out)
{
    switch (error)
    {
    case PacketError::InvalidType:
        *out << "InvalidType";
        return;
    case PacketError::SizeTooSmall:
        *out << "SizeTooSmall";
        return;
    case PacketError::SizeMismatch:
        *out << "SizeMismatch";
        return;
    case PacketError::TooLarge:
        *out << "TooLarge";
        return;
    }
    *out << "PacketError fuera del enum";
}

} // namespace Client::Network
