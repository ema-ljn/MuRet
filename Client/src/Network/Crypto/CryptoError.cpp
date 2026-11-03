#include "Network/Crypto/CryptoError.h"

#include <string_view>

namespace Client::Network::Crypto
{
std::string_view CryptoErrorText(CryptoError error)
{
    switch (error)
    {
    case CryptoError::InvalidKeyFile:
        return "el archivo de la clave no es uno del SimpleModulus";
    case CryptoError::InvalidKey:
        return "la clave tiene un módulo o un XOR imposible";
    case CryptoError::WrongPacketType:
        return "el paquete no es del tipo que se cifra o se descifra";
    case CryptoError::ChecksumMismatch:
        return "el checksum de un bloque no coincide";
    case CryptoError::InvalidBlock:
        return "un bloque dice un largo útil imposible";
    case CryptoError::NotWholeBlocks:
        return "lo cifrado no mide bloques enteros";
    case CryptoError::TooShort:
        return "lo descifrado no trae el serial y el opcode";
    case CryptoError::TooLarge:
        return "cifrado, el paquete pasaría del tamaño máximo";
    }
    return "un error fuera del enum";
}
} // namespace Client::Network::Crypto
