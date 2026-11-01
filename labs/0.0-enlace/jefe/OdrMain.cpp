// Imprime lo que da cada unidad para un C1. CTest compara la salida con lo que se espera según
// el orden de los objetos (labs/CMakeLists.txt).

#include "Odr.h"

#include <cstdio>

int main()
{
    std::printf("FromA=%d FromB=%d\n", Labs::Odr::FromA(0xC1), Labs::Odr::FromB(0xC1));
    return 0;
}
