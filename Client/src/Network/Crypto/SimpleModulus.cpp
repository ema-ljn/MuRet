#include "Network/Crypto/SimpleModulus.h"

namespace Client::Network::Crypto
{

namespace
{

constexpr unsigned BitsPerByte = 8;
// Un carril en claro, y lo que pasa al carril que sigue: sus 16 bits de abajo.
constexpr std::uint32_t LaneMask = 0xFFFF;
constexpr std::uint32_t ByteMask = 0xFF;
// Los dos bits de arriba de un carril cifrado, que van después de sus 16 de abajo: el 17 primero y
// después el 16 (0x685bb0 los copia con AddBits desde la posición 22 del dword, MSB primero).
constexpr unsigned FirstHighBit = 17;
constexpr unsigned SecondHighBit = 16;
// El checksum es este valor con XOR de los 8 bytes en claro.
constexpr std::uint8_t ChecksumSeed = 0xF8;
// El byte del largo útil va con XOR de este valor y del checksum.
constexpr std::uint8_t UsefulSizeXor = 0x3D;
// Dónde va la cola, después de los 4 carriles de 18 bits: 72 bits, 9 bytes enteros.
constexpr std::size_t UsefulSizeIndex = 9;
constexpr std::size_t ChecksumIndex = 10;

// Escribe bits MSB primero en un bloque que empieza en cero, como AddBits (0x685e20), que hace OR.
class BitWriter
{
public:
    explicit BitWriter(std::span<std::byte> target) : bytes(target) {}

    // Los `count` bits de abajo de `value`, del más alto al más bajo.
    void Put(std::uint32_t value, unsigned count)
    {
        for (unsigned k = count; k > 0; --k)
        {
            if (((value >> (k - 1)) & 1u) != 0)
            {
                bytes[position / BitsPerByte] |= std::byte{0x80} >> (position % BitsPerByte);
            }
            ++position;
        }
    }

private:
    std::span<std::byte> bytes;
    std::size_t position = 0;
};

// Lee bits MSB primero.
class BitReader
{
public:
    explicit BitReader(std::span<const std::byte> source) : bytes(source) {}

    // `count` bits, el primero como el más alto del resultado.
    std::uint32_t Take(unsigned count)
    {
        std::uint32_t value = 0;
        for (unsigned k = 0; k < count; ++k)
        {
            const std::byte bit = bytes[position / BitsPerByte] & (std::byte{0x80} >> (position % BitsPerByte));
            value = (value << 1) | (bit != std::byte{0} ? 1u : 0u);
            ++position;
        }
        return value;
    }

private:
    std::span<const std::byte> bytes;
    std::size_t position = 0;
};

// Los 18 bits de un carril cifrado: sus dos bytes de abajo, el primero el menos significativo, cada
// uno MSB primero, y después el bit 17 y el 16. Lo que pase de 18 bits se pierde, como en el
// original: con un módulo que no pasa de MaxModulus y un XOR de 16 bits no queda nada.
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

// El carril `lane` de un bloque en claro: dos bytes little-endian.
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

    // Cada carril con su XOR y el anterior ya cifrado. El producto es de std::uint32_t por
    // std::uint32_t, que da la vuelta como el IMUL de 32 bits.
    Lanes lanes{};
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < LaneCount; ++i)
    {
        const std::uint32_t mixed = xorKey[i] ^ PlainLane(plain, i) ^ previous;
        lanes[i] = (mixed * multiplier[i]) % modulus[i];
        previous = lanes[i] & LaneMask;
    }

    // La segunda pasada, del carril 2 al 0: cada uno con su XOR y el que lo sigue, como era antes
    // de esta pasada. El 3 queda igual.
    std::uint32_t next = lanes[LaneCount - 1] & LaneMask;
    for (std::size_t i = LaneCount - 1; i-- > 0;)
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

    // Deshace la segunda pasada, del carril 2 al 0, con el que sigue ya deshecho.
    std::uint32_t next = lanes[LaneCount - 1] & LaneMask;
    for (std::size_t i = LaneCount - 1; i-- > 0;)
    {
        lanes[i] ^= xorKey[i] ^ next;
        next = lanes[i] & LaneMask;
    }

    // Cada carril por la clave de descifrar, con su XOR y el anterior todavía cifrado. Un carril
    // cifrado más grande que su módulo (un servidor bueno no lo manda) se multiplica igual, y el
    // producto puede dar la vuelta.
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

} // namespace Client::Network::Crypto
