#include "Network/EncryptedPacket.h"

#include "Network/IdentityObfuscation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Client::Network
{
namespace
{
using Crypto::CipherBlockSize;
using Crypto::CryptoError;
using Crypto::PlainBlockSize;

// Desde este largo cifrado, un C1 sale como C4 (cmp eax,0x100, en 0x4e22a8).
constexpr std::size_t C4CipherSize = 0x100;

// El serial: el primer byte de lo descifrado, que el rearmado no copia.
constexpr std::size_t SerialSize = 1;

// Lo mínimo que trae un C3/C4 descifrado: el serial y el opcode.
constexpr std::size_t MinDecryptedSize = 2;

// Cuántos bloques hacen falta para `useful` bytes: el último puede ir incompleto.
std::size_t BlocksFor(std::size_t useful)
{
    return (useful + PlainBlockSize - 1) / PlainBlockSize;
}

// Si a `packet` ya se le sacaron los bytes. Un Packet después de TakeBytes conserva el tipo y queda
// vacío: sin esto, el subspan que salta la cabecera pasaría del final, y es comportamiento
// indefinido.
bool IsEmptied(const Packet& packet)
{
    return packet.Bytes().size() < MinPacketSize(packet.Type());
}

// Un Packet de bytes que ya se sabe que lo forman: el tipo, el tamaño y el máximo los puso quien
// llama.
Packet KnownGood(std::vector<std::byte> bytes)
{
    return Packet::FromBytes(std::move(bytes)).Value();
}

// Si `useful` es un largo útil que EncryptPacket nunca escribe en un bloque: 0, más de 8, o menos
// de 8 en uno que no es el último.
bool IsImpossibleUsefulSize(std::size_t useful, bool last)
{
    return useful == 0 || useful > PlainBlockSize || (!last && useful < PlainBlockSize);
}

// Lo útil de cada bloque de `cipher`, uno detrás del otro: el serial, el opcode y los campos. Si
// hay más de un problema gana el primero de esta lista: NotWholeBlocks, el primer checksum malo, un
// largo útil imposible en cualquier bloque, y TooShort (EncryptedPacket.h).
Core::Expected<std::vector<std::byte>, CryptoError> DecryptBlocks(const Crypto::DecryptionKey& key,
                                                                  std::span<const std::byte> cipher)
{
    if (cipher.size() % CipherBlockSize != 0)
    {
        return Core::Unexpected(CryptoError::NotWholeBlocks);
    }

    // Q-C05: lo descifrado va a un buffer del tamaño que hace falta, no a los ~2.046 bytes de pila
    // del original. Un largo útil imposible se anota y se sigue, porque un checksum malo más
    // adelante gana.
    std::vector<std::byte> decrypted;
    decrypted.reserve(cipher.size() / CipherBlockSize * PlainBlockSize);
    bool invalidBlock = false;
    for (std::size_t offset = 0; offset < cipher.size(); offset += CipherBlockSize)
    {
        const auto block = Crypto::DecryptBlock(key, cipher.subspan(offset).first<CipherBlockSize>());
        if (!block)
        {
            return Core::Unexpected(CryptoError::ChecksumMismatch);
        }
        const bool last = offset + CipherBlockSize == cipher.size();
        if (IsImpossibleUsefulSize(block->usefulSize, last))
        {
            invalidBlock = true;
            continue;
        }
        decrypted.insert(decrypted.end(), block->bytes.begin(),
                         block->bytes.begin() + static_cast<std::ptrdiff_t>(block->usefulSize));
    }
    if (invalidBlock)
    {
        return Core::Unexpected(CryptoError::InvalidBlock);
    }
    if (decrypted.size() < MinDecryptedSize)
    {
        return Core::Unexpected(CryptoError::TooShort);
    }
    return decrypted;
}

// Q-C10: un C3 se rearma como C1 y un C4 como C2, con lo descifrado sin el serial y el tamaño que
// mide. Entra siempre: un C3 trae a lo sumo 23 bloques, 184 bytes, y un C1 de 185; un C4, a lo sumo
// 744 dentro de MaxPacketSize, 5.952 bytes, y un C2 de 5.954.
Packet Rebuilt(PacketType encryptedType, std::span<const std::byte> decrypted)
{
    const PacketType type = encryptedType == PacketType::C3 ? PacketType::C1 : PacketType::C2;
    const std::span<const std::byte> afterSerial = decrypted.subspan(SerialSize);
    const std::size_t size = HeaderSize(type) + afterSerial.size();
    std::vector<std::byte> bytes;
    bytes.reserve(size);
    AppendHeader(bytes, type, size);
    bytes.insert(bytes.end(), afterSerial.begin(), afterSerial.end());
    return KnownGood(std::move(bytes));
}
} // namespace

Core::Expected<Packet, CryptoError> EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key,
                                                  PacketSerials& serials, std::byte padding)
{
    return EncryptPacket(plain, key, serials, IdentityObfuscation::Shared(), padding);
}

