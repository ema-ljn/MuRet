#pragma once

#include "Network/Crypto/CryptoError.h"
#include "Network/ISocket.h"
#include "Network/ObfuscationError.h"
#include "Network/PacketError.h"
#include "Network/PacketSerials.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"

#include <ostream>

// Cómo imprime GoogleTest los enums de Client::Network en un mensaje: sin esto, un
// std::optional<PacketError> sale como "(1-byte object <01>)". GoogleTest busca PrintTo en el
// namespace del tipo, así que cada uno va en el suyo.
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

inline void PrintTo(ObfuscationError error, std::ostream* out)
{
    switch (error)
    {
    case ObfuscationError::InvalidPacket:
        *out << "InvalidPacket";
        return;
    }
    *out << "ObfuscationError fuera del enum";
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

inline void PrintTo(SocketError error, std::ostream* out)
{
    switch (error)
    {
    case SocketError::ResolveFailed:
        *out << "ResolveFailed";
        return;
    case SocketError::Refused:
        *out << "Refused";
        return;
    case SocketError::Closed:
        *out << "Closed";
        return;
    case SocketError::Failed:
        *out << "Failed";
        return;
    }
    *out << "SocketError fuera del enum";
}

inline void PrintTo(ConnectProgress progress, std::ostream* out)
{
    switch (progress)
    {
    case ConnectProgress::InProgress:
        *out << "InProgress";
        return;
    case ConnectProgress::Connected:
        *out << "Connected";
        return;
    }
    *out << "ConnectProgress fuera del enum";
}
} // namespace Client::Network

namespace Client::Network::SocketApi
{
inline void PrintTo(Status status, std::ostream* out)
{
    switch (status)
    {
    case Status::Done:
        *out << "Done";
        return;
    case Status::TryLater:
        *out << "TryLater";
        return;
    case Status::InProgress:
        *out << "InProgress";
        return;
    case Status::Refused:
        *out << "Refused";
        return;
    case Status::PeerClosed:
        *out << "PeerClosed";
        return;
    case Status::Failed:
        *out << "Failed";
        return;
    }
    *out << "Status fuera del enum";
}
} // namespace Client::Network::SocketApi

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
