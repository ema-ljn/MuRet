// Test de la misión de la 0.0 (docs/learning/ola-0.0-del-codigo-al-programa.md, actividad 5).
//
// Antes de tener el header, la definición y los targets, fallaba en una etapa distinta en cada
// paso: el preprocesador, el linker y el test en rojo (ver labs/0.0-enlace/README.md).
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

    // Un primer byte que no es C1 ni C2 no abre ningún paquete: el parser del original descarta
    // todo lo acumulado (04-red.md, "Máquina de estados"). HeadOffset no puede cortar nada, así
    // que devuelve std::nullopt. C3 y C4 son los paquetes cifrados del cliente; entre el
    // GameServer y el DataServer no existen.
    Check(!Labs::HeadOffset(0xC3).has_value(), "C3: no es un paquete del DataServer");
    Check(!Labs::HeadOffset(0x00).has_value(), "00: no es un paquete");

    if (failures == 0)
    {
        std::printf("ok\n");
    }
    return failures == 0 ? 0 : 1;
}
