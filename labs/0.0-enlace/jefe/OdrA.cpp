#include "Odr.h"

namespace Labs::Odr
{

// Cuenta las posiciones desde 0, como el research: el head de un C1 está en la 2.
inline int HeadPosition(std::uint8_t firstByte)
{
    return firstByte == 0xC1 ? 2 : 3;
}

int FromA(std::uint8_t firstByte)
{
    return HeadPosition(firstByte);
}

} // namespace Labs::Odr