Core::Expected<Packet, CryptoError> EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key,
                                                  PacketSerials& serials, const IObfuscation& obfuscation,
                                                  std::byte padding)
{
    const PacketType type = plain.Type();
    if ((type != PacketType::C1 && type != PacketType::C2) || IsEmptied(plain))
    {
        return Core::Unexpected(CryptoError::WrongPacketType);
    }

    // Lo útil empieza en el último byte de la cabecera, el del tamaño que el serial pisa: pkt[1] en
    // un C1, pkt[2] en un C2.
    const std::span<const std::byte> fromSize = plain.Bytes().subspan(HeaderSize(type) - 1);
    const std::size_t useful = fromSize.size();
    const std::size_t blocks = BlocksFor(useful);
    const std::size_t cipherSize = blocks * CipherBlockSize;
    const PacketType encryptedType =
        type == PacketType::C1 && cipherSize < C4CipherSize ? PacketType::C3 : PacketType::C4;
    const std::size_t size = HeaderSize(encryptedType) + cipherSize;
    if (size > MaxPacketSize)
    {
        return Core::Unexpected(CryptoError::TooLarge);
    }

    // Lo que se cifra: lo útil, el relleno detrás y ceros hasta llenar el último bloque. Con el
    // último bloque lleno, el relleno queda afuera de lo cifrado, en un byte de más, porque
    // BeforeEncrypt lo recibe siempre: el original lo escribe igual, detrás del paquete.
    const std::size_t sealedSize = blocks * PlainBlockSize;
    std::vector<std::byte> sealed(std::max(sealedSize, useful + 1));
    std::ranges::copy(fromSize, sealed.begin());
    sealed[useful] = padding;
    sealed[0] = std::byte{serials.TakeSend()};
    obfuscation.BeforeEncrypt(std::span<std::byte>(sealed).first(useful + 1));

    std::vector<std::byte> bytes;
    bytes.reserve(size);
    AppendHeader(bytes, encryptedType, size);
    const std::span<const std::byte> sealedView = std::span<const std::byte>(sealed).first(sealedSize);
    for (std::size_t offset = 0; offset < sealedSize; offset += PlainBlockSize)
    {
        const auto usefulInBlock = static_cast<std::uint8_t>(std::min(useful - offset, PlainBlockSize));
        const auto cipher =
            Crypto::EncryptBlock(key, sealedView.subspan(offset).first<PlainBlockSize>(), usefulInBlock);
        bytes.insert(bytes.end(), cipher.begin(), cipher.end());
    }
    return KnownGood(std::move(bytes));
}

Core::Expected<DecryptedPacket, CryptoError> DecryptPacket(const Packet& encrypted, const Crypto::DecryptionKey& key,
                                                           PacketSerials& serials)
{
    const PacketType type = encrypted.Type();
    if ((type != PacketType::C3 && type != PacketType::C4) || IsEmptied(encrypted))
    {
        return Core::Unexpected(CryptoError::WrongPacketType);
    }

    const auto decrypted = DecryptBlocks(key, encrypted.Bytes().subspan(HeaderSize(type)));
    if (!decrypted.HasValue())
    {
        // Un paquete que no se puede descifrar no se despacha, y gasta el serial que se esperaba.
        serials.SkipReceive();
        return Core::Unexpected(decrypted.Error());
    }
    const std::span<const std::byte> plain = decrypted.Value();
    const SerialCheck serial = serials.Receive(std::to_integer<std::uint8_t>(plain.front()));
    return DecryptedPacket{Rebuilt(type, plain), serial};
}
} // namespace Client::Network
