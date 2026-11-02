// Cómo imprime GoogleTest los enums de Client::Network en un mensaje: sin esto, un
// std::optional<PacketError> sale como "(1-byte object <01>)". GoogleTest busca PrintTo en el
// namespace del tipo, así que cada uno va en el suyo.

#pragma once

#include "Network/Crypto/CryptoError.h"
#include "Network/PacketError.h"
#include "Network/PacketSerials.h"

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

inline void PrintTo(SerialCheck check, std::ostream* out)
{
    switch (check)
    {
    case SerialCheck::InOrder:
        *out << "InOrder";
        return;
    case SerialCheck::Resynced:
        *out << "Resynced";
        return;
    }
    *out << "SerialCheck fuera del enum";
}

} // namespace Client::Network

namespace Client::Network::Crypto
{

inline void PrintTo(CryptoError error, std::ostream* out)
{
    switch (error)
    {
    case CryptoError::InvalidKeyFile:
        *out << "InvalidKeyFile";
        return;
    case CryptoError::InvalidKey:
        *out << "InvalidKey";
        return;
    case CryptoError::WrongPacketType:
        *out << "WrongPacketType";
        return;
    case CryptoError::ChecksumMismatch:
        *out << "ChecksumMismatch";
        return;
    case CryptoError::InvalidBlock:
        *out << "InvalidBlock";
        return;
    case CryptoError::NotWholeBlocks:
        *out << "NotWholeBlocks";
        return;
    case CryptoError::TooShort:
        *out << "TooShort";
        return;
    case CryptoError::TooLarge:
        *out << "TooLarge";
        return;
    }
    *out << "CryptoError fuera del enum";
}

} // namespace Client::Network::Crypto
