#pragma once

#include <iosfwd>

// Los fixtures de cámara y giro guardan cada float32 en exactamente ocho dígitos hexadecimales.
namespace Client::Tests
{
// Como operator>>: un token inválido marca failbit; el cero devuelto no debe usarse sin comprobarlo.
[[nodiscard]] float ReadHexFloat(std::istream& input);
} // namespace Client::Tests
