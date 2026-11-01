// Jefe de la 0.0: una violación de la ODR que compila y enlaza sin un solo aviso.
//
// OdrA.cpp y OdrB.cpp definen inline la misma función, Labs::Odr::HeadPosition, con cuerpos
// distintos. Cada unidad expone una función propia que la llama. Lo que devuelve cada una no
// depende de su propio código sino de qué objeto ve primero el linker (README.md de 0.0-enlace).

#pragma once

#include <cstdint>

namespace Labs::Odr
{

int FromA(std::uint8_t firstByte);
int FromB(std::uint8_t firstByte);

} // namespace Labs::Odr
