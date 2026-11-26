#include "Crypto/Xor32.h"

#include <algorithm>
#include <cstddef>
#include <span>

namespace Client::Crypto
{
namespace
{
// i & 31: the key index wraps every 32 bytes.
constexpr std::size_t KeyMask = Xor32Key.size() - 1;
static_assert((Xor32Key.size() & KeyMask) == 0, "la máscara necesita una clave de largo potencia de 2");

// Byte 0 has no previous byte to chain with.
constexpr std::size_t FirstChainable = 1;

std::byte KeyAt(std::size_t index, std::size_t keyShift)
{
    return static_cast<std::byte>(Xor32Key[(index + keyShift) & KeyMask]);
}
} // namespace

void Xor32EncodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift)
{
    for (std::size_t i = std::max(first, FirstChainable); i < bytes.size(); ++i)
    {
        bytes[i] ^= KeyAt(i, keyShift) ^ bytes[i - 1];
    }
}

void Xor32DecodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift)
{
    const std::size_t start = std::max(first, FirstChainable);
    // Last to first: the previous byte is still encrypted, which is what it was chained with.
    for (std::size_t i = bytes.size(); i > start; --i)
    {
        bytes[i - 1] ^= KeyAt(i - 1, keyShift) ^ bytes[i - 2];
    }
}
} // namespace Client::Crypto
