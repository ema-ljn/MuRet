#include "Network/EncryptedPacket.h"

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

// Lo mínimo que trae un C3/C4 descifrado: el serial y el opcode.
constexpr std::size_t MinDecryptedSize = 2;

// Cuántos bloques hacen falta para `useful` bytes: el último puede ir incompleto.
std::size_t BlocksFor(std::size_t useful)
{
    return (useful + PlainBlockSize - 1) / PlainBlockSize;
}

// Una cabecera de tipo `type` para un paquete de `size` bytes: el tamaño en el byte 1 en un C1/C3,
// y en los bytes 1 y 2, big-endian, en un C2/C4. Quien llama sabe que `size` entra.
std::vector<std::byte> HeaderFor(PacketType type, std::size_t size)
{
    std::vector<std::byte> header{static_cast<std::byte>(type)};
    if (type == PacketType::C2 || type == PacketType::C4)
    {
        header.push_back(static_cast<std::byte>(size >> 8));
    }
    header.push_back(static_cast<std::byte>(size & 0xFF));
    return header;
}

// Un Packet de bytes que ya se sabe que lo forman: el tipo, el tamaño y el máximo los puso quien
// llama.
Packet KnownGood(std::vector<std::byte> bytes)
{
    return Packet::FromBytes(std::move(bytes)).Value();
}

} // namespace

Core::Expected<Packet, CryptoError> EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key,
                                                  PacketSerials& serials, std::byte padding)
{
    const PacketType type = plain.Type();
    if (type != PacketType::C1 && type != PacketType::C2)
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

    // Lo que se cifra: lo útil, el relleno detrás y ceros hasta llenar el último bloque. Un bloque
    // lleno no deja lugar para el relleno.
    std::vector<std::byte> sealed(blocks * PlainBlockSize);
    std::ranges::copy(fromSize, sealed.begin());
    if (useful < sealed.size())
    {
        sealed[useful] = padding;
    }
    sealed[0] = std::byte{serials.TakeSend()};

    std::vector<std::byte> bytes = HeaderFor(encryptedType, size);
    bytes.reserve(size);
    const std::span<const std::byte> sealedView(sealed);
    for (std::size_t offset = 0; offset < sealed.size(); offset += PlainBlockSize)
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
    if (type != PacketType::C3 && type != PacketType::C4)
    {
        return Core::Unexpected(CryptoError::WrongPacketType);
    }
    // Un paquete que no se puede descifrar no se despacha, y gasta el serial que se esperaba.
    const auto fail = [&serials](CryptoError error)
    {
        serials.SkipReceive();
        return Core::Unexpected(error);
    };

    const std::span<const std::byte> cipher = encrypted.Bytes().subspan(HeaderSize(type));
    if (cipher.size() % CipherBlockSize != 0)
    {
        return fail(CryptoError::NotWholeBlocks);
    }

    // Q-C05: lo descifrado va a un buffer del tamaño que hace falta, no a los ~2.046 bytes de pila
    // del original. Un largo útil imposible se anota y se sigue, porque un checksum malo más
    // adelante gana (EncryptedPacket.h).
    std::vector<std::byte> decrypted;
    decrypted.reserve(cipher.size() / CipherBlockSize * PlainBlockSize);
    bool invalidBlock = false;
    for (std::size_t offset = 0; offset < cipher.size(); offset += CipherBlockSize)
    {
        const auto block = Crypto::DecryptBlock(key, cipher.subspan(offset).first<CipherBlockSize>());
        if (!block)
        {
            return fail(CryptoError::ChecksumMismatch);
        }
        const bool last = offset + CipherBlockSize == cipher.size();
        const std::size_t usefulInBlock = block->usefulSize;
        if (usefulInBlock == 0 || usefulInBlock > PlainBlockSize || (!last && usefulInBlock < PlainBlockSize))
        {
            invalidBlock = true;
            continue;
        }
        decrypted.insert(decrypted.end(), block->bytes.begin(),
                         block->bytes.begin() + static_cast<std::ptrdiff_t>(usefulInBlock));
    }
    if (invalidBlock)
    {
        return fail(CryptoError::InvalidBlock);
    }
    if (decrypted.size() < MinDecryptedSize)
    {
        return fail(CryptoError::TooShort);
    }

    const SerialCheck serial = serials.Receive(std::to_integer<std::uint8_t>(decrypted.front()));

    // Q-C10: un C3 se rearma como C1 y un C4 como C2, con lo descifrado sin el serial y el tamaño
    // que mide. Entra siempre: un C3 trae a lo sumo 23 bloques, 184 bytes, y un C1 de 185; un C4,
    // a lo sumo 744 dentro de MaxPacketSize, 5.952 bytes, y un C2 de 5.954.
    const PacketType rebuiltType = type == PacketType::C3 ? PacketType::C1 : PacketType::C2;
    const std::size_t size = HeaderSize(rebuiltType) + decrypted.size() - 1;
    std::vector<std::byte> bytes = HeaderFor(rebuiltType, size);
    bytes.reserve(size);
    bytes.insert(bytes.end(), decrypted.begin() + 1, decrypted.end());
    return DecryptedPacket{KnownGood(std::move(bytes)), serial};
}

} // namespace Client::Network
