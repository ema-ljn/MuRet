#include "Network/EncryptedPacket.h"

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

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
using Client::Crypto::CipherBlockSize;
using Client::Crypto::CryptoError;
using Client::Crypto::PlainBlockSize;

// From this encrypted length a C1 goes out as C4 (cmp eax,0x100, at 0x4e22a8).
constexpr std::size_t C4CipherSize = 0x100;

// The first decrypted byte, which the rebuild does not copy.
constexpr std::size_t SerialSize = 1;

// The serial and the opcode.
constexpr std::size_t MinDecryptedSize = 2;

// The last one may be partial.
std::size_t BlocksFor(std::size_t useful)
{
    return (useful + PlainBlockSize - 1) / PlainBlockSize;
}

// A Packet after TakeBytes keeps its type and is empty: without this, the subspan that skips the header would run past
// the end, which is undefined behavior.
bool IsEmptied(const Packet& packet)
{
    return packet.Bytes().size() < MinPacketSize(packet.Type());
}

// The caller already set the type, size and maximum.
Packet KnownGood(std::vector<std::byte> bytes)
{
    return Packet::FromBytes(std::move(bytes)).Value();
}

// A useful size EncryptPacket never writes in a block: 0, more than 8, or fewer than 8 in one that is not the last.
bool IsImpossibleUsefulSize(std::size_t useful, bool last)
{
    return useful == 0 || useful > PlainBlockSize || (!last && useful < PlainBlockSize);
}

// Each block's useful bytes, one after another: the serial, the opcode and the fields. With several problems the
// first of this list wins: NotWholeBlocks, the first bad checksum, an impossible useful size in any block, and
// TooShort (EncryptedPacket.h).
Core::Expected<std::vector<std::byte>, CryptoError> DecryptBlocks(const Client::Crypto::DecryptionKey& key,
                                                                  std::span<const std::byte> cipher)
{
    if (cipher.size() % CipherBlockSize != 0)
    {
        return Core::Unexpected(CryptoError::NotWholeBlocks);
    }

    // Q-C05: decrypted into a buffer of the needed size, not the original's ~2,046 stack bytes. An impossible useful
    // size is noted and decryption goes on, because a later bad checksum wins.
    std::vector<std::byte> decrypted;
    decrypted.reserve(cipher.size() / CipherBlockSize * PlainBlockSize);
    bool invalidBlock = false;
    for (std::size_t offset = 0; offset < cipher.size(); offset += CipherBlockSize)
    {
        const auto block = Client::Crypto::DecryptBlock(key, cipher.subspan(offset).first<CipherBlockSize>());
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

// Q-C10: a C3 is rebuilt as C1 and a C4 as C2, without the serial and with the size it measures. It always fits: a C3
// carries at most 23 blocks, 184 bytes, for a C1 of 185; a C4, at most 744 within MaxPacketSize, 5,952 bytes, for a
// C2 of 5,954.
Packet Rebuilt(PacketType encryptedType, std::span<const std::byte> decrypted)
{
    const PacketType type = encryptedType == PacketType::C3 ? PacketType::C1 : PacketType::C2;
    const std::span<const std::byte> afterSerial = decrypted.subspan(SerialSize);
    const std::size_t size = HeaderSize(type) + afterSerial.size();
    std::vector<std::byte> bytes;
    bytes.reserve(size);
    AppendHeader(bytes, type, size);
    bytes.insert(bytes.end(), afterSerial.begin(), afterSerial.end());
    return Packet::FromBytes(std::move(bytes), true).Value();
}
} // namespace

Core::Expected<Packet, CryptoError> EncryptPacket(const Packet& plain, const Client::Crypto::EncryptionKey& key,
                                                  PacketSerials& serials, std::byte padding)
{
    return EncryptPacket(plain, key, serials, IdentityObfuscation::Shared(), padding);
}

Core::Expected<Packet, CryptoError> EncryptPacket(const Packet& plain, const Client::Crypto::EncryptionKey& key,
                                                  PacketSerials& serials, const IObfuscation& obfuscation,
                                                  std::byte padding)
{
    const PacketType type = plain.Type();
    if ((type != PacketType::C1 && type != PacketType::C2) || IsEmptied(plain))
    {
        return Core::Unexpected(CryptoError::WrongPacketType);
    }

    // The useful bytes start at the header's last byte, the size the serial overwrites: pkt[1] in a C1, pkt[2] in a C2.
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

    // The useful bytes, the padding behind and zeros to fill the last block. With the last block full, the padding
    // falls outside the encrypted bytes, in one extra byte, because BeforeEncrypt always gets it: the original writes
    // it anyway, after the packet.
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
            Client::Crypto::EncryptBlock(key, sealedView.subspan(offset).first<PlainBlockSize>(), usefulInBlock);
        bytes.insert(bytes.end(), cipher.begin(), cipher.end());
    }
    return KnownGood(std::move(bytes));
}

Core::Expected<DecryptedPacket, CryptoError>
DecryptPacket(const Packet& encrypted, const Client::Crypto::DecryptionKey& key, PacketSerials& serials)
{
    const PacketType type = encrypted.Type();
    if ((type != PacketType::C3 && type != PacketType::C4) || IsEmptied(encrypted))
    {
        return Core::Unexpected(CryptoError::WrongPacketType);
    }

    const auto decrypted = DecryptBlocks(key, encrypted.Bytes().subspan(HeaderSize(type)));
    if (!decrypted.HasValue())
    {
        // An undecryptable packet is not dispatched, and uses up the expected serial.
        serials.SkipReceive();
        return Core::Unexpected(decrypted.Error());
    }
    const std::span<const std::byte> plain = decrypted.Value();
    const SerialCheck serial = serials.Receive(std::to_integer<std::uint8_t>(plain.front()));
    return DecryptedPacket{Rebuilt(type, plain), serial};
}
} // namespace Client::Network
