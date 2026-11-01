// Test de la misión de la 0.0 (docs/learning/ola-0.0-del-codigo-al-programa.md, actividad 5).
//
// Todavía no compila, a propósito: falta registrarlo en labs/CMakeLists.txt, falta el header y
// falta la definición. Cada paso que des lo hace fallar en una etapa distinta; antes de compilar,
// predecí cuál.
//
// No usa GoogleTest (llega en la 0.2): sale con 0 si todo pasa y con 1 si algo falla, y CTest lo
// lee así.

#include "HeadOffset.h"

#include <cstdio>

namespace
{

int failures = 0;

void Check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("FALLA: %s\n", what);
        ++failures;
    }
}

} // namespace

int main()
{
    // Server/DataServer/docs/research/04-red.md, "El framing C1/C2": el head está en la posición
    // 2 en un C1 y en la 3 en un C2.
    Check(Labs::HeadOffset(0xC1) == 2, "C1: el head va en la posicion 2");
    Check(Labs::HeadOffset(0xC2) == 3, "C2: el head va en la posicion 3");

    // TODO(human): un primer byte que no es C1 ni C2 (por ejemplo 0xC3 o 0x00). Mirá qué hace el
    // parser del original con ese byte (04-red.md, "Máquina de estados"), decidí qué devuelve
    // HeadOffset y escribí el caso acá.

    if (failures == 0)
    {
        std::printf("ok\n");
    }
    return failures == 0 ? 0 : 1;
}
