#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <system_error>

// Muestras independientes del contador de milisegundos del sistema, módulo 2^32. No son
// duraciones del bucle SDL: pueden repetirse o envolver. El caller decide cuándo muestrear.
namespace Client::Core
{
class IMillisecondClock32
{
public:
    virtual ~IMillisecondClock32() = default;
    virtual Expected<std::uint32_t, std::error_code> Sample() = 0;
};
} // namespace Client::Core
