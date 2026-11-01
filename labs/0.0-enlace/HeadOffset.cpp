#include "HeadOffset.h"

namespace
{

// 04-red.md, "El framing C1/C2". Son constexpr de namespace: tienen enlace interno, así que no
// llegan a la tabla de símbolos de libMuRetLabs.a como globales (mirá `nm -C`).
constexpr std::uint8_t HeaderC1 = 0xC1;
constexpr std::uint8_t HeaderC2 = 0xC2;
constexpr std::size_t HeadOffsetC1 = 2; // c1 | size | head
constexpr std::size_t HeadOffsetC2 = 3; // c2 | sizeHi | sizeLo | head

} // namespace

namespace Labs
{

std::optional<std::size_t> HeadOffset(std::uint8_t firstByte)
{
    switch (firstByte)
    {
    case HeaderC1:
        return HeadOffsetC1;
    case HeaderC2:
        return HeadOffsetC2;
    default:
        return std::nullopt;
    }
}

} // namespace Labs
