#include "Odr.h"

namespace Labs::Odr
{

// La misma función, contando desde 1. Esta definición y la de OdrA.cpp no son idénticas: el
// programa está mal formado y el estándar no exige diagnóstico.
inline int HeadPosition(std::uint8_t firstByte)
{
    return firstByte == 0xC1 ? 3 : 4;
}

int FromB(std::uint8_t firstByte)
{
    return HeadPosition(firstByte);
}

} // namespace Labs::Odr
