// Misión de la 0.0: en qué posición del paquete está el head.
// Contrato: Server/DataServer/docs/research/04-red.md, "El framing C1/C2".
//
// El header solo declara. La definición está en HeadOffset.cpp, adentro de la biblioteca
// estática MuRetLabs: quien incluya esto compila con la declaración y el linker trae la
// definición desde libMuRetLabs.a (MuRetLabs.lib en Windows).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Labs
{

// Posición del head según el primer byte del paquete: 2 en un C1 y 3 en un C2.
//
// Con cualquier otro primer byte no hay paquete. El parser del original descarta todo lo
// acumulado ("error-L1 Header error", 04-red.md, "Máquina de estados"); esta función no puede
// hacer eso, así que lo dice con std::nullopt y decide quien la llama.
std::optional<std::size_t> HeadOffset(std::uint8_t firstByte);

} // namespace Labs
