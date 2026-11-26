#include "Crypto/SimpleModulus.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Crypto
{
namespace
{
// A plain lane is two bytes; an encrypted one, two and two loose bits.
constexpr unsigned BitsPerByte = 8;
// A plain lane, and what carries to the next lane: its low 16 bits.
constexpr std::uint32_t LaneMask = 0xFFFF;
constexpr std::uint32_t ByteMask = 0xFF;
// An encrypted lane's two high bits, after its low 16: bit 17 first, then 16 (0x685bb0 copies them with AddBits from
// the dword's bit 22, MSB first).
constexpr unsigned FirstHighBit = 17;
constexpr unsigned SecondHighBit = 16;
// The checksum is this value XOR the 8 plain bytes.
constexpr std::uint8_t ChecksumSeed = 0xF8;
// The useful-size byte goes XOR this value and the checksum.
constexpr std::uint8_t UsefulSizeXor = 0x3D;
// The tail goes after the 4 lanes of 18 bits (72 bits, 9 whole bytes): first the useful size, then the checksum.
constexpr std::size_t UsefulSizeIndex = 9;
constexpr std::size_t ChecksumIndex = 10;
static_assert(UsefulSizeIndex * BitsPerByte == LaneCount * CipherLaneBits);
static_assert(ChecksumIndex + 1 == CipherBlockSize);

// The second pass, from lane 2 to 0: lane 3 stays.
constexpr std::array<std::size_t, LaneCount - 1> SecondPassLanes{2, 1, 0};

// MSB first into a block that starts at zero, like AddBits (0x685e20), which ORs.
class BitWriter
{
public:
    explicit BitWriter(std::span<std::byte> target) : m_bytes(target) {}

    // From the highest to the lowest.
    void Put(std::uint32_t value, unsigned count)
    {
        for (unsigned k = count; k > 0; --k)
        {
            if (((value >> (k - 1)) & 1u) != 0)
            {
                m_bytes[m_position / BitsPerByte] |= std::byte{0x80} >> (m_position % BitsPerByte);
            }
            ++m_position;
        }
    }

private:
    std::span<std::byte> m_bytes;
    std::size_t m_position = 0;
};

// MSB first.
class BitReader
{
public:
    explicit BitReader(std::span<const std::byte> source) : m_bytes(source) {}

    // The first bit as the result's highest.
    std::uint32_t Take(unsigned count)
    {
        std::uint32_t value = 0;
        for (unsigned k = 0; k < count; ++k)
        {
            const std::byte bit = m_bytes[m_position / BitsPerByte] & (std::byte{0x80} >> (m_position % BitsPerByte));
            value = (value << 1) | (bit != std::byte{0} ? 1u : 0u);
            ++m_position;
        }
        return value;
    }

private:
    std::span<const std::byte> m_bytes;
    std::size_t m_position = 0;
};

// Its two low bytes, least significant first, each MSB first, then bits 17 and 16. Anything over 18 bits is lost,
// as in the original: with a key that passes FromLanes nothing is.
void PutLane(BitWriter& writer, std::uint32_t lane)
{
    writer.Put(lane & ByteMask, BitsPerByte);
    writer.Put((lane >> BitsPerByte) & ByteMask, BitsPerByte);
    writer.Put((lane >> FirstHighBit) & 1u, 1);
    writer.Put((lane >> SecondHighBit) & 1u, 1);
}

std::uint32_t TakeLane(BitReader& reader)
{
    const std::uint32_t low = reader.Take(BitsPerByte);
    const std::uint32_t high = reader.Take(BitsPerByte);
    const std::uint32_t first = reader.Take(1);
    const std::uint32_t second = reader.Take(1);
    return low | (high << BitsPerByte) | (first << FirstHighBit) | (second << SecondHighBit);
}

// Two little-endian bytes.
std::uint32_t PlainLane(std::span<const std::byte, PlainBlockSize> plain, std::size_t lane)
{
    return std::to_integer<std::uint32_t>(plain[2 * lane]) |
           (std::to_integer<std::uint32_t>(plain[2 * lane + 1]) << BitsPerByte);
}

std::uint8_t Checksum(std::span<const std::byte, PlainBlockSize> plain)
{
    std::uint8_t checksum = ChecksumSeed;
    for (const std::byte value : plain)
    {
        checksum ^= std::to_integer<std::uint8_t>(value);
    }
    return checksum;
}
} // namespace

std::array<std::byte, CipherBlockSize>
EncryptBlock(const EncryptionKey& key, std::span<const std::byte, PlainBlockSize> plain, std::uint8_t usefulSize)
{
    const Lanes& modulus = key.Modulus();
    const Lanes& multiplier = key.Key();
    const Lanes& xorKey = key.XorKey();

    // The product is std::uint32_t times std::uint32_t, which wraps like the 32-bit IMUL.
    Lanes lanes{};
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < LaneCount; ++i)
    {
        const std::uint32_t mixed = xorKey[i] ^ PlainLane(plain, i) ^ previous;
        lanes[i] = (mixed * multiplier[i]) % modulus[i];
        previous = lanes[i] & LaneMask;
    }

    // The second pass: each lane with its XOR and the next one as it was before this pass.
    std::uint32_t next = lanes[LaneCount - 1] & LaneMask;
    for (const std::size_t i : SecondPassLanes)
    {
        const std::uint32_t before = lanes[i];
        lanes[i] = before ^ xorKey[i] ^ next;
        next = before & LaneMask;
    }

    std::array<std::byte, CipherBlockSize> cipher{};
    BitWriter writer(cipher);
    for (const std::uint32_t lane : lanes)
    {
        PutLane(writer, lane);
    }
    const std::uint8_t checksum = Checksum(plain);
    writer.Put(static_cast<std::uint8_t>(usefulSize ^ UsefulSizeXor ^ checksum), BitsPerByte);
    writer.Put(checksum, BitsPerByte);
    return cipher;
}

std::optional<DecryptedBlock> DecryptBlock(const DecryptionKey& key, std::span<const std::byte, CipherBlockSize> cipher)
{
    const Lanes& modulus = key.Modulus();
    const Lanes& multiplier = key.Key();
    const Lanes& xorKey = key.XorKey();

    Lanes lanes{};
    BitReader reader(cipher);
    for (std::uint32_t& lane : lanes)
    {
        lane = TakeLane(reader);
    }

    // With the next one already undone.
    std::uint32_t next = lanes[LaneCount - 1] & LaneMask;
    for (const std::size_t i : SecondPassLanes)
    {
        lanes[i] ^= xorKey[i] ^ next;
        next = lanes[i] & LaneMask;
    }

    // With its XOR and the previous one still encrypted. An encrypted lane larger than its modulus (a sane server never
    // sends one) is multiplied anyway, and the product may wrap.
    DecryptedBlock block;
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < LaneCount; ++i)
    {
        const std::uint32_t word = (((multiplier[i] * lanes[i]) % modulus[i]) ^ xorKey[i] ^ previous) & LaneMask;
        block.bytes[2 * i] = static_cast<std::byte>(word & ByteMask);
        block.bytes[2 * i + 1] = static_cast<std::byte>(word >> BitsPerByte);
        previous = lanes[i] & LaneMask;
    }

    const std::uint8_t checksum = Checksum(block.bytes);
    if (std::to_integer<std::uint8_t>(cipher[ChecksumIndex]) != checksum)
    {
        return std::nullopt;
    }
    block.usefulSize =
        static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(cipher[UsefulSizeIndex]) ^ checksum ^ UsefulSizeXor);
    return block;
}
} // namespace Client::Crypto
